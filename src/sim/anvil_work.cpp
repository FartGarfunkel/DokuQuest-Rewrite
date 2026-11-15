// sim/anvil_work.cpp — see anvil_work.h
#include "sim/anvil_work.h"
#include "sim/stations.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/magic_combat.h"
#include "core/registry.h"
#include <cstdlib>

namespace dq {

std::vector<ItemStack> anvilWorkItems(World& w, ivec3 at) {
  std::vector<ItemStack> v(2);
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (e && e->is_object() && e->value("type", "") == "anvilwork" && e->contains("items") && (*e)["items"].is_array())
    for (int i = 0; i < 2 && i < (int)(*e)["items"].size(); i++) v[i] = ItemStack::fromJson((*e)["items"][i]);
  return v;
}

static void save(World& w, ivec3 at, const std::vector<ItemStack>& v) {
  if (v[0].empty() && v[1].empty()) { w.setBlockEntity(at.x, at.y, at.z, nullptr); return; }
  w.setBlockEntity(at.x, at.y, at.z, {{"type", "anvilwork"}, {"items", json::array({v[0].empty() ? json() : v[0].toJson(), v[1].empty() ? json() : v[1].toJson()})}});
}

static bool isAnvil(World& w, ivec3 at) { return reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "anvil"; }

bool anvilWorkPut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held) {
  if (slot < 0 || slot > 1 || held.empty() || !isAnvil(w, at)) return false;
  std::vector<ItemStack> v = anvilWorkItems(w, at);
  if (!v[slot].empty() && !v[slot].canMerge(held)) { toast(lv, p, "Something's there already"); return false; }
  const int room = v[slot].empty() ? held.maxStack() : v[slot].maxStack() - v[slot].n;
  const int put = slot == 0 ? 1 : std::min(room, held.n);   // (the work one at a time; what goes into it, all of it)
  if (put <= 0 || (slot == 0 && !v[0].empty())) { toast(lv, p, "Something's there already"); return false; }
  if (v[slot].empty()) { v[slot] = held; v[slot].n = 0; }
  v[slot].n += put;
  if ((held.n -= put) <= 0) held = ItemStack();
  save(w, at, v);
  sfxMaterial(lv, "metal", "place", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  return true;
}

bool anvilWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot) {
  if (slot < 0 || slot > 1) return false;
  std::vector<ItemStack> v = anvilWorkItems(w, at);
  if (v[slot].empty()) return false;
  ItemStack s = v[slot]; v[slot] = ItemStack();
  save(w, at, v);
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
  return true;
}

StationResult anvilWorkResult(World& w, ivec3 at) {
  const std::vector<ItemStack> v = anvilWorkItems(w, at);
  if (v[0].empty()) return StationResult{};
  return stationResult("anvil", v[0], v[1]);
}

bool anvilWorkForge(Level& lv, Player& p, World& w, ivec3 at, bool leaveOnAnvil) {
  if (!isAnvil(w, at)) return false;
  std::vector<ItemStack> v = anvilWorkItems(w, at);
  StationResult r = v[0].empty() ? StationResult{} : stationResult("anvil", v[0], v[1]);
  if (!r.ok) { toast(lv, p, "That won't forge into anything"); return false; }
  if (r.lv && !p.creative() && p.level < r.lv) { toast(lv, p, "You need " + std::to_string(r.lv) + " level" + (r.lv > 1 ? "s" : "") + " for that", "warn"); return false; }
  if (r.lv && !p.creative()) spendLevels(p, r.lv);
  auto use = [&](int k, int n) { if (v[k].empty() || n <= 0) return; if ((v[k].n -= n) <= 0) v[k] = ItemStack(); };
  use(0, r.useA); use(1, r.useB);
  if (leaveOnAnvil && v[0].empty()) v[0] = r.out;
  save(w, at, v);
  ItemStack out = r.out;
  if (!leaveOnAnvil || v[0].id != r.out.id) if (int left = giveItem(lv, p, out); left > 0) { out.n = left; dropItem(lv, p.realm, out, p.pos + vec3(0, 1, 0), true, 0.f); }
  sfxAt(lv, "anvil", vec3(at) + vec3(0.5f, 1.f, 0.5f));
  // anvils wear out (the anvil screen's way): anvil -> chipped -> damaged -> gone
  if (std::rand() / (double)RAND_MAX < 0.12) {
    const BlockId id = w.getBlock(at.x, at.y, at.z); const u8 m = w.getMeta(at.x, at.y, at.z);
    const std::string& k = reg().block(id).key;
    const BlockId next = k == "anvil" ? reg().blockId("chipped_anvil") : k == "chipped_anvil" ? reg().blockId("damaged_anvil") : AIR;
    if (!next) for (auto& s : anvilWorkItems(w, at)) if (!s.empty()) dropItem(lv, p.realm, s, vec3(at) + vec3(0.5f, 1.f, 0.5f), true, 0.f);
    if (!next) w.setBlockEntity(at.x, at.y, at.z, nullptr);
    const json keep = !next ? json() : (w.blockEntity(at.x, at.y, at.z) ? *w.blockEntity(at.x, at.y, at.z) : json());
    w.setBlock(at.x, at.y, at.z, next, next ? m : 0);
    if (next && keep.is_object()) w.setBlockEntity(at.x, at.y, at.z, keep);   // (what's on it stays on the chipped one)
    toast(lv, p, next ? "The anvil chips" : "The anvil breaks", "warn");
  }
  return true;
}

} // namespace dq
