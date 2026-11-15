// sim/tactile.cpp — see tactile.h
#include "sim/tactile.h"
#include "sim/blockinfo.h"
#include "sim/worldsys.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "core/registry.h"
#include "world/world.h"

namespace dq {

int tactileFacing(World& w, ivec3 at) {
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  const u8 m = w.getMeta(at.x, at.y, at.z);
  static const int FM[4] = {2, 1, 0, 3};   // a turned model's meta (north, east, south, west)
  if (b.props.contains("tex") && b.props["tex"].is_object() && b.props["tex"].contains("front")) return m & 3;
  if (b.has("facingModel")) return FM[m & 3];
  return 2;
}
float tactileFacingDegrees(int f) { static const float D[4] = {180.f, -90.f, 0.f, 90.f}; return D[f & 3]; }
vec3 tactileNorthLocal(int f, vec3 p) {   // (Tactile's unrotate: the turn undone about the middle)
  const float a = -glm::radians(tactileFacingDegrees(f)), c = std::cos(a), s = std::sin(a);
  const float x = p.x - 0.5f, z = p.z - 0.5f;
  return vec3(0.5f + x * c + z * s, p.y, 0.5f - x * s + z * c);
}
vec3 tactileFromNorth(int f, vec3 p) {
  const float a = glm::radians(tactileFacingDegrees(f)), c = std::cos(a), s = std::sin(a);
  const float x = p.x - 0.5f, z = p.z - 0.5f;
  return vec3(0.5f + x * c + z * s, p.y, 0.5f - x * s + z * c);
}

bool tactileDoorOpen(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  return e && e->is_object() && e->value("door", false);
}

namespace {
bool inBox(vec3 n, const float b[6]) {   // (pixels, Tactile's boxes)
  auto within = [](float v, float lo, float hi) { return v >= lo / 16.f - 1e-4f && v <= hi / 16.f + 1e-4f; };
  return within(n.x, b[0], b[3]) && within(n.y, b[1], b[4]) && within(n.z, b[2], b[5]);
}
// (FumoirBlock, HautFourneauBlock: the door's box shut and open, the mould's)
const float SMOKER_DOOR_SHUT[6] = {3, 8, -1, 13, 12, 2}, SMOKER_DOOR_OPEN[6] = {-6, 8, -4, 4, 12, 2};
const float BLAST_DOOR_SHUT[6] = {3, 14, 3, 13, 22, 7}, BLAST_DOOR_OPEN[6] = {-2, 14, -3, 5, 22, 6}, BLAST_MOLD[6] = {5, 10, -4, 11, 13, 2};
}  // namespace

bool tactileFurnaceUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all) {
  const ivec3 at = hit.block;
  const BlockInfo& bi = binfo(w.getBlock(at.x, at.y, at.z));
  const std::string kind = bi.furnace;
  if (kind != "furnace" && kind != "smoker" && kind != "blast") return false;
  json* e = containerEntity(w, at.x, at.y, at.z);
  if (!e) return false;
  if (!e->contains("items") || !(*e)["items"].is_array()) (*e)["items"] = json::array({nullptr, nullptr, nullptr});
  while ((*e)["items"].size() < 3) (*e)["items"].push_back(nullptr);
  const int f = tactileFacing(w, at);
  const vec3 n = tactileNorthLocal(f, hit.point - vec3(at));
  const vec3 c = vec3(at) + vec3(0.5f);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); };
  // the door (the smoker's on its front, the blast furnace's hatch on top): used, it opens or shuts
  if (kind != "furnace") {
    const bool open = e->value("door", false);
    const float* box = kind == "smoker" ? (open ? SMOKER_DOOR_OPEN : SMOKER_DOOR_SHUT) : (open ? BLAST_DOOR_OPEN : BLAST_DOOR_SHUT);
    if (inBox(n, box)) {
      (*e)["door"] = !open; changed();
      sfxAt(lv, open ? "door_close" : "door_open", c);
      return true;
    }
  }
  json& items = (*e)["items"];
  auto slot = [&](int k) { return ItemStack::fromJson(items[k]); };
  auto take = [&](int k) {
    ItemStack s = slot(k); if (s.empty()) return false;
    items[k] = nullptr; if (k == 0) (*e)["cook"] = 0.0;
    if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
    changed(); sfxAt(lv, "pop", c);
    return true;
  };
  auto put = [&](int k) {   // one of what's held (all: as much as there's room for)
    ItemStack s = slot(k);
    if (!s.empty() && !(s.id == held.id && s.canMerge(held))) return false;
    const int room = s.empty() ? held.maxStack() : s.maxStack() - s.n;
    const int n = std::min(all ? room : std::min(room, 1), held.n);
    if (n <= 0) return false;
    if (s.empty()) { s = held; s.n = 0; }
    s.n += n; items[k] = s.toJson();
    if ((held.n -= n) <= 0) held = ItemStack();
    changed(); sfxAt(lv, "pop", c);
    return true;
  };
  const bool doorOpen = kind == "furnace" || e->value("door", false);
  // the blast furnace's mould: its ingots taken from it
  if (kind == "blast" && inBox(n, BLAST_MOLD)) return held.empty() ? take(2) : false;
  const bool upper = n.y >= (kind == "blast" ? 0.6f : 0.45f);
  if (!held.empty()) {
    if (upper) {
      if (smeltResult(held.id, kind).empty()) {   // (Tactile says nothing; a word on why helps)
        if (fuelValue(held.id) <= 0) toast(lv, p, std::string("That won't cook in the ") + (kind == "blast" ? "blast furnace" : kind));
        return false;
      }
      if (!doorOpen) { toast(lv, p, kind == "blast" ? "Open its hatch first (use it)" : "Open its door first (use it)"); return true; }
      return put(0);
    }
    if (fuelValue(held.id) <= 0) return false;
    return put(1);
  }
  if (upper) {
    if (!doorOpen) return false;
    return take(2) || take(0);
  }
  return take(1);
}

} // namespace dq
