// sim/chest_work.cpp — see chest_work.h
#include "sim/chest_work.h"
#include "sim/controller.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {
namespace {
const vec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};   // meta & 3 -> its front (render/chest_layer.cpp)
enum Kind { K_NONE, K_BLOCK, K_ENDER, K_PARTY };

bool endsWith(const std::string& k, const char* e) { const size_t n = std::strlen(e); return k.size() >= n && k.compare(k.size() - n, n, e) == 0; }
bool isBarrel(const BlockDef& b) { return endsWith(b.key, "barrel"); }
Kind kindAt(World& w, ivec3 at) {
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  if (b.render != Render::Cube) return K_NONE;
  if (b.has("enderChest")) return K_ENDER;
  if (b.has("partyChest")) return K_PARTY;
  if (b.has("container") && (endsWith(b.key, "chest") || isBarrel(b))) return K_BLOCK;
  return K_NONE;
}
float margin(World& w, ivec3 at) { return isBarrel(reg().block(w.getBlock(at.x, at.y, at.z))) ? 0.12f : 0.07f; }   // (a barrel is round: its slots kept off the staves)

json& partyStore(Level& lv) {
  json& d = lv.info.data["partyChest"];
  if (!d.is_array() || d.size() != 27) { json n = json::array(); for (int i = 0; i < 27; i++) n.push_back(i < (int)(d.is_array() ? d.size() : 0) ? d[i] : json()); d = n; }
  return d;
}
json* entityOf(World& w, ivec3 at, bool make) {
  if (make) return containerEntity(w, at.x, at.y, at.z);
  json* e = w.blockEntity(at.x, at.y, at.z);
  return e && e->is_object() && e->contains("items") && (*e)["items"].is_array() ? e : nullptr;
}
// slot -> (half, its index there)
bool locate(World& w, ivec3 at, int slot, ivec3& half, int& i) {
  ivec3 hs[2]; const int n = chestHalves(w, at, hs), per = chestSlotsPerHalf(w, at);
  if (!n || slot < 0 || slot >= n * per) return false;
  half = hs[slot / per]; i = slot % per;
  return true;
}
ItemStack slotGet(Level& lv, const Player* p, World& w, ivec3 half, int i) {
  switch (kindAt(w, half)) {
    case K_ENDER: return p && i < (int)p->ender.size() ? p->ender[i] : ItemStack();
    case K_PARTY: { const json& d = partyStore(lv); return i < (int)d.size() ? ItemStack::fromJson(d[i]) : ItemStack(); }
    case K_BLOCK: { const json* e = entityOf(w, half, false); return e && i < (int)(*e)["items"].size() ? ItemStack::fromJson((*e)["items"][i]) : ItemStack(); }
    default: return ItemStack();
  }
}
void slotSet(Level& lv, Player& p, World& w, ivec3 half, int i, const ItemStack& s) {
  switch (kindAt(w, half)) {
    case K_ENDER: if (p.ender.size() < 27) p.ender.resize(27); if (i < (int)p.ender.size()) p.ender[i] = s; break;
    case K_PARTY: { json& d = partyStore(lv); if (i < (int)d.size()) d[i] = s.empty() ? json() : s.toJson(); break; }
    case K_BLOCK: {
      json* e = entityOf(w, half, true);
      if (!e) return;
      json& it = (*e)["items"];
      if (!it.is_array()) it = json::array();
      while ((int)it.size() <= i) it.push_back(nullptr);
      it[i] = s.empty() ? json() : s.toJson();
      w.blockEntityChanged(half.x, half.y, half.z);
      break;
    }
    default: break;
  }
}
void sound(Level& lv, ivec3 half) { sfxMaterial(lv, "wood", "place", vec3(half) + vec3(0.5f, 0.7f, 0.5f)); }
}  // namespace

int chestHalves(World& w, ivec3 at, ivec3 out[2]) {
  const Kind k = kindAt(w, at);
  if (!k) return 0;
  ivec3 q;
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  if (k == K_BLOCK && (b.key == "chest" || b.str("paintOf") == "chest") && chestPartner(w, at.x, at.y, at.z, q) && kindAt(w, q) == K_BLOCK) {
    const bool lo = (q.x + q.z) < (at.x + at.z);   // (the container screen's order: ui/inventory.cpp, sim/inv_ops.cpp)
    out[0] = lo ? q : at; out[1] = lo ? at : q;
    return 2;
  }
  out[0] = at;
  return 1;
}
int chestSlotsPerHalf(World& w, ivec3 at) { const Kind k = kindAt(w, at); return k == K_BLOCK ? (int)reg().block(w.getBlock(at.x, at.y, at.z)).num("container", 27) : 27; }
int chestLidRow(World& w, ivec3 at) { return isBarrel(reg().block(w.getBlock(at.x, at.y, at.z))) ? 27 : 20; }   // (a barrel's top board; a chest's lid at the tiles' seam)
// chests and barrels are hollow inside (render/chest_layer.cpp CHEST_FLOOR_ROW): their things lie on the floor of the well
float chestFloorY(World&, ivec3) { return 8 / 32.f; }
float chestSlotSize(World& w, ivec3 at) { return (1 - 2 * margin(w, at)) / 9; }

std::vector<ItemStack> chestItems(Level& lv, Player& p, World& w, ivec3 at) {
  ivec3 hs[2]; const int n = chestHalves(w, at, hs), per = chestSlotsPerHalf(w, at);
  std::vector<ItemStack> v;
  for (int h = 0; h < n; h++) for (int i = 0; i < per; i++) v.push_back(slotGet(lv, &p, w, hs[h], i));
  return v;
}
std::vector<ItemStack> chestHalfItems(Level& lv, const Player* viewer, World& w, ivec3 half) {
  std::vector<ItemStack> v;
  const int per = chestSlotsPerHalf(w, half);
  for (int i = 0; i < per; i++) v.push_back(slotGet(lv, viewer, w, half, i));
  return v;
}

