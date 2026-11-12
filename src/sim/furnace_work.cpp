// sim/furnace_work.cpp — see furnace_work.h
#include "sim/furnace_work.h"
#include "sim/worldsys.h"
#include "sim/blockinfo.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {


static json* furnaceEntity(World& w, ivec3 at) {
  const BlockId id = w.getBlock(at.x, at.y, at.z);
  if (!bhas(id, BF::Furnace)) return nullptr;
  json* e = w.blockEntity(at.x, at.y, at.z, true);
  if (!e->is_object() || e->value("type", "") != "furnace") *e = {{"type", "furnace"}, {"items", {nullptr, nullptr, nullptr}}, {"burn", 0}, {"burnMax", 0}, {"cook", 0}};
  json& it = (*e)["items"];
  if (!it.is_array()) it = json::array();
  while (it.size() < 3) it.push_back(nullptr);
  return e;
}

std::vector<ItemStack> furnaceItems(World& w, ivec3 at) {
  std::vector<ItemStack> v(3);
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (e && e->is_object() && e->contains("items") && (*e)["items"].is_array())
    for (int i = 0; i < 3 && i < (int)(*e)["items"].size(); i++) v[i] = ItemStack::fromJson((*e)["items"][i]);
  return v;
}

bool furnacePut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held) {
  if (slot < 0 || slot > 1 || held.empty()) return false;
  json* e = furnaceEntity(w, at);
  if (!e) return false;
  const std::string kind = binfo(w.getBlock(at.x, at.y, at.z)).furnace;
  if (slot == 0 && smeltResult(held.id, kind).empty()) { toast(lv, p, "That doesn't smelt here"); return false; }
  if (slot == 1 && fuelValue(held.id) <= 0) { toast(lv, p, "That doesn't burn"); return false; }
  ItemStack in = ItemStack::fromJson((*e)["items"][slot]);
  if (!in.empty() && !in.canMerge(held)) { toast(lv, p, "Something else is in there"); return false; }
  const int room = in.empty() ? held.maxStack() : in.maxStack() - in.n;
  const int put = std::min(room, held.n);
  if (put <= 0) return false;
  if (in.empty()) { in = held; in.n = 0; }
  in.n += put;
  if ((held.n -= put) <= 0) held = ItemStack();
  (*e)["items"][slot] = in.toJson();
  w.blockEntityChanged(at.x, at.y, at.z);
  sfxMaterial(lv, slot == 1 ? "wood" : "stone", "place", vec3(at) + vec3(0.5f));
  return true;
}

bool furnaceTake(Level& lv, Player& p, World& w, ivec3 at, int slot) {
  if (slot < 0 || slot > 2) return false;
  json* e = w.blockEntity(at.x, at.y, at.z);
  if (!e || !e->is_object() || !e->contains("items") || !(*e)["items"].is_array() || (int)(*e)["items"].size() <= slot) return false;
  ItemStack s = ItemStack::fromJson((*e)["items"][slot]);
  if (s.empty()) return false;
  (*e)["items"][slot] = nullptr;
  w.blockEntityChanged(at.x, at.y, at.z);
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
  return true;
}

float furnaceCook(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  return e && e->is_object() ? (float)std::clamp(e->value("cook", 0.0) / SMELT_TIME, 0.0, 1.0) : 0.f;
}
float furnaceBurn(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  if (!e || !e->is_object()) return 0.f;
  const double mx = e->value("burnMax", 0.0);
  return mx > 0 ? (float)std::clamp(e->value("burn", 0.0) / mx, 0.0, 1.0) : 0.f;
}

} // namespace dq
