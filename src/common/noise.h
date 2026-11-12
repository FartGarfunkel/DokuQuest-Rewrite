// common/noise.h — exact ports of the original core/util.js randomness and noise, so world
// generation from a seed matches the HTML game's terrain shapes.
#pragma once
#include "common/common.h"

namespace dq {

// mulberry32 — same sequence as the JS version (returns [0,1)).
struct Mulberry32 {
  u32 a;
  explicit Mulberry32(u32 seed) : a(seed) {}
  double operator()() {
    a = a + 0x6D2B79F5u;
    u32 t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double)(t ^ (t >> 14)) / 4294967296.0;
  }
  int range(int lo, int hi) { return lo + (int)std::floor((*this)() * (hi - lo + 1)); } // randInt(rand, a, b)
};

// hash32(a,b,c,d) — per-position determinism (matches JS Math.imul semantics).
u32 hash32(i32 a, i32 b, i32 c, i32 d);
inline double hashFloat(i32 a, i32 b, i32 c, i32 d) { return hash32(a, b, c, d) / 4294967296.0; }
i32 seedFromString(const std::string& s);

class SimplexNoise {
public:
  explicit SimplexNoise(u32 seed);
  double noise2(double x, double y) const;
  double noise3(double x, double y, double z) const;
  double fbm2(double x, double y, int octaves, double lacunarity = 2, double gain = 0.5) const;
private:
  u8 perm[512];
  u8 permMod12[512];
};

} // namespace dq
