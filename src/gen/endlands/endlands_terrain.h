// gen/endlands/endlands_terrain.h — the End's island terrain (the terrain generator, its island layers, layer options and
// generator options) and its End biome layout (the land decider + the hex biome source, see endlands_biomemap.h), standalone.
//
// Terrain: three IslandLayers (big / medium / small) each scatter one candidate island per grid cell, keep it where an
// OpenSimplex coverage noise is > -0.0001 (about half), and give every kept island the same SDF (capped cones: a root
// spike, an underside shoulder, a flat rim and a low dome, smooth-unioned; a radial noise on the top whose strength is
// the biome terrainHeight), scaled 0.5..1.5. Density = -min SDF, warped up to ±35 blocks, roughened by 3 octaves of 3D
// noise, faded to -1 over island y 100..127. Evaluated at the corners of 8 x 4 x 8 cells and trilinearly interpolated
// (y, then x, then z). density > 0 = solid.
//
// Y: the End noise column is island y 0..127; DokuQuest's End band puts it at ourY = beY + yOffset (56 by default: the
// centre island's top ~70 lands at 126). Everything outside island y 0..127 is void.
//
// Thread safety: every query is const and may run on any number of threads at once; the land and biome-map caches are
// internally locked. Construction is not cheap (a few permutation tables) — build one per world seed and share it.
#pragma once
#include "gen/endlands/endlands_biomemap.h"
#include "gen/endlands/endlands_noise.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace dq::endlands {

// customGenerator.layers.<name> in the End's generator.json (LayerOptions). The derived values follow LayerOptions exactly.
struct LayerOptions {
  float distance;         // grid cell (blocks) holding at most one island
  float scale;            // SDF unit in blocks (island radius = 0.5 * scale * s, s in 0.5..1.5)
  float averageHeight;    // island centre y as a fraction of the 128-tall column
  float heightVariation;  // ± fraction
  float coverage = 0.5f;  // 0..1 -> keep threshold 0.9999 - 2 * coverage
  bool hasCentralIsland = false;
};

struct EndTerrainOptions {
  LayerOptions big{300.0f, 200.0f, 0.546875f, 0.078125f, 0.5f, false};
  LayerOptions medium{150.0f, 100.0f, 0.546875f, 0.15625f, 0.5f, true};
  LayerOptions small{60.0f, 50.0f, 0.546875f, 0.234375f, 0.5f, false};
  int voidRing = 1000;           // empty ring round the centre island (voidRingSize, the map's inner void radius)
  bool centralIsland = true;     // generateCentralIsland: the medium-layer island at (0, 64, 0), scale 1.3
  int yOffset = 56;              // ourY = beY + yOffset
  int landBiomeSize = 256;       // hex map land biome size
  int voidBiomeSize = 256;       // hex map void biome size
  bool vanillaBiomes = true;     // keep end_highlands / end_midlands / small_end_islands in the biome pools
  float isLandHeight = 0.05f;    // radial-noise strength isLand sees (the reference reuses whatever the last fill set) [guess]
  HexMapOptions hex{};           // reconstructed hex-map knobs
  bool pruneIslands = true;      // skip islands that cannot change the min SDF (identical output; false = the plain loop)
};

enum VanillaKind : int { V_NONE = -1, V_HIGHLANDS = 0, V_MIDLANDS = 1, V_SMALL_ISLANDS = 2, V_BARRENS = 3, V_THE_END = 4 };

class EndTerrain {
public:
  static constexpr int kNoiseH = 128;                 // End noise column height
  static constexpr int kChunkVol = 16 * 256 * 16;     // chunkDensity() output size

  explicit EndTerrain(int64_t worldSeed, const EndTerrainOptions& opt = {});
  ~EndTerrain();
  EndTerrain(const EndTerrain&) = delete;
  EndTerrain& operator=(const EndTerrain&) = delete;

  // Density of every block of chunk (cx, cz): out[(y << 8) | (z << 4) | x], y = OUR band-local y 0..255.
  // > 0 = solid. -1 outside island y 0..127. The reference way: corner columns every 8 blocks, 33 corners every 4 y, trilinear.
  void chunkDensity(int cx, int cz, float* out) const;

