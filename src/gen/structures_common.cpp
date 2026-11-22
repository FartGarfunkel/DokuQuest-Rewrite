// gen/structures_common.cpp — shared structure helpers (see structures_common.h).
#include <unordered_map>
#include "sim/spell_items.h"
#include "core/tool_parts.h"
#include "sim/balance.h"
#include "gen/structures_common.h"
#include <mutex>

namespace dq {

const json& SD() {
  static const json d = json::parse(structureDataJson());
  return d;
}

namespace {
struct SvHash { using is_transparent = void; size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); } };
struct SvEq { using is_transparent = void; bool operator()(std::string_view a, std::string_view b) const { return a == b; } };
struct KeyTable { std::unordered_map<std::string, BlockId, SvHash, SvEq> ids; size_t n = 0; };
const KeyTable& keyTable() {
  static std::mutex mx;
  static std::shared_ptr<const KeyTable> cur;
  thread_local std::shared_ptr<const KeyTable> mine;
  const size_t n = reg().blocks.size();
  if (mine && mine->n == n) return *mine;
  std::lock_guard<std::mutex> lk(mx);
  if (!cur || cur->n != n) {
    auto t = std::make_shared<KeyTable>(); t->n = n;
    for (const auto& b : reg().blocks) t->ids.emplace(b.key, b.id);
    cur = t;
  }
  mine = cur;
  return *mine;
}
} // namespace

BlockId bid(std::string_view key) {
  if (key == "air") return AIR;
  if (key.empty()) return NOBLOCK;      // the original's null / undefined key: nothing placed
  const auto& t = keyTable().ids;
  auto it = t.find(key);
  return it == t.end() ? NOBLOCK : it->second;
}
bool hasItem(const std::string& key) { return reg().item(key) != nullptr; }
const std::string& blockKey(BlockId id) { static const std::string none; return id == NOBLOCK ? none : reg().block(id).key; }

std::vector<std::string> strList(const json& j) {
  std::vector<std::string> v;
  if (j.is_array()) for (auto& e : j) if (e.is_string()) v.push_back(e.get<std::string>());
  return v;
}
const std::vector<std::string>& loreKeys() { static const std::vector<std::string> k = strList(SD()["LORE_KEYS"]); return k; }

// one pass of a loot table into 27 slots (the shared loop of structLoot / dungeonLoot / vLoot)
static void rollTable(json& items, const json& table, Mulberry32& rnd, double chanceMul = 1) {
  for (const auto& e : table) {
    const std::string id = e[0].get<std::string>();
    const int a = e[1].get<int>(), b = e[2].get<int>(); const double p = std::min(1.0, e[3].get<double>() * chanceMul);
    if (!hasItem(id) || rnd() > p) continue;
    int k = jsFloor(rnd() * 27); for (int t = 0; t < 27 && !items[k].is_null(); t++) k = (k + 1) % 27;
    items[k] = json{{"id", id}, {"n", a + jsFloor(rnd() * (b - a + 1))}};
  }
}
static json emptySlots() { json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr); return items; }

// now and then a spell scroll (sim/spell_items.h): a spell any tome teaches, its rank by the
// place (deeper and grander places, rarer scrolls)
static void maybeScroll(json& items, Mulberry32& rnd, double chance, int maxRank) {
  if (!hasItem("spell_scroll") || rnd() >= chance) return;
  static const std::vector<std::string> spells = [] { std::vector<std::string> v; for (const ItemDef& d : reg().items) if (d.has("tome")) v.push_back(d.str("tome")); std::sort(v.begin(), v.end()); return v; }();
  if (spells.empty()) return;
  const std::string k = spells[jsFloor(rnd() * spells.size()) % spells.size()];
  int r = 1; while (r < maxRank && rnd() < 0.45) r++;
  int slot = jsFloor(rnd() * 27); for (int t = 0; t < 27 && !items[slot].is_null(); t++) slot = (slot + 1) % 27;
  if (items[slot].is_null()) items[slot] = makeScroll(k, r).toJson();
}

