// sim/firepit.cpp — see firepit.h
#include "sim/firepit.h"
#include "sim/camping.h"
#include "sim/heat.h"
#include "sim/metalwork.h"
#include "sim/rules.h"
#include "sim/weather.h"
#include "sim/blockinfo.h"
#include "core/registry.h"

namespace dq {

FuelInfo fuelOf(const std::string& id) {
  if (id.empty()) return {};
  const Registry& R = reg();
  if (id == "charcoal") return {1350, 90};
  if (id == "coal") return {1400, 110};
  if (id == "stick") return {450, 20};
  if (R.itemMatches(id, "#planks")) return {550, 40};
  if (R.itemMatches(id, "#log")) return {650, 75};   // TFC's woods burn at 603-762 °C
  return {};
}

double gameHourSecs(Level& lv) { return (ruleNum(lv, "dayMinutes") + ruleNum(lv, "nightMinutes")) * 60.0 / 24.0; }

static json& fireEntity(World& w, int x, int y, int z) {
  json& e = *w.blockEntity(x, y, z, true);
  if (!e.is_object()) e = json::object();
  if (!e.contains("type")) e["type"] = "campfire";
  if (!e["items"].is_array()) e["items"] = {nullptr, nullptr, nullptr, nullptr};
  if (!e["fuel"].is_array()) e["fuel"] = json::array();
  return e;
}

bool firepitAddFuel(World& w, int x, int y, int z, ItemStack& held) {
  if (fuelOf(held.id).secs <= 0 || held.empty()) return false;
  json& e = fireEntity(w, x, y, z);
  if ((int)e["fuel"].size() >= FIREPIT_FUEL_MAX) return false;
  e["fuel"].push_back(held.id);
  if (--held.n <= 0) held = ItemStack();
  w.blockEntityChanged(x, y, z);
  return true;
}

void firepitTick(Level& lv, World& w, int x, int y, int z, double dt) {
  json* ep = w.blockEntity(x, y, z);
  if (!ep || !ep->is_object()) return;
  json& e = *ep;
  if (!e.contains("temp")) {   // a campfire from before firepits: a day's free burn, so nobody's camp dies on load
    e["temp"] = 650; e["burn"] = gameHourSecs(lv) * 24; e["peak"] = 650;
    if (!e["fuel"].is_array()) e["fuel"] = json::array();
  }
  const u8 meta = w.getMeta(x, y, z);
  bool out = meta & CAMPFIRE_OUT;
  float temp = e.value("temp", 20.f), burn = e.value("burn", 0.f), peak = e.value("peak", 650.f);
  const bool wasOut = out;
  if (!out) {
    if (WeatherSystem* W = weatherOf(lv); W && W->kind != "clear" && W->topAt(w, x, z) <= y + 1 && W->precipAt(w, x + 0.5f, y + 1.f, z + 0.5f) != 0) out = true;   // rain on an open fire
    burn -= (float)dt;
    if (burn <= 0) {
      json& fuel = e["fuel"];
      if (fuel.is_array() && !fuel.empty()) { const FuelInfo f = fuelOf(fuel[0].get<std::string>()); fuel.erase(fuel.begin()); burn = f.secs; peak = f.peak; }
      else burn = 0;
    }
    const float target = burn > 0 ? peak : AIR_TEMP;
    temp += clampv(target - temp, -8.f * (float)dt, 15.f * (float)dt);
    if (burn <= 0 && temp < 80) out = true;
  } else temp += clampv(AIR_TEMP - temp, -8.f * (float)dt, 0.f);
  if (out && !wasOut) { w.setBlock(x, y, z, w.getBlock(x, y, z), meta | CAMPFIRE_OUT); burn = 0; e["fuel"] = json::array(); }
  // what's set on it to heat: metal, ore, pottery
  if (e["items"].is_array()) for (auto& it : e["items"]) {
    if (it.is_null()) continue;
    ItemStack s = ItemStack::fromJson(it);
    if (!heatable(s.id)) continue;
    setItemHeat(s, heatTowards(itemHeat(s), out ? AIR_TEMP : temp, (float)dt));
    vesselMeltStep(s);   // sim/metalwork.h: ore in a vessel melts once it's hot enough
    it = s.toJson();
  }
  e["temp"] = temp; e["burn"] = std::max(0.f, burn); e["peak"] = peak;
  if (out != wasOut || (e["sync"] = e.value("sync", 0.0) + dt).get<double>() > 2) { e["sync"] = 0; w.blockEntityChanged(x, y, z); }   // saved and sent now and then
}

bool strawLiesAt(Level& lv, int realm, int x, int y, int z) {
  for (auto& en : lv.entities()) {
    if (!en || en->removed || en->type != "item" || en->realm != realm) continue;
    if (ifloor(en->pos.x) != x || ifloor(en->pos.y) != y || ifloor(en->pos.z) != z) continue;
    if (en->data.value("stack", json::object()).value("id", "") == "straw") return true;
  }
  return false;
}

bool firepitFromSticks(Level& lv, World& w, int x, int y, int z) {
  std::vector<Entity*> sticks; int n = 0;
  for (auto& en : lv.entities()) {
    if (!en || en->removed || en->type != "item" || en->realm != w.realm) continue;
    if (ifloor(en->pos.x) != x || ifloor(en->pos.y + 0.01f) != y || ifloor(en->pos.z) != z) continue;
    const json st = en->data.value("stack", json::object());
    if (st.value("id", "") != "stick") continue;
    sticks.push_back(en.get()); n += st.value("n", 1);
  }
  const BlockId fire = reg().blockId("campfire");
  if (n < 3 || !fire || w.getBlock(x, y, z) != AIR) return false;
  int need = 3;
  for (Entity* en : sticks) {
    if (!need) break;
    json& st = en->data["stack"]; const int have = st.value("n", 1), take = std::min(have, need);
    need -= take;
    if (take >= have) en->removed = true; else st["n"] = have - take;
  }
  w.setBlock(x, y, z, fire, 0);
  json& e = fireEntity(w, x, y, z);
  e["temp"] = 250; e["burn"] = 20; e["peak"] = 450; e["t"] = {0, 0, 0, 0};
  w.blockEntityChanged(x, y, z);
  return true;
}

} // namespace dq
