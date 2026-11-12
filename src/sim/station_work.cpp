// sim/station_work.cpp — see station_work.h
#include "sim/station_work.h"
#include "sim/stations.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/magic_combat.h"
#include "core/registry.h"
#include "world/dirs.h"
#include "sim/skills.h"

namespace dq {

namespace {
std::vector<ItemStack> items(World& w, ivec3 at, const char* type, int n) {
  std::vector<ItemStack> v(n);
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (e && e->is_object() && e->value("type", "") == type && e->contains("items") && (*e)["items"].is_array())
    for (int i = 0; i < n && i < (int)(*e)["items"].size(); i++) v[i] = ItemStack::fromJson((*e)["items"][i]);
  return v;
}
void save(World& w, ivec3 at, const char* type, const std::vector<ItemStack>& v, int f = -1) {
  bool any = false; json a = json::array();
  for (auto& s : v) { a.push_back(s.empty() ? json() : s.toJson()); any |= !s.empty(); }
  if (!any) { w.setBlockEntity(at.x, at.y, at.z, nullptr); return; }
  json e = {{"type", type}, {"items", a}};
  if (f >= 0) e["f"] = f;
  w.setBlockEntity(at.x, at.y, at.z, e);
}
bool isGrindstone(World& w, ivec3 at) { return reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "grindstone"; }
bool isCutter(World& w, ivec3 at) { return reg().block(w.getBlock(at.x, at.y, at.z)).has("stonecutter"); }
void give(Level& lv, Player& p, ItemStack s) {
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
}
}  // namespace

// ---- the grindstone, smithing table, loom, fletching and cartography tables ----
const char* pairWorkType(const std::string& st) {
  return st == "grindstone" ? "grindwork" : st == "smithing" ? "smithwork" : st == "loom" ? "loomwork" : st == "fletching" ? "fletchwork"
       : st == "cartography" ? "mapwork" : nullptr;
}
std::string pairStationAt(World& w, ivec3 at) {
  const std::string st = reg().block(w.getBlock(at.x, at.y, at.z)).str("station");
  return pairWorkType(st) ? st : std::string();
}
int pairSlotFor(const std::string& st, const ItemStack& s) {
  if (s.empty()) return -1;
  if (st == "smithing") return s.id == "cinderite_shard" || s.id == "tin_ingot" ? 1 : 0;
  if (st == "loom") { const ItemDef* d = reg().item(s.id); return d && d->has("dye") ? 1 : 0; }
  if (st == "fletching") return s.id == "feather" ? 1 : 0;
  if (st == "cartography") return s.id == "filled_map" ? 0 : 1;
  return -1;
}
std::vector<ItemStack> pairWorkItems(World& w, ivec3 at) {
  const std::string st = pairStationAt(w, at);
  return st.empty() ? std::vector<ItemStack>(2) : items(w, at, pairWorkType(st), 2);
}

namespace {
void savePair(World& w, ivec3 at, const std::string& st, const std::vector<ItemStack>& v, int f) {
  save(w, at, pairWorkType(st), v, f);
}
int pairFacing(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  return e && e->is_object() && e->contains("f") ? e->value("f", 0) & 3 : -1;
}
}  // namespace

bool pairWorkPut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held) {
  const std::string st = pairStationAt(w, at);
  if (st.empty() || held.empty() || slot > 1) return false;
  std::vector<ItemStack> v = pairWorkItems(w, at);
  if (const int want = pairSlotFor(st, held); want >= 0) slot = want;
  else if (slot < 0 || !v[slot].empty()) slot = v[0].empty() ? 0 : v[1].empty() ? 1 : -1;
  if (slot < 0) { toast(lv, p, "Both sides are taken"); return false; }
  if (!v[slot].empty() && !v[slot].canMerge(held)) { toast(lv, p, "Something's lying there already"); return false; }
  // one piece of gear (and one a side on the grindstone); the rest as many as stack
  const bool one = st == "grindstone" || (st == "smithing" && slot == 0) || held.maxStack() <= 1;
  const int room = one ? (v[slot].empty() ? 1 : 0) : (v[slot].empty() ? held.maxStack() : v[slot].maxStack() - v[slot].n);
  const int put = std::min(room, held.n);
  if (put <= 0) return false;
  if (v[slot].empty()) { v[slot] = held; v[slot].n = 0; }
  v[slot].n += put;
  enchOf(v[slot]);   // (a treasure book settles what it holds now, not on every look)
  if ((held.n -= put) <= 0) held = ItemStack();
  // (a plain block's frame: the side it was first laid from)
  int f = pairFacing(w, at);
  if (f < 0) {
    const vec3 d = p.pos - (vec3(at) + vec3(0.5f));
    const ivec2 o = std::fabs(d.x) > std::fabs(d.z) ? ivec2(d.x > 0 ? 1 : -1, 0) : ivec2(0, d.z > 0 ? 1 : -1);
    f = 0; for (int k = 0; k < 4; k++) if (DIRX[k] == -o.x && DIRZ[k] == -o.y) f = k;
  }
  savePair(w, at, st, v, f);
  sfxMaterial(lv, st == "grindstone" ? "stone" : "wood", "place", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  return true;
}

bool pairWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot) {
  const std::string st = pairStationAt(w, at);
  if (st.empty() || slot < 0 || slot > 1) return false;
  std::vector<ItemStack> v = pairWorkItems(w, at);
  if (v[slot].empty()) return false;
  ItemStack s = v[slot]; v[slot] = ItemStack();
  savePair(w, at, st, v, std::max(0, pairFacing(w, at)));
  give(lv, p, s);
  return true;
}

