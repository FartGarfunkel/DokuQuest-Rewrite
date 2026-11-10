// gen/structures_aether.cpp — The Aether's three dungeons, for terrain version 7's Aether islands (docs/research/aether.md 3.9):
//  * Bronze: a carved-stone boss hall deep inside a thick island (sentry stones in its walls), a chest room beside it
//    and a holystone tunnel out to the sky. The Slider keeps it. One in about each 6 x 6 chunks where an island is thick.
//  * Silver: a temple of angelic stone standing in the open sky on a bed of cold aerclouds: a colonnade of pillars, a
//    3 x 3 x 3 block of 7-block rooms (ladders between the floors, chests, gold-lit stones) and the boss hall behind it,
//    where the Valkyrie Queen waits with her guard. Three in four of the far sky regions.
//  * Gold: a floating island of its own, 38 across, with stub islets round it, gumdrop caves through it and golden
//    oaks on top; a hellfire-stone hall inside, reached by a tunnel, where the Sun Spirit burns. One in four regions.
// The shapes are the Aether's (sizes measured from its templates); the blocks are ours (tools/port/aether_blocks.py).
#include "gen/structures_common.h"
#include <cmath>

namespace dq {

namespace {
struct AK {
  BlockId carved, sentry, holy, mossy, angelic, angelicLit, hellfire, hellfireLit, pillar, pillarTop, cold, dirt, grass,
          goldLog, goldLeaf, glow, water, chest, white, purple, tallGrass;
};
const AK& ak() {
  static const AK k = [] {
    auto id = [](const char* a, const char* b) { BlockId r = bid(a); return r != NOBLOCK ? r : bid(b); };
    AK r;
    r.carved = id("carved_stone", "stone_bricks"); r.sentry = id("sentry_stone", "sea_lantern");
    r.holy = id("holystone", "stone"); r.mossy = id("mossy_holystone", "mossy_cobblestone");
    r.angelic = id("angelic_stone", "quartz_block"); r.angelicLit = id("light_angelic_stone", "glowstone");
    r.hellfire = id("hellfire_stone", "red_nether_bricks"); r.hellfireLit = id("light_hellfire_stone", "shroomlight");
    r.pillar = id("pillar", "quartz_pillar"); r.pillarTop = id("pillar_top", "chiseled_quartz_block");
    r.cold = id("cold_aercloud", "cloud_puff"); r.dirt = id("aether_dirt", "dirt"); r.grass = id("aether_grass_block", "grass_block");
    r.goldLog = id("golden_oak_log", "oak_log"); r.goldLeaf = id("golden_oak_leaves", "oak_leaves");
    r.glow = id("glowstone", "sea_lantern"); r.water = id("water", "air"); r.chest = id("chest", "air");
    r.white = id("white_flower", "oxeye_daisy"); r.purple = id("purple_flower", "allium"); r.tallGrass = id("tall_grass", "air");
    return r;
  }();
  return k;
}
constexpr int DY = SkylandsGen::AETHER_DY;

// the Aether's treasure: what each dungeon's chests hold (only what this game has)
json aetherLoot(int tier, Mulberry32& r) {
  static const std::vector<std::pair<const char*, int>> T[3] = {
    {{"zanite_ore", 6}, {"ambrosium_ore", 10}, {"gravitite_ore", 3}, {"golden_apple", 1}, {"iron_ingot", 6}, {"gold_ingot", 4},
     {"arrow", 16}, {"bow", 1}, {"saddle", 1}, {"enchanted_book", 1}, {"music_disc_skylark", 1}, {"golden_feather", 1}},
    {{"gravitite_ore", 4}, {"zanite_ore", 8}, {"golden_apple", 2}, {"diamond", 2}, {"gold_ingot", 6}, {"aether_crystal", 6},
     {"golden_feather", 1}, {"enchanted_book", 1}, {"experience_bottle", 4}, {"emerald", 6}, {"valkyrie_lance", 1}, {"heart_crystal", 1}},
    {{"gravitite_ore", 6}, {"diamond", 3}, {"golden_apple", 3}, {"gold_block", 2}, {"emerald", 10}, {"aether_crystal", 8},
     {"heart_crystal", 1}, {"enchanted_book", 2}, {"experience_bottle", 6}, {"golden_feather", 2}, {"mithril_ingot", 2}, {"fire_charge", 8}}};
  json items = json::array(); for (int s = 0; s < 27; s++) items.push_back(nullptr);
  const auto& L = T[std::clamp(tier, 0, 2)];
  const int n = 5 + (int)(r() * 4) + tier;
  for (int k = 0; k < n; k++) {
    const auto& [key, mx] = L[(int)(r() * L.size())];
    if (!hasItem(key)) continue;
    items[(int)(r() * 27)] = json{{"id", key}, {"n", 1 + (int)(r() * mx)}};
  }
  return items;
}
void aetherChest(SP& P, int x, int y, int z, int f, int tier) {
  const int i = P.putId(x, y, z, ak().chest, (u8)((2 - f) & 3));
  if (i < 0) return;
  Mulberry32 r(hash32(x, y, z, 0xAE7C + tier));
  P.setEntity(i, json{{"type", "chest"}, {"items", aetherLoot(tier, r)}});
}
// a box: walls/floor/roof from shell(x, y, z), the inside cleared
template <class F> void room(SP& P, int x0, int y0, int z0, int x1, int y1, int z1, F shell) {
  const int ax = std::max(x0, P.bx), ex = std::min(x1, P.bx + 15), az = std::max(z0, P.bz), ez = std::min(z1, P.bz + 15);
  for (int y = y0; y <= y1; y++) for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) {
    const bool wall = x == x0 || x == x1 || y == y0 || y == y1 || z == z0 || z == z1;
    P.putId(x, y, z, wall ? shell(x, y, z) : AIR);
  }
}
void carve(SP& P, int x0, int y0, int z0, int x1, int y1, int z1) {
  const int ax = std::max(x0, P.bx), ex = std::min(x1, P.bx + 15), az = std::max(z0, P.bz), ez = std::min(z1, P.bz + 15);
  for (int y = y0; y <= y1; y++) for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) P.putId(x, y, z, AIR);
}
bool solidAt(SP& P, int x, int y, int z) { const BlockId b = P.get(x, y, z); return b != NOBLOCK && b != AIR && !tLiquid(b); }
}

