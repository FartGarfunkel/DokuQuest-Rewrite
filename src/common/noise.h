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
// posHashF(a,b,c,d) — the same use, well mixed (splitmix64 rounds): hashFloat is the original game's and gives stripes
// along a coordinate passed third (its multiplier is 2^31-1); terrain version 18 on uses this one where that showed
inline double posHashF(i32 a, i32 b, i32 c, i32 d) {
  auto mx = [](u64 z) { z += 0x9E3779B97F4A7C15ull; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); };
  const u64 h = mx(mx(mx(mx((u64)(u32)a) ^ (u32)b) ^ (u32)c) ^ (u32)d);
  return (double)(h >> 11) / 9007199254740992.0;
}
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