StationResult pairResult(Level& lv, const std::string& st, const ItemStack& a, const ItemStack& b) {
  return st == "cartography" ? mapPairResult(lv, a, b, false) : stationResult(st, a, b);
}
StationResult pairWorkResult(Level& lv, World& w, ivec3 at) {
  const std::string st = pairStationAt(w, at);
  const std::vector<ItemStack> v = pairWorkItems(w, at);
  if (st.empty() || (v[0].empty() && v[1].empty())) return StationResult{};
  return pairResult(lv, st, v[0], v[1]);
}

bool pairWorkDo(Level& lv, Player& p, World& w, ivec3 at, ItemStack* made) {
  const std::string st = pairStationAt(w, at);
  if (st.empty()) return false;
  std::vector<ItemStack> v = pairWorkItems(w, at);
  StationResult r = (v[0].empty() && v[1].empty()) ? StationResult{} : st == "cartography" ? mapPairResult(lv, v[0], v[1], true) : pairResult(lv, st, v[0], v[1]);
  if (!r.ok) {
    toast(lv, p, st == "grindstone" ? "Nothing to grind off that" : st == "smithing" ? "That won't take that upgrade" : st == "loom" ? "That dye won't take on that"
               : st == "fletching" ? "Fletching takes a flint and a feather" : "That doesn't make anything of the map");
    return false;
  }
  auto use = [&](int k, int n) { if (v[k].empty() || n <= 0) return; if ((v[k].n -= n) <= 0) v[k] = ItemStack(); };
  use(0, r.useA); use(1, r.useB);
  savePair(w, at, st, v, std::max(0, pairFacing(w, at)));
  if (made) *made = r.out;
  give(lv, p, r.out);
  if (r.xp > 0) { addXP(p, r.xp); sfxUi(lv, p, "xp"); }   // (the grindstone: the enchantments' experience back)
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  sfxAt(lv, st == "grindstone" ? "grind" : st == "smithing" ? "anvil" : st == "cartography" ? "map_paper" : st == "fletching" ? "bow" : "craft", c);
  if (st == "smithing" || st == "fletching") onCrafted(lv, p, r.out.id, r.out.n, st == "smithing", false);   // (Smithing / Fine Work)
  return true;
}

// ---- the stonecutter ----
ItemStack cutWorkItem(World& w, ivec3 at) { return items(w, at, "cutwork", 1)[0]; }

std::vector<std::pair<std::string, int>> cutWorkOptions(World& w, ivec3 at) {
  const ItemStack s = cutWorkItem(w, at);
  return s.empty() ? std::vector<std::pair<std::string, int>>{} : stonecutterOptions(s.id);
}

