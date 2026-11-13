// gen/overworld.cpp — Overworld terrain: columns (worldgen.js column + worldupdate.js rivers + structure
// levelling), the block columns, caves, ores (+ metals.js), the watcher's oddities, the Deep Dark
// (deepdark.js) and the Ember Crust. Heights/depths go through gen/gendefs.h vmap.
#include "gen/overworld.h"
#include "gen/density.h"
#include "gen/mc_biomes.h"
#include "gen/surface_rules.h"
#include "gen/mc_features.h"
#include "common/fs.h"
#include <map>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace dq {
using namespace vmap;

OverworldGen::OverworldGen(const WorldGenOptions& o)
  : structures(*this), seed_(o.seed),
    nCont(o.seed ^ 0x1234), nHill(o.seed ^ 0x2345), nDetail(o.seed ^ 0x3456), nMount(o.seed ^ 0x4567), nRidge(o.seed ^ 0x5678),
    nTemp(o.seed ^ 0x6789), nHumid(o.seed ^ 0x789a), nCave1(o.seed ^ 0x89ab), nCave2(o.seed ^ 0x9abc), nCave3(o.seed ^ 0xabcd),
    nFloor(o.seed ^ 0xbcde), nRiver(o.seed ^ 0x5eed11), nDeep(o.seed ^ 0xDEE9), nDeepC(o.seed ^ 0xDEEC) {
  climateScale = o.genVersion >= 4 ? 2.0 : 1.0;
  if (o.worldType == "large_biomes") climateScale *= 3;   // the "Bigger biomes" world: every land three times as wide
  wtype = o.worldType;
  genVer_ = o.genVersion;
  deep_ = o.genVersion >= 19 && worldLayout().id >= 2;
  jigsawVillages = o.genVersion >= 9;
  villageStyles = o.genVersion >= 10;
  villagePlan11 = o.genVersion >= 11;
  rockLayers = o.genVersion >= 12;
  dynTrees = o.genVersion >= 13;
  looseGround = o.looseGround;
  floes = o.genVersion >= 5;   // terrain version 5: BTA's ice floes in the seas round the glaciers   // terrain version 4 (BTA): biomes about twice as big
  B.resolve();
  initUnderground();
  // terrain version 14: Minecraft 1.20's land (docs/WORLDGEN_MERGE.md), when its data is here (tools/port/vanilla_worldgen.py);
  // terrain version 15: Soulrend's world (its mods' land, biomes and surfaces: tools/port/soulrend_worldgen/import.py) for the
  // default and bigger-biome worlds when its data is here. The world types that reshape the land their own way keep the old
  if (o.genVersion >= 14 && (wtype.empty() || wtype == "default" || wtype == "amplified" || wtype == "large_biomes" || wtype == "winter" || wtype == "hell")) {
    const bool sr = o.genVersion >= 15 && (wtype.empty() || wtype == "default" || wtype == "large_biomes") && fs::exists(dataDir() / "soulrend_worldgen" / "biome_regions.json");
    const std::string prefix = wtype == "amplified" ? "overworld_amplified" : wtype == "large_biomes" ? "overworld_large_biomes" : "overworld";
    struct Shared { std::shared_ptr<DensityGraph> g; std::shared_ptr<McBiomes> b; std::shared_ptr<McSurface> s; std::shared_ptr<McFeatures> f; };
    static std::mutex mx; static std::map<std::tuple<i32, std::string, bool, int>, Shared> worlds;
    std::lock_guard<std::mutex> lk(mx);
    const bool q17 = o.genVersion >= 17;
    Shared& w = worlds[{o.seed, prefix, sr, (int)q17 + (o.genVersion >= 18)}];
    if (!w.g) {
      auto ng = std::make_shared<DensityGraph>();
      if (ng->load(dataDir() / (sr ? "soulrend_worldgen" : "vanilla_worldgen"), (u64)(i64)o.seed * 0x9E3779B97F4A7C15ull + 14, prefix)) w.g = ng;
      if (w.g && sr) {
        auto nb = std::make_shared<McBiomes>(); auto nsu = std::make_shared<McSurface>();
        if (nb->load(dataDir() / "soulrend_worldgen", (i64)o.seed, q17, o.genVersion >= 18) && nsu->load(dataDir() / "soulrend_worldgen", *w.g, *nb, (u64)(i64)o.seed * 0x2545F4914F6CDD1Dull)) { w.b = nb; w.s = nsu; auto nf = std::make_shared<McFeatures>(); if (nf->load(dataDir() / "soulrend_worldgen", *nb, (u64)(i64)o.seed * 0xD1B54A32D192ED03ull)) w.f = nf; }
      }
    }
    if (w.g) {
      vg_ = w.g; v14 = true;
      // (every function it uses parsed now, on this thread: the generators' workers only read it)
      const DensityGraph* g = w.g.get();
      vSloped = g->find("overworld/sloped_cheese"); vFinal = g->find("final_density"); vCont = g->find("continents"); vEro = g->find("erosion");
      vOff = g->find("overworld/offset"); vJag = g->find("overworld/jaggedness");
      if (vSloped < 0 || vFinal < 0) v14 = false;
      if (v14 && w.b) {
        mcb_ = w.b; mcs_ = w.s; v15 = true; v16 = o.genVersion >= 16; v17 = o.genVersion >= 17; v18 = o.genVersion >= 18; if (v16) mcf_ = w.f;   // (16: the biomes' own plants and the land's extras)
        vTemp = g->find("temperature"); vVeg = g->find("vegetation"); vDepth = g->find("depth"); vRidges = g->find("ridges");
        vInit = g->find("initial_density_without_jaggedness");
        {   // (the extras' noises, registered here on this thread)
          const char* NX[12] = {"minecraft:badlands_surface", "minecraft:badlands_pillar", "minecraft:badlands_pillar_roof", "minecraft:iceberg_surface",
                                "minecraft:iceberg_pillar", "minecraft:iceberg_pillar_roof", "natures_spirit:sugi_surface", "natures_spirit:sugi_pillar",
                                "natures_spirit:sugi_pillar_roof", "natures_spirit:stratified_desert_surface", "natures_spirit:stratified_desert_pillar",
                                "natures_spirit:stratified_desert_pillar_roof"};
          for (int k = 0; k < 12; k++) nX_[k] = w.g->noise(NX[k]);
          bEroded_ = mcb_->find("minecraft:eroded_badlands"); bFrozen_ = mcb_->find("minecraft:frozen_ocean"); bDeepFrozen_ = mcb_->find("minecraft:deep_frozen_ocean");
          bSugi_ = mcb_->find("natures_spirit:sugi_forest"); bSugi2_ = mcb_->find("natures_spirit:blooming_sugi_forest"); bStrat_ = mcb_->find("natures_spirit:stratified_desert");
          bLively_ = mcb_->find("natures_spirit:lively_dunes"); bBlooming_ = mcb_->find("natures_spirit:blooming_dunes");
        }
        // each biome as ours: its kind (for our trees, villages, mobs...) and climate numbers (tints, ice)
        for (int i = 0; i < mcb_->count(); i++) {
          const McBiomes::Info& in = mcb_->info(i);
          const std::string n = in.name.substr(in.name.find(':') + 1);
          auto has = [&](std::initializer_list<const char*> w) { for (const char* k : w) if (n.find(k) != std::string::npos) return true; return false; };
          u8 cat = BIOME_PLAINS;
          if (has({"ocean"})) cat = BIOME_OCEAN;
          else if (has({"beach", "shore", "coast"})) cat = BIOME_BEACH;
          else if (has({"peaks", "slopes", "crags", "cliffs", "volcano", "mountains", "glacial", "ice_cap"})) cat = BIOME_MOUNTAINS;
          else if (has({"desert", "dunes", "badlands", "mesa", "outback", "pinnacles", "salt_lakes", "danakil", "scorched", "ayers_rock", "red_rock"})) cat = BIOME_DESERT;
          else if (has({"jungle", "bamboo", "tropical"})) cat = BIOME_FOREST;
          else if (in.temperature < 0.3f) cat = BIOME_TAIGA;
          else if (has({"forest", "wood", "jungle", "taiga", "grove", "thicket", "swamp", "rainforest", "bog", "covert", "redwood", "sequoia"})) cat = BIOME_FOREST;
          mcCat_.push_back(cat);
          if (mcLandKinds().size() == (size_t)i) {   // (our land kind: villages, structures and our trees by it)
            u8 v = BV_NONE;
            if (has({"mushroom"})) v = BV_MUSHROOM;
            else if (has({"badlands", "mesa", "red_rock", "outback", "ayers", "pinnacles"})) v = BV_REDLANDS;
            else if (has({"swamp", "marsh", "bog", "mangrove", "bayou", "fen", "wetland", "mire"})) v = BV_MOSSMIRE;
            else if (has({"ice_spikes", "glacier", "ice_cap", "glacial", "frozen_peaks"})) v = BV_GLACIER;
            else if (has({"tundra", "snowy_plains", "snowy_flats", "sleeted"})) v = BV_TUNDRA;
            else if (has({"chaparral", "shrub", "scrub", "drylands", "xeric"})) v = BV_THORNSCRUB;
            else if (has({"highlands", "boreal", "alpine"})) v = BV_HIGHLANDS;
            else if (has({"meadow", "flower", "blooming", "wildflower", "heather"})) v = BV_MEADOW;
            else if (has({"maple", "autumn", "sakura", "cherry", "seasonal"})) v = BV_SEASONAL;
            mcLandKinds().push_back(v);
          }
          // our temperature: snowy (below our -0.38: snow villages, winter structures, snowy lands) exactly where Minecraft
          // snows (under 0.15); a taiga at 0.25 is green, so it gets green villages
          const double T = in.temperature;
          const double ours = T < 0.15 ? -0.40 - (0.15 - T) * 0.8 : T < 0.8 ? -0.36 + (T - 0.15) / 0.65 * 0.36 : (T - 0.8) / 1.25;
          mcTemp_.push_back((float)std::clamp(ours, -1.0, 1.0));
          mcHumid_.push_back((float)std::clamp(in.downfall * 2.0 - 1.0, -1.0, 1.0));
        }
      }
    }
  }
}

