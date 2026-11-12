// sim/station_work.cpp — see station_work.h
#include "sim/station_work.h"
#include "sim/stations.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/magic_combat.h"
#include "core/registry.h"
#include "world/dirs.h"

namespace dq {

namespace {
std::vector<ItemStack> items(World& w, ivec3 at, const char* type, int n) {
  std::vector<ItemStack> v(n);
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (e && e->is_object() && e->value("type", "") == type && e->contains("items") && (*e)["items"].is_array())
    for (int i = 0; i < n && i < (int)(*e)["items"].size(); i++) v[i] = ItemStack::fromJson((*e)["items"][i]);
  return v;
}
void save(World& w, ivec3 at, const char* type, const std::vector<ItemStack>& v) {
  bool any = false; json a = json::array();
  for (auto& s : v) { a.push_back(s.empty() ? json() : s.toJson()); any |= !s.empty(); }
  if (!any) { w.setBlockEntity(at.x, at.y, at.z, nullptr); return; }
  w.setBlockEntity(at.x, at.y, at.z, {{"type", type}, {"items", a}});
}
bool isGrindstone(World& w, ivec3 at) { return reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "grindstone"; }
bool isCutter(World& w, ivec3 at) { return reg().block(w.getBlock(at.x, at.y, at.z)).has("stonecutter"); }
void give(Level& lv, Player& p, ItemStack s) {
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
}
}  // namespace

// ---- the grindstone ----
std::vector<ItemStack> grindWorkItems(World& w, ivec3 at) { return items(w, at, "grindwork", 2); }

bool grindWorkPut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held) {
  if (slot < 0 || slot > 1 || held.empty() || !isGrindstone(w, at)) return false;
  std::vector<ItemStack> v = grindWorkItems(w, at);
  if (!v[slot].empty()) { const int o = 1 - slot; if (!v[o].empty()) { toast(lv, p, "Both sides are taken"); return false; } slot = o; }
  v[slot] = held; v[slot].n = 1;   // (one piece a side: the grindstone screen's)
  enchOf(v[slot]);   // (a treasure book settles what it holds now, not on every look)
  if (--held.n <= 0) held = ItemStack();
  save(w, at, "grindwork", v);
  sfxMaterial(lv, "stone", "place", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  return true;
}

bool grindWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot) {
  if (slot < 0 || slot > 1) return false;
  std::vector<ItemStack> v = grindWorkItems(w, at);
  if (v[slot].empty()) return false;
  ItemStack s = v[slot]; v[slot] = ItemStack();
  save(w, at, "grindwork", v);
  give(lv, p, s);
  return true;
}

StationResult grindWorkResult(World& w, ivec3 at) {
  const std::vector<ItemStack> v = grindWorkItems(w, at);
  if (v[0].empty() && v[1].empty()) return StationResult{};
  return stationResult("grindstone", v[0], v[1]);
}

bool grindWorkDo(Level& lv, Player& p, World& w, ivec3 at) {
  if (!isGrindstone(w, at)) return false;
  std::vector<ItemStack> v = grindWorkItems(w, at);
  const StationResult r = (v[0].empty() && v[1].empty()) ? StationResult{} : stationResult("grindstone", v[0], v[1]);
  if (!r.ok) { toast(lv, p, "Nothing to grind off that"); return false; }
  auto use = [&](int k, int n) { if (v[k].empty() || n <= 0) return; if ((v[k].n -= n) <= 0) v[k] = ItemStack(); };
  use(0, r.useA); use(1, r.useB);
  save(w, at, "grindwork", v);
  give(lv, p, r.out);
  if (r.xp > 0) { addXP(p, r.xp); sfxUi(lv, p, "xp"); }   // (the enchantments' experience back)
  sfxAt(lv, "grind", vec3(at) + vec3(0.5f, 1.f, 0.5f));
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
  const int m = w.getMeta(at.x, at.y, at.z) & 3;   // (a facing model: the way its placer looked; its front faces them)
  out = vec3(-DIRX[m], 0, -DIRZ[m]); right = vec3(out.z, 0, -out.x);
}
vec3 grindLiePos(World& w, ivec3 at, int slot) {
  vec3 out, right; stationWorkFrame(w, at, out, right);
  return vec3(at) + vec3(0.5f, 1.f, 0.5f) + right * (slot == 0 ? 0.1f : -0.1f);
}
vec3 cutLiePos(World& w, ivec3 at) {
  vec3 out, right; stationWorkFrame(w, at, out, right);
  return vec3(at) + vec3(0.5f, 0.5625f, 0.5f) - right * 0.25f + out * 0.24f;
}

} // namespace dq
