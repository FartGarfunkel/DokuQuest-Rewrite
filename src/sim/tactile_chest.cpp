// sim/tactile_chest.cpp — a chest worked as Tactile Blocks' coffre (sim/tactile.h): opened and shut by use, its tray of
// fifteen inside (the other fifteen heaped loose at the bottom), things put in and taken out where you look, its two buttons
#include "sim/tactile.h"
#include "sim/chest_work.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "core/registry.h"
#include "world/world.h"
#include <algorithm>

namespace dq {

namespace {
// (CoffreLayout, CoffreBlock: the tray's slots and the open chest's boxes, sixteenths, north-facing)
const float COL_X[5] = {2.4f, 5.2f, 8.0f, 10.8f, 13.6f}, ROW_Z[3] = {9.5f, 6.5f, 3.5f};
const float GRID_SLAB[6] = {1, 8.5f, 2, 15, 9.6f, 11}, LID_BOX[6] = {0, 9, 11, 16, 16, 16}, LID_POKE[6] = {0, 16, 11, 16, 22, 16};
const float BTN_SORT[6] = {11.5f, 12, 11, 13.5f, 14.7f, 13}, BTN_SWAP[6] = {11.5f, 16, 11, 13.5f, 19, 13};
const int SLOTS = 27, PAGE = 15;

// the ray's entry into a box (sixteenths) in the chest's north frame, -1 if it misses
float rayBox(vec3 o, vec3 d, const float b[6]) {
  float t0 = 0, t1 = 1e9f;
  for (int a = 0; a < 3; a++) {
    const float lo = b[a] / 16 - 0.03f, hi = b[a + 3] / 16 + 0.03f;   // (Tactile's half a pixel of slack)
    if (std::fabs(d[a]) < 1e-6f) { if (o[a] < lo || o[a] > hi) return -1; continue; }
    float ta = (lo - o[a]) / d[a], tb = (hi - o[a]) / d[a]; if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return -1;
  }
  return t0;
}
int slotAt(float nx, float nz) {   // (CoffreLayout.slotAt, sixteenths)
  if (nx < 1 || nx > 15 || nz < 2 || nz > 11) return -1;
  const int col = std::clamp((int)std::lround((nx - COL_X[0]) / 2.8f), 0, 4), row = std::clamp((int)std::lround((ROW_Z[0] - nz) / 3.f), 0, 2);
  return row * 5 + col;
}
json& itemsOf(json& e) {
  if (!e.contains("items") || !e["items"].is_array()) e["items"] = json::array();
  while (e["items"].size() < (size_t)SLOTS) e["items"].push_back(nullptr);
  return e["items"];
}
int pageOf(const json& e) { return e.value("page", 0) & 1; }
// the page packed (CoffreBlockEntity.compact): its stacks merged and moved to the front
void compact(json& items, int page, int PG = PAGE) {
  const int a = page * PG, b = std::min(SLOTS, a + PG);
  std::vector<ItemStack> v;
  for (int i = a; i < b; i++) {
    ItemStack s = ItemStack::fromJson(items[i]);
    if (s.empty()) continue;
    for (ItemStack& m : v) {
      if (s.empty()) break;
      if (m.id != s.id || !m.canMerge(s) || m.n >= m.maxStack()) continue;
      const int mv = std::min(s.n, m.maxStack() - m.n); m.n += mv; s.n -= mv;
    }
    if (s.n > 0) v.push_back(s);
  }
  for (int i = a; i < b; i++) items[i] = i - a < (int)v.size() ? v[i - a].toJson() : json();
}
bool placeOne(json& items, int page, ItemStack& held, int PG = PAGE) {   // (CoffreBlockEntity.placeOne: onto the same, else the first free)
  const int a = page * PG, b = std::min(SLOTS, a + PG);
  for (int i = a; i < b; i++) {
    ItemStack s = ItemStack::fromJson(items[i]);
    if (s.empty() || s.id != held.id || !s.canMerge(held) || s.n >= s.maxStack()) continue;
    s.n++; items[i] = s.toJson(); if (--held.n <= 0) held = ItemStack(); return true;
  }
  for (int i = a; i < b; i++) {
    if (!ItemStack::fromJson(items[i]).empty()) continue;
    ItemStack s = held; s.n = 1; items[i] = s.toJson(); if (--held.n <= 0) held = ItemStack(); return true;
  }
  return false;
}
}  // namespace

namespace {
// (the chest's own layout: its tray of fifteen in five by three, a page of the 27; under the barrel's rim instead of the chest's)
const int B_PAGE = PAGE;
float barrelTrayY(World& w, ivec3 at) { return chestLidRow(w, at) / 2.f - 1.f; }   // (sixteenths: a pixel under the rim, as the chest's 9.5 under its 10)
// (its front: the side it was opened from, kept in the block's meta as a chest's facing is, so render/chest_layer.cpp swings
// its top away from you on its back edge)
int barrelFacing(World& w, ivec3 at) { return w.getMeta(at.x, at.y, at.z) & 3; }   // (its front: render/chest_layer.cpp hinges its top on the far side)
// a rim button's box (k 0 sort, 1 next layer) on the raised top's underside, in the north frame (sixteenths)
void barrelButtonBox(World& w, ivec3 at, int k, float out[6]) {
  const vec3 c = tactileBarrelButton(w, at, k) * 16.f;
  out[0] = c.x - 1.6f; out[1] = c.y - 1.6f; out[2] = c.z - 1.6f; out[3] = c.x + 1.6f; out[4] = c.y + 1.6f; out[5] = c.z + 1.6f;
}
}  // namespace

namespace {
// the raised top's box (north frame, sixteenths): its four corners as shut (the board over lidRow, all across), swung up
void barrelLidBox(World& w, ivec3 at, float out[6]) {
  const float lr = chestLidRow(w, at) / 2.f;
  vec3 lo(1e9f), hi(-1e9f);
  for (int k = 0; k < 8; k++) {
    // (a chest-lid point is given as if hinged at y 10, z 12: shift the barrel's own corners there)
    const vec3 q(k & 1 ? 16.f : 0.f, (k & 2 ? 16.f : lr) - lr + 10, (k & 4 ? 16.f : 0.f) - 4);
    const vec3 p = tactileBarrelLidPoint(w, at, q) * 16.f;
    lo = glm::min(lo, p); hi = glm::max(hi, p);
  }
  out[0] = lo.x; out[1] = lo.y; out[2] = lo.z; out[3] = hi.x; out[4] = hi.y; out[5] = hi.z;
}
}  // namespace

vec3 tactileCabinetDoorPoint(World& w, ivec3 at, vec3 q) {
  const float fa = (w.getMeta(at.x, at.y, at.z) & 3) * PI / 2, da = -1.85f;
  vec3 p = q / 16.f;
  const vec3 hinge(2 / 16.f, 0, 15.2f / 16.f);
  auto rotY = [](vec3 v, float a) { return vec3(v.x * std::cos(a) + v.z * std::sin(a), v.y, -v.x * std::sin(a) + v.z * std::cos(a)); };
  p = hinge + rotY(p - hinge, da);
  return vec3(0.5f, 0, 0.5f) + rotY(p - vec3(0.5f, 0, 0.5f), fa);
}
namespace {
float rayAround(vec3 o, vec3 d, vec3 c, vec3 h) {   // (an axis box about c, half sizes h, blocks)
  float t0 = 0, t1 = 1e9f;
  for (int a = 0; a < 3; a++) {
    const float lo = c[a] - h[a], hi = c[a] + h[a];
    if (std::fabs(d[a]) < 1e-6f) { if (o[a] < lo || o[a] > hi) return -1; continue; }
    float ta = (lo - o[a]) / d[a], tb = (hi - o[a]) / d[a]; if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return -1;
  }
  return t0;
}
}  // namespace
float tactileCabinetButtonRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  if (!isCabinet(w, at) || !chestLidOpen(w, at)) return -1;
  return rayAround(eye - vec3(at), dir, tactileCabinetDoorPoint(w, at, vec3(12, 9, 14.2f)), vec3(0.09f));
}
float tactileCabinetDoorRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  if (!isCabinet(w, at) || !chestLidOpen(w, at)) return -1;
  float best = -1;   // (the door, a few boxes along it)
  for (float x : {3.f, 6.f, 9.f, 12.f}) for (float y : {3.f, 7.f, 11.f}) {
    const float t = rayAround(eye - vec3(at), dir, tactileCabinetDoorPoint(w, at, vec3(x, y, 14.8f)), vec3(0.11f, 0.13f, 0.11f));
    if (t >= 0 && (best < 0 || t < best)) best = t;
  }
  return best;
}
void tactileCabinetSort(Level& lv, Player& p, World& w, ivec3 at) {
  std::vector<ItemStack> v = chestItems(lv, p, w, at), k;
  for (auto& s : v) if (!s.empty()) k.push_back(s);
  std::stable_sort(k.begin(), k.end(), [](const ItemStack& a, const ItemStack& b) { return a.id < b.id; });
  for (size_t i = 0; i < v.size(); i++) v[i] = i < k.size() ? k[i] : ItemStack();
  chestSetItems(lv, p, w, at, v);
  if (json* e = w.blockEntity(at.x, at.y, at.z)) { (*e)["press0"] = e->value("press0", 0) + 1; w.blockEntityChanged(at.x, at.y, at.z); }
  sfxAt(lv, "click", vec3(at) + vec3(0.5f));
}

