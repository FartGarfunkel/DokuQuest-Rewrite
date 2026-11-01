// gen/skyblock.cpp — the Skyblock world type (BTA 7.3's): the Overworld is empty sky, but for one small L of grass and
// dirt with an oak tree and a chest (lava, ice, a few seeds and a sapling) at the world's middle. The Depths, the
// Skylands and the End are as ever, so there is somewhere to go once you've built a way there. World type "skyblock".
#include "gen/dimgens.h"
#include "gen/overworld.h"
#include "core/registry.h"

namespace dq {

namespace {
constexpr int TOP = 64;   // band-local y of the grass
bool island(int x, int z) { return x >= 0 && z >= 0 && x <= 5 && z <= 5 && !(x >= 3 && z >= 3); }   // an L of six by six less a corner

class SkyblockGen : public DimGenerator {
public:
  void generate(Chunk& ch) override {
    std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
    if (ch.cx != 0 || ch.cz != 0) return;   // all of it lies in the chunk at the middle
    const Registry& R = reg();
    const BlockId grass = R.blockId("grass"), dirt = R.blockId("dirt"), log = R.blockId("log"), leaves = R.blockId("leaves"), chest = R.blockId("chest");
    auto put = [&](int x, int y, int z, BlockId id, u8 m = 0) { if (x >= 0 && x < 16 && z >= 0 && z < 16 && y >= 0 && y < DIM_H && id) { ch.set(x, y, z, id); ch.setMeta(x, y, z, m); } };
    for (int z = 0; z < 6; z++) for (int x = 0; x < 6; x++) if (island(x, z)) { put(x, TOP, z, grass); put(x, TOP - 1, z, dirt); put(x, TOP - 2, z, dirt); }
    // the tree, at the far end of the short arm
    const int tx = 1, tz = 4;
    for (int y = 1; y <= 5; y++) put(tx, TOP + y, tz, log);
    for (int y = 3; y <= 6; y++) {
      const int r = y <= 4 ? 2 : 1;
      for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        if ((std::abs(dx) == r && std::abs(dz) == r) && (y == 6 || ((dx * 7 + dz * 3 + y) & 1))) continue;   // ragged corners
        if (dx == 0 && dz == 0 && y <= 5) continue;
        if (ch.get(tx + dx, TOP + y, tz + dz) == AIR) put(tx + dx, TOP + y, tz + dz, leaves);
      }
    }
    // the chest at the end of the long arm: what a skyblock needs to begin
    put(5, TOP + 1, 1, chest, 3);
    json items = json::array(); for (int s = 0; s < 27; s++) items.push_back(nullptr);
    auto slot = [&](int s, const char* id, int n) { if (R.item(id)) items[s] = json{{"id", id}, {"n", n}}; };
    slot(0, "lava_bucket", 1); slot(1, "ice", 2); slot(2, "sapling", 1); slot(3, "sugar_cane", 1); slot(4, "pumpkin_seeds", 1);
    slot(5, "melon_seeds", 1); slot(6, "cactus", 1); slot(7, "bone_meal", 3); slot(8, "red_mushroom", 1); slot(9, "brown_mushroom", 1);
    ch.blockEntities[colIdx(5, TOP + 1, 1)] = json{{"type", "chest"}, {"items", items}};
  }
  void sample(int wx, int wz, ColumnSample& s) const override {
    s = ColumnSample();
    if (!island(wx, wz)) { s.height = -1; return; }
    s.height = TOP; s.top = reg().blockId("grass"); s.under = reg().blockId("dirt"); s.floatBottom = TOP - 3;
  }
};
} // namespace

void registerSkyblockWorldGen() {
  WorldGen::registerType("skyblock", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    switch (d) {
      case DIM_OVERWORLD: return std::make_unique<SkyblockGen>();
      case DIM_NETHER: return std::make_unique<DepthsGen>(o);
      case DIM_AETHER: return std::make_unique<SkylandsGen>(o);
      case DIM_END: return std::make_unique<EndGen>(o);
      default: return std::make_unique<VoidGen>();
    }
  });
}

} // namespace dq