// terrain version 14: the top of the land (Minecraft's sloped_cheese: the terrain before the caves) at a column, found from
// above in steps of 4 then to the block; kept per thread for the columns it was last asked about
int OverworldGen::v14Surface(int x, int z) const {
  // Minecraft works its land out at the corners of 4-block cells and blends between: the height here the same way (each
  // corner found once, a sixteenth of the work for everyone asking about columns: structure plans, trees, the far land)
  const int x0 = x & ~3, z0 = z & ~3;
  if (x != x0 || z != z0) {
    const double fx = (x - x0) / 4.0, fz = (z - z0) / 4.0;
    const double a = v14Surface(x0, z0), b2 = v14Surface(x0 + 4, z0), c2 = v14Surface(x0, z0 + 4), d2 = v14Surface(x0 + 4, z0 + 4);
    return (int)std::lround((a * (1 - fx) + b2 * fx) * (1 - fz) + (c2 * (1 - fx) + d2 * fx) * fz);
  }
  return v14Corner(x, z);
}
int OverworldGen::v14Corner(int x, int z) const {
  struct E { i64 k; int y; };
  thread_local std::vector<E> cache(1 << 18, E{(i64)1 << 62, 0});   // (4 MB a thread: structure plans ask about wide areas)
  thread_local const void* owner = nullptr;
  if (owner != vg_.get()) { std::fill(cache.begin(), cache.end(), E{(i64)1 << 62, 0}); owner = vg_.get(); }
  const i64 key = ((i64)x << 32) ^ (u32)z;
  E& e = cache[(u64)(key * 0x9E3779B97F4A7C15ull) >> 46];
  if (e.k == key) return e.y;
  const DensityGraph& g = *vg_;
  int top = 1;
  // from just above where its 2D shape puts the ground (depth = 0 where Minecraft's y = 128 (1.5 + offset) - 64, the jagged
  // peaks at most the jaggedness higher), not from the sky: a dozen samples, not fifty
  const double off = g.eval(vOff, x, 0, z), jag = vJag >= 0 ? std::max(0.0, g.eval(vJag, x, 0, z)) : 0.5;
  int start = (int)std::ceil((1.5 + off + jag) * 128 - 64 + 1 + 20);
  start = std::clamp(start - start % 4 + 4, 8, DIM_H - 6);
  while (start < DIM_H - 6 && g.eval(vSloped, x, start - 1, z) > 0) start += 8;   // (solid already: an overhang higher up)
  for (int y = start; y >= 4; y -= 4) {   // (band y; Minecraft's is one less)
    if (g.eval(vSloped, x, y - 1, z) > 0) {
      top = y;
      for (int k = 3; k >= 1; k--) if (g.eval(vSloped, x, y + k - 1, z) > 0) { top = y + k; break; }
      break;
    }
  }
  e = {key, top};
  return top;
}

// ---- terrain version 15: Soulrend's biomes --------------------------------------------------------------
// the biome at a quart: TerraBlender's region there, and the climate box nearest Minecraft's climate (its noise router's
// temperature, vegetation, continents, erosion, depth, ridges at the quart's corner block, quantized as it does). The 2D
// parts are kept per quart column (depth is the offset spline plus a straight gradient in y: worked from one value)
int OverworldGen::mcNoiseBiome(int qx, int qy, int qz) const {
  struct C2 { i64 k = INT64_MIN; int region = 0; i64 t, h, c, e, w; double depth0; };
  struct C3 { i64 k = INT64_MIN; int y = 0; int b = 0; };
  thread_local std::vector<C2> c2(1 << 12); thread_local std::vector<C3> c3(1 << 14);
  thread_local const void* owner = nullptr;
  if (owner != mcb_.get()) { std::fill(c2.begin(), c2.end(), C2{}); std::fill(c3.begin(), c3.end(), C3{}); owner = mcb_.get(); }
  const i64 key = ((i64)qx << 32) ^ (u32)qz;
  C3& e3 = c3[((u64)(key * 31 + qy) * 0x9E3779B97F4A7C15ull) >> 50];
  if (e3.k == key && e3.y == qy) return e3.b;
  C2& e2 = c2[((u64)key * 0x9E3779B97F4A7C15ull) >> 52];
  const DensityGraph& g = *vg_;
  const double bx = qx * 4.0, bz = qz * 4.0;
  auto Q = [](double v) { return (i64)((float)v * 10000.0f); };
  if (e2.k != key) {
    e2.k = key; e2.region = mcb_->region(qx, qz);
    e2.t = Q(g.eval(vTemp, bx, 0, bz)); e2.h = Q(g.eval(vVeg, bx, 0, bz)); e2.c = Q(g.eval(vCont, bx, 0, bz));
    e2.e = Q(g.eval(vEro, bx, 0, bz)); e2.w = Q(g.eval(vRidges, bx, 0, bz)); e2.depth0 = g.eval(vDepth, bx, 0, bz);
  }
  const double depth = e2.depth0 - std::clamp(qy * 4.0, -64.0, 320.0) * (3.0 / 384.0);   // (y_clamped_gradient -64..320: 1.5..-1.5)
  const i64 p[6] = {e2.t, e2.h, e2.c, e2.e, Q(depth), e2.w};
  const int b = mcb_->lookup(e2.region, p);
  e3 = {key, qy, b};
  return b;
}
std::string OverworldGen::mcBiomeNameAt(int x, int y, int z) const { return v15 ? mcb_->info(mcBiomeAt(x, y, z)).name : std::string(); }
std::string OverworldGen::mcBiomeName(int x, int z) const { return v15 ? mcb_->info(rawColumn(x, z).mc).name : std::string(); }
int OverworldGen::mcBiomeAt(int x, int y, int z) const {
  int qx, qy, qz; mcb_->fuzzyQuart(x, y, z, qx, qy, qz);
  return mcNoiseBiome(qx, qy, qz);
}
// NoiseChunk.preliminarySurfaceLevel: from the top, every cell height (8), the first where the land without its jaggedness
// (initial_density_without_jaggedness) passes 0.390625; at 16-block corners, kept
int OverworldGen::prelimSurface(int x, int z) const {
  struct E { i64 k = INT64_MIN; int y = 0; };
  thread_local std::vector<E> cache(1 << 12); thread_local const void* owner = nullptr;
  if (owner != vg_.get()) { std::fill(cache.begin(), cache.end(), E{}); owner = vg_.get(); }
  const i64 key = ((i64)x << 32) ^ (u32)z;
  E& e = cache[((u64)key * 0x9E3779B97F4A7C15ull) >> 52];
  if (e.k == key) return e.y;
  int top = -64;
  for (int y = (DIM_H - 8) & ~7; y >= -64; y -= 8) if (vg_->eval(vInit, x, y, z) > 0.390625) { top = y; break; }
  e = {key, top};
  return top;
}

