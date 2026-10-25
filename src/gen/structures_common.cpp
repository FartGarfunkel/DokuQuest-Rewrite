// gen/structures_common.cpp — shared structure helpers (see structures_common.h).
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
static void rollTable(json& items, const json& table, Mulberry32& rnd) {
  for (const auto& e : table) {
    const std::string id = e[0].get<std::string>();
    const int a = e[1].get<int>(), b = e[2].get<int>(); const double p = e[3].get<double>();
    if (!hasItem(id) || rnd() > p) continue;
    int k = jsFloor(rnd() * 27); for (int t = 0; t < 27 && !items[k].is_null(); t++) k = (k + 1) % 27;
    items[k] = json{{"id", id}, {"n", a + jsFloor(rnd() * (b - a + 1))}};
  }
}
static json emptySlots() { json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr); return items; }

json structLoot(const std::string& kind, Mulberry32& rnd) {
  json items = emptySlots();
  const json& L = SD()["STRUCT_LOOT"];
  rollTable(items, L.contains(kind) ? L[kind] : L["ruin"], rnd);
  // a chest is never quite empty
  bool any = false; for (auto& s : items) if (!s.is_null()) { any = true; break; }
  if (!any) { int k = jsFloor(rnd() * 27); items[k] = json{{"id", hasItem("emerald") ? "emerald" : "coal"}, {"n", 1 + jsFloor(rnd() * 3)}}; }
  // now and then, an old book
  const double pb = kind == "tower" ? 0.45 : kind == "maze_heart" || kind == "archmage" ? 0.6 : 0.18;
  if (rnd() < pb) {
    const auto& K = loreKeys();
    const std::string k = K[jsFloor(rnd() * K.size())];
    int s = jsFloor(rnd() * 27); for (int t = 0; t < 27 && !items[s].is_null(); t++) s = (s + 1) % 27;
    items[s] = json{{"id", "lore_" + k}, {"n", 1}};
  }
  return items;
}

json dungeonLoot(const std::string& kind, Mulberry32& rnd) {
  json items = emptySlots();
  const json& L = SD()["DUNGEON_LOOT"];
  json table = L[kind];
  if (kind == "boss") for (auto& e : L["rare"]) table.push_back(e);
  else if (kind == "rare") for (int i = 0; i < 8 && i < (int)L["common"].size(); i++) table.push_back(L["common"][i]);
  rollTable(items, table, rnd);
  return items;
}

json vLoot(const std::string& kind, Mulberry32& rnd) {
  json items = emptySlots();
  const json& L = SD()["VLOOT"];
  if (L.contains(kind)) rollTable(items, L[kind], rnd);
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
  int i = put(x, y, z, "chest", (f + 2) & 3);
  if (i >= 0) { Mulberry32 r(hash32(x, y + S.hashDy, z, (i32)S.seed)); setEntity(i, json{{"type", "chest"}, {"items", structLoot(kind, r)}}); }
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
