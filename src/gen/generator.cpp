#include "gen/generator.h"
#include <map>
#include <mutex>

namespace dq {

static std::map<std::string, WorldGen::Factory>& factories() { static std::map<std::string, WorldGen::Factory> f; return f; }
static std::mutex g_factMx;

void WorldGen::registerType(const std::string& t, Factory f) { std::lock_guard<std::mutex> lk(g_factMx); factories()[t] = std::move(f); }
std::vector<std::string> WorldGen::types() {
  std::lock_guard<std::mutex> lk(g_factMx);
  std::vector<std::string> v; for (auto& [k, f] : factories()) v.push_back(k); return v;
}

// ---- "debug" world type: simple layered noise per band, used until/unless a real generator is registered ----
namespace {
class DebugGen : public DimGenerator {
public:
  DebugGen(int dim, const WorldGenOptions& o) : dim(dim), noise((u32)o.seed + dim * 7919u) {
    auto& r = reg();
    stone = r.blockId("stone"); dirt = r.blockId("dirt"); grass = r.blockId("grass"); water = r.blockId("water");
    sand = r.blockId("sand"); bedrock = r.blockId("bedrock"); netherrack = r.blockId("netherrack");
    endstone = r.blockId("end_stone"); skygrass = r.blockId("sky_grass");
    if (!skygrass) skygrass = grass;
    if (!netherrack) netherrack = stone;
    if (!endstone) endstone = sand;
    lava = r.blockId("lava");
  }
  int height(int x, int z) const {
    double n = noise.fbm2(x / 160.0, z / 160.0, 5);
    switch (dim) {
      case DIM_OVERWORLD: return (int)(SEA_LEVEL + 4 + n * 36);
      case DIM_NETHER: return (int)(40 + n * 18);
      case DIM_AETHER: return n > 0.15 ? (int)(120 + n * 40) : -1;
      default: return n > 0.2 ? (int)(64 + n * 24) : -1;
    }
  }
  void generate(Chunk& c) override {
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int wx = c.worldX() + x, wz = c.worldZ() + z, h = height(wx, wz);
      if (dim == DIM_OVERWORLD || dim == DIM_NETHER) c.set(x, 0, z, bedrock);
      if (dim == DIM_NETHER) for (int y = 250; y < 256; y++) c.set(x, y, z, bedrock);
      if (dim == DIM_OVERWORLD) {
        for (int y = 1; y <= h; y++) c.set(x, y, z, y == h ? (h < SEA_LEVEL + 1 ? sand : grass) : (y > h - 4 ? dirt : stone));
        for (int y = h + 1; y <= SEA_LEVEL; y++) c.set(x, y, z, water);
      } else if (dim == DIM_NETHER) {
        for (int y = 1; y <= h; y++) c.set(x, y, z, netherrack);
        for (int y = h + 1; y <= 32; y++) c.set(x, y, z, lava);
        int ceil = 200 - (int)(noise.noise2(wx / 40.0, wz / 40.0) * 20);
        for (int y = ceil; y < 250; y++) c.set(x, y, z, netherrack);
      } else if (h > 0) {
        int thick = 6 + (h % 7);
        for (int y = h - thick; y <= h; y++) c.set(x, y, z, dim == DIM_AETHER ? (y == h ? skygrass : dirt) : endstone);
      }
    }
  }
  void sample(int wx, int wz, ColumnSample& s) const override {
    int h = height(wx, wz);
    s.height = (i16)h;
    if (dim == DIM_OVERWORLD) { s.top = h < SEA_LEVEL + 1 ? sand : grass; s.under = dirt; if (h < SEA_LEVEL) { s.water = SEA_LEVEL; s.fluid = water; } }
    else if (dim == DIM_NETHER) { s.top = netherrack; s.under = netherrack; if (h < 32) { s.water = 32; s.fluid = lava; } }
    else if (h > 0) { s.top = dim == DIM_AETHER ? skygrass : endstone; s.under = dim == DIM_AETHER ? dirt : endstone; s.floatBottom = (i16)(h - 6 - (h % 7)); }
  }
  int dim; SimplexNoise noise;
  BlockId stone, dirt, grass, water, sand, bedrock, netherrack, endstone, skygrass, lava;
};
struct RegisterDebug { RegisterDebug() { WorldGen::registerType("debug", [](int d, const WorldGenOptions& o) { return std::make_unique<DebugGen>(d, o); }); } } g_regDebug;
}

WorldGen::WorldGen(const WorldGenOptions& o) : opts(o) {
  Factory f;
  {
    std::lock_guard<std::mutex> lk(g_factMx);
    auto it = factories().find(o.worldType);
    if (it == factories().end()) it = factories().find("default");
    if (it == factories().end()) it = factories().find("debug");
    f = it->second;
  }
  for (int d = 0; d < DIM_COUNT; d++) dims[d] = f(d, o);
}

} // namespace dq