// now and then a smithing template (in the places that keep them): its own roll, seeded from what the chest already
// holds, so the place's other rolls (and every world made before) come out the same
static void maybeTemplate(json& items, const std::string& kind) {
  static const std::unordered_map<std::string, std::vector<std::pair<const char*, double>>> T = {
    {"ember_bastion", {{"cinderite_upgrade_smithing_template", 0.45}, {"snout_armor_trim_smithing_template", 0.12}}},
    {"ember_spire", {{"cinderite_upgrade_smithing_template", 0.3}}}, {"forge", {{"cinderite_upgrade_smithing_template", 0.2}}},
    {"nether_fortress", {{"rib_armor_trim_smithing_template", 0.15}}}, {"temple", {{"dune_armor_trim_smithing_template", 0.14}, {"wild_armor_trim_smithing_template", 0.1}}},
    {"wreck_treasure", {{"coast_armor_trim_smithing_template", 0.2}}}, {"ocean", {{"tide_armor_trim_smithing_template", 0.2}}},
    {"stronghold", {{"eye_armor_trim_smithing_template", 0.1}}}, {"stronghold_library", {{"eye_armor_trim_smithing_template", 0.1}}},
    {"ancient_city", {{"ward_armor_trim_smithing_template", 0.12}, {"silence_armor_trim_smithing_template", 0.04}}},
    {"end_city", {{"spire_armor_trim_smithing_template", 0.12}}}, {"maze", {{"vex_armor_trim_smithing_template", 0.06}}}, {"maze_heart", {{"vex_armor_trim_smithing_template", 0.3}}},
    {"tower", {{"sentry_armor_trim_smithing_template", 0.12}}},
    {"ruin", {{"wayfinder_armor_trim_smithing_template", 0.06}, {"raiser_armor_trim_smithing_template", 0.05}, {"shaper_armor_trim_smithing_template", 0.05}, {"host_armor_trim_smithing_template", 0.05}}},
    {"deep", {{"flow_armor_trim_smithing_template", 0.06}, {"bolt_armor_trim_smithing_template", 0.06}}},
    {"deep_secret", {{"flow_armor_trim_smithing_template", 0.25}, {"bolt_armor_trim_smithing_template", 0.25}}}};
  auto it = T.find(kind);
  if (it == T.end()) return;
  u32 h = 2166136261u; for (char c : items.dump()) h = (h ^ (u8)c) * 16777619u;
  Mulberry32 r(h);
  for (auto& [key, chance] : it->second) {
    if (!hasItem(key) || r() >= chance) continue;
    int slot = jsFloor(r() * 27); for (int t = 0; t < 27 && !items[slot].is_null(); t++) slot = (slot + 1) % 27;
    if (items[slot].is_null()) items[slot] = json{{"id", key}, {"n", 1}};
  }
}

static void convertLootGear(json& items) {
  for (auto& s : items) if (s.is_object() && s.contains("id") && s["id"].is_string()) {
    const std::string id = s["id"].get<std::string>();
    const int n = s.value("n", 1), d = s.value("d", 0);
    const ItemStack p = proceduralOf(id, n);
    if (p.id == id) continue;
    json j = p.toJson();
    if (d) j["d"] = d;
    s = std::move(j);
  }
}

