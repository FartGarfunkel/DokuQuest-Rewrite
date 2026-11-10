// gen/nether_bta.h — the Nether, in two terrain versions.
// Version 5 is a walkable heightfield of five lands (cells about 230 blocks across). Saved worlds keep it.
//   the Crag, the Volcanic Islands, the Sulfur Pools, the Crystal Forest, the Old World.
// Version 6 follows Better than Adventure 8.0's layout, written for this engine (its own noise and blocks;
// the shape is not a copy of BTA's classes): beta-style 3D caves, eight climate lands, a sea at band y 96.
//   Above y 64 the climate picks the land. Below that the floor is the Old World desert.
//   Volcanic, sulfur and the crag hold lava; the crystal lands hold water under a crystal crust;
//   the shelf is solid rock up to the sea; the old world holds obsidian.
//   Indices 0..4 stay the version-5 lands. Shelf, crystal plains and the desert are appended.
#pragma once
#include <vector>
#include "common/noise.h"

namespace dq {

enum NetherBiome : u8 {
  NB_CRAG = 0, NB_VOLCANIC, NB_SULFUR, NB_CRYSTAL, NB_OLDWORLD,
  NB_SHELF, NB_CRYSTAL_PLAINS, NB_OLD_DESERT, NB_COUNT
};
const char* netherBiomeName(int b);

struct NetherCol {
  int floor = 64, roof = 190;        // band y: the ground's top block, the roof's lowest block
  u8 biome = NB_CRAG, other = NB_CRAG;
  float mix = 1;                     // how much this column is `biome` (0.5 at the border with `other` .. 1)
  bool pillar = false;               // rock from the ground to the roof
  bool cone = false;                 // on a volcano's slope
  int poolTop = -1; u8 pool = 0;     // a crater's lava (1) or a pool's acid (2), filled up to poolTop
};

class NetherShape {
public:
  explicit NetherShape(i32 seed, bool open8 = false);   // open8: terrain 8's lands (walkable, all seven of BTA's)
  static constexpr int LAVA = 50;    // the lava sea's surface (band y), as DepthsGen::LAVA_Y
  NetherCol column(int x, int z) const;
  // the column for lands given as weights (w[NB_CRAG .. NB_OLDWORLD], summing to anything > 0): the open shape under
  // another biome map (terrain 8: BTA's lands, gen/nether_exact.cpp)
  NetherCol columnWeighted(int x, int z, const float w[NB_COUNT]) const;
  void biomeMix(int x, int z, u8& a, u8& b, float& mix) const;
  int biomeAt(int x, int z) const { u8 a, b; float m; biomeMix(x, z, a, b, m); return a; }
  i32 seed() const { return seed_; }
private:
  i32 seed_; bool open8_ = false;
  SimplexNoise nWarp, nA, nB, nC, nRoof, nSt, nPil;
  double floorOf(int b, int x, int z, NetherCol& c) const;
  double floorOfV5(int b, int x, int z, NetherCol& c) const;   // version 5's five shapes
  double roofOf(int b, int x, int z) const;
  double sulfurBase(int x, int z) const;
};

// for the renderer (fog) and the spawner: the land at (x, z) in a world of this seed (version 5's heightfield)
void netherBiomeMix(i32 seed, int x, int z, u8& a, u8& b, float& mix);

// Version 6's climate. Same cave noise in every land; the land only paints the rock and chooses the sea.
// Seeds will not match a BTA world block for block. The scales, the sea line and the climate ranges do.
class NetherClimate {
public:
  static constexpr int OCEAN_Y = 96;
  static constexpr int SAMPLES = 33;     // band height / 8 + 1
  explicit NetherClimate(i32 seed, int genVersion = 6);   // 7: BTA's own climate octaves and main noise (see nether_bta.cpp)
  u8 biomeAt(int x, int y, int z) const;
  int oceanKind(int x, int z) const;     // 1 lava, 2 water, 3 obsidian, 4 solid shelf
  bool solidAt(int x, int y, int z) const;
  // one coarse probe for distant terrain (not the full trilinear field)
  double probeAt(int ix, int dy, int iz) const;
  i32 seed() const { return seed_; }
  int version() const { return ver_; }
private:
  i32 seed_;
  int ver_ = 6;
  SimplexNoise nMin, nMax, nMain, nTemp, nHum, nFuzz, nBeach, nSoil;
  double f3(const SimplexNoise& n, double x, double y, double z, int oct, double freq, double amp) const;
  double f2(const SimplexNoise& n, double x, double z, double scale, double lac) const;
  void climateAt(int x, int z, double& temp, double& humid) const;
  u8 biomeFrom(double temp, double humid, int y) const;
  double fieldDensity(int ix, int dy, int iz, int octaves) const;
  void columnDensity(int x, int z, float dens[SAMPLES], int octaves) const;
  static float densityAtY(const float dens[SAMPLES], int y);
  friend class DepthsGen;
};

// Fog, particles, ambience and spawns. genVersion >= 6 uses the climate at this world position
// (y is a world coordinate, not band-local). Older versions use the heightfield mix.
void netherLandAt(i32 seed, int genVersion, int worldX, int worldY, int worldZ, u8& a, u8& b, float& mix);
bool netherMobsSpawnOn(u16 id);   // BTA's NETHER_MOBS_SPAWN tag on our blocks (gen/nether_exact.cpp)

// the Volcanic Islands' volcanoes within `range` blocks of (x, z): the crater's centre, its lava's band y, the cone's radius
struct NetherVolcano { int x = 0, z = 0, lavaY = 0; float radius = 0, crater = 0, height = 0; u32 hash = 0; };
void netherVolcanoes(i32 seed, int x, int z, int range, std::vector<NetherVolcano>& out);

} // namespace dq
