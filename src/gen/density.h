// gen/density.h — Minecraft 1.20's terrain as data: its density functions (worldgen/density_function, noise,
// noise_settings: tools/port/vanilla_worldgen.py -> data/vanilla_worldgen/) read and evaluated here. Terrain version 14
// (gen/overworld.cpp) shapes the Overworld with them: continents, erosion and ridges through the offset/factor/jaggedness
// splines, the 3D noise that gives cliffs and overhangs, and the caves. The noise is our own Perlin, seeded from the world
// seed by each noise's name: the same kind of land as Minecraft's, not the same land as a Minecraft seed.
// Coordinates are Minecraft's: block x/z, and y with its sea at 63 (ours: band y - 1).
#pragma once
#include "common/common.h"
#include "common/fs.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dq {

class DensityGraph {
public:
  // dir: data/vanilla_worldgen; prefix: "overworld" (or "overworld_amplified" / "overworld_large_biomes" for those worlds'
  // splines). False (and nothing loaded) when the data isn't there.
  bool load(const fs::path& dir, u64 seed, const std::string& prefix = "overworld");
  bool ok() const { return !nodes.empty(); }
  // a named function ("overworld/sloped_cheese", or a noise router entry: "final_density", "continents", "erosion",
  // "ridges", "depth", "temperature", "vegetation"); -1 if unknown
  int find(const std::string& name) const;
  double eval(int node, double x, double y, double z) const;

  struct Perlin;     // Minecraft's PerlinNoise (octaves of ImprovedNoise)
  struct Normal;     // its NormalNoise (two Perlins, the second offset)
  struct Blended;    // old_blended_noise (the 3D terrain noise)
  struct Spline;
  struct Node {
    u8 op = 0;
    double c = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;   // constants (scales, bounds, gradient ends...)
    int a = -1, b = -1, d = -1, e = -1;               // child nodes (and shift_x/y/z for shifted_noise)
    int noise = -1, spline = -1, blended = -1;
  };
  DensityGraph();
  ~DensityGraph();

private:
  int parse(const json& j);
  int parseSpline(const json& j);
  int noiseRef(const std::string& id);
  double evalSpline(int s, double x, double y, double z) const;

  fs::path dir_;
  std::string prefix_;
  u64 seed_ = 0;
  std::vector<Node> nodes;
  std::vector<std::unique_ptr<Normal>> noises;
  std::vector<std::unique_ptr<Blended>> blended;
  std::vector<std::unique_ptr<Spline>> splines;
  std::unordered_map<std::string, int> named, noiseIdx;
  std::unordered_map<std::string, json> router;
  u64 serial_ = 0;   // (per-thread 2D caches are keyed by it)
};

} // namespace dq