// late places keep the rich baked table; everything a new player can walk into from spawn does not.
static bool lateLootKind(const std::string& k) {
  // a wizard tower (archmage) can stand next to spawn: it is not late
  return k == "maze_heart" || k == "end_city" || k == "ancient_city"
      || k == "deep_secret" || k == "deep_vault" || k == "twin_vault" || k == "buried_treasure"
      || k == "ember_spire" || k == "ember_bastion" || k == "sky_temple" || k == "sky_sanctum"
      || k == "skyship" || k == "deep" || k == "forge";
}
static bool dumpGem(const std::string& id) {
  return id == "emerald" || id == "diamond" || id == "ruby" || id == "sapphire"
      || id == "mithril_ingot" || id == "emerald_block" || id == "gold_block";
}
static json stripDumpGems(const json& src) {
  json out = json::array();
  if (!src.is_array()) return out;
  for (const auto& e : src) {
    if (!e.is_array() || e.empty() || !e[0].is_string()) continue;
    if (dumpGem(e[0].get<std::string>())) continue;
    out.push_back(e);
  }
  return out;
}
static json structTable(const std::string& kind, int tier = 0) {
  const json& L = SD()["STRUCT_LOOT"];
  const json& src = L.contains(kind) ? L[kind] : L["ruin"];
  if (lateLootKind(kind)) return src;
  json out = stripDumpGems(src);
  if (tier >= 3) {
    out.push_back(json::array({"gold_ingot", 1, 2, 0.12}));
    out.push_back(json::array({"emerald", 1, 1, 0.06}));
  }
  if (tier >= 6) {
    out.push_back(json::array({"gold_ingot", 1, 3, 0.16}));
    out.push_back(json::array({"emerald", 1, 2, 0.10}));
    out.push_back(json::array({"diamond", 1, 1, 0.08}));
  }
  return out;
}
static json villageTable(const std::string& kind) {
  json table = json::array();
  auto add = [&](const char* id, int a, int b, double p) { table.push_back(json::array({id, a, b, p})); };
  if (kind == "house") {
    add("bread", 1, 3, 0.55); add("apple", 1, 2, 0.40); add("wheat_seeds", 1, 4, 0.35);
    add("torch", 1, 4, 0.30); add("potato", 1, 3, 0.25); add("carrot", 1, 2, 0.25); add("stick", 1, 4, 0.28);
    add("coal", 1, 2, 0.12);
  } else if (kind == "smithy") {
    add("iron_ingot", 1, 2, 0.45); add("coal", 2, 5, 0.55); add("bread", 1, 2, 0.35);
    add("iron_pickaxe", 1, 1, 0.12); add("iron_sword", 1, 1, 0.08); add("iron_helmet", 1, 1, 0.06);
  } else if (kind == "library") {
    add("book", 1, 2, 0.60); add("paper", 1, 4, 0.50); add("feather", 1, 2, 0.35);
    add("empty_map", 1, 1, 0.10); add("ink_sac", 1, 2, 0.20);
  } else if (kind == "tavern" || kind == "store") {
    add("bread", 1, 3, 0.60); add("cooked_beef", 1, 2, 0.28); add("apple", 1, 3, 0.40);
    add("honey_bottle", 1, 1, 0.18); add("bowl", 1, 2, 0.20); add("coal", 1, 2, 0.18);
    add("gold_nugget", 1, 3, 0.08);
  } else {
    const json& L = SD()["VLOOT"];
    if (L.contains(kind)) return stripDumpGems(L[kind]);
  }
  return table;
}

json structLoot(const std::string& kind, Mulberry32& rnd) {
  json items = emptySlots();
  rollTable(items, structTable(kind, 0), rnd);
  convertLootGear(items);
  // a chest is never quite empty
  bool any = false; for (auto& s : items) if (!s.is_null()) { any = true; break; }
  if (!any) { int k = jsFloor(rnd() * 27); items[k] = json{{"id", hasItem("coal") ? "coal" : "bone"}, {"n", 1 + jsFloor(rnd() * 3)}}; }
  // now and then, an old book
  const double pb = kind == "tower" ? 0.45 : kind == "maze_heart" || kind == "archmage" ? 0.6 : 0.18;
  if (rnd() < pb) {
    const auto& K = loreKeys();
    const std::string k = K[jsFloor(rnd() * K.size())];
    int s = jsFloor(rnd() * 27); for (int t = 0; t < 27 && !items[s].is_null(); t++) s = (s + 1) % 27;
    items[s] = json{{"id", "lore_" + k}, {"n", 1}};
  }
  maybeScroll(items, rnd, kind == "tower" ? 0.5 : kind == "archmage" || kind == "maze_heart" ? 0.8 : kind == "temple" || kind == "stronghold_library" ? 0.3 : 0.12, kind == "archmage" ? 8 : kind == "tower" ? 5 : 4);
  maybeTemplate(items, kind);
  return items;
}

