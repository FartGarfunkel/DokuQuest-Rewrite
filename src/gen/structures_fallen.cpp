// gen/structures_fallen.cpp — where the fallen adventurers lie (sim/fallen.h, drawn by render/fallen_layer.cpp): only in the
// abandoned places, in every realm. Ruins, dungeons, mineshafts, temples, strongholds, the Soulrend places, the Nether's
// fortresses and forges, the End's cities, the Aether's dungeons: now and then someone who came for the loot and never left,
// lying on the floor a few steps from a chest or a monster cage. Never in a village or on a road.
#include "gen/structures.h"
#include "gen/gendefs.h"
#include "core/registry.h"
#include "common/noise.h"
#include "common/common.h"
#include <algorithm>
#include <cstdlib>
#include <unordered_map>

namespace dq {

// the remains for a place: the miner in the deep ones, the wanderer in the sand, a traveller elsewhere
static const char* kindFor(const std::string& key) {
  static const std::unordered_map<std::string, const char*> K = {
    {"mineshaft", "miner"}, {"stronghold", "miner"}, {"ancient_city", "miner"}, {"deep_halls", "miner"}, {"twin_vault", "miner"},
    {"dungeon", "miner"}, {"classic_dungeon", "miner"}, {"labyrinth", "traveller"}, {"desert_temple", "desert"},
    {"ruin", "traveller"}, {"wreck", "swamp"}, {"ocean_temple", "swamp"}, {"dragon_lair", "traveller"}, {"poi", "traveller"},
    {"silver_dungeon", "traveller"}, {"gold_dungeon", "traveller"}, {"bronze_dungeon", "traveller"}, {"sky_temple", "traveller"},
    {"skyship", "traveller"}, {"sky_sanctum", "traveller"}, {"ember_bastion", "traveller"}, {"ember_spire", "traveller"},
    {"ruined_forge", "traveller"}, {"bone_pit", "traveller"}, {"nether_fortress", "traveller"}, {"end_city", "traveller"},
  };
  auto it = K.find(key);
  return it == K.end() ? nullptr : it->second;
}

bool fallenPlaceKind(const std::string& structureKey) { return kindFor(structureKey) != nullptr; }

void fallenInPlace(GenBuf& b, Chunk& ch, const std::string& structureKey, int wx0, int wz0, int wx1, int wz1, u32 seed) {
  const char* kind = kindFor(structureKey);
  if (!kind) return;
  BlockId rid = reg().blockId(std::string("remains_") + kind);
  if (!rid) rid = reg().blockId("remains_traveller");
  if (!rid) return;
  const int bx = ch.cx * 16, bz = ch.cz * 16;
  auto open = [&](int x, int y, int z) {   // an empty floor cell: air with air over it and something solid under it
    if (x < 0 || x > 15 || z < 0 || z > 15 || y < 1 || y >= DIM_H - 2) return false;
    if (b.blocks[colIdx(x, y, z)] != AIR || b.blocks[colIdx(x, y + 1, z)] != AIR) return false;
    const BlockDef& u = reg().block(b.blocks[colIdx(x, y - 1, z)]);
    return u.solid && u.render != Render::Liquid && u.render != Render::Model && u.render != Render::Shape;
  };
  // the chests and cages this place has in this chunk, in a fixed order (the same world for everyone)
  std::vector<int> spots;
  for (auto& [i, e] : ch.blockEntities) {
    if (!e.is_object()) continue;
    const std::string t = e.value("type", "");
    if (t != "chest" && t != "spawner") continue;
    const int x = bx + (i & 15), z = bz + ((i >> 4) & 15);
    if (x < wx0 || x > wx1 || z < wz0 || z > wz1) continue;
    spots.push_back(i);
  }
  std::sort(spots.begin(), spots.end());
  for (int i : spots) {
    const int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
    Mulberry32 r(hash32((i32)seed, bx + lx, ly, bz + lz) ^ 0xFA11E7u);
    if (r() >= (structureKey == "classic_dungeon" ? 0.12 : 0.3)) continue;   // about one chest or cage in three (the old monster rooms are everywhere: fewer)
    for (int tries = 0; tries < 14; tries++) {
      const int x = lx + (int)(r() * 7) - 3, z = lz + (int)(r() * 7) - 3, y = ly + (int)(r() * 3) - 1;
      if ((x == lx && z == lz) || !open(x, y, z)) continue;
      const int k = colIdx(x, y, z);
      b.blocks[k] = rid; b.meta[k] = (u8)(r() * 4);
      static const bool lg = std::getenv("DQ_FALLEN_LOG") != nullptr;
      if (lg) DQ_LOG("fallen: %s in %s at %d %d %d", kind, structureKey.c_str(), bx + x, dimBase(ch.dim) + y, bz + z);
      return;   // one to a place in a chunk
    }
  }
}

} // namespace dq
