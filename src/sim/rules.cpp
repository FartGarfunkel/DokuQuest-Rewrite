// sim/rules.cpp — port of game/rules.js (rules, advanceTime, graves).
#include "sim/rules.h"
#include "sim/gameplay.h"

namespace dq {

const json& ruleDefaults() {
  static const json d = {
    {"dayMinutes", 10}, {"nightMinutes", 10},
    {"daylightCycle", true}, {"keepInventory", false}, {"graves", true}, {"mobSpawning", true}, {"mobGriefing", true}, {"tntExplodes", true},
    {"fireSpread", true}, {"blockDrops", true}, {"fallDamage", true}, {"drowning", true}, {"fireDamage", true}, {"naturalRegen", true},
    {"grassSpread", true}, {"saplingsPlant", true}, {"showCoords", false}, {"commands", false}, {"downed", true}, {"haunting", true},
  };
  return d;
}
const std::vector<std::array<std::string, 3>>& ruleToggles() {
  static const std::vector<std::array<std::string, 3>> t = {
    {"daylightCycle", "Day and night cycle", "Turn off to keep the sun where it is"},
    {"keepInventory", "Keep inventory on death", "You respawn with everything you had"},
    {"graves", "Graves", "What you drop on death is kept safe in a grave, with a compass that points to it"},
    {"mobSpawning", "Creatures spawn", "Animals and monsters appear on their own"},
    {"mobGriefing", "Creatures break blocks", "Explosions from creatures and boss attacks damage the land"},
    {"tntExplodes", "TNT explodes", "Turn off and TNT just fizzles"},
    {"fireSpread", "Fire spreads", "Fire can spread to wood and leaves nearby"},
    {"blockDrops", "Blocks drop items", "Broken blocks drop what they're made of"},
    {"fallDamage", "Fall damage", ""},
    {"drowning", "Drowning", "Your breath runs out under water"},
    {"fireDamage", "Fire and lava damage", ""},
    {"naturalRegen", "Slow natural healing", "Heal half a heart every few seconds when not hurt for a while"},
    {"grassSpread", "Grass spreads", "Grass slowly grows onto nearby bare dirt"},
    {"saplingsPlant", "Saplings plant themselves", "A sapling left lying on soil takes root"},
    {"showCoords", "Show coordinates", "Your position, shown in the corner"},
    {"commands", "Allow commands", "Chat commands and WorldEdit (press T or /)"},
    {"downed", "Downed, not dead (co-op)", "With someone else playing, you go down instead of dying, and they can help you back up"},
  };
  return t;
}

json& rules(Level& lv) {
  json& r = lv.info.rules;
  if (!r.is_object() || r.empty()) { r = ruleDefaults(); r["commands"] = lv.info.mode == "creative"; r["_v"] = 1; r["_v2"] = 2; }
  else if (r.value("_v2", 0) != 2) { for (auto& [k, v] : ruleDefaults().items()) if (!r.contains(k)) r[k] = v; r["_v"] = 1; r["_v2"] = 2; }
  return r;
}
bool rule(Level& lv, const std::string& key) {
  const json& r = rules(lv);
  auto it = r.find(key);
  if (it == r.end()) { auto d = ruleDefaults().find(key); return d != ruleDefaults().end() && d->is_boolean() && d->get<bool>(); }
  return it->is_boolean() ? it->get<bool>() : it->is_number() ? it->get<double>() != 0 : false;
}
double ruleNum(Level& lv, const std::string& key) {
  const json& r = rules(lv);
  auto it = r.find(key);
  if (it != r.end() && it->is_number()) return it->get<double>();
  auto d = ruleDefaults().find(key);
  return d != ruleDefaults().end() && d->is_number() ? d->get<double>() : 0;
}

void advanceDayTime(Level& lv, double dt) {
  if (!rule(lv, "daylightCycle")) return;
  double phase = lv.info.time / DAY_LEN;
  double secs = (phase < 0.5 ? ruleNum(lv, "dayMinutes") : ruleNum(lv, "nightMinutes")) * 60;
  lv.info.time += dt * (DAY_LEN / 2) / std::max(30.0, secs);
  while (lv.info.time >= DAY_LEN) { lv.info.time -= DAY_LEN; lv.info.day++; }
}
float daylight(const Level& lv) {
  float t = (float)(std::fmod(lv.info.time, DAY_LEN) / DAY_LEN);
  float dayF = smoothstepf(-0.2f, 0.25f, std::sin(t * TAU));
  return 0.2f + 0.8f * dayF;
}

json makeGrave(Level& lv, Player& p, std::vector<ItemStack> stacks) {
  if (stacks.empty()) return nullptr;
  World& w = lv.world(p.realm);
  const Registry& R = reg();
  vec3 at = p.hasLastSafe && p.pos.y < (p.realm == REALM_MAIN ? -10.f : dimBase(DIM_OVERWORLD) - 10.f) ? p.lastSafe : p.pos;
  int x = ifloor(at.x), y = std::max(1, ifloor(at.y)), z = ifloor(at.z);
  auto freeAt = [&](int a, int b, int c) { BlockId id = w.getBlock(a, b, c); return (id == AIR || R.tReplaceable[id]) && !R.tPortal[id]; };
  bool found = false; int gx = 0, gy = 0, gz = 0;
  for (int r = 0; r <= 4 && !found; r++) for (int dy = 0; dy <= 6 && !found; dy++) for (int dz = -r; dz <= r && !found; dz++) for (int dx = -r; dx <= r && !found; dx++) {
    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
    int a = x + dx, b = y + dy, c = z + dz;
    if (b >= WORLD_H - 1 || !freeAt(a, b, c) || R.tLiquid[w.getBlock(a, b, c)] == 2) continue;
    found = true; gx = a; gy = b; gz = c;
  }
  const KnownBlocks& B = KB();
  if (!found || !B.grave) return nullptr;
  if (!R.tSolid[w.getBlock(gx, gy - 1, gz)]) w.setBlock(gx, gy - 1, gz, B.cobblestone);
  w.setBlock(gx, gy, gz, B.grave, (u8)((lookDir(p) + 2) & 3));
  json items = json::array(); for (auto& s : stacks) items.push_back(s.toJson());
  w.setBlockEntity(gx, gy, gz, {{"type", "grave"}, {"items", items}, {"owner", p.name}, {"xp", 0}, {"at", unixMillis()}});
  return {{"realm", p.realm}, {"dim", jsFromDim(dimOfY(gy))}, {"x", gx}, {"y", gy}, {"z", gz}};
}

void openGrave(Level& lv, Player& p, int x, int y, int z) {
  World& w = lv.world(p.realm);
  json e = w.blockEntity(x, y, z) ? *w.blockEntity(x, y, z) : json();
  w.setBlock(x, y, z, AIR);
  if (e.is_object() && e.contains("items")) for (auto& sj : e["items"]) {
    ItemStack s = ItemStack::fromJson(sj);
    if (s.empty()) continue;
    auto* d = reg().item(s.id);
    // armour and the charm go straight back on if the slot is free
    if (d && d->props.contains("armor")) { int slot = d->props["armor"].value("slot", 0); if (slot >= 0 && slot < 4 && p.armor[slot].empty()) { p.armor[slot] = s; continue; } }
    if (d && d->has("charm") && p.charm[0].empty()) { p.charm[0] = s; continue; }
    if (d && d->has("backpack") && p.back[0].empty()) { p.back[0] = s; continue; }
    int left = p.give(s);
    if (left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 0.5f, 0), false); }
  }
  if (p.grave.is_object() && p.grave.value("x", 0) == x && p.grave.value("y", 0) == y && p.grave.value("z", 0) == z) p.grave = nullptr;
  while (playerCount(p, "death_compass") > 0) if (playerRemove(p, "death_compass", 1) > 0) break;
  sfxUi(lv, p, "chest"); toast(lv, p, "You got your things back", "good");
  lv.fx->particles("motes", vec3(x + 0.5f, y + 0.5f, z + 0.5f), 12, {{"tile", "portal_mote_sky"}});
}

} // namespace dq
