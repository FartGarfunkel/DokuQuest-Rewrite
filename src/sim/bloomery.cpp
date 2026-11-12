// sim/bloomery.cpp — see bloomery.h
#include "sim/bloomery.h"
#include "sim/firepit.h"
#include "core/metals.h"
#include "core/registry.h"

namespace dq {

namespace {
BlockId bloomeryId() { return reg().blockId("bloomery"); }
json& list(Level& lv) { json& l = lv.info.data["bloomeries"]; if (!l.is_array()) l = json::array(); return l; }
json* ent(World& w, int x, int y, int z) {
  if (!bloomeryId() || w.getBlock(x, y, z) != bloomeryId()) return nullptr;
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object() || e->value("type", "") != "bloomery") *e = json{{"type", "bloomery"}, {"ore", 0}, {"units", 0}, {"charcoal", 0}, {"lit", false}, {"t", 0.0}, {"out", 0}};
  return e;
}
bool isChimney(BlockId b) { const std::string& k = reg().block(b).key; return k == "stone_bricks" || k == "bricks" || k.find("stone_brick") != std::string::npos; }
}

int bloomeryCapacity(World& w, int x, int y, int z) {
  int n = 0;
  while (n < 3 && isChimney(w.getBlock(x, y + 1 + n, z))) n++;
  return n * 8;
}
bool bloomeryLoad(World& w, int x, int y, int z, ItemStack& held, std::string* why) {
  json* e = ent(w, x, y, z);
  if (!e || held.empty() || e->value("lit", false)) return false;
  const int cap = bloomeryCapacity(w, x, y, z);
  if (cap <= 0) { if (why) *why = "It needs a chimney of stone bricks over it"; return false; }
  if (held.id == "charcoal") {
    if (e->value("charcoal", 0) >= cap) { if (why) *why = "It has all the charcoal it can take"; return false; }
    (*e)["charcoal"] = e->value("charcoal", 0) + 1;
  } else if (metalOfItem(held.id) == "iron" && unitsOf(held.id) > 0 && held.id != "iron_ingot") {
    if (e->value("ore", 0) >= cap) { if (why) *why = "It's full: a taller chimney holds more"; return false; }
    (*e)["ore"] = e->value("ore", 0) + 1; (*e)["units"] = e->value("units", 0) + unitsOf(held.id);
  } else return false;
  if (--held.n <= 0) held = ItemStack();
  w.blockEntityChanged(x, y, z);
  return true;
}
bool bloomeryLight(Level& lv, World& w, int x, int y, int z) {
  json* e = ent(w, x, y, z);
  if (!e || e->value("lit", false) || e->value("ore", 0) <= 0 || e->value("charcoal", 0) < e->value("ore", 0) || bloomeryCapacity(w, x, y, z) <= 0) return false;
  (*e)["lit"] = true; (*e)["t"] = 0.0;
  list(lv).push_back({w.realm, x, y, z});
  w.blockEntityChanged(x, y, z);
  return true;
}
ItemStack bloomeryTake(World& w, int x, int y, int z) {
  json* e = ent(w, x, y, z);
  if (!e || e->value("out", 0) <= 0) return {};
  (*e)["out"] = e->value("out", 0) - 1;
  w.blockEntityChanged(x, y, z);
  return ItemStack("raw_bloom", 1);
}
void bloomeryTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  json& l = list(lv);
  const double need = gameHourSecs(lv) * BLOOMERY_HOURS;
  for (size_t i = 0; i < l.size();) {
    World* w = lv.worldIfOpen(l[i][0].get<int>());
    const int x = l[i][1], y = l[i][2], z = l[i][3];
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    json* e = ent(*w, x, y, z);
    if (!e || !e->value("lit", false)) { l.erase(l.begin() + i); continue; }
    const double t = e->value("t", 0.0) + dt;
    (*e)["t"] = t;
    if (std::fmod(t, 1.0) < dt) lv.fx->particles("campfire_smoke", vec3(x + 0.5f, y + 1 + bloomeryCapacity(*w, x, y, z) / 8 + 0.2f, z + 0.5f), 1, {{"spread", 0.2}, {"vel", {0, 1.2, 0}}, {"life", 2}, {"size", 0.2}, {"rise", true}});
    if (t >= need) {
      (*e)["out"] = e->value("out", 0) + e->value("units", 0) / 100;
      (*e)["ore"] = 0; (*e)["units"] = 0; (*e)["charcoal"] = 0; (*e)["lit"] = false; (*e)["t"] = 0.0;
      w->blockEntityChanged(x, y, z);
      l.erase(l.begin() + i); continue;
    }
    i++;
  }
}

} // namespace dq