// =====================================================================================================================
// Bronze
static bool bronzePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const SkylandsGen* g = skyOf(t); if (!g || !g->aether()) return false;
  static const int PTS[7][2] = {{-7, -7}, {6, -7}, {-7, 6}, {6, 6}, {0, 0}, {15, -5}, {15, 4}};
  int lo = -1, hi = 9999;
  for (auto& p : PTS) { int tp, bt; if (!g->aetherColumn(x + p[0], z + p[1], tp, bt)) return false; lo = std::max(lo, bt); hi = std::min(hi, tp); }
  if (hi - lo < 12) return false;
  S.y = lo + (hi - lo) / 2 - 5; S.cx = x; S.cz = z; S.x0 = x - 7; S.x1 = x + 55; S.z0 = z - 7; S.z1 = z + 6;
  S.data = {{"boss", {{"at", jat(x, S.y + 1, z)}, {"type", "slider"}, {"box", {x - 6, S.y + 1, z - 6, x + 5, S.y + 8, z + 5}}}},
            {"tunnel", 20 + (int)(rnd() * 20)}};
  return true;
}
static void bronzeBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const AK& K = ak(); const int x = S.cx, z = S.cz, y0 = S.y;
  auto stone = [&](int a, int b, int c) { return P.h(a, b, c, 1) < 0.05 ? K.sentry : K.carved; };
  // the tunnel first, so the rooms' walls close round its mouth: holystone (a fifth mossy) where it runs through rock
  const int tl = S.data.value("tunnel", 30);
  for (int tx = x + 15; tx <= x + 15 + tl; tx++) for (int dy = 0; dy <= 4; dy++) for (int dz = -2; dz <= 2; dz++) {
    const int yy = y0 + dy, zz = z + dz;
    if (tx < P.bx || tx > P.bx + 15 || zz < P.bz || zz > P.bz + 15) continue;
    const bool in = dy >= 1 && dy <= 3 && std::abs(dz) <= 1;
    if (in) P.putId(tx, yy, zz, AIR);
    else if (solidAt(P, tx, yy, zz)) P.putId(tx, yy, zz, P.h(tx, yy, zz, 2) < 0.2 ? K.mossy : K.holy);
  }
  // the boss hall (14 x 10 x 14), a sentry stone in each corner of the floor and a ring of them in the roof
  room(P, x - 7, y0, z - 7, x + 6, y0 + 9, z + 6, stone);
  for (int a : {-5, 4}) for (int b : {-5, 4}) P.putId(x + a, y0, z + b, K.sentry);
  for (int k = -4; k <= 3; k += 2) { P.putId(x + k, y0 + 9, z - 3, K.sentry); P.putId(x + k, y0 + 9, z + 2, K.sentry); }
  // the chest room east of it (10 x 7 x 10), a doorway between, the tunnel through its far wall
  room(P, x + 6, y0, z - 5, x + 15, y0 + 6, z + 4, [&](int a, int b, int c) { return P.h(a, b, c, 3) < 0.1 ? K.sentry : K.carved; });
  carve(P, x + 6, y0 + 1, z - 1, x + 6, y0 + 3, z);
  carve(P, x + 15, y0 + 1, z - 1, x + 15, y0 + 3, z + 1);
  aetherChest(P, x + 13, y0 + 1, z - 4, 0, 0);
  if (P.h(x, y0, z, 4) < 0.6) aetherChest(P, x + 13, y0 + 1, z + 3, 2, 0);
  // the reward behind the Slider
  aetherChest(P, x - 6, y0 + 1, z + 5, 2, 0);
}

