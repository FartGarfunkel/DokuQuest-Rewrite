// gen/nether_bta.h — the Nether of terrain version 5, laid out like Better than Adventure 8.0's: a wide land you can
// walk across under a high roof (not a maze of caverns), made of five lands:
//   the Crag          the old Nether: netherrack hills and crags, rock spires and pillars, lavafalls from the roof,
//                     melting rock (magma) and bleached bones;
//   the Volcanic Islands  a lava sea with islands of basalt and blackstone, and volcanoes: cones with a crater of lava,
//                     breached on one side so it runs down the slope; brimstone (sulphur) on their flanks;
//   the Sulfur Pools  a flat yellow wasteland of sulphur crust set with pools of caustic acid and steam vents;
//   the Crystal Forest a maze of huge red crystal spires (some charged with latent energy: a redstone power source),
//                     crystals hanging from the roof too;
//   the Old World     a colossal desert of ancient ash dotted with dying embers (smouldering ash: watch your step),
//                     ash-choked pits down to the lava, petrified trunks and ruins of scorched brick.
// The lands are big cells (jittered, their borders warped) blended a few dozen blocks across.
#pragma once
#include <vector>
#include "common/noise.h"

namespace dq {

enum NetherBiome : u8 { NB_CRAG = 0, NB_VOLCANIC, NB_SULFUR, NB_CRYSTAL, NB_OLDWORLD, NB_COUNT };
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
  explicit NetherShape(i32 seed);
  static constexpr int LAVA = 50;    // the lava sea's surface (band y), as DepthsGen::LAVA_Y
  NetherCol column(int x, int z) const;
  void biomeMix(int x, int z, u8& a, u8& b, float& mix) const;
  int biomeAt(int x, int z) const { u8 a, b; float m; biomeMix(x, z, a, b, m); return a; }
  i32 seed() const { return seed_; }
private:
  i32 seed_;
  SimplexNoise nWarp, nA, nB, nC, nRoof, nSt, nPil;
  double floorOf(int b, int x, int z, NetherCol& c) const;
  double roofOf(int b, int x, int z) const;
  double sulfurBase(int x, int z) const;
};

// for the renderer (fog) and the spawner: the land at (x, z) in a world of this seed
void netherBiomeMix(i32 seed, int x, int z, u8& a, u8& b, float& mix);

// the Volcanic Islands' volcanoes within `range` blocks of (x, z): the crater's centre, its lava's band y, the cone's radius
struct NetherVolcano { int x = 0, z = 0, lavaY = 0; float radius = 0, crater = 0, height = 0; u32 hash = 0; };
void netherVolcanoes(i32 seed, int x, int z, int range, std::vector<NetherVolcano>& out);

} // namespace dq