bool tactileChest(World& w, ivec3 at) {
  const std::string& k = reg().block(w.getBlock(at.x, at.y, at.z)).key;
  if (k != "chest" && k != "ender_chest" && k != "party_chest" && !(k.size() > 13 && k.compare(k.size() - 13, 13, "painted_chest") == 0)) return false;   // (the plain, the painted, the ender and the party chest)
  ivec3 hs[2];
  return chestHalves(w, at, hs) <= 1;   // (a large chest stays ours: Tactile's is one block)
}

TactileChestAim tactileChestAim(World& w, ivec3 at, vec3 eye, vec3 dir) {
  TactileChestAim r;
  if (!tactileChest(w, at)) return r;
  const int f = tactileFacing(w, at);
  // the ray in the chest's north frame (the turn undone about the block's middle)
  const vec3 o = tactileNorthLocal(f, eye - vec3(at)), o2 = tactileNorthLocal(f, eye + dir - vec3(at));
  const vec3 d = o2 - o;
  if (!chestLidOpen(w, at)) { r.what = TactileChestAim::Shut; return r; }
  float best = 1e9f;
  auto test = [&](const float b[6], int what) { const float t = rayBox(o, d, b); if (t >= 0 && t < best) { best = t; r.what = what; } };
  test(BTN_SORT, TactileChestAim::Sort); test(BTN_SWAP, TactileChestAim::Swap);   // (first: their faces are flush with the lid's, a tie goes to them)
  test(LID_BOX, TactileChestAim::Lid); test(LID_POKE, TactileChestAim::Lid);
  test(GRID_SLAB, TactileChestAim::Tray);
  if (r.what == TactileChestAim::Tray) {
    const vec3 p = o + d * best;
    r.slot = slotAt(p.x * 16, p.z * 16);
    if (r.slot < 0) r.what = TactileChestAim::Lid;
  }
  if (r.what == TactileChestAim::None) r.what = TactileChestAim::Lid;   // (the open chest's rim: shuts it)
  return r;
}

float tactileChestLidRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  if (!tactileChest(w, at) || !chestLidOpen(w, at)) return -1;
  const int f = tactileFacing(w, at);
  const vec3 o = tactileNorthLocal(f, eye - vec3(at)), d = tactileNorthLocal(f, eye + dir - vec3(at)) - o;
  float best = -1;
  for (const float* b : {LID_BOX, LID_POKE, BTN_SORT, BTN_SWAP}) { const float t = rayBox(o, d, b); if (t >= 0 && (best < 0 || t < best)) best = t; }
  return best;
}

bool tactileChestOutline(World& w, ivec3 at, std::vector<std::pair<vec3, vec3>>& out) {
  if (!tactileChest(w, at) || !chestLidOpen(w, at)) return false;
  const int f = tactileFacing(w, at);
  static const float BOXES[2][6] = {{0, 0, 0, 16, 10, 16}, {0, 10, 11, 16, 22, 16}};   // (the body; the lid standing up at the back)
  for (auto& b : BOXES) {
    vec3 lo(1e9f), hi(-1e9f);
    for (int k = 0; k < 8; k++) {
      const vec3 c = vec3(at) + tactileFromNorth(f, vec3(k & 1 ? b[3] : b[0], k & 2 ? b[4] : b[1], k & 4 ? b[5] : b[2]) / 16.f);
      lo = glm::min(lo, c); hi = glm::max(hi, c);
    }
    out.push_back({lo, hi});
  }
  return true;
}

float tactileExtraRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  const float t = tactileChestLidRay(w, at, eye, dir);
  if (t >= 0) return t;
  if (tactileBarrel(w, at) && chestLidOpen(w, at)) {   // (an open barrel's rim buttons stand up over it)
    const int f = barrelFacing(w, at);
    const vec3 o = tactileNorthLocal(f, eye - vec3(at)), d = tactileNorthLocal(f, eye + dir - vec3(at)) - o;
    float best = -1;
    float bs[6], bw[6], lb[6]; barrelButtonBox(w, at, 0, bs); barrelButtonBox(w, at, 1, bw); barrelLidBox(w, at, lb);
    for (const float* b : {(const float*)bs, (const float*)bw, (const float*)lb}) { const float t = rayBox(o, d, b); if (t >= 0 && (best < 0 || t < best)) best = t; }
    if (best >= 0) return best;
  }
  if (const float tb = tactileCabinetButtonRay(w, at, eye, dir); tb >= 0) return tb;
  if (const float td = tactileCabinetDoorRay(w, at, eye, dir); td >= 0) return td;   // (an open cupboard's door: aimed at, it shuts)
  if (reg().block(w.getBlock(at.x, at.y, at.z)).key == "brewing_stand") {   // (the still's recipe book, lying in front)
    const int f = tactileFacing(w, at);
    const vec3 o = tactileNorthLocal(f, eye - vec3(at)), d = tactileNorthLocal(f, eye + dir - vec3(at)) - o;
    static const float BOOK[6] = {1, 0, 0, 15, 1.6f, 7.5f};
    return rayBox(o, d, BOOK);
  }
  return -1;
}

