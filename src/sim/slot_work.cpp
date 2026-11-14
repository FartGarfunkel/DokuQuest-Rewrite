// sim/slot_work.cpp — see slot_work.h
#include "sim/slot_work.h"
#include "sim/trommel.h"
#include "sim/machines.h"
#include "sim/worldsys.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "world/world.h"
#include "core/registry.h"

namespace dq {

SlotRig slotRigAt(World& w, ivec3 at) {
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  if (b.str("furnace") == "trommel") return SlotRig::Trommel;
  if (b.has("machine") && machineTable(b.str("mech"))) return SlotRig::Machine;
  return SlotRig::None;
}

static json* entityOf(World& w, ivec3 at, SlotRig r) {
  json* e = w.blockEntity(at.x, at.y, at.z, true);
  if (!e) return nullptr;
  const int n = r == SlotRig::Trommel ? 5 : 2;
  if (!e->is_object()) *e = json::object();
  if (!e->contains("type")) {
    if (r == SlotRig::Trommel) *e = {{"type", "furnace"}, {"burn", 0}, {"burnMax", 0}, {"cook", 0}};
    else *e = {{"type", "machine"}, {"progress", 0}};
  }
  json& it = (*e)["items"];
  if (!it.is_array()) it = json::array();
  while ((int)it.size() < n) it.push_back(nullptr);
  return e;
}

std::vector<ItemStack> slotItems(World& w, ivec3 at) {
  const SlotRig r = slotRigAt(w, at);
  std::vector<ItemStack> v(r == SlotRig::Trommel ? 5 : 2);
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (e && e->is_object() && e->contains("items") && (*e)["items"].is_array())
    for (size_t i = 0; i < v.size() && i < (*e)["items"].size(); i++) v[i] = ItemStack::fromJson((*e)["items"][i]);
  return v;
}

static bool putInto(json& slotJ, ItemStack& held) {
  ItemStack in = ItemStack::fromJson(slotJ);
  if (!in.empty() && !in.canMerge(held)) return false;
  const int room = in.empty() ? held.maxStack() : in.maxStack() - in.n;
  const int put = std::min(room, held.n);
  if (put <= 0) return false;
  if (in.empty()) { in = held; in.n = 0; }
  in.n += put;
  if ((held.n -= put) <= 0) held = ItemStack();
  slotJ = in.toJson();
  return true;
}

bool slotPut(Level& lv, Player& p, World& w, ivec3 at, int spot, ItemStack& held) {
  const SlotRig r = slotRigAt(w, at);
  if (r == SlotRig::None || held.empty()) return false;
  json* e = entityOf(w, at, r);
  if (!e) return false;
  json& it = (*e)["items"];
  bool ok = false;
  if (r == SlotRig::Trommel) {
    if (spot == 0) {
      if (!trommelSifts(held.id)) { toast(lv, p, "That won't sift"); return false; }
      for (int i = 0; i < 4 && !ok; i++) { ItemStack s = ItemStack::fromJson(it[i]); if (!s.empty() && s.canMerge(held)) ok = putInto(it[i], held); }   // (onto its own kind first)
      for (int i = 0; i < 4 && !ok; i++) ok = putInto(it[i], held);
      if (!ok) { toast(lv, p, "The hopper is full"); return false; }
      sfxMaterial(lv, "gravel", "place", vec3(at) + vec3(0.5f, 1, 0.5f));
    } else {
      if (fuelValue(held.id) <= 0) { toast(lv, p, "That doesn't burn"); return false; }
      if (!(ok = putInto(it[4], held))) { toast(lv, p, "Something else is in the firebox"); return false; }
      sfxMaterial(lv, "wood", "place", vec3(at) + vec3(0.5f));
    }
  } else {
    const json* tab = machineTable(reg().block(w.getBlock(at.x, at.y, at.z)).str("mech"));
    if (spot != 0 || !tab || !tab->contains(held.id)) { toast(lv, p, "That won't go through it"); return false; }
    if (!(ok = putInto(it[0], held))) { toast(lv, p, "Something else is in it"); return false; }
    sfxMaterial(lv, "stone", "place", vec3(at) + vec3(0.5f, 1, 0.5f));
  }
  if (ok) w.blockEntityChanged(at.x, at.y, at.z);
  return ok;
}

bool slotTake(Level& lv, Player& p, World& w, ivec3 at, int spot) {
  const SlotRig r = slotRigAt(w, at);
  json* e = r == SlotRig::None ? nullptr : w.blockEntity(at.x, at.y, at.z);
  if (!e || !e->is_object() || !e->contains("items") || !(*e)["items"].is_array()) return false;
  json& it = (*e)["items"];
  int slot = -1;
  if (r == SlotRig::Trommel) { if (spot == 1) slot = 4; else for (int i = 3; i >= 0 && slot < 0; i--) if (i < (int)it.size() && !it[i].is_null()) slot = i; }
  else slot = spot == 1 ? 1 : 0;
  if (slot < 0 || slot >= (int)it.size()) return false;
  ItemStack s = ItemStack::fromJson(it[slot]);
  if (s.empty()) return false;
  it[slot] = nullptr;
  w.blockEntityChanged(at.x, at.y, at.z);
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
  return true;
}

float slotProgress(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (!e || !e->is_object()) return 0.f;
  if (slotRigAt(w, at) == SlotRig::Trommel) return (float)std::clamp(e->value("cook", 0.0) / TROMMEL_POP, 0.0, 1.0);
  return std::clamp(e->value("progress", 0.f), 0.f, 1.f);
}
float slotFuel(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (!e || !e->is_object()) return 0.f;
  const double mx = e->value("burnMax", 0.0);
  return mx > 0 ? (float)std::clamp(e->value("burn", 0.0) / mx, 0.0, 1.0) : 0.f;
}

} // namespace dq
