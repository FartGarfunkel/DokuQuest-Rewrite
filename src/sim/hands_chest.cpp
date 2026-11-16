// sim/hands_chest.cpp — a chest worked as the hands-on stations' chest (sim/hands.h): opened and shut by use, its tray of
// fifteen inside (the other fifteen heaped loose at the bottom), things put in and taken out where you look, its two buttons
#include "sim/hands.h"
#include "sim/chest_work.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "core/registry.h"
#include "world/world.h"
#include <algorithm>

namespace dq {

namespace {
// (the chest's layout: the tray's slots and the open chest's boxes, sixteenths, north-facing)
const float COL_X[5] = {2.4f, 5.2f, 8.0f, 10.8f, 13.6f}, ROW_Z[3] = {9.5f, 6.5f, 3.5f};
const float GRID_SLAB[6] = {1, 8.5f, 2, 15, 9.6f, 11}, LID_BOX[6] = {0, 9, 11, 16, 16, 16}, LID_POKE[6] = {0, 16, 11, 16, 22, 16};
const float BTN_SORT[6] = {11.5f, 12, 11, 13.5f, 14.7f, 13}, BTN_SWAP[6] = {11.5f, 16, 11, 13.5f, 19, 13};
const int SLOTS = 27, PAGE = 15;

// the ray's entry into a box (sixteenths) in the chest's north frame, -1 if it misses
float rayBox(vec3 o, vec3 d, const float b[6]) {
  float t0 = 0, t1 = 1e9f;
  for (int a = 0; a < 3; a++) {
    const float lo = b[a] / 16 - 0.03f, hi = b[a + 3] / 16 + 0.03f;   // (the hands-on half a pixel of slack)
    if (std::fabs(d[a]) < 1e-6f) { if (o[a] < lo || o[a] > hi) return -1; continue; }
    float ta = (lo - o[a]) / d[a], tb = (hi - o[a]) / d[a]; if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return -1;
  }
  return t0;
}
int slotAt(float nx, float nz) {   // (slotAt, sixteenths)
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
// the page packed (compact): its stacks merged and moved to the front
void compact(json& items, int page, int PG = PAGE, int NS = SLOTS) {
  const int a = page * PG, b = std::min(NS, a + PG);
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
bool placeOne(json& items, int page, ItemStack& held, int PG = PAGE, int NS = SLOTS) {   // (placeOne: onto the same, else the first free)
  const int a = page * PG, b = std::min(NS, a + PG);
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
  const vec3 c = handsBarrelButton(w, at, k) * 16.f;
  const float r = 2.8f;   // (drawn bigger than the chest's: render/hands_chests.cpp BARREL_BUTTON)
  out[0] = c.x - r; out[1] = c.y - r; out[2] = c.z - r; out[3] = c.x + r; out[4] = c.y + r; out[5] = c.z + r;
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
    const vec3 p = handsBarrelLidPoint(w, at, q) * 16.f;
    lo = glm::min(lo, p); hi = glm::max(hi, p);
  }
  out[0] = lo.x; out[1] = lo.y; out[2] = lo.z; out[3] = hi.x; out[4] = hi.y; out[5] = hi.z;
}
}  // namespace

vec3 handsCabinetDoorPoint(World& w, ivec3 at, vec3 q) {
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
float handsCabinetButtonRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  if (!isCabinet(w, at) || !chestLidOpen(w, at)) return -1;
  return rayAround(eye - vec3(at), dir, handsCabinetDoorPoint(w, at, vec3(12, 9, 14.2f)), vec3(0.09f));
}
float handsCabinetDoorRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  if (!isCabinet(w, at) || !chestLidOpen(w, at)) return -1;
  float best = -1;   // (the door, a few boxes along it)
  for (float x : {3.f, 6.f, 9.f, 12.f}) for (float y : {3.f, 7.f, 11.f}) {
    const float t = rayAround(eye - vec3(at), dir, handsCabinetDoorPoint(w, at, vec3(x, y, 14.8f)), vec3(0.11f, 0.13f, 0.11f));
    if (t >= 0 && (best < 0 || t < best)) best = t;
  }
  return best;
}
void handsCabinetSort(Level& lv, Player& p, World& w, ivec3 at) {
  std::vector<ItemStack> v = chestItems(lv, p, w, at), k;
  for (auto& s : v) if (!s.empty()) k.push_back(s);
  std::stable_sort(k.begin(), k.end(), [](const ItemStack& a, const ItemStack& b) { return a.id < b.id; });
  for (size_t i = 0; i < v.size(); i++) v[i] = i < k.size() ? k[i] : ItemStack();
  chestSetItems(lv, p, w, at, v);
  if (json* e = w.blockEntity(at.x, at.y, at.z)) { (*e)["press0"] = e->value("press0", 0) + 1; w.blockEntityChanged(at.x, at.y, at.z); }
  sfxAt(lv, "click", vec3(at) + vec3(0.5f));
}

bool handsChest(World& w, ivec3 at) {
  const std::string& k = reg().block(w.getBlock(at.x, at.y, at.z)).key;
  return k == "chest" || k == "ender_chest" || k == "party_chest" || (k.size() > 13 && k.compare(k.size() - 13, 13, "painted_chest") == 0);   // (the plain, the painted, the ender and the party chest)
  // (a large chest: each half worked as a chest of its own, its own 27 of the 54, its own tray, page, buttons and sheet; the
  // two lids go up and down together: sim/chest_work.h setChestLidOpen)
}
ChestLayout handsChestLayout(World& w, ivec3 at) {
  ChestLayout L; L.key = at;
  ivec3 hs[2];
  if (chestHalves(w, at, hs) != 2) return L;
  const ivec3 other = hs[0] == at ? hs[1] : hs[0];
  const int f = handsFacing(w, at);
  const vec3 xdir = handsFromNorth(f, vec3(1, 0, 0)) - handsFromNorth(f, vec3(0));   // (the chest's own left to right)
  const bool left = glm::dot(vec3(other - at), xdir) > 0;
  L.slots = 54; L.pg = 30; L.off = left ? 0 : 15; L.key = left ? at : other; L.buttons = !left; L.sheet = left;
  return L;
}
namespace {
// every slot of the chest (both halves of a large one) as json, written back when done
struct AllItems {
  Level& lv; Player& p; World& w; ivec3 at; json items = json::array();
  AllItems(Level& lv, Player& p, World& w, ivec3 at, int n) : lv(lv), p(p), w(w), at(at) {
    for (const ItemStack& q : chestItems(lv, p, w, at)) items.push_back(q.empty() ? json() : q.toJson());
    while ((int)items.size() < n) items.push_back(nullptr);
  }
  ~AllItems() { std::vector<ItemStack> v; for (auto& j : items) v.push_back(ItemStack::fromJson(j)); chestSetItems(lv, p, w, at, v); }
};
}  // namespace

HandsChestAim handsChestAim(World& w, ivec3 at, vec3 eye, vec3 dir) {
  HandsChestAim r;
  if (!handsChest(w, at)) return r;
  const int f = handsFacing(w, at);
  // the ray in the chest's north frame (the turn undone about the block's middle)
  const vec3 o = handsNorthLocal(f, eye - vec3(at)), o2 = handsNorthLocal(f, eye + dir - vec3(at));
  const vec3 d = o2 - o;
  if (!chestLidOpen(w, at)) { r.what = HandsChestAim::Shut; return r; }
  float best = 1e9f;
  auto test = [&](const float b[6], int what) { const float t = rayBox(o, d, b); if (t >= 0 && t < best) { best = t; r.what = what; } };
  const ChestLayout L = handsChestLayout(w, at);
  if (L.buttons) { test(BTN_SORT, HandsChestAim::Sort); test(BTN_SWAP, HandsChestAim::Swap); }   // (first: their faces are flush with the lid's, a tie goes to them)
  test(LID_BOX, HandsChestAim::Lid); test(LID_POKE, HandsChestAim::Lid);
  test(GRID_SLAB, HandsChestAim::Tray);
  if (r.what == HandsChestAim::Tray) {
    const vec3 p = o + d * best;
    r.slot = slotAt(p.x * 16, p.z * 16);
    if (r.slot < 0) r.what = HandsChestAim::Lid;
  }
  if (r.what == HandsChestAim::None) r.what = HandsChestAim::Lid;   // (the open chest's rim: shuts it)
  return r;
}

float handsChestLidRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  if (!handsChest(w, at) || !chestLidOpen(w, at)) return -1;
  const int f = handsFacing(w, at);
  const vec3 o = handsNorthLocal(f, eye - vec3(at)), d = handsNorthLocal(f, eye + dir - vec3(at)) - o;
  float best = -1;
  const bool btn = handsChestLayout(w, at).buttons;
  for (const float* b : {LID_BOX, LID_POKE, BTN_SORT, BTN_SWAP}) { if (!btn && (b == BTN_SORT || b == BTN_SWAP)) continue; const float t = rayBox(o, d, b); if (t >= 0 && (best < 0 || t < best)) best = t; }
  return best;
}

