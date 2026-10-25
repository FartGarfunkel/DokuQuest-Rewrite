// gen/carvers.h — Minecraft Beta style carvers: worm caves with rooms and forks (MapGenCaves), ravines
// (MapGenRavine), and the Nether's wide caves (MapGenCavesHell). Each source chunk within 8 chunks of the
// chunk being generated seeds its own cave systems (Java Random, like Minecraft), so tunnels run seamlessly
// across chunk borders with no neighbour access. Thread-safe: all state is on the stack.
#pragma once
#include "gen/gendefs.h"

namespace dq {

// java.util.Random, bit for bit (Minecraft's carvers depend on its exact stream shape)
struct JRandom {
  u64 s = 0;
  explicit JRandom(i64 seed = 0) { setSeed(seed); }
  void setSeed(i64 seed) { s = ((u64)seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); }
  int next(int bits) { s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1); return (int)(i64)(s >> (48 - bits)); }
  int nextInt(int n) {
    if (n <= 0) return 0;
    if ((n & -n) == n) return (int)(((i64)n * (i64)next(31)) >> 31);
    int bits, val;
    do { bits = next(31); val = bits % n; } while ((i32)((u32)bits - (u32)val + (u32)(n - 1)) < 0);
    return val;
  }
  i64 nextLong() { i64 hi = (i64)next(32); i64 lo = (i64)next(32); return (i64)(((u64)hi << 32) + (u64)lo); }
  float nextFloat() { return next(24) / (float)(1 << 24); }
  double nextDouble() { return (double)(((i64)next(26) << 27) + next(27)) * (1.0 / (double)(1LL << 53)); }
  bool nextBool() { return next(1) != 0; }
};

struct CarveRules {
  std::vector<u8> carvable;     // per block id: may be hollowed out
  std::vector<u8> stopper;      // per block id: a segment touching one is skipped (water: no flooded caves)
  BlockId grass = 0, dirt = 0;  // a carved grass top re-grasses the dirt under it
  BlockId lava = 0;             // below lavaY carved blocks become this (0 = air)
  int lavaY = 10;
  int minY = 1, maxY = DIM_H - 2;
  i64 seed = 0;
  // Nether flavour (MapGenCavesHell): wider, more frequent tunnels
  bool hell = false;
  // columns (z*16+x) whose top is protected from this height up (tree trunks), DIM_H = none
  const i16* protect = nullptr;
};

// Minecraft's per-chunk population seed for a source chunk (salted per feature)
i64 chunkSeed(i64 world, int cx, int cz, i64 salt);
// WorldGenMinable: a vein of n blocks of `id` centred near (x, y, z) (world x/z, band y), written where it falls in
// chunk (cx, cz) over blocks with replace[id] set; the stream is consumed the same wherever it lands
void oreVein(GenBuf& b, int cx, int cz, JRandom& rnd, int x, int y, int z, int n, BlockId id, const u8* replace);

// worm caves + rooms into one band chunk (the GenBuf), from every source chunk within 8
void carveCaves(GenBuf& b, int cx, int cz, const CarveRules& r);
// ravines (1 in 50 source chunks), MapGenRavine
void carveRavines(GenBuf& b, int cx, int cz, const CarveRules& r);

} // namespace dq