bool tactileChestUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak) {
  if (!tactileChest(w, at)) return false;
  const TactileChestAim a = tactileChestAim(w, at, eye, dir);
  const vec3 c = vec3(at) + vec3(0.5f);
  json* e = w.blockEntity(at.x, at.y, at.z, true);   // (its page and buttons; what's in it: chestItems, an ender chest's your own)
  if (!e) return false;
  if (!e->is_object()) *e = json::object();
  if (a.what == TactileChestAim::Shut) { containerEntity(w, at.x, at.y, at.z); setChestLidOpen(w, at, true); sfxAt(lv, "chest", c); return true; }
  json itemsArr = json::array(); for (const ItemStack& q : chestItems(lv, p, w, at)) itemsArr.push_back(q.empty() ? json() : q.toJson());
  while (itemsArr.size() < (size_t)SLOTS) itemsArr.push_back(nullptr);
  json& items = itemsArr;
  struct WriteBack { Level& lv; Player& p; World& w; ivec3 at; json& items; ~WriteBack() {
    std::vector<ItemStack> v; for (auto& j : items) v.push_back(ItemStack::fromJson(j)); chestSetItems(lv, p, w, at, v); } } wb{lv, p, w, at, items};
  const int page = pageOf(*e);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); };
  if (a.what == TactileChestAim::Sort) {   // (doSort; sneaking: every stack of both pages merged)
    std::vector<ItemStack> v;
    const int a0 = sneak ? 0 : page * PAGE, b0 = sneak ? SLOTS : std::min(SLOTS, page * PAGE + PAGE);
    for (int i = a0; i < b0; i++) { ItemStack s = ItemStack::fromJson(items[i]); if (!s.empty()) v.push_back(s); }
    std::stable_sort(v.begin(), v.end(), [](const ItemStack& x, const ItemStack& y) { return x.id < y.id; });
    for (int i = a0; i < b0; i++) items[i] = i - a0 < (int)v.size() ? v[i - a0].toJson() : json();
    compact(items, page); if (sneak) compact(items, 1 - page);
    (*e)["press0"] = e->value("press0", 0) + 1; changed(); sfxAt(lv, "click", c);
    return true;
  }
  if (a.what == TactileChestAim::Swap) { (*e)["page"] = 1 - page; (*e)["press1"] = e->value("press1", 0) + 1; changed(); sfxAt(lv, "click", c); return true; }
  if (a.what == TactileChestAim::Tray) {
    if (sneak) {   // (quickStack: what you carry that's already in it goes in)
      bool any = false;
      for (ItemStack& s : p.inv) {
        if (s.empty()) continue;
        bool has = false; for (int i = page * PAGE; i < std::min(SLOTS, page * PAGE + PAGE); i++) if (ItemStack::fromJson(items[i]).id == s.id) has = true;
        while (has && !s.empty() && placeOne(items, page, s)) any = true;
      }
      if (any) { compact(items, page); changed(); sfxAt(lv, "pop", c); }
      return true;
    }
    if (held.empty()) {   // (an empty hand: what lies there, the whole of it, back into your bag)
      const int k = page * PAGE + a.slot;
      ItemStack s2 = k < SLOTS ? ItemStack::fromJson(items[k]) : ItemStack();
      if (s2.empty()) return true;
      items[k] = nullptr; compact(items, page); changed();
      tactileGive(lv, p, s2); sfxAt(lv, "pop", c);
      return true;
    }
    int placed = 0;
    for (int n = 0; n < (all ? 64 : 1) && !held.empty() && placeOne(items, page, held); n++) placed++;
    if (placed) { compact(items, page); changed(); sfxAt(lv, "pop", c); }
    else toast(lv, p, "That page is full (the right-hand button turns to the other)");
    return true;
  }
  setChestLidOpen(w, at, false); sfxAt(lv, "chest", c);   // (the lid, the rim: shut)
  return true;
}

bool tactileChestTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole) {
  const TactileChestAim a = tactileChestAim(w, at, eye, dir);
  if (a.what != TactileChestAim::Tray || a.slot < 0) return false;
  json* e = w.blockEntity(at.x, at.y, at.z, true);
  if (!e) return false;
  if (!e->is_object()) *e = json::object();
  json items = json::array(); for (const ItemStack& q : chestItems(lv, p, w, at)) items.push_back(q.empty() ? json() : q.toJson());
  while (items.size() < (size_t)SLOTS) items.push_back(nullptr);
  struct WriteBack { Level& lv; Player& p; World& w; ivec3 at; json& items; ~WriteBack() {
    std::vector<ItemStack> v; for (auto& j : items) v.push_back(ItemStack::fromJson(j)); chestSetItems(lv, p, w, at, v); } } wb{lv, p, w, at, items};
  const int page = pageOf(*e), k = page * PAGE + a.slot;
  if (k >= SLOTS) return false;
  ItemStack s = ItemStack::fromJson(items[k]);
  if (s.empty()) return false;
  ItemStack taken = s; taken.n = whole ? s.n : 1;
  s.n -= taken.n; items[k] = s.n <= 0 ? json() : s.toJson();
  compact(items, page);
  w.blockEntityChanged(at.x, at.y, at.z);
  tactileGive(lv, p, taken);
  sfxAt(lv, "pop", vec3(at) + vec3(0.5f));
  return true;
}