bool handsChestOutline(World& w, ivec3 at, std::vector<std::pair<vec3, vec3>>& out) {
  if (!handsChest(w, at) || !chestLidOpen(w, at)) return false;
  const int f = handsFacing(w, at);
  static const float BOXES[2][6] = {{0, 0, 0, 16, 10, 16}, {0, 10, 11, 16, 22, 16}};   // (the body; the lid standing up at the back)
  for (auto& b : BOXES) {
    vec3 lo(1e9f), hi(-1e9f);
    for (int k = 0; k < 8; k++) {
      const vec3 c = vec3(at) + handsFromNorth(f, vec3(k & 1 ? b[3] : b[0], k & 2 ? b[4] : b[1], k & 4 ? b[5] : b[2]) / 16.f);
      lo = glm::min(lo, c); hi = glm::max(hi, c);
    }
    out.push_back({lo, hi});
  }
  return true;
}

float handsExtraRay(World& w, ivec3 at, vec3 eye, vec3 dir) {
  const float t = handsChestLidRay(w, at, eye, dir);
  if (t >= 0) return t;
  if (handsBarrel(w, at) && chestLidOpen(w, at)) {   // (an open barrel's rim buttons stand up over it)
    const int f = barrelFacing(w, at);
    const vec3 o = handsNorthLocal(f, eye - vec3(at)), d = handsNorthLocal(f, eye + dir - vec3(at)) - o;
    float best = -1;
    float bs[6], bw[6], lb[6]; barrelButtonBox(w, at, 0, bs); barrelButtonBox(w, at, 1, bw); barrelLidBox(w, at, lb);
    for (const float* b : {(const float*)bs, (const float*)bw, (const float*)lb}) { const float t = rayBox(o, d, b); if (t >= 0 && (best < 0 || t < best)) best = t; }
    if (best >= 0) return best;
  }
  if (const float tb = handsCabinetButtonRay(w, at, eye, dir); tb >= 0) return tb;
  if (const float td = handsCabinetDoorRay(w, at, eye, dir); td >= 0) return td;   // (an open cupboard's door: aimed at, it shuts)
  if (reg().block(w.getBlock(at.x, at.y, at.z)).key == "brewing_stand") {   // (the still's recipe book, lying in front)
    const int f = handsFacing(w, at);
    const vec3 o = handsNorthLocal(f, eye - vec3(at)), d = handsNorthLocal(f, eye + dir - vec3(at)) - o;
    static const float BOOK[6] = {1, 0, 0, 15, 1.6f, 7.5f};
    return rayBox(o, d, BOOK);
  }
  return -1;
}