// ---- columns ---------------------------------------------------------------------------------------------
OverworldGen::BaseCol OverworldGen::baseColumn(int x, int z) const {
  if (v14) {
    // the land's height from Minecraft's terrain; the climate (and so the biomes, their variants, surfaces and trees) ours
    BaseCol c;
    c.H = v14Surface(x, z);
    c.hc = owSurfaceInv(c.H + 0.5); c.h = jsFloor(c.hc);
    if (v15) {   // Soulrend's biome at the ground here (Minecraft's y: ours less one)
      const int b = mcBiomeAt(x, std::max(c.H, SEA_LEVEL) - 1, z);
      c.mc = (u16)b; c.biome = mcCat_[b]; c.temp = mcTemp_[b]; c.humid = mcHumid_[b];
      c.special = mcb_->info(b).name == "minecraft:mushroom_fields" ? 1 : 0;
      return c;
    }
    c.temp = nTemp.fbm2(x / (800.0 * climateScale), z / (800.0 * climateScale), 2) - (c.h > 90 ? (c.h - 90) / 40.0 : 0);
    c.humid = nHumid.fbm2(x / (650.0 * climateScale), z / (650.0 * climateScale), 2);
    if (climateScale > 1) c.temp -= 0.06;
    if (wtype == "winter") c.temp = std::min(c.temp, -0.4) - 0.12;
    else if (wtype == "hell") { c.temp = 0.08 + c.temp * 0.25; c.humid = 0.12 + c.humid * 0.3; }
    const double cont = vg_->eval(vCont, x, 64, z), ero = vg_->eval(vEro, x, 64, z);
    const double t = c.temp, u = c.humid;
    u8 b;
    if (c.H < SEA_LEVEL) b = BIOME_OCEAN;
    else if (c.H <= SEA_LEVEL + 2 && cont < -0.05 && ero > -0.2) b = BIOME_BEACH;   // (the sea's edge where the land is low and worn)
    else if (c.H > SEA_LEVEL + 70 || (c.H > SEA_LEVEL + 45 && ero < -0.2)) b = BIOME_MOUNTAINS;   // (Minecraft's peaks: high and little worn)
    else if (t < -0.3) b = BIOME_TAIGA;
    else if (t > 0.32 && u < 0.12) b = BIOME_DESERT;
    else if (u > 0.08) b = BIOME_FOREST;
    else b = BIOME_PLAINS;
    if (b == BIOME_BEACH && t > 0.32 && u < 0.12) b = BIOME_DESERT;
    // Minecraft's mushroom fields: land rising out of the deepest sea, far from any other
    if (cont < -0.95 && c.H >= SEA_LEVEL) { b = BIOME_PLAINS; c.special = 1; }
    c.biome = b;
    return c;
  }
  double cont = nCont.fbm2(x / 900.0, z / 900.0, 4);
  double land = jsSmooth(-0.4, -0.1, cont);
  double h = JS_SEA - 16 + land * 20;
  h += nHill.fbm2(x / 140.0, z / 140.0, 3) * (3 + 9 * land);
  h += nDetail.noise2(x / 28.0, z / 28.0) * 1.6 * land;
  double mRaw = nMount.fbm2(x / 520.0, z / 520.0, 2);
  double mMask = jsSmooth(0.18, 0.55, mRaw) * land;
  if (mMask > 0) { double r = 1 - std::fabs(nRidge.fbm2(x / 230.0, z / 230.0, 3)); h += mMask * (r * r * 58 + 6); }
  BaseCol c;
  // BTA's world types: Amplified (the hills and mountains near twice as high), Islands (the land sunk: the seas risen)
  if (wtype == "amplified" && h > JS_SEA) h = JS_SEA + (h - JS_SEA) * 1.85;
  else if (wtype == "islands") h -= 11 + 4 * land;
  c.hc = jsClamp(h, 4, 120);
  c.h = jsFloor(c.hc);
  c.temp = nTemp.fbm2(x / (800.0 * climateScale), z / (800.0 * climateScale), 2) - (c.h > 90 ? (c.h - 90) / 40.0 : 0);
  c.humid = nHumid.fbm2(x / (650.0 * climateScale), z / (650.0 * climateScale), 2);
  if (climateScale > 1) c.temp -= 0.06;   // and the cold lands a little more common (BTA: more snow biomes)
  if (wtype == "winter") c.temp = std::min(c.temp, -0.4) - 0.12;             // Endless Winter: everywhere snow-bound
  else if (wtype == "hell") { c.temp = 0.08 + c.temp * 0.25; c.humid = 0.12 + c.humid * 0.3; }   // Hell: the green lands burnt (not desert: scorched earth)
  const int hh = c.h; const double t = c.temp, u = c.humid;
  u8 b;
  if (hh < JS_SEA - 1) b = BIOME_OCEAN;
  else if (hh <= JS_SEA + 1 && land < 0.98) b = BIOME_BEACH;
  else if (mMask > 0.45 && hh > JS_SEA + 24) b = BIOME_MOUNTAINS;
  else if (t < -0.3) b = BIOME_TAIGA;
  else if (t > 0.32 && u < 0.12) b = BIOME_DESERT;
  else if (u > 0.08) b = BIOME_FOREST;
  else b = BIOME_PLAINS;
  if (b == BIOME_BEACH && t > 0.32 && u < 0.12) b = BIOME_DESERT;
  c.biome = b;
  return c;
}

double OverworldGen::riverNoise(int x, int z) const {
  return nRiver.fbm2(x / 760.0, z / 760.0, 3) + nDetail.noise2(x / 90.0, z / 90.0) * 0.012;
}

OwColumn OverworldGen::rawColumn(int x, int z) const {
  BaseCol b = baseColumn(x, z);
  if (v14) {   // (its rivers are the land's own valleys)
    OwColumn c; c.h = b.h; c.biome = b.biome; c.temp = b.temp; c.humid = b.humid; c.H = b.H; c.special = b.special; c.mc = b.mc;
    return c;
  }
  OwColumn c; c.h = b.h; c.biome = b.biome; c.temp = b.temp; c.humid = b.humid;
  double frac = b.hc - b.h;
  // rivers (worldupdate.js, terrain version 3+): a bowl mid-stream, banks rising from the water's edge
  if (b.h >= JS_SEA - 1) {
    const double RW0 = 0.016, RW1 = 0.048;
    double rv = std::fabs(riverNoise(x, z));
    if (rv < RW1) {
      int h = b.h;
      if (rv < RW0) {
        double gx = std::fabs(riverNoise(x + 1, z)) - std::fabs(riverNoise(x - 1, z)), gz = std::fabs(riverNoise(x, z + 1)) - std::fabs(riverNoise(x, z - 1));
        double d = (RW0 - rv) / std::max(1e-4, std::hypot(gx, gz) / 2);
        int depth = d < 1.6 ? 0 : d < 3.2 ? 1 : d < 4.8 ? 2 : 3;
        h = std::min(h, JS_SEA - 1 - depth);
      } else {
        double k = (rv - RW0) / (RW1 - RW0);
        h = std::min(h, jsRound(JS_SEA + (h - JS_SEA) * k * k));
      }
      if (h != b.h) {
        c.h = h; frac = 0.5;
        c.river = rv < RW0 ? 1.f : 0.5f;
        if (h < JS_SEA) c.biome = BIOME_OCEAN;
        else if (h <= JS_SEA + 1 && b.biome != BIOME_DESERT) c.biome = BIOME_BEACH;
      }
    }
  }
  c.H = std::min(DIM_H - 8, jsFloor(owSurface(c.h + frac)));
  return c;
}

OwColumn OverworldGen::column(int x, int z) const {
  OwColumn c = rawColumn(x, z);
  double w; int ty;
  if (structures.active() && structures.flatten(x, z, w, ty)) {
    int H = jsRound(c.H + (ty - c.H) * w);
    if (H != c.H) {
      c.H = H; c.h = jsFloor(owSurfaceInv(H + 0.5));
      if (c.h > JS_SEA + 1 && (c.biome == BIOME_OCEAN || c.biome == BIOME_BEACH)) c.biome = BIOME_PLAINS;
    }
  }
  return c;
}

TerrainColumn OverworldGen::terrainColumn(int x, int z) const {
  OwColumn c = rawColumn(x, z);
  TerrainColumn t; t.y = c.H; t.water = c.H < SEA_LEVEL ? SEA_LEVEL : -1; t.biome = c.biome; t.temp = c.temp; t.humid = c.humid;
  t.jsY = c.h; t.river = c.river > 0; t.special = c.special; t.mc = c.mc;
  return t;
}

bool OverworldGen::goodSpawn(int x, int z) const {
  OwColumn c = column(x, z);
  if (c.biome == BIOME_OCEAN || c.biome == BIOME_BEACH || c.h <= JS_SEA || c.h > JS_SEA + 28) return false;
  static const int D[4][2] = {{4, 0}, {-4, 0}, {0, 4}, {0, -4}};
  if (wtype == "floating") for (auto& d : D) if (floatingBottom(x + d[0] * 2, z + d[1] * 2, c.H) < 0) return false;   // well inside an island
  for (auto& d : D) if (std::abs(column(x + d[0], z + d[1]).h - c.h) > 3) return false;
  return true;
}

