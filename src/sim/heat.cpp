// sim/heat.cpp — see heat.h
#include "sim/heat.h"
#include <memory>
#include "sim/level.h"
#include "sim/player.h"
#include "sim/content.h"
#include "sim/camping.h"
#include "world/world.h"
#include "core/registry.h"

namespace dq {

float itemHeat(const ItemStack& s) {
  if (s.tag.is_object()) if (auto it = s.tag.find("heat"); it != s.tag.end() && it->is_number()) return it->get<float>();
  return AIR_TEMP;
}
void setItemHeat(ItemStack& s, float c) {
  if (c <= 25) {
    if (s.tag.is_object()) { s.tag.erase("heat"); if (s.tag.empty()) s.tag = json(); }
    return;
  }
  if (!s.tag.is_object()) s.tag = json::object();
  s.tag["heat"] = std::round(c * 10) / 10;
}
bool heatable(const std::string& id) {
  static const char* ends[] = {"_ingot", "_ore", "_head", "_sheet", "_double_ingot", "_double_sheet", "_bloom", "_ore_piece", "_ore_sample", "_ore_powder", "_mold"};
  for (const char* e : ends) { const size_t n = std::strlen(e); if (id.size() >= n && id.compare(id.size() - n, n, e) == 0) return true; }
  return id.rfind("unfired_", 0) == 0 || id.rfind("ceramic_", 0) == 0 || id == "crucible" || id == "pig_iron" || id == "raw_bloom" || id == "high_carbon_steel";
}
float heatTowards(float h, float target, float dt) {
  if (target > h) return std::min(target, h + std::max(40.f, (target - h) * 0.25f) * dt);
  return std::max(target, h - std::max(1.f, (h - target) * 0.02f) * dt);
}
std::string heatName(float c) {
  static const std::pair<float, const char*> B[] = {{1600, "Brilliant White"}, {1500, "White"}, {1400, "Yellow White"}, {1300, "Yellow"}, {1100, "Orange"},
    {930, "Bright Red"}, {730, "Dark Red"}, {580, "Faint Red"}, {480, "Very Hot"}, {210, "Hot"}, {80, "Warming"}};
  for (auto& [t, n] : B) if (c >= t) return n;
  return "";
}
u32 heatGlow(float c) {
  if (c < 480) return 0;
  if (c < 730) return 0x7a1a10;
  if (c < 930) return 0xb02010;
  if (c < 1100) return 0xe04818;
  if (c < 1300) return 0xff8a20;
  if (c < 1500) return 0xffd060;
  return 0xfff4d8;
}

std::shared_ptr<void>& levelSlotRaw(Level& lv, const std::string& key);   // (sim/gameplay.h, which clashes with content.h here)
namespace {
double& slotD(Level& lv, const char* k) { auto& s = levelSlotRaw(lv, k); if (!s) s = std::make_shared<double>(0.0); return *static_cast<double*>(s.get()); }
// water at a block: a liquid block of water or a cauldron holding some (as the quench by hand, sim/controller.cpp)
bool waterAt(World& w, int x, int y, int z) {
  const BlockId id = w.getBlock(x, y, z);
  return id && (reg().tLiquid[id] == 1 || (reg().block(id).has("cauldron") && (w.getMeta(x, y, z) & 3)));
}
// one stack's JSON (the tag flattened into it): cooled in place; true if it changed
bool coolJson(json& j, float step, bool water) {
  if (!j.is_object() || !j.contains("heat")) return false;
  ItemStack s = ItemStack::fromJson(j);
  setItemHeat(s, water ? AIR_TEMP : heatTowards(itemHeat(s), AIR_TEMP, step));
  j = s.toJson();
  return true;
}
// what lies on the ground cools; in water it's cold at once, with a hiss and steam
void coolDropped(Level& lv, float step) {
  for (auto& e : lv.entities()) {
    if (e->removed || e->type != "item" || !e->data.is_object() || !e->data.contains("stack") || !e->data["stack"].contains("heat")) continue;
    World* w = lv.worldIfOpen(e->realm); if (!w) continue;
    const ivec3 b = ivec3(glm::floor(e->pos));
    const bool water = waterAt(*w, b.x, b.y, b.z);
    const bool wasHot = itemHeat(ItemStack::fromJson(e->data["stack"])) > 100;
    coolJson(e->data["stack"], step, water);
    if (water && wasHot && lv.fx) { lv.fx->sound("fizz", e->pos, 16); lv.fx->particles("campfire_smoke", e->pos + vec3(0, 0.5f, 0), 8, {{"spread", 0.3}, {"vel", {0, 1.4, 0}}, {"life", 1.2}, {"size", 0.14}, {"rise", true}}); }
  }
}
// what's kept in blocks (chests, piles, racks, barrels, a cold forge or kiln) cools; a lit fire keeps its own (sim/forge,
// pit_kiln, camping); a barrel of water cools what's in it at once
void coolStored(Level& lv, float step) {
  std::vector<std::pair<World*, ivec3>> changed;
  for (auto& [r, wp] : lv.allRealms()) {
    World& w = *wp;
    for (auto& [k, c] : w.chunks()) for (auto& [idx, e] : c->blockEntities) {
      if (!e.is_object()) continue;
      const int x = c->cx * 16 + (idx & 15), z = c->cz * 16 + ((idx >> 4) & 15), y = dimBase(c->dim) + (idx >> 8);
      const std::string type = e.value("type", std::string());
      if (e.value("lit", false)) continue;                                   // the forge, the kiln, the charcoal pit, burning
      if (type == "campfire" && campfireLit(w, x, y, z)) continue;          // the firepit heats its own
      const bool water = type == "sbarrel" && e.value("liquid", std::string()) == "water" && e.value("amount", 0) > 0;
      bool any = false;
      for (const char* f : {"items", "out", "rack"}) if (e.contains(f) && e[f].is_array()) for (auto& j : e[f]) any |= coolJson(j, step, water);
      for (const char* f : {"item", "stack"}) if (e.contains(f)) any |= coolJson(e[f], step, water);
      if (any) changed.push_back({&w, ivec3(x, y, z)});
    }
  }
  for (auto& [w, p] : changed) w->blockEntityChanged(p.x, p.y, p.z);   // (after the walk: a change may touch the maps)
}
}

void heatTick(Level& lv, double dt) {
  double& acc = slotD(lv, "heatAcc"); double& accStored = slotD(lv, "heatAccStored");   // (per level)
  acc += dt; accStored += dt;
  if (accStored >= 4 && acc >= 1) { if (!lv.isRemote()) coolStored(lv, (float)accStored); accStored = 0; }   // (the host's world: a guest gets it synced)   // the stored, every few seconds
  if (acc < 1) return;
  const float step = (float)acc; acc = 0;
  if (!lv.isRemote()) coolDropped(lv, step);
  auto cool = [&](ItemStack& s) { if (!s.empty() && s.tag.is_object() && s.tag.contains("heat")) setItemHeat(s, heatTowards(itemHeat(s), AIR_TEMP, step)); };
  for (Player* p : levelPlayers(lv)) {
    for (auto& s : p->inv) cool(s); for (auto& s : p->offhand) cool(s);
    // hot metal held bare-handed singes you now and then (not with tongs in the other hand)
    const ItemStack& h = p->held();
    if (!h.empty() && itemHeat(h) > 100 && p->offhand[0].id != "tongs" && p->mode != "creative") {
      const float t = (p->data.is_object() ? p->data.value("singeT", 0.f) : 0.f) + step;
      if (t >= 3) { p->data["singeT"] = 0; hurtPlayer(lv, *p, 1, "burn"); } else p->data["singeT"] = t;
    }
  }
}

bool quench(ItemStack& s) {
  const float h = itemHeat(s);
  if (h <= 100) return false;
  const ItemDef* d = s.def();
  if (d && d->has("metalHead") && h >= 300) { if (!s.tag.is_object()) s.tag = json::object(); s.tag["q"] = 1; }
  setItemHeat(s, AIR_TEMP);
  return true;
}

} // namespace dq