bool cutWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held) {
  if (held.empty() || !isCutter(w, at)) return false;
  if (stonecutterOptions(held.id).empty()) { toast(lv, p, "The saw won't cut that into anything"); return false; }
  std::vector<ItemStack> v{cutWorkItem(w, at)};
  if (!v[0].empty() && !v[0].canMerge(held)) { toast(lv, p, "Something's on the saw already"); return false; }
  const int room = v[0].empty() ? held.maxStack() : v[0].maxStack() - v[0].n;
  const int put = std::min(room, held.n);
  if (put <= 0) return false;
  if (v[0].empty()) { v[0] = held; v[0].n = 0; }
  v[0].n += put;
  if ((held.n -= put) <= 0) held = ItemStack();
  save(w, at, "cutwork", v);
  sfxMaterial(lv, "stone", "place", vec3(at) + vec3(0.5f, 0.6f, 0.5f));
  return true;
}

bool cutWorkTake(Level& lv, Player& p, World& w, ivec3 at) {
  std::vector<ItemStack> v{cutWorkItem(w, at)};
  if (v[0].empty()) return false;
  ItemStack s = v[0]; v[0] = ItemStack();
  save(w, at, "cutwork", v);
  give(lv, p, s);
  return true;
}

int cutWorkDo(Level& lv, Player& p, World& w, ivec3 at, int k, bool all) {
  if (!isCutter(w, at)) return 0;
  std::vector<ItemStack> v{cutWorkItem(w, at)};
  const auto opts = v[0].empty() ? std::vector<std::pair<std::string, int>>{} : stonecutterOptions(v[0].id);
  if (k < 0 || k >= (int)opts.size()) return 0;
  const int blocks = all ? v[0].n : 1;
  ItemStack out(opts[k].first, opts[k].second * blocks);
  if ((v[0].n -= blocks) <= 0) v[0] = ItemStack();
  save(w, at, "cutwork", v);
  // (more than a stack when all are cut: given a stack at a time)
  while (out.n > 0) { ItemStack o = out; o.n = std::min(out.n, std::max(1, o.maxStack())); out.n -= o.n; give(lv, p, o); }
  sfxAt(lv, "saw", vec3(at) + vec3(0.5f, 0.6f, 0.5f));
  return blocks;
}

// ---- where things lie ----
void stationWorkFrame(World& w, ivec3 at, vec3& out, vec3& right) {
  // (a facing model: the way its placer looked, its front toward them; a plain block: the side the work was laid from)
  int m = 0;
  if (const int f = pairFacing(w, at); f >= 0) m = f;
  else if (reg().block(w.getBlock(at.x, at.y, at.z)).has("facingModel")) m = w.getMeta(at.x, at.y, at.z) & 3;
  out = vec3(-DIRX[m], 0, -DIRZ[m]); right = vec3(out.z, 0, -out.x);
}
// where each lies (right, out) on top: the grindstone's wheel 0.1 either side of its middle; the smithing table's gear in
// the middle and the material to the right (SmithingStation LAYOUT); the loom's cloth on its bed, the dye at its corner;
// the fletching table's flint and feather side by side; the cartography table's map and the paper beside it
// (CartographyStation LAYOUT)
vec3 pairLiePos(World& w, ivec3 at, int slot) {
  vec3 out, right; stationWorkFrame(w, at, out, right);
  const std::string st = pairStationAt(w, at);
  vec2 o = slot == 0 ? vec2(0.1f, 0) : vec2(-0.1f, 0);
  if (st == "smithing") o = slot == 0 ? vec2(0.f, 0.06f) : vec2(0.27f, -0.12f);
  else if (st == "loom") o = slot == 0 ? vec2(0.06f, 0.04f) : vec2(0.32f, -0.22f);
  else if (st == "fletching") o = slot == 0 ? vec2(-0.16f, 0.04f) : vec2(0.16f, 0.04f);
  else if (st == "cartography") o = slot == 0 ? vec2(-0.04f, 0.04f) : vec2(0.34f, -0.1f);
  return vec3(at) + vec3(0.5f, 1.f, 0.5f) + right * o.x + out * o.y;
}
float pairLieSize(const std::string& st, int slot) {
  if (st == "smithing") return slot == 0 ? 0.34f : 0.2f;
  if (st == "cartography") return slot == 0 ? 0.5f : 0.24f;
  if (st == "loom") return slot == 0 ? 0.24f : 0.2f;
  return 0.3f;
}
vec3 cutLiePos(World& w, ivec3 at) {
  vec3 out, right; stationWorkFrame(w, at, out, right);
  return vec3(at) + vec3(0.5f, 0.5625f, 0.5f) - right * 0.25f + out * 0.24f;
}

} // namespace dq