  // the terrain density fill for the corner column at cell (cellX, cellZ) = block (cellX*8, cellZ*8):
  // out[i] = density at island y = 4*i, i = 0..32 (out[32] is always -1).
  void cornerColumn(int cellX, int cellZ, float out[33]) const;

  // isLand / the land decider: any island at this 8 x 8 column (block coords), cached.
  bool isLand(int x, int z) const;

  // End biome at block (x, z) as OUR land index 0..18 (B_* < B_LAND_COUNT); -1 for a base End biome
  // (*vanillaKind = V_HIGHLANDS / V_MIDLANDS / V_SMALL_ISLANDS / V_BARRENS), -2 for the centre (the_end, V_THE_END).
  // Biomes are per 4 x 4 quart (sampled at the quart's min corner, no fuzzy zoom).
  int land(int x, int z, int* vanillaKind = nullptr) const;
  int biome(int x, int z) const;             // the BiomeId (0..23) at block (x, z)
  // land() for the 16 x 16 columns of chunk (cx, cz) in one go (16 biome lookups): out[(z << 4) | x],
  // optional vanillaKind[(z << 4) | x]
  void chunkLand(int cx, int cz, int8_t out[256], int8_t* vanillaKind = nullptr) const;
  int biomeQuart(int qx, int qz) const;      // the hex biome source's biome at quart (qx, 0, qz)

  // terrainHeight of our land index (-1 / -2 -> the default 0.1); kBiomes[id].terrainHeight for any BiomeId.
  static double terrainHeight(int land);
  // getAverageDepth at quart (qx, qz): terrainHeight averaged over 29 quarts within 3, weights |o|/3 (0 if the centre
  // biome's is < 0.1). The fill uses half of it as the island-top noise strength.
  float avgTerrainHeight(int qx, int qz) const;

  // The domain warp: (cx, cz) in 8-block cell units (the fill passes integer cells, isLand cell + 0.5) -> block coords.
  void warp(double cx, double cz, double& px, double& pz) const;
  // Raw density at an already-warped point (block x/z, island y), h = island-top noise strength (avgTerrainHeight * 0.5),
  // before the 3D roughness and the top fade (= the max of the three layers' -SDF).
  float islandDensity(double px, double py, double pz, float h) const;
  // The full per-point fill: islandDensity + roughness + top fade.
  float density(double px, double py, double pz, float h) const;

  int ourY(int beY) const { return beY + opt_.yOffset; }
  int beY(int ourY) const { return ourY - opt_.yOffset; }
  const EndTerrainOptions& options() const { return opt_; }

  // ---- introspection for tools / tests
  struct Island { int x, y, z; float s; int layer; };
  // the islands each layer would test at warped point (px, pz) (the 3 x 3 grid cells round it)
  int islandsNear(double px, double pz, Island* out, int maxOut) const;
  // one island's own SDF (island density units: -SDF, > 0 inside) at block point p, without warp, roughness or fade
  float islandOwnDensity(const Island& is, double px, double py, double pz, float h) const;
  struct LayerStats { int candidates, kept; };
  LayerStats layerStats(int layer, int ix0, int iz0, int ix1, int iz1) const;  // over grid cells [ix0,ix1) x [iz0,iz1)
  float layerDistance(int layer) const;

  class Layer;  // internal (endlands_terrain.cpp)

private:
  struct Positions;
  void positions(double px, double pz, Positions& p) const;
  void fillCorner(int cellX, int cellZ, float h, float out[33]) const;
  float layersDensity(const Positions& p, double px, double py, double pz, float intensity, float radius) const;
  bool isLandCell(int cellX, int cellZ) const;

  EndTerrainOptions opt_;
  std::unique_ptr<Layer> large_, medium_, small_;
  OpenSimplexNoise noise1_, noise2_;
  BiomePicker landPicker_, voidPicker_;
  std::unique_ptr<HexBiomeMap> landMap_, voidMap_;
  std::array<float, 29> coef_{};
  std::array<std::array<int8_t, 2>, 29> offs_{};

  struct LandSection { std::atomic<uint8_t> v[128 * 128]; };
  mutable std::shared_mutex landMx_;
  mutable std::unordered_map<uint64_t, std::shared_ptr<LandSection>> land_;
};

}  // namespace dq::endlands