// ---- a barrel, worked the same way: a tray of nine under its rim (three layers of the 27), its two buttons on the rim ----

vec3 tactileBarrelLidPoint(World& w, ivec3 at, vec3 chestLidPx) {
  // a point of the chest's lid (its own sixteenths, as shut: hinge at y 10, z 12) on the barrel's top: its hinge at the top's
  // underside at the back (y lidRow, z 16), swung up 1.25 as render/chest_layer.cpp swings it
  const float lr = chestLidRow(w, at) / 2.f, ang = 1.25f;
  const vec3 rel = (chestLidPx - vec3(0, 10, 12)) / 16.f;
  const float y = rel.y * std::cos(ang) - rel.z * std::sin(ang), z = rel.y * std::sin(ang) + rel.z * std::cos(ang);
  return vec3(rel.x, lr / 16 + y, 1 + z);
}
vec3 tactileBarrelButton(World& w, ivec3 at, int k) {   // (the chest lid's two buttons' middles: sort 12.5,10.25,8.65; next 12.5,10.25,4.5)
  return tactileBarrelLidPoint(w, at, k == 0 ? vec3(12.5f, 10.25f, 8.65f) : vec3(12.5f, 10.25f, 4.5f));
}
bool tactileBarrel(World& w, ivec3 at) {
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  return b.has("container") && b.key.size() >= 6 && b.key.compare(b.key.size() - 6, 6, "barrel") == 0 && !b.has("sealed");
}
int tactileBarrelFacing(World& w, ivec3 at) { return barrelFacing(w, at); }

TactileChestAim tactileBarrelAim(World& w, ivec3 at, vec3 eye, vec3 dir) {
  TactileChestAim r;
  if (!tactileBarrel(w, at)) return r;
  if (!chestLidOpen(w, at)) { r.what = TactileChestAim::Shut; return r; }
  const int f = barrelFacing(w, at);
  const vec3 o = tactileNorthLocal(f, eye - vec3(at)), d = tactileNorthLocal(f, eye + dir - vec3(at)) - o;
  float best = 1e9f;
  auto test = [&](const float b[6], int what) { const float t = rayBox(o, d, b); if (t >= 0 && t < best) { best = t; r.what = what; } };
  float bs[6], bw[6]; barrelButtonBox(w, at, 0, bs); barrelButtonBox(w, at, 1, bw);
  test(bs, TactileChestAim::Sort); test(bw, TactileChestAim::Swap);
  float lb[6]; barrelLidBox(w, at, lb);
  if (r.what == TactileChestAim::None) test(lb, TactileChestAim::Lid);   // (the raised top: shuts it)
  const float ty = barrelTrayY(w, at);
  const float tray[6] = {GRID_SLAB[0], ty - 1, GRID_SLAB[2], GRID_SLAB[3], ty + 0.6f, GRID_SLAB[5]};
  test(tray, TactileChestAim::Tray);
  if (r.what == TactileChestAim::Tray) {   // (the chest's slot under where the look meets the tray)
    const float t = std::fabs(d.y) > 1e-5f ? (ty / 16 - o.y) / d.y : best;
    const vec3 p = o + d * (t > 0 ? t : best);
    r.slot = slotAt(std::clamp(p.x * 16, 1.f, 15.f), std::clamp(p.z * 16, 2.f, 11.f));
  }
  if (r.what == TactileChestAim::None) r.what = TactileChestAim::Lid;   // (the rim: shuts it)
  return r;
}

