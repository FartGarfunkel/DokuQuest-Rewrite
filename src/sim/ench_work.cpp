// sim/ench_work.cpp — see ench_work.h
#include "sim/ench_work.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {

std::vector<ItemStack> enchWorkItems(World& w, ivec3 at) {
  std::vector<ItemStack> v(2);
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (e && e->is_object() && e->value("type", "") == "enchwork" && e->contains("items") && (*e)["items"].is_array())
    for (int i = 0; i < 2 && i < (int)(*e)["items"].size(); i++) v[i] = ItemStack::fromJson((*e)["items"][i]);
  return v;
}

static void save(World& w, ivec3 at, const std::vector<ItemStack>& v) {
  if (v[0].empty() && v[1].empty()) { w.setBlockEntity(at.x, at.y, at.z, nullptr); return; }
  w.setBlockEntity(at.x, at.y, at.z, {{"type", "enchwork"}, {"items", json::array({v[0].empty() ? json() : v[0].toJson(), v[1].empty() ? json() : v[1].toJson()})}});
}
static bool isTable(World& w, ivec3 at) { return reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "enchant"; }
static void giveBack(Level& lv, Player& p, ItemStack s) {
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
}

bool enchWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held) {
  if (held.empty() || !isTable(w, at)) return false;
  std::vector<ItemStack> v = enchWorkItems(w, at);
  if (held.id == "lapis_lazuli") {   // the lapis: all of it, by the table
    const int room = v[1].empty() ? held.maxStack() : v[1].maxStack() - v[1].n;
    if (room <= 0) return false;
    const int put = std::min(room, held.n);
    if (v[1].empty()) { v[1] = held; v[1].n = 0; }
    v[1].n += put; if ((held.n -= put) <= 0) held = ItemStack();
  } else {                           // the thing to enchant: one, over the table
    if (!v[0].empty()) { toast(lv, p, "Something's over the table already"); return false; }
    if (enchKind(held.id).empty()) { toast(lv, p, "That can't be enchanted"); return false; }
    v[0] = held; v[0].n = held.id == "book" ? held.n : 1;   // (books: the whole stack, as the screen takes them)
    if ((held.n -= v[0].n) <= 0) held = ItemStack();
  }
  save(w, at, v);
  sfxMaterial(lv, "stone", "place", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  return true;
}

bool enchWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot) {
  if (slot < 0 || slot > 1) return false;
  std::vector<ItemStack> v = enchWorkItems(w, at);
  if (v[slot].empty()) return false;
  ItemStack s = v[slot]; v[slot] = ItemStack();
  save(w, at, v); giveBack(lv, p, s);
  return true;
}

bool enchWorkOffers(World& w, ivec3 at, Player& p, std::array<EnchOffer, 3>& out) {
  const std::vector<ItemStack> v = enchWorkItems(w, at);
  if (v[0].empty()) return false;
  return enchOffers(v[0], enchShelves(w, at.x, at.y, at.z), enchSeed(p), out);
}

std::string enchWorkApply(Level& lv, Player& p, World& w, ivec3 at, int offer) {
  if (!isTable(w, at)) return "The table is gone";
  std::vector<ItemStack> v = enchWorkItems(w, at);
  if (v[0].empty()) return "Nothing over the table";
  const json before = v[0].tag; const std::string idBefore = v[0].id;
  ItemStack leftover;
  const std::string why = enchApplyOffer(lv, p, v[0], v[1], offer, enchShelves(w, at.x, at.y, at.z), vec3(at), &leftover);
  if (!why.empty()) return why;
  if (v[0].tag == before && v[0].id == idBefore) return "Nothing to enchant";
  save(w, at, v);
  if (!leftover.empty()) giveBack(lv, p, leftover);
  return "";
}

} // namespace dq