bool OverworldGen::riverFlow(int x, int z, double& fx, double& fz) const {
  if (std::fabs(riverNoise(x, z)) >= 0.048 * 0.8) return false;
  const int e = 3;
  double gx = riverNoise(x + e, z) - riverNoise(x - e, z), gz = riverNoise(x, z + e) - riverNoise(x, z - e);
  double tx = -gz, tz = gx, l = std::hypot(tx, tz); if (l == 0) l = 1;
  tx /= l; tz /= l;
  int a = baseColumn(jsRound(x + tx * 24), jsRound(z + tz * 24)).h, b = baseColumn(jsRound(x - tx * 24), jsRound(z - tz * 24)).h;
  if (a > b) { tx = -tx; tz = -tz; }
  fx = tx; fz = tz;
  return true;
}

double OverworldGen::deepZone(int x, int z) const { return jsSmooth(0.32, 0.46, nDeep.fbm2(x / 260.0, z / 260.0, 3)); }

// The column's surface materials (generateOverworld; distant.js distTop is the same choice).
SurfaceMat OverworldGen::surface(const OwColumn& c, int wx, int wz) const {
  if (v15) {   // Soulrend's rules for the top of the land here (the far land, the map: as the chunk will be)
    McSurface::Column col;
    const int x0 = wx & ~15, z0 = wz & ~15;
    const double fx = (wx & 15) / 16.0, fz = (wz & 15) / 16.0;
    const double l0 = prelimSurface(x0, z0) + fx * (prelimSurface(x0 + 16, z0) - prelimSurface(x0, z0));
    const double l1 = prelimSurface(x0, z0 + 16) + fx * (prelimSurface(x0 + 16, z0 + 16) - prelimSurface(x0, z0 + 16));
    mcs_->begin(col, wx, wz, (int)std::floor(l0 + fz * (l1 - l0)));
    col.steep = std::abs(v14Surface(wx, wz + 1) - v14Surface(wx, wz - 1)) >= 4 || std::abs(v14Surface(wx - 1, wz) - v14Surface(wx + 1, wz)) >= 4;
    col.biomeAt = [&](int y) { return mcBiomeAt(wx, y, wz); };
    col.waterHeight = c.H < SEA_LEVEL ? SEA_LEVEL : INT32_MIN;
    BlockId got[3];
    for (int d = 0; d < 3; d++) {
      col.y = c.H - 1 - d; col.stoneAbove = d + 1; col.stoneBelow = 64;
      const BlockId r = mcs_->apply(col);
      got[d] = r != 0xFFFF ? r : B.stone;
    }
    return {got[0], got[1], got[2], 2};
  }
  const int h = c.h;
  double fr = hashFloat(wx, wz, seed_, 2);
  BlockId top = B.grass, filler = B.dirt, under = B.stone; int fillDepth = 3 + jsFloor(fr * 2);
  switch (c.biome) {
    case BIOME_DESERT: top = B.sand; filler = B.sand; fillDepth = 4; under = B.sandstone; break;
    case BIOME_BEACH: top = B.sand; filler = B.sand; fillDepth = 3; break;
    case BIOME_TAIGA: top = B.snowy_grass; break;
    case BIOME_OCEAN: {
      double f = nFloor.noise2(wx / 24.0, wz / 24.0);
      top = f > 0.45 ? B.clay : f > 0.05 ? B.sand : B.gravel; filler = top == B.clay ? B.clay : B.sand; fillDepth = 2; break;
    }
    case BIOME_MOUNTAINS:
      if (v14) {   // Minecraft's way on its land: bare stone where it's steep, snow on the heights, grass on the high meadows
        const int H0 = c.H, dx = std::abs(v14Surface(wx + 2, wz) - v14Surface(wx - 2, wz)), dz = std::abs(v14Surface(wx, wz + 2) - v14Surface(wx, wz - 2));
        const bool steep = std::max(dx, dz) >= 6;
        top = H0 > SEA_LEVEL + 120 ? B.snow_block : steep ? B.stone : H0 > SEA_LEVEL + 95 && hashFloat(wx, wz, seed_, 17) < 0.5 ? B.snowy_grass : B.grass;
        filler = steep || H0 > SEA_LEVEL + 120 ? B.stone : B.dirt;
        break;
      }
      top = h > 96 ? B.snow_block : h > 82 ? B.stone : B.grass; filler = h > 82 ? B.stone : B.dirt; break;
  }
  if ((c.biome == BIOME_PLAINS || c.biome == BIOME_FOREST) && h <= JS_SEA + 1 && c.humid > 0.38 && c.temp > -0.1) { top = B.mud; filler = B.mud; fillDepth = 2; }
  if (c.biome == BIOME_TAIGA && hashFloat(wx, wz, seed_, 13) < 0.18) top = B.podzol;
  if (c.biome == BIOME_PLAINS && c.temp > 0.3 && c.humid < -0.1 && hashFloat(wx, wz, seed_, 14) < 0.3) top = B.coarse_dirt;
  double fq = hashFloat(wx, wz, seed_, 15);
  if (top == B.grass && c.biome == BIOME_PLAINS && c.temp > 0.28 && c.humid < 0.02 && fq < 0.65) top = B.dirt_with_dry_grass;
  if (top == B.snowy_grass && c.temp > -0.42 && fq < 0.22) top = B.dirt_with_coniferous_litter;
  if (top == B.grass && c.temp > 0.25 && c.humid > 0.3 && fq < 0.3) top = B.dirt_with_rainforest_litter;
  if (c.biome == BIOME_DESERT && c.temp > 0.55) { top = B.desert_sand; filler = B.desert_sand; under = B.desert_stone; }
  if (c.biome == BIOME_BEACH && c.temp < -0.2) { top = B.silver_sand; filler = B.silver_sand; }
  switch (btaVariant(c)) {
    case BV_REDLANDS: top = fq < 0.55 ? B.red_sand : fq < 0.82 && B.scorched_dirt ? B.scorched_dirt : B.coarse_dirt; filler = B.red_sand; under = B.terracotta ? B.terracotta : B.sandstone; fillDepth = 3;
      if (fq > 0.985 && B.rich_scorched_dirt) top = B.rich_scorched_dirt;   // BTA: rich scorched dirt sifts out metal
      break;
    case BV_GLACIER: if (h <= 96) { double g = nFloor.noise2(wx / 18.0, wz / 18.0); top = g > 0.1 ? B.packed_ice : B.snow_block; filler = g > 0.3 && B.perma_ice ? B.perma_ice : B.packed_ice; under = g > 0.4 && B.blue_ice ? B.blue_ice : B.stone; fillDepth = 4; } break;
    case BV_MOSSMIRE: top = fq < 0.45 ? B.moss_block : B.mud; filler = B.mud; fillDepth = 3; break;
    case BV_THORNSCRUB: if (fq > 0.55) top = B.coarse_dirt; break;   // dry ground showing through the grass
    case BV_HIGHLANDS: if (top == B.grass || top == B.snowy_grass || top == B.dirt_with_coniferous_litter) top = fq < 0.5 && B.podzol ? B.podzol : fq < 0.62 ? B.coarse_dirt : top; break;
    case BV_MUSHROOM: if (top == B.grass || top == B.snowy_grass || top == B.sand) top = B.mycelium ? B.mycelium : top; if (filler == B.sand) filler = B.dirt; break;
    case BV_TUNDRA: if (top == B.grass) top = B.snowy_grass ? B.snowy_grass : top; if (B.permafrost) under = B.permafrost; fillDepth = 2; break;
    default: break;
  }
  if (top == B.mud) under = B.peat;
  return {top, filler, under, fillDepth};
}

