// sim/moving_use.cpp — hinges and sliders by hand (moving structures stage 1, see moving.h). Sneak-use on a hinge cycles its
// turn (clockwise, anticlockwise, half turn, turning: continuous needs an axle, stage 2; by hand it acts as a quarter);
// sneak-use on a slider cycles its distance 1..16. Using the machine, or any block of its piece, starts it (shut: opens;
// open: shuts). Blocks with a use of their own (chests, doors, levers, stations...) keep it, and glue in hand glues.
#include "sim/moving.h"
#include "sim/glue.h"
#include "sim/player.h"
#include "sim/redstone.h"
#include "sim/blockinfo.h"
#include "sim/events.h"
#include "sim/gameplay.h"
#include "world/shapes.h"
#include "core/registry.h"
#include <unordered_set>

namespace dq {

namespace {
struct CellHash { size_t operator()(const ivec3& p) const { return (size_t)((u32)p.x * 73856093u ^ (u32)p.y * 19349663u ^ (u32)p.z * 83492791u); } };

bool hasOwnUse(BlockId id) {
  const BlockDef& b = reg().block(id); const int k = reg().tShape[id];
  if (bhas(id, BF::Container | BF::Door | BF::Trapdoor | BF::Bed | BF::Sign | BF::Furnace) || k == ShapeKind::Gate) return true;
  return b.has("rs") || b.has("station") || b.has("table") || b.has("noteBlock") || b.has("container");
}

// the hinge or slider whose piece holds `at`: walk the glue from it; a machine beside the piece whose moving face is glued
// to it (or a slider slid out, whose piece starts 1 + distance along its face)
bool machineFor(const World& w, ivec3 at, ivec3& out) {
  std::unordered_set<ivec3, CellHash> seen{at};
  std::vector<ivec3> q{at};
  for (size_t i = 0; i < q.size() && q.size() <= (size_t)PIECE_MAX; i++) {
    const ivec3 c = q[i];
    for (int f = 0; f < 6; f++) {
      const ivec3 n = c + faceDir(f);
      if (isMachine(w.getBlock(n.x, n.y, n.z)) && machineFace(w, n) == (f ^ 1) && glued(w, n, f ^ 1)) { out = n; return true; }
      if (seen.count(n) || !glued(w, c, f)) continue;
      seen.insert(n); q.push_back(n);
    }
    for (int f = 0; f < 6; f++)
      for (int k = 2; k <= 17; k++) {
        const ivec3 n = c - faceDir(f) * k;
        const BlockId id = w.getBlock(n.x, n.y, n.z);
        if (!isMachine(id)) continue;
        if (reg().block(id).str("machine") == "slider" && machineFace(w, n) == f && machineOpen(w, n)) {
          const json* e = const_cast<World&>(w).blockEntity(n.x, n.y, n.z);
          if (e && 1 + std::clamp(e->value("distance", 1), 1, 16) == k) { out = n; return true; }
        }
        break;   // (the first machine on the line decides)
      }
  }
  return false;
}

class MovingUse : public LevelSystem {
public:
  explicit MovingUse(Level& l) : lv(l) {
    if (!lv.events) return;
    hUse = lv.events->on("itemUse", [this](const json& a) { return use(a); });
    // netAction [player, "movingUse", [the itemUse args]]: a guest's use, done here on the host (whose world is the real one)
    hAct = lv.events->on("netAction", [this](const json& a) {
      if (lv.isRemote() || !a.is_array() || a.size() < 3 || a[1] != "movingUse" || !a[2].is_array()) return true;
      use(a[2]); return false;
    });
  }
  bool use(const json& a) {
    {
      if (!a.is_array() || a.size() < 6 || !a[2].is_number() || !a[5].is_number() || a[5].get<int>() < 0) return true;   // (no block aimed at)
      const std::string item = a[1].is_string() ? a[1].get<std::string>() : "";
      if (item == "glue") return true;
      Player* p = rs::playerNamed(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p) return true;
      World& w = lv.world(p->realm);
      const ivec3 at(a[2].get<int>(), a[3].get<int>(), a[4].get<int>());
      const BlockId id = w.getBlock(at.x, at.y, at.z);
      if (!id) return true;
      auto toHost = [&] { if (lv.net) lv.net->action(*p, "movingUse", a); return false; };   // (a guest: the host moves it)
      if (isMachine(id)) {
        if (lv.isRemote()) return toHost();
        if (p->sneaking && !item.empty()) return true;   // (sneaking with something in hand: build against it as usual)
        if (p->sneaking) { cycle(w, *p, at, id); return false; }
        std::string why;
        if (machineHeld(w, at)) { toast(lv, *p, "A redstone signal holds it"); return false; }
        if (!startMove(lv, p->realm, at, &why) && !why.empty()) toast(lv, *p, "It won't move: " + why);
        return false;
      }
      if (p->sneaking || hasOwnUse(id) || !glueMask(w, at)) return true;
      ivec3 m;
      if (!machineFor(w, at, m)) return true;
      if (lv.isRemote()) return toHost();
      std::string why;
      if (machineHeld(w, m)) { toast(lv, *p, "A redstone signal holds it"); return false; }
      if (!startMove(lv, p->realm, m, &why) && !why.empty()) toast(lv, *p, "It won't move: " + why);
      return false;
    }
  }
  ~MovingUse() override { if (lv.events) { lv.events->off(hUse); lv.events->off(hAct); } }
  const char* name() const override { return "movingUse"; }
private:
  void cycle(World& w, Player& p, ivec3 at, BlockId id) {
    json& st = machineState(w, at);
    if (reg().block(id).str("machine") == "slider") {
      const int d = std::clamp(st.value("distance", 1), 1, 16) % 16 + 1;
      st["distance"] = d;
      toast(lv, p, "Slider: " + std::to_string(d) + (d == 1 ? " block" : " blocks"));
    } else {
      static const int ORDER[4] = {1, -1, 2, 0};
      static const char* NAMES[4] = {"clockwise", "anticlockwise", "half turn", "turning"};
      const int q = st.value("quarters", 1);
      int i = 0; while (i < 4 && ORDER[i] != q) i++;
      i = (i + 1) % 4;
      st["quarters"] = ORDER[i];
      toast(lv, p, std::string("Hinge: ") + NAMES[i]);
    }
    w.blockEntityChanged(at.x, at.y, at.z);
    sfxAt(lv, "click", vec3(at) + vec3(0.5f));
  }
  Level& lv; int hUse = -1, hAct = -1;
};
}

void registerMovingUse() { registerLevelSystem([](Level& lv) { return std::make_shared<MovingUse>(lv); }); }

} // namespace dq