static json dungeonTable(const std::string& kind, int tier = 0) {
  // world gen has no player: a first dungeon is poor. Later opens (lootTopUp) use a richer table.
  json table = json::array();
  auto add = [&](const char* id, int a, int b, double p) { table.push_back(json::array({id, a, b, p})); };
  if (kind == "common") {
    add("bread", 1, 2, 0.40); add("bone", 1, 4, 0.55); add("arrow", 2, 6, 0.30);
    add("coal", 1, 4, 0.30); add("string", 1, 3, 0.22); add("rotten_flesh", 1, 3, 0.28);
    add("torch", 2, 4, 0.22); add("iron_ingot", 1, 1, 0.10);
    if (tier >= 3) { add("iron_ingot", 1, 2, 0.18); add("gold_ingot", 1, 1, 0.08); add("iron_sword", 1, 1, 0.08); }
    if (tier >= 6) { add("emerald", 1, 1, 0.08); add("steel_ingot", 1, 1, 0.08); }
  } else if (kind == "rare") {
    add("iron_ingot", 1, 2, 0.30); add("bread", 1, 2, 0.25); add("bone", 2, 4, 0.30);
    add("iron_sword", 1, 1, 0.10); add("iron_helmet", 1, 1, 0.07); add("potion_heal", 1, 1, 0.08);
    if (tier >= 3) { add("gold_ingot", 1, 2, 0.16); add("steel_sword", 1, 1, 0.10); add("emerald", 1, 1, 0.08); add("enchanted_book", 1, 1, 0.10); }
    if (tier >= 6) { add("diamond", 1, 1, 0.10); add("ruby", 1, 1, 0.10); add("sapphire", 1, 1, 0.10); add("steel_chestplate", 1, 1, 0.08); }
  } else {   // boss
    add("iron_chestplate", 1, 1, 0.16); add("steel_ingot", 1, 2, 0.18); add("iron_sword", 1, 1, 0.12);
    add("potion_heal", 1, 1, 0.14); add("golden_apple", 1, 1, 0.08);
    add("enchanted_book", 1, 1, 0.10);
    if (tier >= 3) { add("steel_sword", 1, 1, 0.16); add("steel_chestplate", 1, 1, 0.12); add("gold_sword", 1, 1, 0.10); add("emerald", 1, 2, 0.14); }
    if (tier >= 6) { add("diamond", 1, 2, 0.18); add("mithril_sword", 1, 1, 0.10); add("mithril_ingot", 1, 2, 0.14); }
  }
  return table;
}

// a chest first opened by a player who has come far: more passes over its own table (sim/balance.h progress), the extras
// into whatever slots are free; the place keeps its identity (no new kinds of find), it just gives more, and likelier the rare
int lootTopUp(json& items, const std::string& tableKind, int tier, Mulberry32& rnd) {
  if (tier <= 0 || !items.is_array() || tableKind.size() < 3) return 0;
  const bool dungeon = tableKind.rfind("d:", 0) == 0;
  const std::string kind = tableKind.substr(2);
  json table;
  if (dungeon) { if (!SD()["DUNGEON_LOOT"].contains(kind)) return 0; table = dungeonTable(kind, tier); }
  else { table = structTable(kind, tier); }
  float passes = progressLootRolls(tier); int added = 0;
  const double mul = progressLootChance(tier);
  for (; passes > 0; passes -= 1) {
    if (passes < 1 && rnd() >= passes) break;
    json extra = emptySlots(); rollTable(extra, table, rnd, mul);
    for (auto& s : extra) {
      if (!s.is_object()) continue;
      if (s["id"].is_string()) { const ItemStack p = proceduralOf(s["id"].get<std::string>(), s.value("n", 1)); if (p.id != s["id"].get<std::string>()) s = p.toJson(); }
      for (size_t k = 0; k < items.size(); k++) if (items[k].is_null()) { items[k] = s; added++; break; }
    }
  }
  return added;
}