// ---- terrain version 14: the blocks ----------------------------------------------------------------------
// Minecraft's final density (the land, its cliffs and overhangs, and its caves) worked out on its cells (every 4 blocks
// across, 8 up) and blended between them; a structure's levelling draws it to its ground. Solid is stone (our rock layers),
// its surface the biome's ground (and ledges and overhangs that see the sky); air at or under the sea above the land is
// water. The columns are updated to the land as built (the trees, decoration and structures go on what's there).
void OverworldGen::fillV14(GenBuf& b, OwColumn* cols, int cx, int cz) const {
  const DensityGraph& g = *vg_;
  const int bx = cx * CS, bz = cz * CS;
  constexpr int NY = DIM_H / 8 + 1;
  static thread_local std::vector<double> lat; lat.assign(5 * 5 * NY, -1);
  int hmax = 0; for (int i = 0; i < 256; i++) hmax = std::max(hmax, cols[i].H);
  const int jmax = std::min(NY - 1, hmax / 8 + 2);   // (above the highest ground here it's all air: no need to ask)
  for (int k = 0; k < 5; k++) for (int i = 0; i < 5; i++) for (int j = 0; j <= jmax; j++)
    lat[(k * 5 + i) * NY + j] = g.eval(vFinal, bx + i * 4, j * 8 - 1, bz + k * 4);
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    OwColumn& c = cols[z * 16 + x];
    const int wx = bx + x, wz = bz + z;
    const int i0 = x >> 2, k0 = z >> 2; const double fx = (x & 3) / 4.0, fz = (z & 3) / 4.0;
    double lw = 0; int lty = 0;
    const bool lev = structures.active() && structures.flatten(wx, wz, lw, lty);
    const int Hs = v14Surface(wx, wz);   // (the land without its caves: where the ground's materials go)
    // the ground is exactly where column() says (the height everything is planned on: roads, structures, trees): nothing
    // above it, its top few blocks whole; the cells below make the caves and the hollows under overhangs
    const int Hc = std::clamp(c.H, 1, DIM_H - 2);
    int top = 0;
    // (by the water the planned height can stand a block or two over the land itself: there the land's own shape wins,
    // or single columns of grass stood up out of the shallows)
    auto latD = [&](int y) {   // (the land's density at a block, from the cell corners)
      const int j0 = y >> 3; const double fy = (y & 7) / 8.0;
      auto L = [&](int di, int dj, int dk) { return lat[((k0 + dk) * 5 + (i0 + di)) * NY + std::min(NY - 1, j0 + dj)]; };
      const double c00 = L(0, 0, 0) + (L(1, 0, 0) - L(0, 0, 0)) * fx, c10 = L(0, 1, 0) + (L(1, 1, 0) - L(0, 1, 0)) * fx;
      const double c01 = L(0, 0, 1) + (L(1, 0, 1) - L(0, 0, 1)) * fx, c11 = L(0, 1, 1) + (L(1, 1, 1) - L(0, 1, 1)) * fx;
      const double d0 = c00 + (c10 - c00) * fy, d1 = c01 + (c11 - c01) * fy;
      return d0 + (d1 - d0) * fz;
    };
    const int nTop = v17 ? v14Spike(wx, wz, Hc) : Hc;   // (a lone column planned over all round it by the water: theirs)
    const bool whole = !v17 || lev || Hc != Hs || (nTop == Hc && (Hc > SEA_LEVEL + 4 || Hc / 8 > jmax || latD(Hc) > 0));   // (terrain version 17)
    for (int y = whole ? Hc : nTop; y >= (deep_ ? 0 : 1); y--) {   // (the deep world: row 0 too, Minecraft's y -1)
      if (whole && y >= Hc - 3) { b.blocks[colIdx(x, y, z)] = B.stone; if (!top) top = y; continue; }
      const int j0 = y >> 3; const double fy = (y & 7) / 8.0;
      auto L = [&](int di, int dj, int dk) { return lat[((k0 + dk) * 5 + (i0 + di)) * NY + std::min(NY - 1, j0 + dj)]; };
      const double c00 = L(0, 0, 0) + (L(1, 0, 0) - L(0, 0, 0)) * fx, c10 = L(0, 1, 0) + (L(1, 1, 0) - L(0, 1, 0)) * fx;
      const double c01 = L(0, 0, 1) + (L(1, 0, 1) - L(0, 0, 1)) * fx, c11 = L(0, 1, 1) + (L(1, 1, 1) - L(0, 1, 1)) * fx;
      const double d0 = c00 + (c10 - c00) * fy, d1 = c01 + (c11 - c01) * fy;
      double d = d0 + (d1 - d0) * fz;
      if (lev) d = d * (1 - lw) + lw * std::clamp((lty + 0.5 - y) * 0.25, -1.0, 1.0);   // (a structure's level ground)
      if (Hc != Hs && y > std::min(Hc, Hs) - 4) d = 1;   // (land raised by a structure's levelling: whole ground under it)
      if (d > 0) { b.blocks[colIdx(x, y, z)] = B.stone; if (!top) top = y; }
    }
    if (!top) top = 1;
    c.H = top; c.h = jsFloor(owSurfaceInv(top + 0.5));
    if (c.H < SEA_LEVEL && c.biome != BIOME_OCEAN && !c.special) c.biome = BIOME_OCEAN;
    else if (c.H >= SEA_LEVEL && c.biome == BIOME_OCEAN) c.biome = c.H <= SEA_LEVEL + 2 ? BIOME_BEACH : BIOME_PLAINS;
    if (v15) {   // (Soulrend's surface: surfaceV15, once the chunk's land is all here)
      if (!deep_) for (int y = 0; y < 5; y++) if (y == 0 || hf(wx, y, wz, seed_) < (5 - y) / 5.0) b.blocks[colIdx(x, y, z)] = B.bedrock;
      for (int y = top + 1; y <= SEA_LEVEL; y++) b.blocks[colIdx(x, y, z)] = B.water;
      continue;
    }
    const SurfaceMat m = surface(c, wx, wz);
    const int fd = m.fillDepth;
    // the ground's materials under every surface near the land's top that sees the sky (ledges, overhangs' tops)
    bool sky = true; int depth = -1;
    for (int y = DIM_H - 1; y >= 1; y--) {
      BlockId& here = b.blocks[colIdx(x, y, z)];
      if (here == AIR) { if (depth >= 0 && y < Hs - 8) sky = false; depth = -1; continue; }
      depth++;
      const bool nearTop = y >= Hs - 8;
      BlockId id = B.stone;
      if (sky && nearTop && depth == 0) id = m.top;
      else if (sky && nearTop && depth < fd) id = m.filler;
      else if (sky && nearTop && depth < fd + 3 && m.under == B.sandstone) id = B.sandstone;
      else if (sky && nearTop && depth < fd + 6 && m.under == B.desert_stone) id = depth < fd + 2 ? B.desert_sandstone : B.desert_stone;
      else if (sky && nearTop && depth < fd + 2 && m.under == B.peat) id = B.peat;
      else id = rockLayers ? layerRock(wx, y, wz, top) : B.stone;
      if (y < Hs - 8) sky = false;   // (deeper down: caves, no grass)
      here = id;
    }
    // bedrock (row 0 becomes the Ember Crust)
    for (int y = 0; y < 5; y++) if (y == 0 || hf(wx, y, wz, seed_) < (5 - y) / 5.0) b.blocks[colIdx(x, y, z)] = B.bedrock;
    // the sea and the lakes and rivers in the land's hollows at its level
    const bool cold = c.temp < -0.38;
    for (int y = top + 1; y <= SEA_LEVEL; y++) b.blocks[colIdx(x, y, z)] = (y == SEA_LEVEL && cold) ? B.ice : B.water;
    if (floes && c.temp < -0.5 && top < SEA_LEVEL - 3) {
      const double f = nFloor.noise2(wx / 26.0 + 77, wz / 26.0 - 31) + 0.35 * nFloor.noise2(wx / 7.0, wz / 7.0);
      if (f > 0.12) {
        const int t = std::min(5, 2 + (int)((f - 0.12) * 9));
        for (int y = std::max(top + 1, SEA_LEVEL - t + 2); y <= SEA_LEVEL + 1; y++) b.blocks[colIdx(x, y, z)] = B.packed_ice;
        if (f > 0.45 && B.snow_block) b.blocks[colIdx(x, SEA_LEVEL + 1, z)] = B.snow_block;
      } else b.blocks[colIdx(x, SEA_LEVEL, z)] = B.water;
    }
  }
  if (v17) {   // by the water: a lone column standing 2+ over all four round it (as built; past the chunk's edge, as planned) is cut down to them
    for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
      OwColumn& c = cols[z * 16 + x];
        if (c.H > SEA_LEVEL + 4 || c.H <= SEA_LEVEL) continue;
      int m = 0;
      for (int k = 0; k < 4; k++) {
        const int nx = x + DIRX[k], nz = z + DIRZ[k];
        m = std::max(m, nx >= 0 && nx < 16 && nz >= 0 && nz < 16 ? cols[nz * 16 + nx].H : v14Surface(bx + nx, bz + nz));
      }
      if (c.H < m + 2) continue;
      for (int y = c.H; y > m; y--) b.blocks[colIdx(x, y, z)] = y <= SEA_LEVEL ? B.water : AIR;
      c.H = std::max(1, m);
    }
  }
  if (v15) surfaceV15(b, cols, cx, cz);
}

