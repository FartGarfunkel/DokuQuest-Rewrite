// gen/world_types.cpp — Better than Adventure's world types on DokuQuest's own Overworld (the create-world screen):
//   flat       BTA's Flat: an endless flat field of grass (the other worlds of the column as ever)
//   amplified  the hills and mountains near twice as high            (OverworldGen::baseColumn)
//   islands    the land sunk, so the seas cover most of it          (baseColumn)
//   winter     Endless Winter: every land snow-bound, the lakes frozen, snow lying on the ground
//   hell       BTA's Hell: a world that ended long ago — scorched ground, lava where the seas were, trees bare
//   floating   Floating Islands: the land cut into islands hanging over the void, each with a long rocky underside
// The column changes are in gen/overworld.cpp; this is the pass over each finished chunk, and the flat world.
#include "gen/dimgens.h"
#include "gen/overworld.h"
#include "core/registry.h"

namespace dq {

namespace {
// what each block is to the passes: 1 water / ice, 2 leaves, 3 a small plant, 4 soil that scorches, 5 snow
const std::vector<u8>& kinds() {
  static std::vector<u8> k;
  if (!k.empty()) return k;
  const Registry& R = reg();
  k.assign(R.blocks.size(), 0);
  for (size_t i = 0; i < R.blocks.size(); i++) {
    const BlockDef& b = R.blocks[i];
    if (R.tLiquid[i] == 1 || b.key == "ice" || b.key == "packed_ice" || b.key == "blue_ice") k[i] = 1;
    else if (b.has("isLeaves")) k[i] = 2;
    else if (b.key == "snow_layer" || b.key == "snow_block" || b.key == "powder_snow") k[i] = 5;
    else if (b.key == "grass" || b.key == "dirt" || b.key == "snowy_grass" || b.key == "podzol" || b.key == "mycelium" || b.key == "coarse_dirt" || b.key == "mud" || b.key == "moss_block") k[i] = 4;
    else if (i && !b.solid && R.tLiquid[i] == 0 && b.render == Render::Cross) k[i] = 3;
  }
  return k;
}
}

int OverworldGen::floatingBottom(int wx, int wz, int H) const {
  const double m = nFloor.noise2(wx / 210.0 + 311, wz / 210.0 - 97) + 0.45 * nFloor.noise2(wx / 70.0 - 13, wz / 70.0 + 51);
  const double thr = 0.1;
  if (m < thr) return -1;
  // a long rocky underside, deepest at the island's heart: a little ragged
  const int thick = (int)std::min(70.0, 5 + (m - thr) * 95 + nFloor.noise2(wx / 9.0, wz / 9.0) * 3);
  return std::max(1, H - thick);
}

void OverworldGen::worldTypePass(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  const auto& K = kinds();
  const BlockId lava = B.lava, scorch = B.scorched_dirt ? B.scorched_dirt : B.dirt, ash = B.ash, ice = B.ice, snowL = reg().blockId("snow_layer");
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = cx * 16 + x, wz = cz * 16 + z;
    if (wtype == "hell") {
      bool top = true;   // the first solid ground from the sky scorches
      for (int y = DIM_H - 1; y >= 1; y--) {
        const int i = colIdx(x, y, z); const BlockId id = b.blocks[i];
        if (!id) continue;
        const u8 kd = id < K.size() ? K[id] : 0;
        if (kd == 1) { b.blocks[i] = y <= SEA_LEVEL + 1 ? lava : AIR; b.meta[i] = 0; continue; }
        if (kd == 2 || kd == 3) { b.blocks[i] = AIR; b.meta[i] = 0; continue; }
        if (kd == 5) { b.blocks[i] = ash && y > SEA_LEVEL ? ash : AIR; b.meta[i] = 0; continue; }
        if (top && kd == 4) b.blocks[i] = scorch;
        if (reg().tSolid[id]) top = false;
      }
    } else if (wtype == "winter") {
      for (int y = DIM_H - 2; y >= 1; y--) {   // the top: snow lies on it, still water freezes
        const int i = colIdx(x, y, z); const BlockId id = b.blocks[i];
        if (!id) continue;
        const u8 kd = id < K.size() ? K[id] : 0;
        if (kd == 1 && reg().tLiquid[id] == 1) { b.blocks[i] = ice; b.meta[i] = 0; break; }
        if (kd == 3) continue;   // a plant: look under it
        if (reg().tSolid[id] && snowL && b.blocks[colIdx(x, y + 1, z)] == AIR && kd != 5) b.blocks[colIdx(x, y + 1, z)] = snowL;
        break;
      }
    } else if (wtype == "floating") {
      const int fb = floatingBottom(wx, wz, cols[z * 16 + x].H);
      for (int y = 0; y < DIM_H; y++) {
        const int i = colIdx(x, y, z); const BlockId id = b.blocks[i];
        if (!id) continue;
        if (fb < 0 || y < fb || reg().tLiquid[id]) { b.blocks[i] = AIR; b.meta[i] = 0; }   // the void round and under the islands; no seas
      }
    }
  }
}

namespace {
// BTA's Flat world: bedrock, stone, three of dirt and grass at band-local 64
class FlatGen : public DimGenerator {
public:
  void generate(Chunk& ch) override {
    std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
    const Registry& R = reg();
    const BlockId bed = R.blockId("bedrock"), stone = R.blockId("stone"), dirt = R.blockId("dirt"), grass = R.blockId("grass");
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      ch.set(x, 0, z, bed);
      for (int y = 1; y < 61; y++) ch.set(x, y, z, stone);
      for (int y = 61; y < 64; y++) ch.set(x, y, z, dirt);
      ch.set(x, 64, z, grass);
    }
  }
  void sample(int, int, ColumnSample& s) const override {
    s = ColumnSample(); s.height = 64; s.top = reg().blockId("grass"); s.under = reg().blockId("dirt");
  }
};
}

void registerFlatWorldGen() {
  WorldGen::registerType("flat", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    switch (d) {
      case DIM_OVERWORLD: return std::make_unique<FlatGen>();
      case DIM_NETHER: return std::make_unique<DepthsGen>(o);
      case DIM_AETHER: return std::make_unique<SkylandsGen>(o);
      case DIM_END: return std::make_unique<EndGen>(o);
      default: return std::make_unique<VoidGen>();
    }
  });
}

} // namespace dq
