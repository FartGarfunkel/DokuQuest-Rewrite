// gen/aether_noise.h — the density of The Aether's islands (data/aether/worldgen noise_settings "aether:skylands" with
// density_function "aether:base_3d_noise_aether"): Minecraft's old blended terrain noise (two 16-octave limit noises
// chosen between by an 8-octave selector, with the y "smear" that makes the terraces) at xz/y scale 0.25, factors
// 80 / 160, smear 8, shaped by two height gradients:
//     -0.15 + G1 * (G2 * (N + 0.07) - 0.1)     G1: 0 at y <= 8 -> 1 at y >= 40   G2: 1 at y <= 56 -> 0 at y >= 128
// solid where > 0, evaluated at cell corners (8 blocks across, 4 tall) and interpolated between them. Written for this
// engine from the numbers; the permutations come from the world seed, so the islands have the Aether's sizes and shapes
// (28-33 % of columns, tops mostly y 60-73, two stacked 6 % of the time) but are not the mod's own seed-for-seed.
#pragma once
#include "common/common.h"
#include <vector>

namespace dq {

class AetherNoise {
public:
  explicit AetherNoise(u64 seed);
  // the density before interpolation at a cell corner (x, z on the 8-block grid, y on the 4-block grid; Aether y 0..128)
  double corner(int x, int y, int z) const;
  // the 33 corner densities (y = 0, 4, ... 128) of the corner column at (x, z) (multiples of 8), cached per thread
  const double* cornerColumn(int x, int z) const;
  static constexpr int CY = 33;   // corners in y
private:
  struct Octave { u8 p[256]; double xo, yo, zo; double noise(double x, double y, double z, double yScale, double yMax) const; };
  std::vector<Octave> minL, maxL, mainN;
  double blended(int x, int y, int z) const;
  u64 seed_;
};

} // namespace dq