bool handsChestUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak) {
  if (!handsChest(w, at)) return false;
  const HandsChestAim a = handsChestAim(w, at, eye, dir);
  const vec3 c = vec3(at) + vec3(0.5f);
  json* e = w.blockEntity(at.x, at.y, at.z, true);   // (its page and buttons; what's in it: chestItems, an ender chest's your own)
  if (!e) return false;
  if (!e->is_object()) *e = json::object();
  if (a.what == HandsChestAim::Shut) {   // (a large chest: both halves' records, so both lids are drawn up)
    ivec3 hs[2]; const int n = chestHalves(w, at, hs);
    for (int i = 0; i < n; i++) containerEntity(w, hs[i].x, hs[i].y, hs[i].z);
    containerEntity(w, at.x, at.y, at.z); setChestLidOpen(w, at, true); sfxAt(lv, "chest", c); return true;
  }
  const ChestLayout L = handsChestLayout(w, at);
  const int NS = L.slots, PG = L.pg;
  json* ke = L.key == at ? e : w.blockEntity(L.key.x, L.key.y, L.key.z, true);   // (a large chest's page and buttons: its left half's)
  if (!ke) return false;
  if (!ke->is_object()) *ke = json::object();
  AllItems slotsAll(lv, p, w, at, NS);
  json& items = slotsAll.items;
  const int page = pageOf(*ke);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); if (L.key != at) w.blockEntityChanged(L.key.x, L.key.y, L.key.z); };
  if (a.what == HandsChestAim::Sort) {   // (doSort; sneaking: every stack of both pages merged)
    std::vector<ItemStack> v;
    const int a0 = sneak ? 0 : page * PG, b0 = sneak ? NS : std::min(NS, page * PG + PG);
    for (int i = a0; i < b0; i++) { ItemStack s = ItemStack::fromJson(items[i]); if (!s.empty()) v.push_back(s); }
    std::stable_sort(v.begin(), v.end(), [](const ItemStack& x, const ItemStack& y) { return x.id < y.id; });
    for (int i = a0; i < b0; i++) items[i] = i - a0 < (int)v.size() ? v[i - a0].toJson() : json();
    compact(items, page, PG, NS); if (sneak) compact(items, 1 - page, PG, NS);
    (*ke)["press0"] = ke->value("press0", 0) + 1; changed(); sfxAt(lv, "click", c);
    return true;
  }
  if (a.what == HandsChestAim::Swap) { (*ke)["page"] = 1 - page; (*ke)["press1"] = ke->value("press1", 0) + 1; changed(); sfxAt(lv, "click", c); return true; }
  if (a.what == HandsChestAim::Tray) {
    if (sneak) {   // (quickStack: what you carry that's already in it goes in)
      bool any = false;
      for (ItemStack& s : p.inv) {
        if (s.empty()) continue;
        bool has = false; for (int i = page * PG; i < std::min(NS, page * PG + PG); i++) if (ItemStack::fromJson(items[i]).id == s.id) has = true;
        while (has && !s.empty() && placeOne(items, page, s, PG, NS)) any = true;
      }
      if (any) { compact(items, page, PG, NS); changed(); sfxAt(lv, "pop", c); }
      return true;
    }
    if (held.empty()) {   // (an empty hand: what lies there, the whole of it, back into your bag)
      const int k = page * PG + L.off + a.slot;
      ItemStack s2 = k < NS ? ItemStack::fromJson(items[k]) : ItemStack();
      if (s2.empty()) return true;
      items[k] = nullptr; compact(items, page, PG, NS); changed();
      handsGive(lv, p, s2); sfxAt(lv, "pop", c);
      return true;
    }
    int placed = 0;
    for (int n = 0; n < (all ? 64 : 1) && !held.empty() && placeOne(items, page, held, PG, NS); n++) placed++;
    if (placed) { compact(items, page, PG, NS); changed(); sfxAt(lv, "pop", c); }
    else toast(lv, p, "That page is full (the right-hand button turns to the other)");
    return true;
  }
  setChestLidOpen(w, at, false); sfxAt(lv, "chest", c);   // (the lid, the rim: shut)
  return true;
}

