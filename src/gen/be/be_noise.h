// gen/be/be_noise.h — the random and noise sources of Better End 3.2.5 (github.com/quiqueck/BetterEnd, MIT), bit-exact:
//   JavaRandom        java.util.Random == Minecraft's LegacyRandomSource (class_5820): the 48-bit LCG, same draws.
//   OpenSimplexNoise  org.betterx.betterend.noise.OpenSimplexNoise (Kurt Spencer's 2014 OpenSimplex, long seed),
//                     2D and 3D eval ported line for line from the jar (checked against the class: tools in README).
//   beHash            the int hash BE (IslandLayer.getSeed) and BCLib (MHelper.getSeed) seed per-cell randoms with.
// No engine dependencies; safe to use from any thread (const evals, no shared state).
#pragma once
#include <cstdint>

namespace dq::be {

class JavaRandom {
public:
  explicit JavaRandom(int64_t seed = 0) { setSeed(seed); }
  void setSeed(int64_t seed) { s_ = (uint64_t(seed) ^ 0x5DEECE66DULL) & kMask; }
  int32_t next(int bits) {
    s_ = (s_ * 0x5DEECE66DULL + 0xBULL) & kMask;
    return int32_t(uint32_t(s_ >> (48 - bits)));
  }
  int32_t nextInt() { return next(32); }
  int32_t nextInt(int32_t bound);  // bound > 0, Java's rejection loop
  int64_t nextLong() { int64_t hi = next(32); return int64_t(uint64_t(hi) << 32) + next(32); }
  float nextFloat() { return float(next(24)) * 5.9604645e-8f; }  // next(24) / 2^24
  double nextDouble() { return double((int64_t(next(26)) << 27) + next(27)) * 0x1.0p-53; }
  bool nextBoolean() { return next(1) != 0; }

private:
  static constexpr uint64_t kMask = (1ULL << 48) - 1;
  uint64_t s_ = 0;
};

// h = seed + x*374761393 + z*668265263; h = (h ^ h>>13) * 1274126177; return h ^ h>>16   (Java int arithmetic)
inline int32_t beHash(int32_t seed, int32_t x, int32_t z) {
  int32_t h = int32_t(uint32_t(seed) + uint32_t(x) * 374761393u + uint32_t(z) * 668265263u);
  h = int32_t(uint32_t(h ^ (h >> 13)) * 1274126177u);
  return h ^ (h >> 16);
}

class OpenSimplexNoise {
public:
  explicit OpenSimplexNoise(int64_t seed = 0);
  double eval(double x, double y) const;            // about -1..1
  double eval(double x, double y, double z) const;  // about -1..1

private:
  double extrapolate(int xsb, int ysb, double dx, double dy) const;
  double extrapolate(int xsb, int ysb, int zsb, double dx, double dy, double dz) const;
  static int fastFloor(double x) { int xi = int(x); return x < double(xi) ? xi - 1 : xi; }
  int16_t perm[256];
  int16_t permGradIndex3D[256];
};

}  // namespace dq::be
