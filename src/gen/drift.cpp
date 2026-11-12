// gen/drift.cpp — Better than Adventure 8.0.1's Drift (world type "drift", the side realm REALM_DRIFT): BTA's own
// DensityGeneratorDrift (three fractal Perlin noises — 16 octaves for the low and high limits, 8 for the blend — the
// old Sky Dimension's way: land hangs in the air in great drifting masses, faded out at the top and the bottom) on
// its TerrainGeneratorLerp (a 5 x 33 x 5 density grid per chunk, filled in 4 x 8 x 4), bit-exact through the Nether
// port's BTA noise (gen/bta/bta_noise.h). The terrain is in the Overworld band of its own World. On top: grass and
// three of dirt over stone (SurfaceGeneratorOverworld), then oak trees, long grass and flowers, coal and iron.
// The portal, the golden sky and its chickens: sim/drift.cpp, render/sky.cpp.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")   // (the density as BTA's doubles: no fused multiply-adds)
#endif
#include "gen/dimgens.h"
#include "gen/bta/bta_noise.h"
#include "core/registry.h"
#include "common/noise.h"
#include <array>

namespace dq {

namespace {
constexpr int YS = 33;   // densities up the 256-tall band (BTA: terrainHeight / 8 + 1)

class DriftGen : public DimGenerator {
public:
  explicit DriftGen(const WorldGenOptions& o)
    : seed_(o.seed), minN(bta::ImprovedPerlin::genOctaves((int64_t)o.seed, 16, 0)), maxN(bta::ImprovedPerlin::genOctaves((int64_t)o.seed, 16, 16)),
      mainN(bta::ImprovedPerlin::genOctaves((int64_t)o.seed, 8, 32)) {
    const Registry& R = reg();
    stone = R.blockId("stone"); dirt = R.blockId("dirt"); grass = R.blockId("grass"); log = R.blockId("log"); leaves = R.blockId("leaves");
    tall = R.blockId("tall_grass"); if (!tall) tall = R.blockId("grass_plant");
    flowers[0] = R.blockId("dandelion"); flowers[1] = R.blockId("poppy"); flowers[2] = R.blockId("cornflower");
    coal = R.blockId("coal_ore"); iron = R.blockId("iron_ore");
  }
  // DensityGeneratorDrift.generateDensityMap: index (dx * 5 + dz) * YS + dy
  void density(int cx, int cz, double* out) const {
    static thread_local std::vector<double> mainA, minA, maxA;
    mainA.resize(5 * YS * 5); minA.resize(5 * YS * 5); maxA.resize(5 * YS * 5);
    const double x = cx * 4.0, z = cz * 4.0;
    mainN.getRegion3(mainA.data(), x, 0.0, z, 5, YS, 5, 2.1387875000000003, 2.851716666666667, 2.1387875000000003);
    minN.getRegion3(minA.data(), x, 0.0, z, 5, YS, 5, 171.103, 342.206, 171.103);
    maxN.getRegion3(maxA.data(), x, 0.0, z, 5, YS, 5, 171.103, 342.206, 171.103);
    for (int i = 0; i < 5 * 5 * YS; i++) {
      const int dy = i % YS;
      const double mn = minA[i] / 512.0, mx = maxA[i] / 512.0, md = (mainA[i] / 10.0 + 1.0) / 2.0;
      double d = md < 0.0 ? mn : md > 1.0 ? mx : mn + (mx - mn) * md;
      d -= 8.0;
      if (dy > YS - 8) { const double k = (float)(dy - (YS - 8)) / (8.0f - 1.0f); d = d * (1.0 - k) + -30.0 * k; }
      if (dy < 2) { const double k = (float)(2 - dy) / (2.0f - 1.0f); d = d * (1.0 - k) + -30.0 * k; }
      out[i] = d;
    }
  }
  // TerrainGeneratorLerp: solid where the interpolated density is above 0
  void solidGrid(int cx, int cz, std::vector<u8>& solid) const {
    double D[5 * 5 * YS];
    density(cx, cz, D);
    solid.assign(16 * 256 * 16, 0);
    for (int bx = 0; bx < 4; bx++) for (int bz = 0; bz < 4; bz++) for (int by = 0; by < YS - 1; by++) {
      auto at = [&](int dx, int dz, int dy) { return D[((bx + dx) * 5 + (bz + dz)) * YS + by + dy]; };
      double a00 = at(0, 0, 0), a01 = at(0, 1, 0), a10 = at(1, 0, 0), a11 = at(1, 1, 0);
      const double s00 = (at(0, 0, 1) - a00) * 0.125, s01 = (at(0, 1, 1) - a01) * 0.125, s10 = (at(1, 0, 1) - a10) * 0.125, s11 = (at(1, 1, 1) - a11) * 0.125;
      for (int yi = 0; yi < 8; yi++) {
        double xz0 = a00, xz1 = a01;
        const double sx0 = (a10 - a00) * 0.25, sx1 = (a11 - a01) * 0.25;
        for (int xi = 0; xi < 4; xi++) {
          double v = xz0; const double sz = (xz1 - xz0) * 0.25;
          for (int zi = 0; zi < 4; zi++) {
            if (v > 0.0) solid[((by * 8 + yi) * 16 + (bz * 4 + zi)) * 16 + (bx * 4 + xi)] = 1;
            v += sz;
          }
          xz0 += sx0; xz1 += sx1;
        }
        a00 += s00; a01 += s01; a10 += s10; a11 += s11;
      }
    }
  }
  void generate(Chunk& ch) override {
    for (size_t i = 0; i + 1 < ch.climate.size(); i += 2) { ch.climate[i] = 204; ch.climate[i + 1] = 102; }   // plains' warmth and rain: green grass
    static thread_local std::vector<u8> S;
    solidGrid(ch.cx, ch.cz, S);
    auto sAt = [&](int x, int y, int z) { return y >= 0 && y < 256 && S[(y * 16 + z) * 16 + x]; };
    Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 0xD21F7));
    std::array<int, 256> top; top.fill(-1);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int depth = -1;
      for (int y = 255; y >= 0; y--) {
        if (!sAt(x, y, z)) { depth = -1; continue; }
        BlockId id = stone;
        if (depth == -1) { id = grass; if (top[z * 16 + x] < 0) top[z * 16 + x] = y; depth = 0; }
        else if (depth < 3) { id = dirt; depth++; }
        else if (rnd() < 0.006 && coal) id = coal;
        else if (rnd() < 0.0035 && iron) id = iron;
        ch.set(x, y, z, id);
      }
    }
    // a few oaks, long grass and flowers on the grass
    const int trees = (int)(rnd() * 3.2);
    for (int t = 0; t < trees; t++) {
      const int x = 2 + (int)(rnd() * 12), z = 2 + (int)(rnd() * 12), y = top[z * 16 + x];
      if (y < 4 || y > 240 || ch.get(x, y, z) != grass) continue;
      const int H = 4 + (int)(rnd() * 3);
      ch.set(x, y, z, dirt);
      for (int k = 1; k <= H; k++) ch.set(x, y + k, z, log);
      for (int dy = H - 2; dy <= H + 1; dy++) {
        const int r = dy >= H ? 1 : 2;
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
          if (std::abs(dx) == r && std::abs(dz) == r && (dy == H + 1 || rnd() < 0.5)) continue;
          if (ch.get(x + dx, y + dy, z + dz) == AIR) ch.set(x + dx, y + dy, z + dz, leaves);
        }
      }
    }
    for (int k = 0; k < 24; k++) {
      const int x = (int)(rnd() * 16), z = (int)(rnd() * 16), y = top[z * 16 + x];
      if (y < 0 || y > 250 || ch.get(x, y, z) != grass || ch.get(x, y + 1, z) != AIR) continue;
      const double r = rnd();
      const BlockId p = r < 0.8 ? tall : flowers[(int)(rnd() * 3) % 3];
      if (p) ch.set(x, y + 1, z, p);
    }
  }
  // far off: the top of the highest mass and where that mass ends below
  void sample(int wx, int wz, ColumnSample& s) const override {
    s = ColumnSample();
    const int cx = floorDiv(wx, 16), cz = floorDiv(wz, 16), x = wx - cx * 16, z = wz - cz * 16;
    static thread_local int lcx = INT32_MIN, lcz = INT32_MIN; static thread_local std::vector<u8> S;
    if (cx != lcx || cz != lcz) { solidGrid(cx, cz, S); lcx = cx; lcz = cz; }
    int y = 255;
    while (y >= 0 && !S[(y * 16 + z) * 16 + x]) y--;
    if (y < 0) { s.height = -1; return; }
    int b = y; while (b > 0 && S[((b - 1) * 16 + z) * 16 + x]) b--;
    s.height = (i16)y; s.top = grass; s.under = dirt; s.floatBottom = (i16)b;
  }
  int spawnHeight(int wx, int wz) const override { ColumnSample s; sample(wx, wz, s); return s.height < 0 ? -1 : s.height + 1; }

private:
  i32 seed_;
  bta::Fractal<bta::ImprovedPerlin> minN, maxN, mainN;
  BlockId stone = 0, dirt = 0, grass = 0, log = 0, leaves = 0, tall = 0, coal = 0, iron = 0;
  std::array<BlockId, 3> flowers{};
  static int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
};
}

void registerDriftWorldGen() {
  WorldGen::registerType("drift", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    if (d == DIM_OVERWORLD) return std::make_unique<DriftGen>(o);
    return std::make_unique<VoidGen>();
  });
}

} // namespace dq