bool handsChestTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole) {
  const HandsChestAim a = handsChestAim(w, at, eye, dir);
  if (a.what != HandsChestAim::Tray || a.slot < 0) return false;
  json* e = w.blockEntity(at.x, at.y, at.z, true);
  if (!e) return false;
  if (!e->is_object()) *e = json::object();
  const ChestLayout L = handsChestLayout(w, at);
  const json* ke = L.key == at ? e : w.blockEntity(L.key.x, L.key.y, L.key.z);
  AllItems slotsAll(lv, p, w, at, L.slots);
  json& items = slotsAll.items;
  const int page = ke ? pageOf(*ke) : 0, k = page * L.pg + L.off + a.slot;
  if (k >= L.slots) return false;
  ItemStack s = ItemStack::fromJson(items[k]);
  if (s.empty()) return false;
  ItemStack taken = s; taken.n = whole ? s.n : 1;
  s.n -= taken.n; items[k] = s.n <= 0 ? json() : s.toJson();
  compact(items, page, L.pg, L.slots);
  w.blockEntityChanged(at.x, at.y, at.z);
  handsGive(lv, p, taken);
  sfxAt(lv, "pop", vec3(at) + vec3(0.5f));
  return true;
}

// ---- a barrel, worked the same way: a tray of nine under its rim (three layers of the 27), its two buttons on the rim ----