// terrain version 15: Soulrend's surface, as Minecraft's SurfaceSystem.buildSurface lays it: each column walked down from its
// top, every stone block given to the surface rules with how deep it is under the open air or water above (and over the
// air or water under it), the water's height, the slope (the chunk's heightmap), its biome there; what they don't change
// stays rock (our layers). The sea's top freezes where its biome is cold enough to snow (Minecraft's freeze_top_layer).
void OverworldGen::surfaceV15(GenBuf& b, OwColumn* cols, int cx, int cz) const {
  const int bx = cx * CS, bz = cz * CS;
  int hm[256];   // WORLD_SURFACE_WG: the highest block that isn't air (Minecraft's y)
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    int y = DIM_H - 1; while (y > 0 && b.blocks[colIdx(x, y, z)] == AIR) y--;
    hm[z * 16 + x] = y - 1;
  }
  // the preliminary surface at the chunk's 16-block cell corners (a chunk is one cell)
  const int p00 = prelimSurface(bx, bz), p10 = prelimSurface(bx + 16, bz), p01 = prelimSurface(bx, bz + 16), p11 = prelimSurface(bx + 16, bz + 16);
  // the spires, icebergs and pillars first (the rules then lay the ground over them), the heights again after
  int ground0[256];   // (the land's top before the extras: what rises over it is their plain stone, not our rock layers)
  for (int i = 0; i < 256; i++) { int y = DIM_H - 1; while (y > 0 && (b.blocks[colIdx(i & 15, y, i >> 4)] == AIR || b.blocks[colIdx(i & 15, y, i >> 4)] == B.water)) y--; ground0[i] = y; }
  if (v16) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const float fx = x / 16.0f, fz = z / 16.0f;
    const double l0 = p00 + fx * (double)(p10 - p00), l1 = p01 + fx * (double)(p11 - p01);
    surfaceExtras(b, x, z, bx + x, bz + z, hm[z * 16 + x] + 2, (int)std::floor(l0 + fz * (l1 - l0)));
    int y = DIM_H - 1; while (y > 0 && b.blocks[colIdx(x, y, z)] == AIR) y--;
    hm[z * 16 + x] = y - 1;
  }
  McSurface::Column col;
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    OwColumn& c = cols[z * 16 + x];
    const int wx = bx + x, wz = bz + z;
    const float fx = (x & 15) / 16.0f, fz = (z & 15) / 16.0f;
    const double l0 = p00 + fx * (double)(p10 - p00), l1 = p01 + fx * (double)(p11 - p01);
    mcs_->begin(col, wx, wz, (int)std::floor(l0 + fz * (l1 - l0)));
    {   // SteepMaterialCondition (within the chunk, as Minecraft)
      const int zN = std::max(z - 1, 0), zS = std::min(z + 1, 15), xW = std::max(x - 1, 0), xE = std::min(x + 1, 15);
      col.steep = hm[zS * 16 + x] >= hm[zN * 16 + x] + 4 || hm[z * 16 + xW] >= hm[z * 16 + xE] + 4;
    }
    col.biomeAt = [&](int y) { return mcBiomeAt(wx, y, wz); };
    int stoneAbove = 0, waterHeight = INT32_MIN, stoneBelowY = INT32_MAX;
    const int top = hm[z * 16 + x] + 2;   // (band y: the air above the highest block)
    for (int y = std::min(top, DIM_H - 1); y >= (deep_ ? 0 : 1); y--) {
      const BlockId here = b.blocks[colIdx(x, y, z)];
      const int my = y - 1;
      if (here == AIR) { stoneAbove = 0; waterHeight = INT32_MIN; continue; }
      if (here == B.water || here == B.lava) { if (waterHeight == INT32_MIN) waterHeight = my + 1; continue; }
      if (stoneBelowY >= my) {   // (the bottom of this run of solid ground)
        stoneBelowY = -100000;
        for (int k = y - 1; k >= 0; k--) { const BlockId u = b.blocks[colIdx(x, k, z)]; if (u == AIR || u == B.water || u == B.lava) { stoneBelowY = k; break; } }   // (Minecraft's y of the block above it)
      }
      col.y = my; col.stoneAbove = ++stoneAbove; col.stoneBelow = my - stoneBelowY + 1; col.waterHeight = waterHeight;
      if (here != B.stone) continue;
      const BlockId r = mcs_->apply(col);
      b.blocks[colIdx(x, y, z)] = r != 0xFFFF ? r : (rockLayers && (!v17 || y <= ground0[z * 16 + x]) ? layerRock(wx, y, wz, c.H) : B.stone);
    }
    // the sea's top, frozen where it's cold enough to snow
    if (b.blocks[colIdx(x, SEA_LEVEL, z)] == B.water && b.blocks[colIdx(x, SEA_LEVEL + 1, z)] == AIR && mcb_->coldEnoughToSnow(mcBiomeAt(wx, SEA_LEVEL - 1, wz), wx, SEA_LEVEL - 1, wz))
      b.blocks[colIdx(x, SEA_LEVEL, z)] = B.ice;
  }
}

// a column's extras before its surface (Minecraft's y throughout; top: WORLD_SURFACE_WG's height + 1, as buildSurface has it):
// SurfaceSystem.erodedBadlandsExtension and frozenOceanExtension, Nature's Spirit's placeSugiPillar and
// placeStratifiedDesertPillar. Each raises stone (or ice) from the ground up to a height its noises give.
void OverworldGen::surfaceExtras(GenBuf& b, int x, int z, int wx, int wz, int top, int prelim) const {
  const DensityGraph& g = *vg_;
  const int bio = mcBiomeAt(wx, top, wz);
  auto N = [&](int k, double px, double pz) { return nX_[k] >= 0 ? g.noiseValue(nX_[k], px, 0, pz) : 0.0; };
  auto at = [&](int my) -> BlockId& { return b.blocks[colIdx(x, std::clamp(my + 1, 0, DIM_H - 1), z)]; };
  const bool built = structures.active() && structures.flagged(wx, wz, 0, SF_SURFACE);   // (no spire or pillar through a village)
  auto fill = [&](int j, bool water) {   // stone from j down through the air (and the water) to the ground
    if (built) return;
    for (int k = std::min(j, DIM_H - 2); k >= 0; k--) { BlockId& h = at(k); if (h == AIR || (water && h == B.water)) h = B.stone; else break; }
  };
  if (bio == bEroded_) {
    const double e = std::min(std::abs(N(0, wx, wz) * 8.25), N(1, wx * 0.2, wz * 0.2) * 15.0);
    if (e > 0) {
      const double f = std::abs(N(2, wx * 0.75, wz * 0.75) * 1.5);
      const int i = (int)std::floor(64.0 + std::min(e * e * 2.5, std::ceil(f * 50.0) + 24.0));
      if (top <= i) {
        bool wet = false;
        for (int j = std::min(i, DIM_H - 2); j >= 0; j--) { const BlockId h = at(j); if (h == B.stone) break; if (h == B.water) { wet = true; break; } }
        if (!wet) fill(i, false);
      }
    }
  }
  if (bio == bFrozen_ || bio == bDeepFrozen_) {
    const double e = std::min(std::abs(N(3, wx, wz) * 8.25), N(4, wx * 1.28, wz * 1.28) * 15.0);
    if (e > 1.8) {
      const double f = std::abs(N(5, wx * 1.17, wz * 1.17) * 1.5);
      double gg = std::min(e * e * 1.2, std::ceil(f * 40.0) + 14.0), h;
      const int sea = SEA_LEVEL - 1;
      if (gg > 2.0) { h = sea - gg - 7.0; gg += sea; } else { gg = 0; h = 0; }
      u64 rs = v18 ? (u64)(posHashF(seed_, wx, wz, 0x1CE) * 9007199254740992.0) * 0x9E3779B97F4A7C15ull : (u64)hash32(seed_, wx, wz, 0x1CE) * 0x9E3779B97F4A7C15ull;
      auto rnd = [&]() { rs = rs * 6364136223846793005ull + 1442695040888963407ull; return (double)(rs >> 11) / 9007199254740992.0; };
      const int j = 2 + (int)(rnd() * 4), kk = sea + 18 + (int)(rnd() * 10);
      int l = 0;
      McSurface::Column col; mcs_->begin(col, wx, wz, prelim);
      for (int m = std::max(top, (int)gg + 1); m >= col.minSurface && m >= 0; m--) {
        if (m + 1 >= DIM_H) continue;
        BlockId& here = at(m);
        const bool air = here == AIR && m < (int)gg && rnd() > 0.01;
        const bool water = !air && here == B.water && m > (int)h && m < sea && h != 0.0 && rnd() > 0.15;
        if (!air && !water) continue;
        if (l <= j && m > kk) { here = B.snow_block; l++; } else here = B.packed_ice;
      }
    }
  }
  if (bio >= 0 && (bio == bSugi_ || bio == bSugi2_ || bio == bStrat_ || bio == bLively_ || bio == bBlooming_)) {
    int o = 0; for (int y = DIM_H - 1; y > 0; y--) { const BlockId h = b.blocks[colIdx(x, y, z)]; if (h != AIR && h != B.water && reg().tSolid[h]) { o = y + 1; break; } }   // (OCEAN_FLOOR_WG's height, + 1: Nature's Spirit's)
    if (bio == bSugi_ || bio == bSugi2_) {
      const double e = std::min(std::abs(N(6, wx, wz) * 8.5), N(8, wx * 0.2, wz * 0.2) * 12.0);
      const double hh = std::abs(N(7, wx * 0.9, wz * 0.8) * 2.05);
      const int j = (int)std::floor(32.0 + std::min(e * e * 6.75, std::ceil(hh * 30.0) + 48.0));
      if (e > -10.0 && o <= j) fill(j, true);
    } else {
      const double e = std::min(std::abs(N(9, wx, wz) * 8.5), N(10, wx * 0.2, wz * 0.2) * 14.0);
      const double hh = std::abs(N(11, wx * 0.75, wz * 0.75) * 2.25);
      const int j = (int)std::floor(54.0 + std::min(e * e * 3.5, std::ceil(hh * 30.0) + 38.0));
      if (e > 0 && o <= j) fill(j, true);
    }
  }
}

