// sim/forge.cpp — see forge.h
#include "sim/forge.h"
#include "sim/heat.h"
#include "sim/metalwork.h"
#include "sim/weather.h"
#include "sim/machines.h"
#include "core/registry.h"
#include "sim/blockinfo.h"

namespace dq {

namespace {
BlockId forgeId() { return reg().blockId("forge"); }
json& forgeList(Level& lv) { json& l = lv.info.data["forges"]; if (!l.is_array()) l = json::array(); return l; }
bool stoneAt(World& w, int x, int y, int z) {
  if (!w.isLoaded(x, y, z)) return true;
  const BlockId b = w.getBlock(x, y, z);
  return reg().tSolid[b] && reg().tOpaque[b] && !bhas(b, BF::IsLog);
}
json* forgeEnt(World& w, int x, int y, int z) {
  if (w.getBlock(x, y, z) != forgeId() || !forgeId()) return nullptr;
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object() || e->value("type", "") != "forge") *e = json{{"type", "forge"}, {"fuel", 0}, {"burn", 0.0}, {"temp", 20.0}, {"lit", false}, {"items", json::array({nullptr, nullptr, nullptr, nullptr, nullptr})}};
  return e;
}
// bellows blowing: turned by a shaft (sim/machines.h), or pumped by hand in the last ten seconds
int bellowsBeside(Level& lv, World& w, int x, int y, int z, double dt) {
  static BlockId B = 0; if (!B) B = reg().blockId("bellows");
  int n = 0; const int N[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  Mechanics* M = mechanicsOf(lv, w.realm);
  for (auto& d : N) {
    const int bx = x + d[0], bz = z + d[1];
    if (!B || w.getBlock(bx, y, bz) != B) continue;
    json* e = w.blockEntity(bx, y, bz);
    const float pump = e ? e->value("pump", 0.f) : 0.f;
    if (e && pump > 0) (*e)["pump"] = std::max(0.f, pump - (float)dt);
    if (pump > 0 || (M && M->speedAt(bx, y, bz) > 0.15f)) n++;
  }
  return std::min(2, n);
}
bool walled(World& w, int x, int y, int z) {
  // the pit's sides may be bellows too
  static BlockId B = 0; if (!B) B = reg().blockId("bellows");
  auto side = [&](int ax, int az) { return stoneAt(w, ax, y, az) || (B && w.getBlock(ax, y, az) == B); };
  return side(x + 1, z) && side(x - 1, z) && side(x, z + 1) && side(x, z - 1) && stoneAt(w, x, y - 1, z);
}
}
bool forgeBuild(World& w, int x, int y, int z, ItemStack& coal) {
  if (coal.id != "charcoal" || coal.empty() || !forgeId()) return false;
  if (w.getBlock(x, y, z) != AIR || !walled(w, x, y, z)) return false;
  w.setBlock(x, y, z, forgeId(), 0, SetFlags::Urgent);
  json* e = forgeEnt(w, x, y, z);
  (*e)["fuel"] = 1;
  if (--coal.n <= 0) coal = ItemStack();
  w.blockEntityChanged(x, y, z);
  return true;
}
bool forgeAddFuel(World& w, int x, int y, int z, ItemStack& coal) {
  json* e = forgeEnt(w, x, y, z);
  if (!e || coal.id != "charcoal" || coal.empty() || e->value("fuel", 0) >= FORGE_FUEL_MAX) return false;
  (*e)["fuel"] = e->value("fuel", 0) + 1;
  if (--coal.n <= 0) coal = ItemStack();
  w.blockEntityChanged(x, y, z);
  return true;
}
bool forgeLight(Level& lv, World& w, int x, int y, int z) {
  json* e = forgeEnt(w, x, y, z);
  if (!e || e->value("lit", false) || e->value("fuel", 0) <= 0 || !walled(w, x, y, z)) return false;
  (*e)["lit"] = true;
  json& l = forgeList(lv);
  bool have = false; for (auto& k : l) if (k[0] == w.realm && k[1] == x && k[2] == y && k[3] == z) have = true;
  if (!have) l.push_back({w.realm, x, y, z});
  w.blockEntityChanged(x, y, z);
  return true;
}
bool forgePut(World& w, int x, int y, int z, ItemStack& held) {
  json* e = forgeEnt(w, x, y, z);
  if (!e || held.empty() || !heatable(held.id)) return false;
  for (auto& it : (*e)["items"]) if (it.is_null()) { ItemStack one = held; one.n = 1; it = one.toJson(); if (--held.n <= 0) held = ItemStack(); w.blockEntityChanged(x, y, z); return true; }
  return false;
}
ItemStack forgeTake(World& w, int x, int y, int z) {
  json* e = forgeEnt(w, x, y, z);
  if (!e) return {};
  auto& items = (*e)["items"];
  for (int i = (int)items.size() - 1; i >= 0; i--) if (!items[i].is_null()) { ItemStack s = ItemStack::fromJson(items[i]); items[i] = nullptr; w.blockEntityChanged(x, y, z); return s; }
  return {};
}

bool bellowsPump(World& w, int x, int y, int z) {
  static BlockId B = 0; if (!B) B = reg().blockId("bellows");
  if (!B || w.getBlock(x, y, z) != B) return false;
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object()) *e = json::object();
  (*e)["type"] = "bellows"; (*e)["pump"] = 10.0;
  w.blockEntityChanged(x, y, z);   // (a guest's pumping reaches the host)
  return true;
}

void forgeTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  json& list = forgeList(lv);
  for (size_t i = 0; i < list.size();) {
    const json& k = list[i];
    World* w = lv.worldIfOpen(k[0].get<int>());
    const int x = k[1], y = k[2], z = k[3];
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    json* e = forgeEnt(*w, x, y, z);
    if (!e) { list.erase(list.begin() + i); continue; }
    bool lit = e->value("lit", false);
    float temp = e->value("temp", 20.f), burn = e->value("burn", 0.f);
    int fuel = e->value("fuel", 0);
    if (lit) {
      if (WeatherSystem* W = weatherOf(lv); W && W->kind != "clear" && W->topAt(*w, x, z) <= y + 1 && W->precipAt(*w, x + 0.5f, y + 1.f, z + 0.5f) != 0) lit = false;
      burn -= (float)dt;
      if (burn <= 0) { if (fuel > 0) { fuel--; burn = 90; } else { burn = 0; lit = false; } }
    }
    const float peak = lit ? FORGE_PEAK + BELLOWS_HEAT * bellowsBeside(lv, *w, x, y, z, dt) : AIR_TEMP;
    temp += clampv(peak - temp, -10.f * (float)dt, 15.f * (float)dt);
    for (auto& it : (*e)["items"]) {
      if (it.is_null()) continue;
      ItemStack s = ItemStack::fromJson(it);
      setItemHeat(s, heatTowards(itemHeat(s), temp, (float)dt));
      vesselMeltStep(s);
      it = s.toJson();
    }
    (*e)["lit"] = lit; (*e)["temp"] = temp; (*e)["burn"] = burn; (*e)["fuel"] = fuel;
    if (lit && std::fmod(e->value("sync", 0.0) + dt, 0.5) < dt) lv.fx->particles("flame", vec3(x + 0.5f, y + 0.7f, z + 0.5f), 1, {{"spread", 0.3}, {"vel", {0, 0.6, 0}}, {"life", 0.6}, {"size", 0.1}, {"rise", true}});
    if ((e->operator[]("sync") = e->value("sync", 0.0) + dt).get<double>() > 2) { (*e)["sync"] = 0; w->blockEntityChanged(x, y, z); }
    if (!lit && temp < 30) { list.erase(list.begin() + i); w->blockEntityChanged(x, y, z); continue; }
    i++;
  }
}

} // namespace dq
