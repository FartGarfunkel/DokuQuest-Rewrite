// gen/structures_common.h — what every structure builder shares: the original's sPut (SP below), its block
// lookups (B[key] — an unknown key is skipped, never placed as air), loot tables (structures.js structLoot,
// dungeons.js dungeonLoot, villages.js vLoot), the generated data tables (structures_data.cpp) and the land
// helpers (sLand, sLandy).
//
// Heights: plans decide with the original's heights (TerrainColumn::jsY, 128-world units) so they accept exactly
// the places the original did, and build at band heights (TerrainColumn::y). StructurePlan::hashDy carries the
// difference of the two base heights so per-block random picks and loot match the original's.
// Data written for other workstreams (residents, spawners, boss, folk...) uses band-local y, like StructurePlan::y.
#pragma once
#include "gen/structures.h"
#include "gen/overworld.h"
#include "gen/dimgens.h"
#include <string_view>

namespace dq {

const char* structureDataJson();          // generated: tools/port/structdata.py
const json& SD();                          // the parsed tables (STRUCT_LOOT, VSTYLE, DTHEME, LORE_KEYS...)

constexpr BlockId NOBLOCK = 0xFFFF;
BlockId bid(std::string_view key);         // the block, AIR for "air", NOBLOCK for "" (null) or a block this game lacks
inline bool hasBlock(std::string_view k) { return bid(k) != NOBLOCK; }
bool hasItem(const std::string& key);
inline std::string PLK(const std::string& k, const std::string& alt) { return hasBlock(k) ? k : alt; }   // places.js PLK / towns.js VB
inline bool tSolid(BlockId id) { return id != NOBLOCK && reg().tSolid[id]; }
inline bool tLiquid(BlockId id) { return id != NOBLOCK && reg().tLiquid[id]; }
const std::string& blockKey(BlockId id);

// loot: 27 slots of null | {id, n}
json structLoot(const std::string& kind, Mulberry32& rnd);
json dungeonLoot(const std::string& kind, Mulberry32& rnd);
json vLoot(const std::string& kind, Mulberry32& rnd);
const std::vector<std::string>& loreKeys();
std::vector<std::string> strList(const json& j);   // a JSON array of strings

// how even the land is over a footprint (structures.js sLand): band heights and the original's
struct Land { int lo, hi, mean, jlo, jhi, jmean; };
Land sLand(const TerrainQuery& t, int x0, int z0, int x1, int z1);
inline bool sLandy(const TerrainColumn& c) { return c.jsY > vmap::JS_SEA + 1 && c.biome != BIOME_OCEAN && c.biome != BIOME_BEACH; }
inline const OverworldGen* owOf(const TerrainQuery& t) { return dynamic_cast<const OverworldGen*>(&t); }
inline const SkylandsGen* skyOf(const TerrainQuery& t) { return dynamic_cast<const SkylandsGen*>(&t); }
inline const EndGen* endOf(const TerrainQuery& t) { return dynamic_cast<const EndGen*>(&t); }
inline const DepthsGen* depthsOf(const TerrainQuery& t) { return dynamic_cast<const DepthsGen*>(&t); }

template <class T> const T& extOf(const StructurePlan& S) { return *static_cast<const T*>(S.ext.get()); }
using vmap::JS_SEA;
constexpr double JTAU = 6.283185307179586;     // the original's TAU (a double; common.h TAU is a float)
constexpr double JPI = 3.141592653589793;
inline int jsign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
inline json jat(int x, int y, int z) { return json::array({x, y, z}); }
inline json mobAt(int x, int y, int z, const char* mob) { return json{{"at", jat(x, y, z)}, {"mob", mob}}; }

// The original sPut: one structure's builder within the chunk being generated (band-local y).
class SP {
public:
  explicit SP(ChunkWriter& w) : w(w), S(w.S), bx(w.bx), bz(w.bz) {}
  ChunkWriter& w; const StructurePlan& S; const int bx, bz;
  bool in(int x, int z) const { return x >= bx && x < bx + 16 && z >= bz && z < bz + 16; }
  int put(int x, int y, int z, std::string_view key, u8 meta = 0) { BlockId id = bid(key); return id == NOBLOCK ? -1 : w.put(x, y, z, id, meta); }
  int putId(int x, int y, int z, BlockId id, u8 meta = 0) { return id == NOBLOCK ? -1 : w.put(x, y, z, id, meta); }
  BlockId get(int x, int y, int z) const { return w.get(x, y, z); }     // NOBLOCK outside the chunk
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view key, u8 meta = 0);
  template <class F> void fillf(int x0, int y0, int z0, int x1, int y1, int z1, F f, u8 meta = 0) {
    const int ax = std::max(x0, bx), ex = std::min(x1, bx + 15), az = std::max(z0, bz), ez = std::min(z1, bz + 15);
    for (int y = y0; y <= y1; y++) for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) put(x, y, z, f(x, y, z), meta);
  }
  // the original's P.h / P.pick: h() when y is a constant or a local count, hw() / pickw() when y is a world height
  // (then hashDy is added, so the picks match the original's at the same place in the structure)
  double h(int x, int y, int z, int s = 0) const { return w.h(x, y, z, s); }
  double hw(int x, int y, int z, int s = 0) const { return w.h(x, y + S.hashDy, z, s); }
  template <class V> const auto& pick(const V& list, int x, int y, int z, int s = 0) const { return list[(size_t)std::floor(h(x, y, z, s) * list.size())]; }
  template <class V> const auto& pickw(const V& list, int x, int y, int z, int s = 0) const { return list[(size_t)std::floor(hw(x, y, z, s) * list.size())]; }
  json* entity(int idx) { auto it = w.ch.blockEntities.find(idx); return it == w.ch.blockEntities.end() ? nullptr : &it->second; }
  void setEntity(int idx, json e) { if (idx >= 0) w.ch.blockEntities[idx] = std::move(e); }
  // a chest whose front faces f (0 north, 1 east, 2 south, 3 west), filled from a loot table
  int chest(int x, int y, int z, int f, const std::string& kind);
  int lectern(int x, int y, int z, int f, const std::string& lore = "");
  void ladder(int x, int y0, int y1, int z, int f) { for (int y = y0; y <= y1; y++) put(x, y, z, "ladder", f); }
  int ground(int x, int z) const { return w.ground(x, z); }
  // a monster cage with what it breathes out (block entity {type:'spawner', mob} — an addition: the original kept
  // the mob in the plan's spawners list only)
  int spawner(int x, int y, int z, const std::string& mob);
};

} // namespace dq