// terrain version 14+: is the planned ground height (column().H) solid land here? Always away from the water; by it, only
// where the land's density says so (fillV14 and the trees both ask)
int OverworldGen::v14Spike(int x, int z, int H) const {   // H, or by the water the highest of the four round it when H stands 2+ over them all
  if (H > SEA_LEVEL + 4) return H;
  int m = 0; for (int k = 0; k < 4; k++) m = std::max(m, v14Surface(x + DIRX[k], z + DIRZ[k]));
  return H >= m + 2 ? m : H;
}

bool OverworldGen::v14Whole(int x, int z, int H) const {
  if (H > SEA_LEVEL + 4) return true;
  if (v14Spike(x, z, H) != H) return false;
  // (as fillV14 has it: the density at its 4x8x4 cell's corners, blended)
  const DensityGraph& g = *vg_;
  const int x0 = x & ~3, z0 = z & ~3, j0 = H >> 3;
  const double fx = (x - x0) / 4.0, fz = (z - z0) / 4.0, fy = (H & 7) / 8.0;
  auto L = [&](int di, int dj, int dk) { return g.eval(vFinal, x0 + di * 4, (j0 + dj) * 8 - 1, z0 + dk * 4); };
  const double c00 = L(0, 0, 0) + (L(1, 0, 0) - L(0, 0, 0)) * fx, c10 = L(0, 1, 0) + (L(1, 1, 0) - L(0, 1, 0)) * fx;
  const double c01 = L(0, 0, 1) + (L(1, 0, 1) - L(0, 0, 1)) * fx, c11 = L(0, 1, 1) + (L(1, 1, 1) - L(0, 1, 1)) * fx;
  const double d0 = c00 + (c10 - c00) * fy, d1 = c01 + (c11 - c01) * fy;
  return d0 + (d1 - d0) * fz > 0;
}

// ---- generate ---------------------------------------------------------------------------------------------
void OverworldGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  const int bx = ch.cx * CS, bz = ch.cz * CS;
  OwColumn cols[256];
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) cols[z * 16 + x] = column(bx + x, bz + z);
  for (int i = 0; i < 256; i++) {
    ch.climate[i * 2] = (u8)jsClamp(jsRound(cols[i].temp * 127 + 128), 0, 255);
    ch.climate[i * 2 + 1] = (u8)jsClamp(jsRound(cols[i].humid * 127 + 128), 0, 255);
  }
  Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 1));

  if (v14) fillV14(b, cols, ch.cx, ch.cz);
  else for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const OwColumn& c = cols[z * 16 + x];
    const int H = c.H, wx = bx + x, wz = bz + z;
    double fr = hashFloat(wx, wz, seed_, 2);
    SurfaceMat m = surface(c, wx, wz);
    const int fd = m.fillDepth, slateTop = 10 + jsFloor(fr * 4);
    for (int y = 0; y <= H; y++) {
      BlockId id;
      double yo = owCaveO(y, H, c.h);
      // a jagged bedrock floor (Minecraft: y 0 always, then 4/5, 3/5, 2/5, 1/5); row 0 becomes the Ember Crust
      if (y == 0 || (y < 5 && hf(wx, y, wz, seed_) < (5 - y) / 5.0)) id = B.bedrock;
      else if (y == H) id = m.top;
      else if (y > H - fd) id = m.filler;
      else if (y > H - fd - 3 && m.under == B.sandstone) id = B.sandstone;
      else if (y > H - fd - 6 && m.under == B.desert_stone) id = y > H - fd - 2 ? B.desert_sandstone : B.desert_stone;
      else if (y > H - fd - 2 && m.under == B.peat) id = B.peat;
      else id = rockLayers ? layerRock(wx, y, wz, H) : yo < slateTop ? B.slate : B.stone;
      b.blocks[colIdx(x, y, z)] = id;
    }
    const bool cold = c.temp < -0.38;
    for (int y = H + 1; y <= SEA_LEVEL; y++) b.blocks[colIdx(x, y, z)] = (y == SEA_LEVEL && cold) ? B.ice : B.water;
    if (floes && c.temp < -0.5 && H < SEA_LEVEL - 3) {   // the seas by the glaciers: great floes of packed ice, open water between
      const double f = nFloor.noise2(wx / 26.0 + 77, wz / 26.0 - 31) + 0.35 * nFloor.noise2(wx / 7.0, wz / 7.0);
      if (f > 0.12) {
        const int t = std::min(5, 2 + (int)((f - 0.12) * 9));
        for (int y = std::max(H + 1, SEA_LEVEL - t + 2); y <= SEA_LEVEL + 1; y++) b.blocks[colIdx(x, y, z)] = B.packed_ice;
        if (f > 0.45 && B.snow_block) b.blocks[colIdx(x, SEA_LEVEL + 1, z)] = B.snow_block;
      } else b.blocks[colIdx(x, SEA_LEVEL, z)] = B.water;
    }
  }

  if (!v14) carve(b, cols, ch.cx, ch.cz);   // (terrain version 14: Minecraft's own caves are in the land)
  placeOres(b, ch.cx, ch.cz, rnd);
  placeLakes(b, cols, ch.cx, ch.cz);
  placeSprings(b, cols, ch.cx, ch.cz);
  placeTrees(b, ch.cx, ch.cz);
  decorate(b, cols, ch.cx, ch.cz);
  placeOddities(b, ch.cx, ch.cz);
  placeDungeons(b, ch, cols);
  // the Ember Crust at the bottom of the Overworld (worldupdate.js), then the structures, then the Deep Dark
  if (B.ember_crust && !deep_) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) b.blocks[colIdx(x, 0, z)] = B.ember_crust;   // (the deep world: at the Underworld's floor)
  carveDeep(b, ch.cx, ch.cz);   // (before the structures: run after, it ate into anything built down there — stone to slate, sculk and caves)
  structures.build(b, ch);
  if (!wtype.empty() && wtype != "default" && wtype != "large_biomes") worldTypePass(b, cols, ch.cx, ch.cz);
  b.store(ch);
}