vec3 handsBarrelLidPoint(World& w, ivec3 at, vec3 chestLidPx) {
  // a point of the chest's lid (its own sixteenths, as shut: hinge at y 10, z 12) on the barrel's top: its hinge at the top's
  // underside at the back (y lidRow, z 16), swung up 1.25 as render/chest_layer.cpp swings it
  const float lr = chestLidRow(w, at) / 2.f, ang = 1.25f;
  const vec3 rel = (chestLidPx - vec3(0, 10, 12)) / 16.f;
  const float y = rel.y * std::cos(ang) - rel.z * std::sin(ang), z = rel.y * std::sin(ang) + rel.z * std::cos(ang);
  return vec3(rel.x, lr / 16 + y, 1 + z);
}
vec3 handsBarrelButton(World& w, ivec3 at, int k) {   // (the chest lid's two buttons' middles: sort 12.5,10.25,8.65; next 12.5,10.25,4.5)
  return handsBarrelLidPoint(w, at, k == 0 ? vec3(12.5f, 10.25f, 8.65f) : vec3(12.5f, 10.25f, 4.5f));
}
bool handsBarrel(World& w, ivec3 at) {
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  return b.has("container") && b.key.size() >= 6 && b.key.compare(b.key.size() - 6, 6, "barrel") == 0 && !b.has("sealed");
}
int handsBarrelFacing(World& w, ivec3 at) { return barrelFacing(w, at); }

HandsChestAim handsBarrelAim(World& w, ivec3 at, vec3 eye, vec3 dir) {
  HandsChestAim r;
  if (!handsBarrel(w, at)) return r;
  if (!chestLidOpen(w, at)) { r.what = HandsChestAim::Shut; return r; }
  const int f = barrelFacing(w, at);
  const vec3 o = handsNorthLocal(f, eye - vec3(at)), d = handsNorthLocal(f, eye + dir - vec3(at)) - o;
  float best = 1e9f;
  auto test = [&](const float b[6], int what) { const float t = rayBox(o, d, b); if (t >= 0 && t < best) { best = t; r.what = what; } };
  float bs[6], bw[6]; barrelButtonBox(w, at, 0, bs); barrelButtonBox(w, at, 1, bw);
  test(bs, HandsChestAim::Sort); test(bw, HandsChestAim::Swap);
  float lb[6]; barrelLidBox(w, at, lb);
  if (r.what == HandsChestAim::None) test(lb, HandsChestAim::Lid);   // (the raised top: shuts it)
  const float ty = barrelTrayY(w, at);
  const float tray[6] = {GRID_SLAB[0], ty - 1, GRID_SLAB[2], GRID_SLAB[3], ty + 0.6f, GRID_SLAB[5]};
  test(tray, HandsChestAim::Tray);
  if (r.what == HandsChestAim::Tray) {   // (the chest's slot under where the look meets the tray)
    const float t = std::fabs(d.y) > 1e-5f ? (ty / 16 - o.y) / d.y : best;
    const vec3 p = o + d * (t > 0 ? t : best);
    r.slot = slotAt(std::clamp(p.x * 16, 1.f, 15.f), std::clamp(p.z * 16, 2.f, 11.f));
  }
  if (r.what == HandsChestAim::None) r.what = HandsChestAim::Lid;   // (the rim: shuts it)
  return r;
}