vec3 chestSlotCenter(World& w, ivec3 at, int slot) {
  ivec3 half; int i;
  if (!locate(w, at, slot, half, i)) return vec3(at) + vec3(0.5f);
  const vec3 d = FRONT[w.getMeta(half.x, half.y, half.z) & 3], right(d.z, 0, -d.x);   // (the right of someone facing its front)
  const int cols = 9, rows = std::max(1, (chestSlotsPerHalf(w, at) + 8) / 9);
  const int c = i % cols, r = i / cols;
  const float span = 1 - 2 * margin(w, half);
  return vec3(half) + vec3(0.5f, chestFloorY(w, half), 0.5f) + right * (((c + 0.5f) / cols - 0.5f) * span) + d * (((r + 0.5f) / rows - 0.5f) * span);
}

int chestSlotAt(World& w, ivec3 at, vec3 p) {
  ivec3 hs[2]; const int n = chestHalves(w, at, hs), per = chestSlotsPerHalf(w, at);
  for (int h = 0; h < n; h++) {
    const ivec3 half = hs[h];
    const float mg = margin(w, half);
    if (p.x < half.x + mg || p.x > half.x + 1 - mg || p.z < half.z + mg || p.z > half.z + 1 - mg) continue;
    const vec3 d = FRONT[w.getMeta(half.x, half.y, half.z) & 3], right(d.z, 0, -d.x);
    const vec3 rel = p - (vec3(half) + vec3(0.5f, 0, 0.5f));
    const float span = 1 - 2 * mg, u = glm::dot(rel, right) / span + 0.5f, v = glm::dot(rel, d) / span + 0.5f;
    const int rows = std::max(1, (per + 8) / 9);
    const int c = std::clamp((int)(u * 9), 0, 8), r = std::clamp((int)(v * rows), 0, rows - 1);
    const int i = r * 9 + c;
    if (i < per) return h * per + i;
  }
  return -1;
}

bool chestPut(Level& lv, Player& p, World& w, ivec3 at, int slot, bool one) {
  ItemStack& held = p.held();
  ivec3 half; int i;
  if (held.empty() || !locate(w, at, slot, half, i)) return false;
  ItemStack in = slotGet(lv, &p, w, half, i);
  const int want = one ? 1 : held.n;
  if (in.empty()) {
    ItemStack put = held; put.n = want;
    slotSet(lv, p, w, half, i, put);
    if ((held.n -= want) <= 0) held = ItemStack();
  } else if (in.canMerge(held)) {
    const int moved = std::min(want, in.maxStack() - in.n);
    if (moved <= 0) { toast(lv, p, "That stack is full"); return false; }
    in.n += moved; slotSet(lv, p, w, half, i, in);
    if ((held.n -= moved) <= 0) held = ItemStack();
  } else if (!one) {   // something else there: swap it into your hand (as the container screen's click does)
    const ItemStack h = held;
    held = in;
    slotSet(lv, p, w, half, i, h);
  } else return false;
  sound(lv, half);
  return true;
}

bool chestTake(Level& lv, Player& p, World& w, ivec3 at, int slot, bool one, bool toBag) {
  ivec3 half; int i;
  if (!locate(w, at, slot, half, i)) return false;
  ItemStack in = slotGet(lv, &p, w, half, i);
  if (in.empty()) return false;
  ItemStack take = in; take.n = one ? 1 : in.n;
  ItemStack& held = p.held();
  if (!toBag && held.empty()) held = take;
  else if (!toBag && held.canMerge(take) && held.n + take.n <= held.maxStack()) held.n += take.n;
  else if (const int left = giveItem(lv, p, take); left > 0) {
    if (left == take.n) { toast(lv, p, "Your bag is full"); return false; }
    take.n -= left;
  }
  in.n -= take.n;
  slotSet(lv, p, w, half, i, in.n > 0 ? in : ItemStack());
  sound(lv, half);
  return true;
}

int chestTakeAll(Level& lv, Player& p, World& w, ivec3 at) {
  ivec3 hs[2]; const int n = chestHalves(w, at, hs), per = chestSlotsPerHalf(w, at);
  int moved = 0;
  for (int s = 0; s < n * per; s++) if (chestTake(lv, p, w, at, s, false, true)) moved++;
  return moved;
}

int chestStoreAll(Level& lv, Player& p, World& w, ivec3 at) {
  ivec3 hs[2]; const int n = chestHalves(w, at, hs), per = chestSlotsPerHalf(w, at);
  if (!n) return 0;
  int moved = 0;
  for (int k = 9; k < 36 && k < (int)p.inv.size(); k++) {   // the bag's rows (the hotbar stays to hand)
    ItemStack& s = p.inv[k];
    if (s.empty()) continue;
    for (int pass = 0; pass < 2 && !s.empty(); pass++)   // onto stacks of it first, then empty slots
      for (int slot = 0; slot < n * per && !s.empty(); slot++) {
        ivec3 half; int i; locate(w, at, slot, half, i);
        ItemStack in = slotGet(lv, &p, w, half, i);
        if (pass == 0 && in.canMerge(s)) { const int m = std::min(s.n, in.maxStack() - in.n); if (m > 0) { in.n += m; s.n -= m; slotSet(lv, p, w, half, i, in); } }
        else if (pass == 1 && in.empty()) { const ItemStack put = s; s.n = 0; slotSet(lv, p, w, half, i, put); }
      }
    if (s.n <= 0) { s = ItemStack(); moved++; }
  }
  if (moved) sound(lv, hs[0]);
  return moved;
}

} // namespace dq