bool tactileBarrelUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak) {
  if (!tactileBarrel(w, at)) return false;
  json* e = containerEntity(w, at.x, at.y, at.z);
  if (!e) return false;
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); };
  if (!chestLidOpen(w, at)) {   // (opened: its front to whoever opened it)
    vec3 toMe = eye - (vec3(at) + vec3(0.5f)); toMe.y = 0;
    int bf = 2; float bd = -1e9f;
    for (int f = 0; f < 4; f++) { const vec3 fr = tactileFromNorth(f, vec3(0.5f, 0, 0)) - vec3(0.5f, 0, 0.5f); const float dd = glm::dot(fr, toMe); if (dd > bd) { bd = dd; bf = f; } }
    const u8 m = w.getMeta(at.x, at.y, at.z);
    const u8 want = (u8)bf;   // (its front toward you: render/chest_layer.cpp hinges the top on the far side)
    if ((m & 3) != want) w.setBlock(at.x, at.y, at.z, w.getBlock(at.x, at.y, at.z), (u8)((m & ~3) | want));
    setChestLidOpen(w, at, true); sfxAt(lv, "chest", c);
    return true;
  }
  const TactileChestAim a = tactileBarrelAim(w, at, eye, dir);
  json& items = itemsOf(*e);
  const int pages = (SLOTS + B_PAGE - 1) / B_PAGE, page = std::clamp(e->value("page", 0), 0, pages - 1);
  if (a.what == TactileChestAim::Sort) {
    std::vector<ItemStack> v; for (int i = 0; i < SLOTS; i++) { ItemStack s = ItemStack::fromJson(items[i]); if (!s.empty()) v.push_back(s); }
    std::stable_sort(v.begin(), v.end(), [](const ItemStack& x, const ItemStack& y) { return x.id < y.id; });
    for (int i = 0; i < SLOTS; i++) items[i] = i < (int)v.size() ? v[i].toJson() : json();
    for (int pg = 0; pg < pages; pg++) compact(items, pg, B_PAGE);
    (*e)["press0"] = e->value("press0", 0) + 1; changed(); sfxAt(lv, "click", c);
    return true;
  }
  if (a.what == TactileChestAim::Swap) { (*e)["page"] = (page + 1) % pages; (*e)["press1"] = e->value("press1", 0) + 1; changed(); sfxAt(lv, "click", c); return true; }
  if (a.what == TactileChestAim::Tray) {
    const int k = page * B_PAGE + a.slot;
    if (held.empty()) {   // (an empty hand: what lies there, back into your bag)
      if (k >= SLOTS) return true;
      ItemStack s = ItemStack::fromJson(items[k]);
      if (s.empty()) return true;
      items[k] = nullptr; compact(items, page, B_PAGE); changed();
      tactileGive(lv, p, s); sfxAt(lv, "pop", c);
      return true;
    }
    if (sneak) return true;
    int placed = 0;
    for (int n = 0; n < (all ? 64 : 1) && !held.empty() && placeOne(items, page, held, B_PAGE); n++) placed++;
    if (placed) { compact(items, page, B_PAGE); changed(); sfxAt(lv, "pop", c); }
    else toast(lv, p, "This layer is full (the button on the rim turns to the next)");
    return true;
  }
  setChestLidOpen(w, at, false); sfxAt(lv, "chest", c);
  return true;
}

bool tactileBarrelTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole) {
  const TactileChestAim a = tactileBarrelAim(w, at, eye, dir);
  if (a.what != TactileChestAim::Tray || a.slot < 0) return false;
  json* e = w.blockEntity(at.x, at.y, at.z);
  if (!e) return false;
  json& items = itemsOf(*e);
  const int page = e->value("page", 0), k = page * B_PAGE + a.slot;
  if (k >= SLOTS) return false;
  ItemStack s = ItemStack::fromJson(items[k]);
  if (s.empty()) return false;
  ItemStack taken = s; taken.n = whole ? s.n : 1;
  s.n -= taken.n; items[k] = s.n <= 0 ? json() : s.toJson();
  compact(items, page, B_PAGE);
  w.blockEntityChanged(at.x, at.y, at.z);
  tactileGive(lv, p, taken);
  sfxAt(lv, "pop", vec3(at) + vec3(0.5f));
  return true;
}

} // namespace dq