json dungeonLoot(const std::string& kind, Mulberry32& rnd) {
  json items = emptySlots();
  json table = dungeonTable(kind, 0);   // a chest in the world: the poor table; a far-come player tops it up on first open
  rollTable(items, table, rnd);
  maybeScroll(items, rnd, kind == "boss" ? 0.25 : kind == "rare" ? 0.08 : 0.03, kind == "boss" ? 4 : kind == "rare" ? 3 : 2);
  convertLootGear(items);
  bool any = false; for (auto& s : items) if (!s.is_null()) { any = true; break; }
  if (!any) { int k = jsFloor(rnd() * 27); items[k] = json{{"id", hasItem("coal") ? "coal" : "bone"}, {"n", 1 + jsFloor(rnd() * 3)}}; }
  return items;
}

json vLoot(const std::string& kind, Mulberry32& rnd) {
  json items = emptySlots();
  rollTable(items, villageTable(kind), rnd);
  convertLootGear(items);
  return items;
}

Land sLand(const TerrainQuery& t, int x0, int z0, int x1, int z1) {
  Land r{999, -1, 0, 999, -1, 0};
  double sum = 0, jsum = 0; int n = 0;
  const int sz = std::max(1, (z1 - z0) >> 2), sx = std::max(1, (x1 - x0) >> 2);
  for (int z = z0; z <= z1; z += sz) for (int x = x0; x <= x1; x += sx) {
    TerrainColumn c = t.terrainColumn(x, z);
    r.lo = std::min(r.lo, c.y); r.hi = std::max(r.hi, c.y); sum += c.y;
    r.jlo = std::min(r.jlo, c.jsY); r.jhi = std::max(r.jhi, c.jsY); jsum += c.jsY;
    n++;
  }
  r.mean = jsRound(sum / n); r.jmean = jsRound(jsum / n);
  return r;
}

// ---- SP ----
void SP::fill(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view key, u8 meta) {
  BlockId id = bid(key);
  if (id == NOBLOCK) return;
  w.fill(x0, y0, z0, x1, y1, z1, id, meta);
}

int SP::chest(int x, int y, int z, int f, const std::string& kind) {
  int i = put(x, y, z, "chest", (2 - f) & 3);   // the placing meta: 0 S, 1 E, 2 N, 3 W (east and west ran backwards)
  if (i >= 0) { Mulberry32 r(hash32(x, y + S.hashDy, z, (i32)S.seed)); setEntity(i, json{{"type", "chest"}, {"items", structLoot(kind, r)}, {"lk", "s:" + kind}}); }
  return i;
}

int SP::lectern(int x, int y, int z, int f, const std::string& lore) {
  int i = put(x, y, z, "lectern", (f + 2) & 3);
  if (i >= 0) {
    const auto& K = loreKeys();
    setEntity(i, json{{"type", "lectern"}, {"book", true}, {"lore", !lore.empty() ? lore : K[jsFloor(hw(x, y, z, 5) * K.size())]}});
  }
  return i;
}

int SP::spawner(int x, int y, int z, const std::string& mob) {
  int i = put(x, y, z, "monster_spawner");
  if (i >= 0) setEntity(i, json{{"type", "spawner"}, {"mob", mob}, {"structure", S.id}});
  return i;
}

} // namespace dq