// ---- signs of the watcher: straight tunnels, sand pyramids in the sea, lone old torches --------------------
namespace {
struct Oddity { bool pyramid; int x, y, z, dx, dz, len; bool torch; int base, s; };
}
void OverworldGen::placeOddities(GenBuf& b, int cx, int cz) const {
  const int bx = cx * 16, bz = cz * 16;
  auto natural = [&](BlockId id) {
    return id == B.stone || id == B.dirt || id == B.grass || id == B.gravel || id == B.sand || id == B.sandstone || id == B.snowy_grass ||
           id == B.snow_block || id == B.clay || id == B.coal_ore || id == B.iron_ore || id == B.tall_grass;
  };
  auto inC = [&](int x, int z) { return x >= bx && x < bx + 16 && z >= bz && z < bz + 16; };
  static const int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  std::vector<Oddity> list;
  for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
    const int ocx = cx + dx, ocz = cz + dz;
    list.clear();
    Mulberry32 rnd(hash32(seed_, ocx, ocz, 66));
    if (rnd() < 0.05) {
      int x = ocx * 16 + jsFloor(rnd() * 16), z = ocz * 16 + jsFloor(rnd() * 16);
      OwColumn c = column(x, z);
      int s0 = jsFloor(rnd() * 4), len = 10 + jsFloor(rnd() * 22); bool torch = rnd() < 0.5;
      if (c.h > JS_SEA + 1) for (int k = 0; k < 4; k++) {
        const int* d = DIRS[(s0 + k) % 4];
        if (column(x + d[0] * 4, z + d[1] * 4).h >= c.h + 5) { list.push_back({false, x, c.H + 1, z, d[0], d[1], len, torch, 0, 0}); break; }
      }
    }
    if (rnd() < 0.025) {
      int x = ocx * 16 + jsFloor(rnd() * 16), z = ocz * 16 + jsFloor(rnd() * 16), y = 14 + jsFloor(rnd() * 28);
      const int* d = DIRS[jsFloor(rnd() * 4)];
      int len = 20 + jsFloor(rnd() * 26); bool torch = rnd() < 0.35;
      list.push_back({false, x, owUnder(y), z, d[0], d[1], len, torch, 0, 0});
    }
    if (rnd() < 0.014) {
      int x = ocx * 16 + jsFloor(rnd() * 16), z = ocz * 16 + jsFloor(rnd() * 16);
      OwColumn c = column(x, z);
      if (c.biome == BIOME_OCEAN && c.h <= JS_SEA - 2) list.push_back({true, x, 0, z, 0, 0, 0, false, c.H, (int)jsClamp(SEA_LEVEL - c.H + 2, 3, 7)});
    }
    for (const Oddity& o : list) {
      if (!o.pyramid) {
        for (int i = 0; i < o.len; i++) {
          int x = o.x + o.dx * i, z = o.z + o.dz * i;
          if (!inC(x, z)) continue;
          for (int y : {o.y, o.y + 1}) {
            if (y < 2 || y >= DIM_H) continue;
            BlockId& cur = b.blocks[colIdx(x - bx, y, z - bz)];
            if (natural(cur)) cur = AIR;
          }
          if (o.torch && i == o.len - 1 && o.y >= 1 && o.y < DIM_H) {
            int k = colIdx(x - bx, o.y, z - bz);
            if (b.blocks[k] == AIR && B.solid[b.blocks[colIdx(x - bx, o.y - 1, z - bz)]]) b.blocks[k] = B.old_torch;
          }
        }
      } else {
        for (int k = 0; k <= o.s; k++) {
          int r = o.s - k, y = o.base + 1 + k;
          if (y >= DIM_H) break;
          for (int zz = o.z - r; zz <= o.z + r; zz++) for (int xx = o.x - r; xx <= o.x + r; xx++) {
            if (!inC(xx, zz)) continue;
            for (int yy = y; yy > 1; yy--) {     // down to the sea floor so it never floats
              BlockId& cur = b.blocks[colIdx(xx - bx, yy, zz - bz)];
              if (yy < y && cur != B.water && cur != AIR) break;
              cur = B.sand; b.meta[colIdx(xx - bx, yy, zz - bz)] = 0;
            }
          }
        }
      }
    }
  }
  // a lone old torch somewhere in this chunk's caves
  Mulberry32 r(hash32(seed_, cx, cz, 68));
  if (r() < 0.06) for (int tries = 0; tries < 24; tries++) {
    int x = jsFloor(r() * 16), z = jsFloor(r() * 16), y = owUnder(12 + jsFloor(r() * 40));
    if (b.blocks[colIdx(x, y, z)] == AIR && b.blocks[colIdx(x, y + 1, z)] == AIR && b.blocks[colIdx(x, y - 1, z)] == B.stone) { b.blocks[colIdx(x, y, z)] = B.old_torch; break; }
  }
}

// ---- the Deep Dark (deepdark.js): deepslate and sculk-grown halls far down under some of the land ---------
void OverworldGen::carveDeep(GenBuf& b, int cx, int cz) const {
  const int bx = cx * 16, bz = cz * 16;
  bool any = false;
  static const int P[5][2] = {{0, 0}, {15, 0}, {0, 15}, {15, 15}, {8, 8}};
  for (auto& p : P) if (deepZone(bx + p[0], bz + p[1]) > 0) { any = true; break; }
  if (!any) return;
  const double DEEP_TOP = 34;
  auto rockish = [&](BlockId id) { return id == B.stone || id == B.slate || id == B.granite || id == B.diorite || id == B.andesite || id == B.tuff || id == B.dirt || id == B.gravel || id == B.cobbled_slate; };
  const int yTop = owUnder(DEEP_TOP + 4);
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    const int wx = bx + lx, wz = bz + lz;
    double zone = deepZone(wx, wz);
    if (zone <= 0) continue;
    const int H = column(wx, wz).H;
    const bool city = structures.active() && structures.flagged(wx, wz, 0, SF_NO_CARVE);
    // the rock turns to deepslate, and great halls open in it
    for (int y = owUnder(12); y < yTop; y++) {
      const double yo = owUnderO(y);
      BlockId& id = b.blocks[colIdx(lx, y, lz)];
      if (id == B.stone && yo < DEEP_TOP + jsTrunc(hf(wx, y, wz, 31) * 4)) id = B.slate;
      if (city || yo > DEEP_TOP - 2 || y >= H - 16) continue;
      double v = nDeepC.noise3(wx / 30.0, yo / 11, wz / 30.0) + nDeepC.noise3(wx / 11.0, yo / 7, wz / 11.0) * 0.25;
      if (v + (zone - 1) * 0.7 - std::max(0.0, yo - 26) * 0.06 > 0.2 && !B.liquid[id]) id = AIR;
    }
    // sculk over the floors, veins up the walls and over the roof
    for (int y = owUnder(13); y < owUnder(DEEP_TOP); y++) {
      const int i = colIdx(lx, y, lz);
      if (b.blocks[i] != AIR) continue;
      const double yo = owUnderO(y);
      BlockId below = b.blocks[colIdx(lx, y - 1, lz)], above = b.blocks[colIdx(lx, y + 1, lz)];
      double r = hf(wx, y, wz, seed_ ^ 0x5c1);
      bool patch = nDeepC.noise3(wx / 9.0, yo / 9, wz / 9.0) > -0.15 + (1 - zone) * 0.6;
      if (rockish(below) && patch) {
        b.blocks[colIdx(lx, y - 1, lz)] = B.sculk;
        if (city) continue;
        if (r < 0.010) b.blocks[i] = B.sculk_sensor;
        else if (r < 0.014) { b.blocks[i] = B.sculk_shrieker; b.meta[i] = 1; }
        else if (r < 0.0155) b.blocks[colIdx(lx, y - 1, lz)] = B.sculk_catalyst;
        else if (r < 0.2) b.blocks[i] = B.sculk_vein;
      } else if (rockish(above) && patch && r < 0.35) b.blocks[colIdx(lx, y + 1, lz)] = B.sculk;
    }
  }
}

// ---- distant terrain ---------------------------------------------------------------------------------------
void OverworldGen::sample(int wx, int wz, ColumnSample& s) const {
  if (v14) { wx &= ~3; wz &= ~3; }   // (the far land: its cell corner, the exact height Minecraft worked out there)
  OwColumn c = column(wx, wz);
  SurfaceMat m = surface(c, wx, wz);
  s.height = (i16)c.H; s.top = m.top; s.under = m.top == B.grass ? B.dirt : m.filler;
  s.water = -1; s.fluid = AIR;
  if (c.H < SEA_LEVEL) {
    s.water = SEA_LEVEL; s.fluid = c.temp < -0.38 ? B.ice : B.water;
    if (v15) s.fluid = mcb_->coldEnoughToSnow(c.mc, wx, SEA_LEVEL - 1, wz) ? B.ice : B.water;
  }
  s.temp = (u8)jsClamp(jsRound(c.temp * 127 + 128), 0, 255); s.humid = (u8)jsClamp(jsRound(c.humid * 127 + 128), 0, 255);
  s.biome = c.biome; s.variant = (u8)btaVariant(c); s.tree = 0; s.treeTop = 0; s.leaves = AIR; s.floatBottom = -1; s.tint = 0xFFFFFFFF;
  if (wtype == "hell") {   // far off too: scorched ground, lava seas, no leaves
    if (B.scorched_dirt && (s.top == B.grass || s.top == B.dirt || s.top == B.snowy_grass || s.top == B.podzol)) s.top = B.scorched_dirt;
    if (s.water >= 0) s.fluid = B.lava;
    return;
  }
  if (wtype == "floating") {
    const int fb = floatingBottom(wx, wz, c.H);
    if (fb < 0) { s.height = -1; s.water = -1; return; }
    s.floatBottom = (i16)fb; s.water = -1;
  }
  // far off, single trees can't be told apart: forest cover as tall as its trees (distant.js)
  if (c.h >= JS_SEA && c.biome != BIOME_DESERT && c.biome != BIOME_BEACH && !(c.biome == BIOME_MOUNTAINS && c.h > 82)) {
    bool jungle = c.temp > 0.25 && c.humid > 0.3, sav = c.biome == BIOME_PLAINS && c.temp > 0.28 && c.humid < 0.02;
    double per = c.biome == BIOME_FOREST ? 7 : c.biome == BIOME_PLAINS ? 0.6 : c.biome == BIOME_TAIGA ? 4 : c.biome == BIOME_MOUNTAINS ? 0.8 : 0;
    if (jungle) per = 9;
    if (sav) per = 0.9;
    double cover = std::min(1.0, per * 22 / 256);
    if (cover > 0.02) {
      bool taiga = c.biome == BIOME_TAIGA || c.biome == BIOME_MOUNTAINS;
      s.tree = jungle ? TREE_JUNGLE : taiga ? TREE_SPRUCE : TREE_OAK;
      s.leaves = jungle ? B.jungle_leaves : taiga ? B.spruce_leaves : B.leaves;
      s.treeTop = (i16)(c.H + jsRound(cover * (jungle ? 9 : taiga ? 7 : 5)));
    }
  }
}

} // namespace dq
