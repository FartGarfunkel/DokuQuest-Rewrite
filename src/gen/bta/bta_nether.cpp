// BTA 8.0.1 Nether: biome provider, terrain (DensityGeneratorNether + TerrainGeneratorNether), surface
// (SurfaceGeneratorNether) and large features (Caves, LavaFloe, RubyglassFloe, Shelf). Exact port.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include <algorithm>
#include <cmath>
#include <cstring>

#include "bta_nether_internal.h"

namespace bta {

// ---- block table -------------------------------------------------------------------------------
namespace {
struct NBInfo { NB nb; uint16_t id; const char* name; };
const NBInfo kNB[] = {
    {NB_AIR, bid::AIR, "minecraft:block/air"},
    {NB_BEDROCK, bid::BEDROCK, "minecraft:block/bedrock"},
    {NB_NETHERRACK, bid::NETHERRACK, "minecraft:block/netherrack"},
    {NB_COBBLE_NETHERRACK, bid::COBBLE_NETHERRACK, "minecraft:block/cobble_netherrack"},
    {NB_COBBLE_NETHERRACK_CRYSTALLINE, bid::COBBLE_NETHERRACK_CRYSTALLINE, "minecraft:block/cobble_netherrack_mossy"},
    {NB_LAVA_STILL, bid::LAVA_STILL, "minecraft:block/fluid_lava_still"},
    {NB_LAVA_FLOWING, bid::LAVA_FLOWING, "minecraft:block/fluid_lava_flowing"},
    {NB_WATER_STILL, bid::WATER_STILL, "minecraft:block/fluid_water_still"},
    {NB_WATER_FLOWING, bid::WATER_FLOWING, "minecraft:block/fluid_water_flowing"},
    {NB_ACID_STILL, bid::ACID_STILL, "minecraft:block/fluid_acid_still"},
    {NB_ACID_FLOWING, bid::ACID_FLOWING, "minecraft:block/fluid_acid_flowing"},
    {NB_OBSIDIAN, bid::OBSIDIAN, "minecraft:block/obsidian"},
    {NB_SOULSAND, bid::SOULSAND, "minecraft:block/soulsand"},
    {NB_SULFUR, bid::SULFUR, "minecraft:block/sulfur"},
    {NB_BRIMSAND, bid::BRIMSAND, "minecraft:block/brimsand"},
    {NB_BRIMSTONE, bid::BRIMSTONE, "minecraft:block/brimstone"},
    {NB_BRIMTHAW, bid::BRIMTHAW, "minecraft:block/brimthaw"},
    {NB_GLOOMSTONE, bid::GLOOMSTONE, "minecraft:block/gloomstone"},
    {NB_COBBLE_GLOOMSTONE, bid::COBBLE_GLOOMSTONE, "minecraft:block/cobble_gloomstone"},
    {NB_GLOWSTONE, bid::GLOWSTONE, "minecraft:block/glowstone"},
    {NB_BLOCK_ASH, bid::BLOCK_ASH, "minecraft:block/block_ash"},
    {NB_LAYER_ASH, bid::LAYER_ASH, "minecraft:block/layer_ash"},
    {NB_SLATE, bid::SLATE, "minecraft:block/slate"},
    {NB_BASALT, bid::BASALT, "minecraft:block/basalt"},
    {NB_COBBLE_BASALT, bid::COBBLE_BASALT, "minecraft:block/cobble_basalt"},
    {NB_PUMICE_WET, bid::PUMICE_WET, "minecraft:block/pumice_wet"},
    {NB_PUMICE_DRY, bid::PUMICE_DRY, "minecraft:block/pumice_dry"},
    {NB_MAGMA, bid::MAGMA, "minecraft:block/magma"},
    {NB_RUBYGLASS_CRYSTAL, bid::RUBYGLASS_CRYSTAL, "minecraft:block/rubyglass_crystal"},
    {NB_RUBYGLASS_COLUMN, bid::RUBYGLASS_COLUMN, "minecraft:block/rubyglass_column"},
    {NB_RUBYGLASS_NODE, bid::RUBYGLASS_NODE, "minecraft:block/rubyglass_node"},
    {NB_RUBYGLASS_GROWTH, bid::RUBYGLASS_GROWTH, "minecraft:block/rubyglass_growth"},
    {NB_ORE_NETHERCOAL_NETHERRACK, bid::ORE_NETHERCOAL_NETHERRACK, "minecraft:block/ore_nethercoal_netherrack"},
    {NB_ORE_NETHERCOAL_BASALT, bid::ORE_NETHERCOAL_BASALT, "minecraft:block/ore_nethercoal_basalt"},
    {NB_ORE_NETHERCOAL_GLOOMSTONE, bid::ORE_NETHERCOAL_GLOOMSTONE, "minecraft:block/ore_nethercoal_gloomstone"},
    {NB_BLOCK_NETHER_COAL, bid::BLOCK_NETHER_COAL, "minecraft:block/block_nethercoal"},
    {NB_THERMAL_VENT, bid::THERMAL_VENT, "minecraft:block/thermal_vent"},
    {NB_FIRE, bid::FIRE, "minecraft:block/fire"},
    {NB_FIRE_SULFURIC, bid::FIRE_SULFURIC, "minecraft:block/fire_sulfuric"},
    {NB_EMBER, bid::EMBER, "minecraft:block/ember"},
    {NB_BONE_PILE, bid::BONE_PILE, "minecraft:block/bone_pile"},
    {NB_BOULDER_MAGMATIC, bid::BOULDER_MAGMATIC, "minecraft:block/boulder_magmatic"},
    {NB_BOULDER_SULFURIC, bid::BOULDER_SULFURIC, "minecraft:block/boulder_sulfuric"},
    {NB_SOUL_CATCHER, bid::SOUL_CATCHER, "minecraft:block/soul_catcher"},
    {NB_OVERLAY_PEBBLES, bid::OVERLAY_PEBBLES, "minecraft:block/overlay_pebbles"},
    {NB_OTHER, 0, "minecraft:block/unknown"},
};
static_assert(sizeof(kNB) / sizeof(kNB[0]) == NB_COUNT, "NB table");

struct PropTable {
  BlockProps p[bid::MAX_ID];
  PropTable() {
    // Values dumped from BTA 8.0.1 (Blocks.init(), Block.getMaterial(), hasTag, Block.solid(), ...)
    auto set = [&](uint16_t id, Mat m, uint32_t f) { p[id].mat = m; p[id].f = f | P_KNOWN; };
    const uint32_t SOLIDCUBE = P_MAT_SOLID | P_BLOCKS_MOTION | P_SOLID | P_CUBE | P_SOLID_RENDER;
    set(bid::AIR, Mat::AIR, T_PLACE_OVERWRITES);
    set(bid::BASALT, Mat::BASALT, SOLIDCUBE | T_CAVES_CUT);
    set(bid::SLATE, Mat::SLATE, SOLIDCUBE);
    set(bid::COBBLE_BASALT, Mat::BASALT, SOLIDCUBE | T_NETHER_SURFACE);
    set(bid::OBSIDIAN, Mat::STONE, SOLIDCUBE);
    set(bid::PUMICE_DRY, Mat::STONE, SOLIDCUBE);
    set(bid::PUMICE_WET, Mat::STONE, SOLIDCUBE);
    set(bid::BEDROCK, Mat::STONE, SOLIDCUBE);
    set(bid::WATER_FLOWING, Mat::WATER, P_LIQUID | P_FLUID_LOGIC | T_IS_WATER | T_PLACE_OVERWRITES);
    set(bid::WATER_STILL, Mat::WATER, P_LIQUID | P_FLUID_LOGIC | T_IS_WATER | T_PLACE_OVERWRITES);
    set(bid::LAVA_FLOWING, Mat::LAVA, P_LIQUID | P_FLUID_LOGIC | T_IS_LAVA | T_PLACE_OVERWRITES);
    set(bid::LAVA_STILL, Mat::LAVA, P_LIQUID | P_FLUID_LOGIC | T_IS_LAVA | T_PLACE_OVERWRITES);
    set(bid::ORE_NETHERCOAL_NETHERRACK, Mat::NETHERRACK, SOLIDCUBE);
    set(bid::ORE_NETHERCOAL_BASALT, Mat::BASALT, SOLIDCUBE);
    set(bid::ORE_NETHERCOAL_GLOOMSTONE, Mat::GLOOMSTONE, SOLIDCUBE);
    set(bid::BLOCK_NETHER_COAL, Mat::NETHERRACK, SOLIDCUBE);
    set(bid::FIRE, Mat::FIRE, T_BROKEN_BY_FLUIDS | T_PLACE_OVERWRITES);
    set(bid::FIRE_SULFURIC, Mat::FIRE, T_BROKEN_BY_FLUIDS | T_PLACE_OVERWRITES);
    set(bid::LAYER_ASH, Mat::TOP_ASH, T_BROKEN_BY_FLUIDS | T_PLACE_OVERWRITES);
    set(bid::ICE, Mat::ICE, P_MAT_SOLID | P_BLOCKS_MOTION | P_CUBE);
    set(bid::BLOCK_ASH, Mat::ASH, SOLIDCUBE | T_CAVES_CUT);
    set(bid::COBBLE_NETHERRACK, Mat::NETHERRACK, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE | T_INF_BURN);
    set(bid::MAGMA, Mat::NETHERRACK, SOLIDCUBE);
    set(bid::COBBLE_NETHERRACK_CRYSTALLINE, Mat::NETHERRACK, SOLIDCUBE | T_INF_BURN);
    set(bid::NETHERRACK, Mat::NETHERRACK, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE | T_INF_BURN);
    set(bid::SOULSAND, Mat::SOUL_SAND, SOLIDCUBE | T_CAVES_CUT);
    set(bid::GLOWSTONE, Mat::STONE, SOLIDCUBE | T_INF_BURN_SULF);
    set(bid::RUBYGLASS_COLUMN, Mat::STONE, SOLIDCUBE);
    set(bid::RUBYGLASS_CRYSTAL, Mat::ICE, P_MAT_SOLID | P_BLOCKS_MOTION | P_CUBE | T_CAVES_CUT | T_NETHER_SURFACE);
    set(bid::RUBYGLASS_GROWTH, Mat::PLANT, 0);
    set(bid::RUBYGLASS_NODE, Mat::STONE, SOLIDCUBE);
    set(bid::OVERLAY_PEBBLES, Mat::DECORATION, T_BROKEN_BY_FLUIDS);
    set(bid::THERMAL_VENT, Mat::BASALT, SOLIDCUBE);
    set(bid::BONE_PILE, Mat::PLANT, T_BROKEN_BY_FLUIDS | T_PLACE_OVERWRITES);
    set(bid::SOUL_CATCHER, Mat::PLANT, T_BROKEN_BY_FLUIDS | T_PLACE_OVERWRITES);
    set(bid::GLOOMSTONE, Mat::GLOOMSTONE, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE);
    set(bid::COBBLE_GLOOMSTONE, Mat::GLOOMSTONE, SOLIDCUBE | T_CAVES_CUT);
    set(bid::BOULDER_MAGMATIC, Mat::PLANT, T_BROKEN_BY_FLUIDS);
    set(bid::BOULDER_SULFURIC, Mat::PLANT, T_BROKEN_BY_FLUIDS);
    set(bid::ACID_FLOWING, Mat::ACID, P_LIQUID | P_FLUID_LOGIC | T_PLACE_OVERWRITES);
    set(bid::ACID_STILL, Mat::ACID, P_LIQUID | P_FLUID_LOGIC | T_PLACE_OVERWRITES);
    set(bid::EMBER, Mat::STONE, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE);
    set(bid::BRIMSAND, Mat::SAND, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE | T_INF_BURN_SULF);
    set(bid::BRIMSTONE, Mat::STONE, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE | T_INF_BURN_SULF);
    set(bid::BRIMTHAW, Mat::NETHERRACK, SOLIDCUBE | T_INF_BURN_SULF);
    set(bid::SULFUR, Mat::SOUL_SAND, SOLIDCUBE | T_CAVES_CUT | T_NETHER_SURFACE | T_INF_BURN_SULF);
    set(bid::DIRT, Mat::OTHER, SOLIDCUBE | T_CAVES_CUT);
    set(bid::GRASS, Mat::OTHER, SOLIDCUBE | T_CAVES_CUT | T_CAVE_REPLACES_SURFACE);
    for (int i = 0; i < bid::MAX_ID; ++i) p[i].nb = NB_OTHER;
    for (const NBInfo& n : kNB)
      if (n.nb != NB_OTHER) p[n.id].nb = n.nb;
  }
};
const PropTable& ptable() { static const PropTable t; return t; }
}  // namespace

const BlockProps& props(uint16_t id) {
  static const BlockProps none{};
  return id < bid::MAX_ID ? ptable().p[id] : none;
}
const char* nbName(NB k) { return k < NB_COUNT ? kNB[k].name : kNB[NB_OTHER].name; }
uint16_t nbToBtaId(NB k) { return k < NB_COUNT ? kNB[k].id : 0; }
NB nbFromBtaId(uint16_t id) { return id < bid::MAX_ID ? (NB)ptable().p[id].nb : NB_OTHER; }

const char* netherBiomeName(int b) {
  static const char* n[] = {"minecraft:nether.old_world", "minecraft:nether.crystal_plains",
                            "minecraft:nether.crystal_forest", "minecraft:nether.shelf", "minecraft:nether.crag",
                            "minecraft:nether.sulfur_pools", "minecraft:nether.volcanic_islands",
                            "minecraft:nether.old_world.desert"};
  return b >= 0 && b < BI_COUNT ? n[b] : "?";
}

int32_t javaStringHash(const char* s) {
  uint32_t h = 0;
  for (; *s; ++s) h = 31u * h + (uint32_t)(unsigned char)*s;
  return (int32_t)h;
}

// ---- construction ------------------------------------------------------------------------------
namespace {
struct Range { double t0, t1, h0, h1, a0, a1; uint8_t b; };
// BiomeProviderNether.init() insertion order (LinkedHashMap), last match wins in findBiome
const Range kRanges[] = {
    {0.95, 1.0, 0.0, 1.0, 0.5, 1.0, BI_VOLCANIC_ISLANDS}, {0.75, 0.95, 0.0, 0.85, 0.5, 1.0, BI_CRAG},
    {0.75, 0.95, 0.85, 1.0, 0.5, 1.0, BI_SULFUR_POOLS},    {0.7, 0.75, 0.0, 1.0, 0.5, 1.0, BI_SHELF},
    {0.45, 0.7, 0.1, 1.0, 0.5, 1.0, BI_CRYSTAL_FOREST},    {0.45, 0.7, 0.0, 0.1, 0.5, 1.0, BI_CRYSTAL_PLAINS},
    {0.0, 0.45, 0.0, 1.0, 0.5, 1.0, BI_OLD_WORLD},         {0.0, 1.0, 0.0, 1.0, 0.0, 0.5, BI_OLD_WORLD_DESERT},
};
const char* const kDecoNames[] = {
    "rubyglass_crystal_ceiling", "rubyglass_crystal_floor", "rubyglass_crystal_floor_2", "rubyglass_node",
    "rubyglass_bud_patch", "rubyglass_growth_patch", "scatter_crystalline", "clump_crystalline", "clump_brimthaw",
    "sulfur_pocket", "soulsand_pocket", "water_spring", "lava_spring", "lava_spring_2", "lava_lake",
    "lava_pool_terrace", "boulder_magma", "acid_spring", "pillar_netherrack", "pillar_netherrack_2",
    "pillar_cobble_basalt", "pillar_slate", "boulder_magma", "sulfur_pool_terrace", "floor_vent",
    "volcanic_islands_spire", "sulfur_pool_boulder", "nether_roof_spire", "volcano_roof_spire",
    "sulfur_roof_spire", "oldworld_roof_spire", "nether_coal_ore", "nether_coal_ore_2", "nether_coal_ore_3",
    "glowstone_1", "glowstone_2", "glowstone_sulfur", "patch_fire", "patch_fire_volcanic", "patch_fire_sulfuric",
    "scatter_ember_ash", "scatter_bone_pile", "scatter_boulder_magmatic", "scatter_boulder_magmatic_sparce",
    "scatter_boulder_sulfuric", "patch_soul_catcher", "pebbles"};
}  // namespace

NetherGen::Impl::Impl(int64_t s, const NetherGenOptions& o)
    : opt(o),
      seed(s),
      tempNoise(Simplex::genOctaves(lmul(s, 9871), 4)),
      downNoise(Simplex::genOctaves(lmul(s, 39811), 4)),
      fuzzNoise(Simplex::genOctaves(lmul(s, 543321), 4)),
      minLimit(ImprovedPerlin::genOctaves(s, 16, 0)),
      maxLimit(ImprovedPerlin::genOctaves(s, 16, 16)),
      mainNoise(ImprovedPerlin::genOctaves(s, 8, 32)),
      beachNoise(ImprovedPerlin::genOctaves(s, 4, 40)),
      soilNoise(ImprovedPerlin::genOctaves(s, 4, 44)),
      floeWorley([&] { JavaRandom r(s); return Worley(r); }()),
      floeGap(ImprovedPerlin::genOctaves(s, 4)),
      floeScale(Fractal<ImprovedPerlin>(ImprovedPerlin::genOctaves(s, 2))),
      pillarWorley([&] { JavaRandom r(s); return Worley(r); }()),
      crystalDensity(ImprovedPerlin::genOctaves(s, 8, 74)),
      veinNoise([&] { JavaRandom r(ladd(s, (int64_t)bid::COBBLE_NETHERRACK_CRYSTALLINE)); return ImprovedPerlin(r); }()) {
  if (opt.regionSize < 1) opt.regionSize = 1;
  if (opt.margin < 0) opt.margin = 0;
  if (opt.terrainCache < 64) opt.terrainCache = 64;
  if (opt.regionCache < 2) opt.regionCache = 2;
  tempNoise.setLacunarity(0.25);
  downNoise.setLacunarity(0.3333333333333333);
  fuzzNoise.setLacunarity(0.5882352941176471);
  for (int alt = 0; alt < 2; ++alt)
    for (int iH = 0; iH < 50; ++iH)
      for (int iT = 0; iT < 50; ++iT) {
        double t = (double)iT / 50.0, h = (double)iH / 50.0, a = alt ? 25.0 / 50.0 : 0.0;
        uint8_t last = BI_OLD_WORLD_DESERT;
        for (const Range& r : kRanges)
          if (t >= r.t0 && t < r.t1 && h >= r.h0 && h < r.h1 && a >= r.a0 && a < r.a1) last = r.b;
        biomeLut[(alt * 50 + iH) * 50 + iT] = last;
      }
  const double PI = 3.141592653589793;
  for (int dy = 0; dy < 25; ++dy) {
    densityOffset[dy] = std::cos((double)dy * PI * 6.0 / 25.0) * 2.0;
    double o = dy;
    if (dy > 12) o = 25 - 1 - dy;
    if (o < 4.0) { o = 4.0 - o; densityOffset[dy] -= o * o * o * 10.0; }
  }
  {
    JavaRandom r(s);
    lfRandX = r.nextLong() / 2 * 2 + 1;
    lfRandZ = r.nextLong() / 2 * 2 + 1;
  }
  {
    JavaRandom r(s);  // ChunkFeatureDecorator seed constants
    decoA = r.nextLong() / 2 * 2 + 1;
    decoB = r.nextLong() / 2 * 2 + 1;
  }
  for (const char* n : kDecoNames) {
    std::string id = std::string("minecraft:decoration/nether/default/") + n;
    decoSeeds.push_back(javaStringHash(id.c_str()));
  }
}

// ---- biomes ------------------------------------------------------------------------------------
namespace {
inline int integerize(double d) {
  int i = jd2i(d * 50.0);
  if (i < 0) i = 0;
  if (i >= 50) i = 49;
  return i;
}
inline double clamp01(double v) { return jmax(0.0, jmin(1.0, v)); }  // JOML Math.clamp(0,1,v)
}  // namespace

void NetherGen::Impl::climateRegion(int x0, int z0, uint8_t* out, double* temps) const {
  double t[256], h[256], f[256];
  tempNoise.getRegion2(t, x0, z0, 16, 16, 0.016666666666666666, 0.016666666666666666);
  downNoise.getRegion2(h, x0, z0, 16, 16, 0.03333333333333333, 0.03333333333333333);
  fuzzNoise.getRegion2(f, x0, z0, 16, 16, 0.16666666666666666, 0.16666666666666666);
  for (int xx = 0; xx < 16; ++xx)
    for (int zz = 0; zz < 16; ++zz) {
      int i = xx * 16 + zz;
      double a = f[i] * 1.1 + 0.5;
      double b = 0.01, c = 1.0 - b;
      double temperature = (t[i] * 0.15 + 0.7) * c + a * b;
      b = 0.002; c = 1.0 - b;
      double downfall = (h[i] * 0.15 + 0.5) * c + a * b;
      temperature = 1.0 - (1.0 - temperature) * (1.0 - temperature);
      if (temperature < 0.0) temperature = 0.0;
      if (temperature > 1.0) temperature = 1.0;
      if (downfall < 0.0) downfall = 0.0;
      if (downfall > 1.0) downfall = 1.0;
      int iT = integerize(clamp01(temperature)), iH = integerize(clamp01(downfall));
      out[(zz << 4) | xx] = biomeLut[(50 + iH) * 50 + iT];
      if (temps) temps[(zz << 4) | xx] = temperature;
    }
}

int NetherGen::Impl::climateAt(int x, int z, double* temp) const {
  double t, h, f;
  tempNoise.getRegion2(&t, x, z, 1, 1, 0.016666666666666666, 0.016666666666666666);
  downNoise.getRegion2(&h, x, z, 1, 1, 0.03333333333333333, 0.03333333333333333);
  fuzzNoise.getRegion2(&f, x, z, 1, 1, 0.16666666666666666, 0.16666666666666666);
  double a = f * 1.1 + 0.5;
  double b = 0.01, c = 1.0 - b;
  double temperature = (t * 0.15 + 0.7) * c + a * b;
  b = 0.002; c = 1.0 - b;
  double downfall = (h * 0.15 + 0.5) * c + a * b;
  temperature = 1.0 - (1.0 - temperature) * (1.0 - temperature);
  if (temperature < 0.0) temperature = 0.0;
  if (temperature > 1.0) temperature = 1.0;
  if (downfall < 0.0) downfall = 0.0;
  if (downfall > 1.0) downfall = 1.0;
  if (temp) *temp = temperature;
  return biomeLut[(50 + integerize(clamp01(downfall))) * 50 + integerize(clamp01(temperature))];
}

// ---- terrain -----------------------------------------------------------------------------------
void NetherGen::Impl::density(int cx, int cz, double* outer) const {
  const int ySize = 25;
  double mainA[625], mnA[625], mxA[625];
  int x = jmul(cx, 4), z = jmul(cz, 4);
  mainNoise.getRegion3(mainA, x, 0, z, 5, ySize, 5, 8.555150000000001, 34.2206, 8.555150000000001);
  minLimit.getRegion3(mnA, x, 0, z, 5, ySize, 5, 684.412, 2053.236, 684.412);
  maxLimit.getRegion3(mxA, x, 0, z, 5, ySize, 5, 684.412, 2053.236, 684.412);
  for (int i = 0; i < 5 * 5 * 33; ++i) outer[i] = 0.0;
  int mi = 0;
  for (int dx = 0; dx < 5; ++dx)
    for (int dz = 0; dz < 5; ++dz)
      for (int dy = 0; dy < ySize; ++dy) {
        double off = densityOffset[dy];
        double mnD = mnA[mi] / 512.0, mxD = mxA[mi] / 512.0;
        double mainD = (mainA[mi] / 10.0 + 1.0) / 2.0;
        double d = mainD < 0.0 ? mnD : (mainD > 1.0 ? mxD : mnD + (mxD - mnD) * mainD);
        d -= off;
        if (dy > ySize - 4) {
          double mod = (double)((float)(dy - (ySize - 4)) / 3.0f);
          d = d * (1.0 - mod) + -10.0 * mod;
        }
        outer[dx * 5 * 33 + dz * 33 + (dy + 9 - 1)] = d;
        ++mi;
      }
}

namespace {
inline uint16_t fluidForBiome(int b) {
  switch (b) {
    case BI_VOLCANIC_ISLANDS: case BI_SULFUR_POOLS: case BI_CRAG: return bid::LAVA_STILL;
    case BI_CRYSTAL_FOREST: case BI_CRYSTAL_PLAINS: return bid::WATER_STILL;
    case BI_SHELF: return bid::COBBLE_NETHERRACK;
    case BI_OLD_WORLD: return bid::OBSIDIAN;
    default: return 0;
  }
}
inline int64_t chunkSeed(int cx, int cz) { return ladd(lmul(cx, 341873128712LL), lmul(cz, 132897987541LL)); }
inline int idx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
inline uint16_t getR(const TerrainChunk& t, int x, int y, int z) {
  if (x < 0 || x >= 16 || y < 0 || y >= 256 || z < 0 || z >= 16) return 0;
  return t.id[idx(x, y, z)];
}
inline void setR(TerrainChunk& t, int x, int y, int z, int id) {
  if (x < 0 || x >= 16 || y < 0 || y >= 256 || z < 0 || z >= 16 || id < 0) return;
  t.id[idx(x, y, z)] = (uint16_t)id;
}
inline int biomeIn(const TerrainChunk& t, int x, int y, int z) { return y < 64 ? (int)BI_OLD_WORLD_DESERT : (int)t.climate[(z << 4) | x]; }
void biomeTopFiller(int b, uint16_t& top, uint16_t& fill) {
  switch (b) {
    case BI_VOLCANIC_ISLANDS: top = bid::BRIMSAND; fill = bid::BRIMSTONE; return;
    case BI_SULFUR_POOLS: top = bid::BRIMSTONE; fill = bid::BRIMSTONE; return;
    case BI_OLD_WORLD: top = bid::BLOCK_ASH; fill = bid::BLOCK_ASH; return;
    case BI_OLD_WORLD_DESERT: top = bid::SOULSAND; fill = bid::SOULSAND; return;
    default: top = bid::COBBLE_NETHERRACK; fill = bid::COBBLE_NETHERRACK; return;  // crag, crystal*, shelf
  }
}
}  // namespace

void NetherGen::Impl::buildTerrain(int cx, int cz, TerrainChunk& tc, int stages) const {
  climateRegion(jmul(cx, 16), jmul(cz, 16), tc.climate, nullptr);
  std::memset(tc.id, 0, sizeof(tc.id));
  if ((stages & 1) && opt.shape) {
    // a given shape (NetherGenOptions::shape): bedrock as BTA lays it, the rest by the columns
    NetherGenOptions::ShapeColumn C[256];
    opt.shape(cx, cz, C);
    JavaRandom rand(chunkSeed(cx, cz));
    for (int Z = 0; Z < 16; ++Z)
      for (int X = 0; X < 16; ++X) {
        const NetherGenOptions::ShapeColumn& c = C[(Z << 4) | X];
        const int topRock = 255 - rand.nextInt(10), lowRock = 63 + rand.nextInt(10);
        for (int Y = 0; Y < 256; ++Y) {
          int id;
          if (Y < 63 || Y >= topRock || Y <= lowRock) id = bid::BEDROCK;
          else if (Y <= c.floor || Y >= c.roof) id = bid::COBBLE_NETHERRACK;
          else if (c.pool && Y <= c.poolTop) id = c.pool == 2 ? bid::ACID_STILL : bid::LAVA_STILL;
          else if (Y < 96) {
            uint16_t fl = fluidForBiome(tc.climate[(Z << 4) | X]);
            if (fl == bid::WATER_STILL && Y == 95) id = bid::RUBYGLASS_CRYSTAL;
            else if (fl == bid::COBBLE_NETHERRACK) id = rand.nextInt(2) == 0 ? bid::NETHERRACK : bid::COBBLE_NETHERRACK;
            else id = fl;
          } else id = bid::AIR;
          setR(tc, X, Y, Z, id);
        }
      }
  } else if (stages & 1) {
    double D[5 * 5 * 33];
    density(cx, cz, D);
    JavaRandom rand(chunkSeed(cx, cz));
    const int YD = 33;
    for (int bx = 0; bx < 4; ++bx)
      for (int bz = 0; bz < 4; ++bz)
        for (int by = 0; by < 32; ++by) {
          double d000 = D[(bx * 5 + bz) * YD + by], d001 = D[(bx * 5 + (bz + 1)) * YD + by];
          double d010 = D[(bx * 5 + bz) * YD + (by + 1)], d011 = D[(bx * 5 + (bz + 1)) * YD + (by + 1)];
          double d100 = D[((bx + 1) * 5 + bz) * YD + by], d101 = D[((bx + 1) * 5 + (bz + 1)) * YD + by];
          double d110 = D[((bx + 1) * 5 + bz) * YD + (by + 1)], d111 = D[((bx + 1) * 5 + (bz + 1)) * YD + (by + 1)];
          double ya00 = (d010 - d000) * 0.125, ya01 = (d011 - d001) * 0.125;
          double ya10 = (d110 - d100) * 0.125, ya11 = (d111 - d101) * 0.125;
          double y00 = d000, y01 = d001, y10 = d100, y11 = d101;
          for (int yi = 0; yi < 8; ++yi) {
            double xa0 = (y10 - y00) * 0.25, xa1 = (y11 - y01) * 0.25;
            double x0 = y00, x1 = y01;
            for (int xi = 0; xi < 4; ++xi) {
              double za = (x1 - x0) * 0.25;
              double zd = x0;
              for (int zi = 0; zi < 4; ++zi) {
                int X = xi + bx * 4, Y = yi + by * 8, Z = zi + bz * 4;
                int id;
                if (Y < 63) id = bid::BEDROCK;
                else if (Y >= 255 - rand.nextInt(10)) id = bid::BEDROCK;
                else if (Y <= 63 + rand.nextInt(10)) id = bid::BEDROCK;
                else if (zd > 0.0) id = bid::COBBLE_NETHERRACK;
                else if (Y < 96) {
                  uint16_t fl = fluidForBiome(tc.climate[(Z << 4) | X]);
                  if (fl == bid::WATER_STILL && Y == 95) id = bid::RUBYGLASS_CRYSTAL;
                  else if (fl == bid::COBBLE_NETHERRACK) id = rand.nextInt(2) == 0 ? bid::NETHERRACK : bid::COBBLE_NETHERRACK;
                  else id = fl;
                } else id = bid::AIR;
                setR(tc, X, Y, Z, id);
                zd += za;
              }
              x0 += xa0; x1 += xa1;
            }
            y00 += ya00; y01 += ya01; y10 += ya10; y11 += ya11;
          }
        }
  }
  if (stages & 2) surface(cx, cz, tc);
  if (stages & 4) caves(cx, cz, tc);
  if (stages & (8 | 16 | 32)) floes(cx, cz, tc, stages);
}

// ---- SurfaceGeneratorNether --------------------------------------------------------------------
namespace {
inline int mixStone(int biome, int id, JavaRandom& r) {
  if ((biome == BI_CRAG || biome == BI_SHELF || biome == BI_CRYSTAL_PLAINS || biome == BI_CRYSTAL_FOREST) &&
      id == bid::COBBLE_NETHERRACK)
    return r.nextInt(2) == 0 ? bid::NETHERRACK : bid::COBBLE_NETHERRACK;
  if ((biome == BI_OLD_WORLD || biome == BI_OLD_WORLD_DESERT) && (id == bid::COBBLE_GLOOMSTONE || id == bid::GLOOMSTONE))
    return r.nextInt(2) == 0 ? bid::GLOOMSTONE : bid::COBBLE_GLOOMSTONE;
  return id;
}
inline int biomeStone(int b) {
  switch (b) {
    case BI_VOLCANIC_ISLANDS: return bid::BRIMSAND;
    case BI_SULFUR_POOLS: return bid::BRIMSTONE;
    case BI_CRYSTAL_PLAINS: case BI_CRYSTAL_FOREST: return bid::COBBLE_NETHERRACK;
    case BI_OLD_WORLD: case BI_OLD_WORLD_DESERT: return bid::COBBLE_GLOOMSTONE;
    default: return bid::COBBLE_NETHERRACK;  // worldFillBlock
  }
}
}  // namespace

void NetherGen::Impl::surface(int cx, int cz, TerrainChunk& tc) const {
  const int oceanY = 96, minY = 0, maxY = 255, absOcean = minY + oceanY;
  const int wx0 = jmul(cx, 16), wz0 = jmul(cz, 16);
  const int oceanBlock = bid::LAVA_STILL, fill = bid::COBBLE_NETHERRACK;
  JavaRandom rand(chunkSeed(cx, cz));
  const double bs = 0.03125;
  double sand[256], gravel[256], soil[256];
  beachNoise.getRegion3(sand, wx0, wz0, 0.0, 16, 16, 1, bs, bs, 1.0);
  beachNoise.getRegion3(gravel, wx0, 109.0134, wz0, 16, 1, 16, bs, 1.0, bs);
  soilNoise.getRegion3(soil, wx0, wz0, 0.0, 16, 16, 1, bs * 2.0, bs * 2.0, bs * 2.0);
  for (int z = 0; z < 16; ++z)
    for (int x = 0; x < 16; ++x) {
      int ni = z + x * 16;
      bool soulBeach = sand[ni] + rand.nextDouble() * 0.2 > 0.0;
      bool gravelBeach = gravel[ni] + rand.nextDouble() * 0.2 > 0.0;
      int soilThickness = jd2i(soil[ni] / 3.0 + 3.0 + rand.nextDouble() * 0.25);
      int depth = -1;
      int top = -1, filler = -1;
      int lastBiome = -1;
      uint16_t cTop = 0xFFFF, cFill = 0xFFFF;
      bool cSulfur = false;
      int cStone = fill;
      int biome = -1;
      int lastCell = INT32_MIN;
      for (int y = maxY; y >= minY; --y) {
        int cell = y >> 3;
        if (cell != lastCell) { lastCell = cell; biome = biomeIn(tc, x, y, z); }
        if (biome != lastBiome) {
          biomeTopFiller(biome, cTop, cFill);
          cSulfur = biome == BI_SULFUR_POOLS;
          cStone = biomeStone(biome);
          lastBiome = biome;
        }
        int block = tc.id[idx(x, y, z)];
        if ((top == -1 || filler == -1) && block == 0) { top = cTop; filler = cFill; }
        if (block == 0) { depth = -1; continue; }
        if (block != fill) continue;
        if (depth == -1) {
          if (soilThickness <= 0) {
            top = 0;
            if (biome == BI_VOLCANIC_ISLANDS) filler = bid::BRIMSAND;
            else if (biome == BI_SULFUR_POOLS) filler = bid::SULFUR;
            else if (biome == BI_OLD_WORLD || biome == BI_OLD_WORLD_DESERT)
              filler = rand.nextInt(2) == 0 ? bid::GLOOMSTONE : bid::COBBLE_GLOOMSTONE;
            else filler = bid::NETHERRACK;
          } else if (y >= absOcean - 4 && y <= absOcean + 1) {
            top = cTop; filler = cFill;
            if (gravelBeach || soulBeach) { top = bid::SOULSAND; filler = bid::SOULSAND; }
          } else if (y <= oceanY && cSulfur) {
            top = bid::BRIMSAND; filler = bid::BRIMSTONE;
          }
          if (y < absOcean && top == 0) top = oceanBlock;
          depth = soilThickness + 2;
          if (y >= absOcean - 1) setR(tc, x, y, z, mixStone(biome, top, rand));
          else setR(tc, x, y, z, mixStone(biome, filler, rand));
          continue;
        }
        if (depth > 0) {
          setR(tc, x, y, z, mixStone(biome, filler, rand));
          --depth;
        }
        if (depth == 0 && biome == BI_OLD_WORLD && filler == bid::BLOCK_ASH) {
          depth = rand.nextInt(8) + 2;
          filler = bid::SLATE;
        }
        if (depth > 0) continue;
        int stone = cStone;
        if (biome == BI_CRAG || biome == BI_SHELF) stone = rand.nextInt(2) == 0 ? bid::NETHERRACK : bid::COBBLE_NETHERRACK;
        else if (biome == BI_OLD_WORLD || biome == BI_OLD_WORLD_DESERT)
          stone = rand.nextInt(2) == 0 ? bid::GLOOMSTONE : bid::COBBLE_GLOOMSTONE;
        setR(tc, x, y, z, stone);
      }
    }
}

// ---- CavesLargeFeature(64, 256) ----------------------------------------------------------------
namespace {
constexpr float PIF = 3.14159265358979323846f;  // (float)Math.PI
struct CaveCtx {
  const NetherGen::Impl* g;
  TerrainChunk* tc;
  int bcx, bcz;
  static constexpr int minY = 64, maxY = 256;
};

void generateCave(CaveCtx& c, int64_t seed, double bx, double by, double bz, float initialRadius, float yRot, float xRot,
                  int startPos, int endPos, double heightMod) {
  TerrainChunk& tc = *c.tc;
  double cmx = (double)(c.bcx * 16 + 8), cmz = (double)(c.bcz * 16 + 8);
  float rotHor = 0.0f, rotVer = 0.0f;
  JavaRandom r(seed);
  if (endPos <= 0) {
    int maxLength = 8 * 16 - 16;
    endPos = maxLength - r.nextInt(maxLength / 4);
  }
  bool noBranches = false;
  if (startPos == -1) { startPos = endPos / 2; noBranches = true; }
  int branchPos = r.nextInt(endPos / 2) + endPos / 4;
  bool sharp = r.nextInt(6) == 0;
  for (; startPos < endPos; ++startPos) {
    double width = 1.5 + (double)(mh::sin((float)startPos * PIF / (float)endPos) * initialRadius * 1.0f);
    double height = width * heightMod;
    float xzScale = mh::cos(xRot);
    float yOffset = mh::sin(xRot);
    bx += (double)(mh::cos(yRot) * xzScale);
    by += (double)yOffset;
    bz += (double)(mh::sin(yRot) * xzScale);
    xRot = sharp ? xRot * 0.92f : xRot * 0.7f;
    xRot += rotVer * 0.1f;
    yRot += rotHor * 0.1f;
    rotVer *= 0.9f;
    rotHor *= 0.75f;
    { float a = r.nextFloat(); float b = r.nextFloat(); float d = r.nextFloat(); rotVer += (a - b) * d * 2.0f; }
    { float a = r.nextFloat(); float b = r.nextFloat(); float d = r.nextFloat(); rotHor += (a - b) * d * 4.0f; }
    if (!noBranches && startPos == branchPos && initialRadius > 1.0f) {
      int64_t s1 = r.nextLong(); float r1 = r.nextFloat() * 0.5f + 0.5f;
      generateCave(c, s1, bx, by, bz, r1, yRot - 1.5707964f, xRot / 3.0f, startPos, endPos, 1.0);
      int64_t s2 = r.nextLong(); float r2 = r.nextFloat() * 0.5f + 0.5f;
      generateCave(c, s2, bx, by, bz, r2, yRot + 1.5707964f, xRot / 3.0f, startPos, endPos, 1.0);
      return;
    }
    if (!noBranches && r.nextInt(4) == 0) continue;
    double dxm = bx - cmx, dzm = bz - cmz;
    double length = endPos - startPos;
    double maxRadius = (double)(initialRadius + 2.0f + 16.0f);
    if (dxm * dxm + dzm * dzm - length * length > maxRadius * maxRadius) return;
    if (bx < cmx - 16.0 - width * 2.0 || bz < cmz - 16.0 - width * 2.0 || bx > cmx + 16.0 + width * 2.0 ||
        bz > cmz + 16.0 + width * 2.0)
      continue;
    int minX = mh::floor(bx - width) - c.bcx * 16 - 1;
    int maxX = mh::floor(bx + width) - c.bcx * 16 + 1;
    int minY = mh::floor(by - height) - 1;
    int maxY = mh::floor(by + height) + 1;
    int minZ = mh::floor(bz - width) - c.bcz * 16 - 1;
    int maxZ = mh::floor(bz + width) - c.bcz * 16 + 1;
    if (minX < 0) minX = 0;
    if (maxX > 16) maxX = 16;
    if (minY < CaveCtx::minY + 1) minY = CaveCtx::minY + 1;
    if (maxY > CaveCtx::maxY - 8) maxY = CaveCtx::maxY - 8;
    if (minZ < 0) minZ = 0;
    if (maxZ > 16) maxZ = 16;
    bool hit = false;
    for (int x = minX; !hit && x < maxX; ++x)
      for (int z = minZ; !hit && z < maxZ; ++z)
        for (int y = maxY + 1; !hit && y >= minY - 1; --y) {
          int id = getR(tc, x, y, z);
          if (y < CaveCtx::minY || y >= CaveCtx::maxY) continue;
          if (id == bid::LAVA_STILL || id == bid::WATER_STILL) { hit = true; }
          if (y == minY - 1 || x == minX || x == maxX - 1 || z == minZ || z == maxZ - 1) continue;
          y = minY;
        }
    if (hit) continue;
    for (int x = minX; x < maxX; ++x) {
      double xp = ((double)(x + c.bcx * 16) + 0.5 - bx) / width;
      for (int z = minZ; z < maxZ; ++z) {
        double zp = ((double)(z + c.bcz * 16) + 0.5 - bz) / width;
        int yIndex = maxY;
        bool replaceTop = false;
        if (xp * xp + zp * zp >= 1.0) continue;
        for (int y = maxY - 1; y >= minY; --y) {
          double yp = ((double)y + 0.5 - by) / height;
          if (yp > -0.7 && xp * xp + yp * yp + zp * zp < 1.0) {
            int id = getR(tc, x, yIndex, z);
            if (hasF((uint16_t)id, T_CAVE_REPLACES_SURFACE)) replaceTop = true;
            if (hasF((uint16_t)id, T_CAVES_CUT)) {
              if (y < 10) setR(tc, x, yIndex, z, bid::LAVA_STILL);
              else {
                setR(tc, x, yIndex, z, 0);
                if (replaceTop) {
                  // BTA bug kept: world.getBlockBiome(x, yIndex - 1, z) with chunk-LOCAL x/z
                  int yy = yIndex - 1;
                  int b = yy < 64 ? BI_OLD_WORLD_DESERT : c.g->climateAt(x, z, nullptr);
                  uint16_t top, fill;
                  biomeTopFiller(b, top, fill);
                  if (getR(tc, x, yy, z) == fill) setR(tc, x, yy, z, top);
                }
              }
            }
          }
          --yIndex;
        }
      }
    }
    if (noBranches) break;
  }
}
}  // namespace

void NetherGen::Impl::caves(int cx, int cz, TerrainChunk& tc) const {
  CaveCtx c{this, &tc, cx, cz};
  JavaRandom rand(seed);
  for (int chunkX = cx - 8; chunkX <= cx + 8; ++chunkX)
    for (int chunkZ = cz - 8; chunkZ <= cz + 8; ++chunkZ) {
      rand.setSeed(ladd(lmul(chunkX, lfRandX), lmul(chunkZ, lfRandZ)) ^ seed);
      int n0 = rand.nextInt(40) + 1;
      int n1 = rand.nextInt(n0) + 1;
      int caves = rand.nextInt(n1);
      if (rand.nextInt(15) != 0) caves = 0;
      const int yRange = 256 - 64;
      for (int i = 0; i < caves; ++i) {
        double bx = (double)(chunkX * 16 + rand.nextInt(16));
        int yy = rand.nextInt(yRange - 8) + 8;
        double by = (double)(64 + rand.nextInt(yy));
        double bz = (double)(chunkZ * 16 + rand.nextInt(16));
        int branches = 1;
        if (rand.nextInt(4) == 0) {
          int64_t s = rand.nextLong();
          float rad = 1.0f + rand.nextFloat() * 6.0f;
          generateCave(c, s, bx, by, bz, rad, 0.0f, 0.0f, -1, -1, 0.5);
          branches += rand.nextInt(4);
        }
        for (int l = 0; l < branches; ++l) {
          float yRot = rand.nextFloat() * PIF * 2.0f;
          float xRot = (rand.nextFloat() - 0.5f) * 2.0f / 8.0f;
          float a = rand.nextFloat();
          float b = rand.nextFloat();
          float ir = a * 2.0f + b;
          int64_t s = rand.nextLong();
          generateCave(c, s, bx, by, bz, ir, yRot, xRot, 0, 0, 1.0);
        }
      }
    }
}

// ---- LavaFloe / RubyglassFloe / Shelf (radius 0) -------------------------------------------------
void NetherGen::Impl::floes(int cx, int cz, TerrainChunk& tc, int stages) const {
  const int oceanY = 96;
  const int64_t s = ladd(lmul(cx, lfRandX), lmul(cz, lfRandZ)) ^ seed;
  Worley::Result wr;
  if (stages & 8) {  // LavaFloeLargeFeature
    JavaRandom random(s);
    for (int x = 0; x < 16; ++x)
      for (int z = 0; z < 16; ++z) {
        if (tc.climate[(z << 4) | x] != BI_VOLCANIC_ISLANDS) continue;
        floeWorley.getValue((double)((float)cx + (float)x / 16.0f), (double)((float)cz + (float)z / 16.0f), wr);
        double gx = (double)((float)(cx * 16 + x) / 5.0f), gz = (double)((float)(cz * 16 + z) / 5.0f);
        double g = floeGap.getValue2(gx, gz);
        float lineWidth = (float)(g / 2.0 + 0.5);
        int heightMod = JavaRandom((int64_t)wr.index).nextInt(3) + 1;
        float height = (float)(g / 2.0 + 0.5) * (float)heightMod;  // heightNoise == gapNoise
        if (mh::unsignedMod((double)wr.index, 15.0) == 0.0 || wr.distance > (double)(0.9f + lineWidth * 0.01f)) continue;
        for (int y = oceanY - 3; (float)y < (float)oceanY + (height - 2.0f); ++y) {
          int id;
          if (random.nextInt(20) == 0) id = bid::PUMICE_WET;
          else id = random.nextInt(2) == 0 ? bid::NETHERRACK : bid::COBBLE_NETHERRACK;
          int cur = getR(tc, x, y, z);
          if (cur == bid::LAVA_STILL || cur == 0) setR(tc, x, y, z, id);
        }
        for (int y = oceanY + jf2i(height - 2.0f); (float)y < (float)oceanY + (height + 1.0f); ++y) {
          int cur = getR(tc, x, y, z);
          if (cur == bid::LAVA_STILL || cur == 0) setR(tc, x, y, z, bid::BRIMSAND);
        }
      }
  }
  if (stages & 16) {  // RubyglassFloeLargeFeature
    JavaRandom random(s);
    for (int x = 0; x < 16; ++x)
      for (int z = 0; z < 16; ++z) {
        int b = tc.climate[(z << 4) | x];
        if (b != BI_CRYSTAL_FOREST && b != BI_CRYSTAL_PLAINS) continue;
        double sx = (double)cx + (double)x / 16.0, sz = (double)cz + (double)z / 16.0;
        floeWorley.getValue(sx, sz, wr);
        double lineWidth = floeGap.getValue2((double)(cx * 16 + x) / 15.0, (double)(cz * 16 + z) / 15.0) / 2.0 + 0.5;
        double edge = 0.8 + lineWidth * 0.05;
        if (mh::unsignedMod((double)wr.index, 15.0) == 0.0 || !(wr.distance <= edge)) continue;
        double tv = floeScale.getValue2((double)x / 10.0, (double)z / 10.0);
        int depth = jd2i(tv * 6.0);
        auto trySet = [&](int y, int id) {
          int cur = getR(tc, x, y, z);
          if (cur == bid::RUBYGLASS_CRYSTAL || cur == bid::WATER_STILL || cur == bid::AIR) setR(tc, x, y, z, id);
        };
        for (int y = oceanY - (depth + 2); y < oceanY; ++y) trySet(y, bid::COBBLE_NETHERRACK_CRYSTALLINE);
        for (int y = oceanY; y < oceanY + depth; ++y) {
          int id = random.nextInt(2) == 0 ? bid::NETHERRACK : bid::COBBLE_NETHERRACK;
          trySet(y, id);
        }
      }
  }
  if (stages & 32) {  // ShelfLargeFeature
    for (int x = 0; x < 16; ++x)
      for (int z = 0; z < 16; ++z) {
        if (tc.climate[(z << 4) | x] != BI_SHELF) continue;
        floeWorley.getValue((double)((float)cx + (float)x / 16.0f), (double)((float)cz + (float)z / 16.0f), wr);
        int heightMod = JavaRandom((int64_t)wr.index).nextInt(5) + 1;
        for (int y = oceanY - 20; y < oceanY + heightMod; ++y) {
          int cur = getR(tc, x, y, z);
          if (cur == bid::WATER_STILL || cur == 0) setR(tc, x, y, z, bid::BRIMSAND);
        }
      }
  }
}

// ---- cache + public API ------------------------------------------------------------------------
std::shared_ptr<const TerrainCached> NetherGen::Impl::terrain(int cx, int cz) {
  int64_t key = ((int64_t)cx << 32) ^ (int64_t)(uint32_t)cz;
  {
    std::lock_guard<std::mutex> lk(mu);
    auto it = index.find(key);
    if (it != index.end()) {
      lru.splice(lru.begin(), lru, it->second);
      return it->second->second;
    }
  }
  auto tc = std::make_unique<TerrainChunk>();
  buildTerrain(cx, cz, *tc, 63);
  auto c = std::make_shared<TerrainCached>();
  c->sections = 0;
  for (int i = 0; i < 65536; ++i) {
    c->kind[i] = (uint8_t)nbFromBtaId(tc->id[i]);
    if (tc->id[i]) c->sections |= (uint16_t)(1u << (i >> 12));
  }
  std::memcpy(c->climate, tc->climate, 256);
  std::lock_guard<std::mutex> lk(mu);
  auto it = index.find(key);
  if (it != index.end()) {
    lru.splice(lru.begin(), lru, it->second);
    return it->second->second;
  }
  lru.emplace_front(key, c);
  index[key] = lru.begin();
  while (lru.size() > opt.terrainCache) {
    index.erase(lru.back().first);
    lru.pop_back();
  }
  return c;
}

NetherGen::NetherGen(int64_t seed, const NetherGenOptions& opt) : impl_(std::make_unique<Impl>(seed, opt)) {}
NetherGen::~NetherGen() = default;
int64_t NetherGen::seed() const { return impl_->seed; }

int NetherGen::biomeAt(int x, int y, int z) const {
  if (y < 64) return BI_OLD_WORLD_DESERT;
  return impl_->climateAt(x, z, nullptr);
}
double NetherGen::temperatureAt(int x, int z) const {
  double t;
  impl_->climateAt(x, z, &t);
  return t;
}

void NetherGen::terrainIds(int cx, int cz, uint16_t* ids, int stages) const {
  auto tc = std::make_unique<TerrainChunk>();
  impl_->buildTerrain(cx, cz, *tc, stages);
  std::memcpy(ids, tc->id, sizeof(tc->id));
}

void NetherGen::densityMap(int cx, int cz, double* out) const { impl_->density(cx, cz, out); }
void NetherGen::setDecorationMaskForTesting(uint64_t m) { impl_->decoMask = m; }

void NetherGen::generateTerrain(int cx, int cz, Out& out) {
  auto tc = impl_->terrain(cx, cz);
  std::memcpy(out.kind, tc->kind, 65536);
  std::memset(out.meta, 0, 65536);
}

void NetherGen::chunkIds(int cx, int cz, uint16_t* ids, uint8_t* meta, DecoStats* stats) {
  impl_->finalChunk(cx, cz, nullptr, meta, ids, stats);
}

void NetherGen::generateChunk(int cx, int cz, Out& out) { impl_->finalChunk(cx, cz, out.kind, out.meta, nullptr, nullptr); }

}  // namespace bta