// =====================================================================================================================
// Silver: local (u, v) = (x - S.x0r, z - S.z0r); u across (0..21), v back (0..46); the door at v = 0 faces north
struct SilverExt { int ox, oz; };
static bool silverPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const SkylandsGen* g = skyOf(t); if (!g || !g->aether()) return false;
  int y = 70 + (int)(rnd() * 36) + DY;                        // in the open sky
  for (int dz = -20; dz <= 40; dz += 10) for (int dx = -12; dx <= 24; dx += 12) {
    int tp, bt; if (g->aetherColumn(x + dx, z + dz, tp, bt) && tp + 3 > y && bt < y + 24) y = tp + 3;   // over an island in the way
  }
  if (y > 228) return false;
  const int ox = x - 11, oz = z - 21;
  S.y = y; S.cx = x; S.cz = z; S.x0 = ox - 16; S.x1 = ox + 38; S.z0 = oz - 18; S.z1 = oz + 60;
  S.ext = std::make_shared<SilverExt>(SilverExt{ox, oz});
  S.data = {{"boss", {{"at", jat(ox + 11, y + 1, oz + 36)}, {"type", "valkyrie_queen"}, {"box", {ox + 1, y + 1, oz + 22, ox + 20, y + 15, oz + 45}}}}};
  return true;
}
static void silverBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const AK& K = ak(); const auto& E = extOf<SilverExt>(S); const int ox = E.ox, oz = E.oz, y = S.y;
  auto ang = [&](int a, int b, int c) { return P.h(a, b, c, 1) < 0.05 ? K.angelicLit : K.angelic; };
  // the cloud bed: blobs of cold aercloud a block or two under the floor, ragged at the edges
  for (int z = std::max(S.z0, P.bz); z <= std::min(S.z1, P.bz + 15); z++) for (int x = std::max(S.x0, P.bx); x <= std::min(S.x1, P.bx + 15); x++) {
    const double ex = (x - (ox + 11)) / 26.0, ez = (z - (oz + 21)) / 38.0, r = ex * ex + ez * ez;
    const double n = P.h(x >> 2, 0, z >> 2, 7) * 0.35 + P.h(x, 0, z, 8) * 0.15;
    if (r + n > 1.05) continue;
    P.putId(x, y - 1, z, K.cold);
    if (r + n < 0.55) P.putId(x, y - 2, z, K.cold);
    if (r + n < 0.2 && P.h(x, 1, z, 9) < 0.5) P.putId(x, y - 3, z, K.cold);
  }
  // the front porch and its colonnade
  P.fillf(ox, y, oz - 4, ox + 21, y, oz - 1, [&](int, int, int) -> std::string_view { return "angelic_stone"; });
  for (int u = 1; u <= 21; u += 4) {
    for (int k = 1; k <= 11; k++) P.putId(ox + u, y + k, oz - 3, K.pillar);
    P.putId(ox + u, y + 12, oz - 3, K.pillarTop);
  }
  for (int u = 0; u <= 21; u++) for (int v = -4; v <= -1; v++) P.putId(ox + u, y + 13, oz + v, K.angelic);
  // the rooms: a 3 x 3 x 3 block of 7-block cells (walls shared), a doorway through every inner wall
  room(P, ox, y, oz, ox + 21, y + 21, oz + 21, ang);
  for (int f = 0; f < 3; f++) {
    const int Y = y + f * 7;
    if (f > 0) for (int v = 1; v <= 20; v++) for (int u = 1; u <= 20; u++) P.putId(ox + u, Y, oz + v, ang(ox + u, Y, oz + v));   // the floor
    for (int i = 1; i < 3; i++) for (int v = 1; v <= 20; v++) for (int k = 1; k <= 6; k++) {
      P.putId(ox + i * 7, Y + k, oz + v, ang(ox + i * 7, Y + k, oz + v));   // walls across
      P.putId(ox + v, Y + k, oz + i * 7, ang(ox + v, Y + k, oz + i * 7));   // walls along
    }
    for (int i = 1; i < 3; i++) for (int c = 0; c < 3; c++) {      // doorways, 2 wide, 3 tall, in each wall's cell
      carve(P, ox + i * 7, Y + 1, oz + c * 7 + 3, ox + i * 7, Y + 3, oz + c * 7 + 4);
      carve(P, ox + c * 7 + 3, Y + 1, oz + i * 7, ox + c * 7 + 4, Y + 3, oz + i * 7);
    }
    for (int cz = 0; cz < 3; cz++) for (int cx = 0; cx < 3; cx++) {
      const int u0 = cx * 7, v0 = cz * 7;
      P.putId(ox + u0 + 3, Y + 6, oz + v0 + 3, K.angelicLit);      // a light in each ceiling
      const double r = P.h(ox + u0, Y, oz + v0, 11);
      if (r < 0.3) aetherChest(P, ox + u0 + 1, Y + 1, oz + v0 + 1, (int)(r * 13) & 3, 1);
    }
  }
  for (int f = 0; f < 2; f++) {   // a ladder up from one room of each floor, against its back wall
    const int Y = y + f * 7, c = (int)(P.h(ox, Y, oz, 12) * 9), u = (c % 3) * 7 + 1, v = (c / 3) * 7 + 6;
    P.putId(ox + u, Y + 7, oz + v, AIR);
    P.ladder(ox + u, Y + 1, Y + 7, oz + v, 2);
  }
  carve(P, ox + 9, y + 1, oz, ox + 12, y + 4, oz);                 // the front door
  // a parapet of pillar tops round the roof
  for (int u = 0; u <= 21; u += 3) { P.putId(ox + u, y + 22, oz, K.pillarTop); P.putId(ox + u, y + 22, oz + 21, K.pillarTop); }
  for (int v = 0; v <= 21; v += 3) { P.putId(ox, y + 22, oz + v, K.pillarTop); P.putId(ox + 21, y + 22, oz + v, K.pillarTop); }
  // the boss hall behind (22 x 17 x 26): pillars, glowstone in the roof, a pool, two golden oak saplings
  room(P, ox, y, oz + 21, ox + 21, y + 16, oz + 46, ang);
  carve(P, ox + 9, y + 1, oz + 21, ox + 12, y + 4, oz + 21);
  for (int a : {4, 17}) for (int b : {26, 41}) { for (int k = 1; k <= 14; k++) P.putId(ox + a, y + k, oz + b, K.pillar); P.putId(ox + a, y + 15, oz + b, K.pillarTop); }
  for (int a : {6, 15}) for (int b : {29, 38}) for (int d = 0; d < 4; d++) P.putId(ox + a + (d & 1), y + 16, oz + b + (d >> 1), K.glow);
  for (int u = 8; u <= 13; u++) for (int v = 42; v <= 44; v++) P.putId(ox + u, y, oz + v, K.water);
  if (BlockId sap = bid("golden_oak_sapling"); sap != NOBLOCK) { P.putId(ox + 3, y + 1, oz + 44, sap); P.putId(ox + 18, y + 1, oz + 44, sap); }
  aetherChest(P, ox + 8, y + 1, oz + 45, 2, 1); aetherChest(P, ox + 13, y + 1, oz + 45, 2, 1);
}

