// sim/glue.cpp — see glue.h.
#include "sim/glue.h"
#include "sim/player.h"
#include "sim/redstone.h"
#include "sim/gameplay.h"
#include "sim/events.h"
#include "core/registry.h"

namespace dq {

ivec3 faceDir(int face) {
  static const ivec3 D[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  return face >= 0 && face < 6 ? D[face] : ivec3(0);
}

bool glueable(BlockId id) {
  if (!id) return false;
  const Registry& R = reg(); const BlockDef& b = R.block(id);
  return !R.tLiquid[id] && b.hardness >= 0 && b.key.find("fire") == std::string::npos;
}

u8 glueMask(const World& w, ivec3 p) {
  const json* e = const_cast<World&>(w).blockEntity(p.x, p.y, p.z);
  return e && e->is_object() ? (u8)e->value("glue", 0) : 0;
}

bool glued(const World& w, ivec3 a, int face) {
  if (face < 0 || face > 5) return false;
  const ivec3 b = a + faceDir(face);
  if (!glueable(w.getBlock(a.x, a.y, a.z)) || !glueable(w.getBlock(b.x, b.y, b.z))) return false;
  return (glueMask(w, a) >> face & 1) && (glueMask(w, b) >> (face ^ 1) & 1);
}

namespace {
// one side's bit, merged into whatever record the block has (a chest keeps its contents)
void setBit(World& w, ivec3 p, int face, bool on) {
  json* e = w.blockEntity(p.x, p.y, p.z, on);
  if (!e) return;
  if (!e->is_object()) *e = json::object();
  int m = e->value("glue", 0);
  m = on ? (m | 1 << face) : (m & ~(1 << face));
  if (m) (*e)["glue"] = m; else e->erase("glue");
  if (e->empty()) w.setBlockEntity(p.x, p.y, p.z, nullptr); else w.blockEntityChanged(p.x, p.y, p.z);
}
}

bool setGlue(World& w, ivec3 a, int face, bool on) {
  if (face < 0 || face > 5) return false;
  const ivec3 b = a + faceDir(face);
  if (on && (!glueable(w.getBlock(a.x, a.y, a.z)) || !glueable(w.getBlock(b.x, b.y, b.z)))) return false;
  setBit(w, a, face, on); setBit(w, b, face ^ 1, on);
  return true;
}

namespace {
class GlueSystem : public LevelSystem {
public:
  explicit GlueSystem(Level& l) : lv(l) {
    if (!lv.events) return;
    hUse = lv.events->on("itemUse", [this](const json& a) { return use(a); });
    // netAction [player, "glueUse", [the itemUse args]]: a guest's use, done here on the host (whose world is the real one)
    hAct = lv.events->on("netAction", [this](const json& a) {
      if (lv.isRemote() || !a.is_array() || a.size() < 3 || a[1] != "glueUse" || !a[2].is_array()) return true;
      use(a[2]); return false;
    });
  }
  bool use(const json& a) {
    {
      if (!a.is_array() || a.size() < 6 || !a[1].is_string() || a[1].get<std::string>() != "glue") return true;
      Player* p = rs::playerNamed(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      const int face = a[5].is_number() ? a[5].get<int>() : -1;
      if (!p || face < 0) return true;
      if (lv.isRemote()) { if (lv.net) lv.net->action(*p, "glueUse", a); return false; }   // (a guest: the host glues)
      World& w = lv.world(p->realm);
      const ivec3 at(a[2].get<int>(), a[3].get<int>(), a[4].get<int>());
      const bool was = glued(w, at, face);
      if (!was && !setGlue(w, at, face, true)) { toast(lv, *p, "Glue won't hold there"); return false; }
      if (was) setGlue(w, at, face, false);
      ItemStack& h = p->held();
      if (!was) { if (!p->creative() && --h.n <= 0) h = ItemStack(); }
      else if (!p->creative()) { if (h.id == "glue") h.n++; else giveItem(lv, *p, ItemStack("glue", 1)); }
      sfxMaterial(lv, "slime", "place", vec3(at) + vec3(0.5f) + vec3(faceDir(face)) * 0.5f);
      return false;
    }
  }
  ~GlueSystem() override { if (lv.events) { lv.events->off(hUse); lv.events->off(hAct); } }
  const char* name() const override { return "glue"; }
private:
  Level& lv; int hUse = -1, hAct = -1;
};
}

void registerGlue() { registerLevelSystem([](Level& lv) { return std::make_shared<GlueSystem>(lv); }); }

} // namespace dq