bool handsBarrelUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak) {
  if (!handsBarrel(w, at)) return false;
  json* e = containerEntity(w, at.x, at.y, at.z);
  if (!e) return false;
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); };
  if (!chestLidOpen(w, at)) {   // (opened: its front to whoever opened it)
    vec3 toMe = eye - (vec3(at) + vec3(0.5f)); toMe.y = 0;
    int bf = 2; float bd = -1e9f;
    for (int f = 0; f < 4; f++) { const vec3 fr = handsFromNorth(f, vec3(0.5f, 0, 0)) - vec3(0.5f, 0, 0.5f); const float dd = glm::dot(fr, toMe); if (dd > bd) { bd = dd; bf = f; } }
    const u8 m = w.getMeta(at.x, at.y, at.z);
    const u8 want = (u8)bf;   // (its front toward you: render/chest_layer.cpp hinges the top on the far side)
    if ((m & 3) != want) w.setBlock(at.x, at.y, at.z, w.getBlock(at.x, at.y, at.z), (u8)((m & ~3) | want));
    // what's in it packed from the first slot and the first layer showing: loot is rolled into any of its 27 slots,
    // and a barrel shows one layer at a time (it looked empty with its goods on the layers below)
    json& its = itemsOf(*e); int k = 0;
    for (int i = 0; i < SLOTS && i < (int)its.size(); i++) if (!its[i].is_null() && !ItemStack::fromJson(its[i]).empty()) { if (i != k) { its[k] = its[i]; its[i] = nullptr; } k++; }
    (*e)["page"] = 0; changed();
    setChestLidOpen(w, at, true); sfxAt(lv, "chest", c);
    return true;
  }
  const HandsChestAim a = handsBarrelAim(w, at, eye, dir);
  json& items = itemsOf(*e);
  const int pages = (SLOTS + B_PAGE - 1) / B_PAGE, page = std::clamp(e->value("page", 0), 0, pages - 1);
  if (a.what == HandsChestAim::Sort) {
    std::vector<ItemStack> v; for (int i = 0; i < SLOTS; i++) { ItemStack s = ItemStack::fromJson(items[i]); if (!s.empty()) v.push_back(s); }
    std::stable_sort(v.begin(), v.end(), [](const ItemStack& x, const ItemStack& y) { return x.id < y.id; });
    for (int i = 0; i < SLOTS; i++) items[i] = i < (int)v.size() ? v[i].toJson() : json();
    for (int pg = 0; pg < pages; pg++) compact(items, pg, B_PAGE);
    (*e)["press0"] = e->value("press0", 0) + 1; changed(); sfxAt(lv, "click", c);
    return true;
  }
  if (a.what == HandsChestAim::Swap) { (*e)["page"] = (page + 1) % pages; (*e)["press1"] = e->value("press1", 0) + 1; changed(); sfxAt(lv, "click", c); return true; }
  if (a.what == HandsChestAim::Tray) {
    const int k = page * B_PAGE + a.slot;
    if (held.empty()) {   // (an empty hand: what lies there, back into your bag)
      if (k >= SLOTS) return true;
      ItemStack s = ItemStack::fromJson(items[k]);
      if (s.empty()) return true;
      items[k] = nullptr; compact(items, page, B_PAGE); changed();
      handsGive(lv, p, s); sfxAt(lv, "pop", c);
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

bool handsBarrelTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole) {
  const HandsChestAim a = handsBarrelAim(w, at, eye, dir);
  if (a.what != HandsChestAim::Tray || a.slot < 0) return false;
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
  handsGive(lv, p, taken);
  sfxAt(lv, "pop", vec3(at) + vec3(0.5f));
  return true;
}

} // namespace dq