// =====================================================================================================================
// Gold: an island centred on (cx, yc, cz) — its top at yc + 10
struct GoldExt {
  int yc;
  struct Stub { double x, z, r; int top; };
  std::vector<Stub> stubs;
  struct Tube { double ax, ay, az, bx, by, bz, r; };
  std::vector<Tube> tubes;
  struct Oak { int x, z, h; };
  std::vector<Oak> oaks;
};
static bool goldPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const SkylandsGen* g = skyOf(t); if (!g || !g->aether()) return false;
  int yc = 55 + (int)(rnd() * 45) + DY;
  int tp, bt; if (g->aetherColumn(x, z, tp, bt)) yc = std::max(yc, tp + 34);
  if (yc > 220) return false;
  auto E = std::make_shared<GoldExt>(); E->yc = yc;
  const int n = 8 + (int)(rnd() * 5);
  for (int i = 0; i < n; i++) {
    const double a = rnd() * JTAU, d = 16.8 + rnd() * 3;
    E->stubs.push_back({x + std::cos(a) * d, z + std::sin(a) * d, 5.5 + rnd() * 2, yc + 10 - (int)(rnd() * 8)});
  }
  for (int i = 0; i < 14; i++) {   // the gumdrop caves: short tubes through the rock, never into the hall
    double ax = x + (rnd() - 0.5) * 34, az = z + (rnd() - 0.5) * 34, ay = yc - 18 + rnd() * 24;
    if (std::abs(ax - x) < 14 && std::abs(az - z) < 16 && ay > yc - 9 && ay < yc + 4) ay = yc - 14;
    const double a = rnd() * JTAU, l = 6 + rnd() * 8;
    E->tubes.push_back({ax, ay, az, ax + std::cos(a) * l, ay + (rnd() - 0.5) * 6, az + std::sin(a) * l, 1.6 + rnd() * 0.8});
  }
  for (int i = 0; i < 5; i++) {
    const double a = rnd() * JTAU, d = 5 + rnd() * 10;
    E->oaks.push_back({x + (int)std::lround(std::cos(a) * d), z + (int)std::lround(std::sin(a) * d), 5 + (int)(rnd() * 3)});
  }
  S.y = yc; S.cx = x; S.cz = z; S.x0 = x - 30; S.x1 = x + 30; S.z0 = z - 34; S.z1 = z + 30; S.surface = true;
  S.ext = E;
  S.data = {{"boss", {{"at", jat(x, yc - 5, z + 4)}, {"type", "sun_spirit"}, {"box", {x - 10, yc - 5, z - 12, x + 10, yc + 2, z + 13}}}}};
  return true;
}
static void goldBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const AK& K = ak(); const auto& E = extOf<GoldExt>(S); const int cx = S.cx, cz = S.cz, yc = E.yc;
  auto column = [&](int x, int z, int top, int bottom) {
    for (int y = bottom; y <= top; y++) P.putId(x, y, z, y == top ? K.grass : y >= top - 3 ? K.dirt : K.holy);
    const double r = P.h(x, top, z, 5);
    if (r < 0.05) P.putId(x, top + 1, z, r < 0.025 ? K.white : K.purple);
    else if (r < 0.2) P.putId(x, top + 1, z, K.tallGrass);
  };
  // the island: a broad gumdrop, flat on top, its underside hanging to a blunt point
  auto radius = [&](int x, int z) { return std::hypot(x - cx, z - cz) / (19.0 + (P.h(x >> 2, 0, z >> 2, 3) - 0.5) * 2); };
  auto mainTop = [&](int x, int z) { const double r = radius(x, z); return yc + 10 + (int)std::lround((1 - r * r) * 2 + P.h(x, 0, z, 4) * 0.8); };
  for (int z = std::max(S.z0, P.bz); z <= std::min(S.z1, P.bz + 15); z++) for (int x = std::max(S.x0, P.bx); x <= std::min(S.x1, P.bx + 15); x++) {
    const double r = radius(x, z);
    if (r < 1) {
      const int bottom = yc + 10 - (int)std::lround(std::pow(1 - std::pow(r, 1.6), 0.8) * 32 + P.h(x, 1, z, 6) * 3);
      column(x, z, mainTop(x, z), bottom);
      continue;
    }
    for (const auto& s : E.stubs) {
      const double rs = std::hypot(x - s.x, z - s.z) / s.r;
      if (rs >= 1) continue;
      column(x, z, s.top + (int)std::lround((1 - rs) * 1.2), s.top - (int)std::lround(std::pow(1 - rs, 0.9) * 12));
    }
  }
  // the gumdrop caves
  for (int z = std::max(S.z0, P.bz); z <= std::min(S.z1, P.bz + 15); z++) for (int x = std::max(S.x0, P.bx); x <= std::min(S.x1, P.bx + 15); x++)
    for (const auto& T : E.tubes) {
      const double mnx = std::min(T.ax, T.bx) - T.r, mxx = std::max(T.ax, T.bx) + T.r, mnz = std::min(T.az, T.bz) - T.r, mxz = std::max(T.az, T.bz) + T.r;
      if (x < mnx || x > mxx || z < mnz || z > mxz) continue;
      const int ylo = (int)std::floor(std::min(T.ay, T.by) - T.r), yhi = (int)std::ceil(std::max(T.ay, T.by) + T.r);
      const double dx = T.bx - T.ax, dy = T.by - T.ay, dz = T.bz - T.az, L2 = dx * dx + dy * dy + dz * dz;
      for (int y = ylo; y <= yhi; y++) {
        double k = ((x - T.ax) * dx + (y - T.ay) * dy + (z - T.az) * dz) / L2; k = std::clamp(k, 0.0, 1.0);
        const double ex = x - (T.ax + k * dx), ey = y - (T.ay + k * dy), ez = z - (T.az + k * dz);
        if (ex * ex + ey * ey + ez * ez < T.r * T.r && solidAt(P, x, y, z) && P.get(x, y + 1, z) != K.grass) P.putId(x, y, z, AIR);
      }
    }
  // the hall (23 x 9 x 28) of hellfire stone, a tenth of it lit, and the tunnel out of its north wall
  auto hf = [&](int a, int b, int c) { return P.h(a, b, c, 1) < 0.1 ? K.hellfireLit : K.hellfire; };
  for (int tz = cz - 30; tz <= cz - 14; tz++) for (int dy = 0; dy <= 4; dy++) for (int dx = -3; dx <= 3; dx++) {
    const int xx = cx + dx, yy = yc - 6 + dy;
    const bool in = dy >= 1 && dy <= 3 && std::abs(dx) <= 2;
    if (in) P.putId(xx, yy, tz, AIR); else if (solidAt(P, xx, yy, tz)) P.putId(xx, yy, tz, hf(xx, yy, tz));
  }
  room(P, cx - 11, yc - 6, cz - 14, cx + 11, yc + 2, cz + 13, hf);
  carve(P, cx - 2, yc - 5, cz - 14, cx + 2, yc - 3, cz - 14);
  for (int a : {-7, 7}) for (int b : {-8, 0, 8}) for (int k = 1; k <= 7; k++) P.putId(cx + a, yc - 6 + k, cz + b, K.hellfireLit);   // burning pillars
  aetherChest(P, cx - 3, yc - 5, cz + 12, 2, 2); aetherChest(P, cx + 3, yc - 5, cz + 12, 2, 2);
  // golden oaks on top
  for (const auto& o : E.oaks) {
    if (o.x < P.bx - 3 || o.x > P.bx + 18 || o.z < P.bz - 3 || o.z > P.bz + 18) continue;
    const int top = mainTop(o.x, o.z);   // worked out, not looked up: the trunk may stand in a neighbour chunk
    for (int dy = -2; dy <= 2; dy++) for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
      const double d = std::sqrt(dx * dx + dz * dz + dy * dy * 1.8);
      if (d > 3.2 - (P.h(o.x + dx, dy, o.z + dz, 9) < 0.3 ? 0.8 : 0)) continue;
      const int yy = top + o.h + dy;
      if (P.get(o.x + dx, yy, o.z + dz) == AIR) P.putId(o.x + dx, yy, o.z + dz, K.goldLeaf);
    }
    for (int k = 1; k <= o.h; k++) P.putId(o.x, top + k, o.z, K.goldLog);
  }
}

void registerStructureAether() {
  auto reg1 = [](const char* key, const char* name, int region, double chance, int margin, int ji, decltype(StructureType::plan) plan, decltype(StructureType::build) build) {
    StructureType t; t.key = key; t.name = name; t.region = region; t.chance = chance; t.margin = margin; t.jsIndex = ji; t.clearance = 24;
    t.dims = 1u << DIM_AETHER; t.plan = std::move(plan); t.build = std::move(build);
    registerStructure(std::move(t));
  };
  reg1("silver_dungeon", "a Silver Dungeon", 576, 0.7, 90, 901, silverPlan, silverBuild);
  reg1("gold_dungeon", "a Gold Dungeon", 576, 0.3, 90, 902, goldPlan, goldBuild);
  reg1("bronze_dungeon", "a Bronze Dungeon", 112, 0.8, 10, 900, bronzePlan, bronzeBuild);
}

} // namespace dq
