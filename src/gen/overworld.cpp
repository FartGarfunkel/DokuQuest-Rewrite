// gen/overworld.cpp — Overworld terrain: columns (worldgen.js column + worldupdate.js rivers + structure
// levelling), the block columns, caves, ores (+ metals.js), the watcher's oddities, the Deep Dark
// (deepdark.js) and the Ember Crust. Heights/depths go through gen/gendefs.h vmap.
#include "gen/overworld.h"

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
  jigsawVillages = o.genVersion >= 9;
  villageStyles = o.genVersion >= 10;
  villagePlan11 = o.genVersion >= 11;
  rockLayers = o.genVersion >= 12;
  looseGround = o.looseGround;
  floes = o.genVersion >= 5;   // terrain version 5: BTA's ice floes in the seas round the glaciers   // terrain version 4 (BTA): biomes about twice as big
  B.resolve();
  initUnderground();
}

// ---- columns ---------------------------------------------------------------------------------------------
OverworldGen::BaseCol OverworldGen::baseColumn(int x, int z) const {
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
  t.jsY = c.h; t.river = c.river > 0;
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
    case BV_TUNDRA: if (top == B.grass) top = B.snowy_grass ? B.snowy_grass : top; if (B.permafrost) under = B.permafrost; fillDepth = 2; break;
    default: break;
  }
  if (top == B.mud) under = B.peat;
  return {top, filler, under, fillDepth};
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

  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const OwColumn& c = cols[z * 16 + x];
    const int H = c.H, wx = bx + x, wz = bz + z;
    double fr = hashFloat(wx, wz, seed_, 2);
    SurfaceMat m = surface(c, wx, wz);
    const int fd = m.fillDepth, slateTop = 10 + jsFloor(fr * 4);
    for (int y = 0; y <= H; y++) {
      BlockId id;
      double yo = owCaveO(y, H, c.h);
      // a jagged bedrock floor (Minecraft: y 0 always, then 4/5, 3/5, 2/5, 1/5); row 0 becomes the Ember Crust
      if (y == 0 || (y < 5 && hashFloat(wx, y, wz, seed_) < (5 - y) / 5.0)) id = B.bedrock;
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

  carve(b, cols, ch.cx, ch.cz);
  placeOres(b, ch.cx, ch.cz, rnd);
  placeLakes(b, cols, ch.cx, ch.cz);
  placeSprings(b, cols, ch.cx, ch.cz);
  placeTrees(b, ch.cx, ch.cz);
  decorate(b, cols, ch.cx, ch.cz);
  placeOddities(b, ch.cx, ch.cz);
  placeDungeons(b, ch, cols);
  // the Ember Crust at the bottom of the Overworld (worldupdate.js), then the structures, then the Deep Dark
  if (B.ember_crust) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) b.blocks[colIdx(x, 0, z)] = B.ember_crust;
  structures.build(b, ch);
  carveDeep(b, ch.cx, ch.cz);
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
      if (id == B.stone && yo < DEEP_TOP + jsTrunc(hashFloat(wx, y, wz, 31) * 4)) id = B.slate;
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
      double r = hashFloat(wx, y, wz, seed_ ^ 0x5c1);
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
  OwColumn c = column(wx, wz);
  SurfaceMat m = surface(c, wx, wz);
  s.height = (i16)c.H; s.top = m.top; s.under = m.top == B.grass ? B.dirt : m.filler;
  s.water = -1; s.fluid = AIR;
  if (c.H < SEA_LEVEL) { s.water = SEA_LEVEL; s.fluid = c.temp < -0.38 ? B.ice : B.water; }
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
