// gen/structures_places.cpp — game/structures.js: ruins, mage towers, desert temples, sunken temples, shipwrecks,
// mineshafts and the Labyrinth (STRUCTS 0..6), built block for block as the original builds them.
// Plans decide with the original's heights (jsY) and build at band heights; underground depths go through
// vmap::owUnder. The residents/boss/spawners lists go in StructurePlan::data in the original's shape.
#include "gen/structures_common.h"

namespace dq {
using namespace vmap;

static const int* DXL = DIRX;
static const int* DZL = DIRZ;

// ==== Ruins ==========================================================================================================
struct RuinPal { std::vector<std::string> wall, floor, rubble; std::string corner, found; };
static const RuinPal& ruinPal(int style) {
  static const std::array<RuinPal, 3> P = [] {
    std::array<RuinPal, 3> r;
    const char* names[3] = {"stone", "sand", "cold"};
    for (int i = 0; i < 3; i++) {
      const json& j = SD()["RUIN_PAL"][names[i]];
      r[i].wall = strList(j["wall"]); r[i].floor = strList(j["floor"]); r[i].rubble = strList(j["rubble"]);
      r[i].corner = j["corner"].get<std::string>(); r[i].found = j["found"].get<std::string>();
    }
    return r;
  }();
  return P[style];
}
struct RuinExt { int W, D, style, door, breach0, maxH; double breach1; bool cellar, lectern; };

static bool ruinPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c)) return false;
  const int W = 7 + 2 * jsFloor(rnd() * 3), D = 7 + 2 * jsFloor(rnd() * 3), x0 = x - (W >> 1), z0 = z - (D >> 1);
  Land L = sLand(t, x0, z0, x0 + W - 1, z0 + D - 1);
  if (L.jhi - L.jlo > 5) return false;
  const int style = c.biome == BIOME_DESERT ? 1 : c.temp < -0.2 ? 2 : 0;
  const int side = jsFloor(rnd() * 4);
  auto e = std::make_shared<RuinExt>();
  e->W = W; e->D = D; e->style = style;
  e->cellar = rnd() < 0.35; e->lectern = rnd() < 0.5; e->door = side;
  e->breach0 = jsFloor(rnd() * 4); e->breach1 = rnd(); e->maxH = 3 + jsFloor(rnd() * 3);
  S.x0 = x0; S.z0 = z0; S.x1 = x0 + W - 1; S.z1 = z0 + D - 1; S.cx = (S.x0 + S.x1) >> 1; S.cz = (S.z0 + S.z1) >> 1;
  S.y = L.mean; S.hashDy = L.jmean - L.mean; S.surface = true;
  S.name = style == 1 ? "Sandstone Ruins" : "Ruins";
  S.data = {{"spawners", json::array()}};
  S.ext = e;
  return true;
}

static void ruinBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const RuinExt& R = extOf<RuinExt>(S); const RuinPal& pal = ruinPal(R.style);
  const bool sand = R.style == 1;
  const int cx = (S.x0 + S.x1) >> 1, cz = (S.z0 + S.z1) >> 1;
  for (int z = S.z0 - 1; z <= S.z1 + 1; z++) for (int x = S.x0 - 1; x <= S.x1 + 1; x++) {
    if (!P.in(x, z)) continue;
    const bool out = x < S.x0 || x > S.x1 || z < S.z0 || z > S.z1; const int gh = P.ground(x, z);
    if (out) { if (gh < S.y) P.fill(x, gh + 1, z, x, S.y, z, sand ? "sand" : "dirt"); if (gh >= S.y) P.put(x, S.y, z, sand ? "sand" : "grass"); continue; }
    // the floor, on a footing down to the land
    for (int y = std::min(gh, S.y) - 2; y < S.y; y++) P.put(x, y, z, pal.found);
    for (int y = S.y + 1; y <= std::max(gh, S.y) + 3; y++) P.put(x, y, z, "air");
    const double fl = P.h(x, 0, z, 1);
    P.put(x, S.y, z, fl < 0.15 ? (sand ? "sand" : "grass") : std::string_view(P.pick(pal.floor, x, 0, z, 2)));
    const bool edge = x == S.x0 || x == S.x1 || z == S.z0 || z == S.z1, corner = (x == S.x0 || x == S.x1) && (z == S.z0 || z == S.z1);
    if (edge) {
      // the walls, broken down to stumps where they fell, worst at the breach
      const int side = z == S.z0 ? 0 : x == S.x1 ? 1 : z == S.z1 ? 2 : 3;
      const double along = side == 0 || side == 2 ? (x - S.x0) / (double)(R.W - 1) : (z - S.z0) / (double)(R.D - 1);
      int h = corner ? R.maxH + 1 : jsRound(R.maxH * (0.35 + 0.65 * P.h(x, 1, z, 3)));
      if (side == R.breach0) h -= jsRound(std::max(0.0, 3 - std::fabs(along - R.breach1) * 10));
      const bool door = side == R.door && std::fabs(along - 0.5) < 0.6 / ((side & 1) ? R.D : R.W) + 0.01;
      for (int y = 1; y <= h; y++) {
        if (door && y <= 2) continue;
        const bool win = !corner && y == 2 && h >= 3 && P.h(x, 2, z, 4) < 0.25;
        P.put(x, S.y + y, z, corner ? std::string_view(pal.corner) : win ? std::string_view("iron_bars") : std::string_view(P.pick(pal.wall, x, y, z, 5)));
      }
      // what fell off lies at its foot
      if (!corner && h < R.maxH - 1 && P.h(x, 9, z, 6) < 0.5) {
        const int ox = side == 1 ? 1 : side == 3 ? -1 : 0, oz = side == 0 ? -1 : side == 2 ? 1 : 0;
        if (P.in(x + ox, z + oz)) P.put(x + ox, S.y + 1, z + oz, P.pick(pal.rubble, x, 3, z, 7));
      }
    } else {
      const double q = P.h(x, 2, z, 8);
      if (q < 0.07) P.put(x, S.y + 1, z, P.pick(pal.rubble, x, 4, z, 9));
      else if (q < 0.11 && !sand) P.put(x, S.y + 1, z, "tall_grass");
      else if (q < 0.13 && R.style == 0) P.put(x, S.y + 1, z, "cobweb");
    }
  }
  // a book left open, and a chest
  if (R.lectern) P.lectern(cx, S.y + 1, cz, R.door);
  if (R.cellar) {
    // a trapdoor in the floor, a ladder down to a little cellar
    const int y0 = S.y - 5;
    P.fill(cx - 2, y0, cz - 2, cx + 2, S.y - 1, cz + 2, pal.found);
    P.fill(cx - 1, y0 + 1, cz - 1, cx + 1, y0 + 3, cz + 1, "air");
    P.put(cx, S.y - 1, cz + 1, "air"); P.put(cx, S.y, cz + 1, R.style == 2 ? "spruce_trapdoor" : "oak_trapdoor", 2 | 8);
    P.ladder(cx, y0 + 1, S.y - 1, cz + 1, 2); P.put(cx, y0 + 1, cz + 2, pal.found); for (int y = y0 + 1; y < S.y; y++) P.put(cx, y, cz + 2, pal.found);
    P.chest(cx - 1, y0 + 1, cz - 1, 2, "ruin"); P.chest(cx + 1, y0 + 1, cz - 1, 2, "ruin");
    P.put(cx + 1, y0 + 3, cz, "cobweb"); P.put(cx - 1, y0 + 3, cz + 1, "cobweb"); P.put(cx, y0 + 3, cz - 1, "old_torch", 3);
  } else P.chest(S.x0 + 1, S.y + 1, S.z0 + 1, 2, "ruin");
}

// ==== Mage towers ====================================================================================================
constexpr int TOWER_R = 5, TOWER_LV = 6;
struct TowerExt { int R, door; std::string roof; };

static bool towerPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c) || c.biome == BIOME_DESERT) return false;
  const int R = TOWER_R; Land L = sLand(t, x - R, z - R, x + R, z + R);
  if (L.jhi - L.jlo > 6 || L.lo + 4 * TOWER_LV + 12 >= DIM_H - 2) return false;
  const int y = L.lo + 1;
  static const char* roofs[4] = {"purple_wool", "blue_wool", "cyan_wool", "black_wool"};
  auto e = std::make_shared<TowerExt>(); e->R = R; e->roof = roofs[jsFloor(rnd() * 4)]; e->door = jsFloor(rnd() * 4);
  S.x0 = x - R - 2; S.z0 = z - R - 2; S.x1 = x + R + 2; S.z1 = z + R + 2; S.cx = x; S.cz = z; S.y = y; S.hashDy = L.jlo - L.lo; S.surface = true;
  S.name = "Mage Tower";
  S.data = {{"residents", json::array({mobAt(x + 2, y + 1, z, "warlock"), mobAt(x - 2, y + 1 + TOWER_LV * 2, z + 1, "warlock"), mobAt(x + 1, y + 1 + TOWER_LV * 2, z - 2, "warlock")})},
            {"boss", {{"at", jat(x, y + 1 + TOWER_LV * 3, z)}, {"type", "hollow_archmage"}, {"box", {x - R, y + TOWER_LV * 3, z - R, x + R, y + TOWER_LV * 4 + 6, z + R}}}}};
  S.ext = e;
  return true;
}

static void towerBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const TowerExt& T = extOf<TowerExt>(S);
  const int cx = S.cx, cz = S.cz, y = S.y, R = T.R, top = y + TOWER_LV * 4;
  static const std::vector<const char*> walls = {"stone_bricks", "stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks", "polished_andesite"};
  static const std::vector<const char*> floors = {"spruce_planks", "dark_oak_planks"};
  auto wall = [&](int x, int yy, int z) { return P.pickw(walls, x, yy, z, 1); };
  const int dx0 = DXL[T.door], dz0 = DZL[T.door];
  const int la0 = cx - dx0 * (R - 1), la1 = cz - dz0 * (R - 1);   // up the wall opposite the door
  for (int z = S.z0; z <= S.z1; z++) for (int x = S.x0; x <= S.x1; x++) {
    if (!P.in(x, z)) continue;
    const double d = std::hypot(x - cx, z - cz); const int gh = P.ground(x, z);
    if (d > R + 1.6) continue;
    if (d > R + 0.5) { if (gh < y) P.fill(x, gh + 1, z, x, y - 1, z, "cobblestone"); P.put(x, y, z, "grass"); for (int yy = y + 1; yy <= std::max(gh, y) + 2; yy++) P.put(x, yy, z, "air"); continue; }
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, "cobblestone");
    const bool ring = d > R - 0.5;
    for (int yy = y; yy <= top; yy++) {
      const int lv = (yy - y) % TOWER_LV, level = (yy - y) / TOWER_LV;
      if (ring) {
        // windows on each floor, the door at the bottom
        const bool card = std::abs(x - cx) < 0.6 || std::abs(z - cz) < 0.6;
        const bool isDoor = level == 0 && (lv == 1 || lv == 2) && x == cx + dx0 * R && z == cz + dz0 * R;
        if (isDoor) { P.put(x, yy, z, "oak_door", T.door | (lv == 2 ? 16 : 0)); continue; }
        if (card && (lv == 2 || lv == 3) && level > 0) { P.put(x, yy, z, "glass_pane"); continue; }
        P.put(x, yy, z, lv == 0 && level > 0 ? "chiseled_stone_bricks" : wall(x, yy, z));
      } else if (lv == 0) {
        // the floors, with the ladder's hole
        const bool hole = x == la0 && z == la1 && yy > y;
        P.put(x, yy, z, hole ? "air" : yy == y ? "stone_bricks" : P.pick(floors, x, 0, z, 2));
      } else P.put(x, yy, z, "air");
    }
    // a pointed roof, round and steep, with a light at its tip
    for (int k = 0;; k++) {
      const double rr = R + 1.4 - k * 0.62;
      if (rr < 0.4) { if (x == cx && z == cz) { P.put(x, top + k, z, "sea_lantern"); P.put(x, top + k + 1, z, "iron_bars"); P.put(x, top + k + 2, z, "iron_bars"); } break; }
      if (d <= rr && d > rr - 1.25) P.put(x, top + 1 + k, z, T.roof);
    }
  }
  // the ladder up through every floor
  P.ladder(la0, y + 1, top - 1, la1, (T.door + 2) & 3);
  auto L = [&](int lv) { return y + 1 + lv * TOWER_LV; };
  // ground floor: a workroom
  P.put(cx - 3, L(0), cz - 1, "cauldron"); P.put(cx - 3, L(0), cz + 1, "barrel", 1); P.put(cx + 3, L(0), cz - 1, "crafting_table"); P.put(cx - 2, L(0), cz - 3, "barrel", 1);
  P.put(cx + 3, L(0), cz + 1, "lantern"); P.put(cx - 3, L(0) + 1, cz + 1, "lantern");
  // first floor: the library
  for (int z = cz - R + 1; z <= cz + R - 1; z++) for (int x = cx - R + 1; x <= cx + R - 1; x++) {
    const double d = std::hypot(x - cx, z - cz);
    if (d > R - 1.5 && d <= R - 0.5 && !(x == la0 && z == la1) && !(std::abs(x - la0) <= 1 && std::abs(z - la1) <= 1)) {
      P.put(x, L(1), z, "bookshelf"); P.put(x, L(1) + 1, z, "bookshelf"); if (P.h(x, 1, z, 3) < 0.6) P.put(x, L(1) + 2, z, "bookshelf");
    }
  }
  P.lectern(cx, L(1), cz, T.door);
  P.chest(cx + dx0 * 2 + dz0, L(1), cz + dz0 * 2 + dx0, (T.door + 2) & 3, "tower");
  P.put(cx - dz0 * 2, L(1), cz - dx0 * 2, "candle", 2); P.put(cx + dz0 * 2, L(1), cz + dx0 * 2, "lantern");
  // second floor: the study
  P.put(cx - 2, L(2), cz - 2, "crafting_table"); P.put(cx - 1, L(2), cz - 2, "barrel", 1); P.put(cx + 2, L(2), cz + 2, "flower_pot"); P.put(cx + 3, L(2), cz, "cauldron");
  P.chest(cx - 3, L(2), cz + 1, 1, "tower"); P.put(cx, L(2), cz + 3, "lantern"); P.put(cx - 2, L(2), cz + 2, "candle", 3);
  // the top: the master's room, a crystal glowing over his hoard
  P.put(cx, L(3), cz - 3, "amethyst_block"); P.put(cx, L(3) + 1, cz - 3, "sea_lantern"); P.put(cx + 3, L(3), cz, "amethyst_block"); P.put(cx - 3, L(3), cz, "amethyst_block");
  P.chest(cx, L(3), cz + 3, 0, "archmage");
  for (auto [a, b] : {std::pair{2, 2}, {-2, 2}, {2, -2}, {-2, -2}}) P.put(cx + a, L(3), cz + b, "soul_lantern");
}

// ==== Desert temples =================================================================================================
static bool desertPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (c.biome != BIOME_DESERT || c.jsY <= JS_SEA + 1) return false;
  Land L = sLand(t, x - 10, z - 10, x + 10, z + 10);
  if (L.jhi - L.jlo > 8) return false;
  S.x0 = x - 10; S.z0 = z - 10; S.x1 = x + 10; S.z1 = z + 10; S.cx = x; S.cz = z; S.y = L.mean; S.hashDy = L.jmean - L.mean; S.surface = true;
  S.name = "Desert Temple";
  return true;
}

static void desertBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int cx = S.cx, cz = S.cz, y = S.y;
  const char *ss = "sandstone", *cut = "cut_sandstone", *sm = "smooth_sandstone", *chis = "chiseled_sandstone";
  static const std::string OT = PLK("orange_terracotta", "terracotta"), BT = PLK("blue_terracotta", "lapis_block");
  for (int z = S.z0; z <= S.z1; z++) for (int x = S.x0; x <= S.x1; x++) {
    if (!P.in(x, z)) continue;
    const int lx = x - cx, lz = z - cz, ax = std::abs(lx), az = std::abs(lz), gh = P.ground(x, z);
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, ss);
    for (int yy = y + 1; yy <= std::max(gh, y) + 2; yy++) P.put(x, yy, z, "air");
    // the floor: a star of orange round a blue heart
    const bool star = (ax == az || ax == 0 || az == 0) && ax <= 3 && az <= 3;
    P.put(x, y, z, ax <= 3 && az <= 3 ? (ax == 0 && az == 0 ? std::string_view(BT) : star ? std::string_view(OT) : cut) : sm);
    const bool tower = lz <= -6 && ax >= 6, edge = ax == 10 || az == 10;
    if (tower) {
      // two hollow towers at the front, a doorway from the hall, a ladder up to a lookout at the top
      const bool tEdge = ax == 10 || ax == 6 || lz == -10 || lz == -6;
      for (int yy = 1; yy <= 14; yy++) {
        if (!tEdge) { P.put(x, y + yy, z, yy == 14 ? cut : "air"); continue; }
        const bool doorway = lz == -6 && ax == 8 && yy <= 2, window = (yy == 11 || yy == 12) && (ax == 8 || lz == -8);
        P.put(x, y + yy, z, doorway || window ? std::string_view("air") : yy == 14 ? cut : (yy == 5 || yy == 10) ? std::string_view(OT) : yy == 13 ? chis : ss);
      }
      if (ax == 8 && lz == -9) for (int yy = 1; yy <= 13; yy++) P.put(x, y + yy, z, "ladder", 0);
      if (ax == 8 && lz == -8) P.put(x, y + 14, z, "air");
    } else if (edge) {
      // the walls, banded, with the doorway at the front
      const bool door = lz == -10 && ax <= 1;
      for (int yy = 1; yy <= 9; yy++) {
        if (door && yy <= 4) continue;
        P.put(x, y + yy, z, yy == 9 ? cut : yy == 6 ? std::string_view(OT) : (yy == 3 || yy == 7) && (ax % 3 == 0 || az % 3 == 0) ? chis : ss);
      }
    } else {
      // pillars round the hall, a roof over it, open to the sky over the star
      const bool pillar = ax == 5 && az == 5;
      if (pillar) for (int yy = 1; yy <= 8; yy++) P.put(x, y + yy, z, yy == 4 ? std::string_view(OT) : cut);
      if (!(ax <= 3 && az <= 3)) P.put(x, y + 9, z, cut);
      else if (ax == 3 || az == 3) P.put(x, y + 9, z, "sandstone_slab");
    }
  }
  // under the blue stone: a shaft down to the treasure, and the trap
  const int yf = y - 14;
  P.fill(cx - 4, yf - 2, cz - 4, cx + 4, y - 1, cz + 4, ss);
  P.fill(cx - 3, yf + 1, cz - 3, cx + 3, yf + 3, cz + 3, "air");
  for (int yy = yf + 4; yy < y; yy++) P.put(cx, yy, cz, "air");
  P.put(cx, yf, cz, ss); P.put(cx, yf + 1, cz, "stone_pressure_plate");
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) P.put(cx + dx, yf - 1, cz + dz, "tnt");
  for (auto [dx, dz, f] : {std::tuple{0, -3, 2}, {3, 0, 3}, {0, 3, 0}, {-3, 0, 1}}) P.chest(cx + dx, yf + 1, cz + dz, f, "temple");
  for (auto [dx, dz] : {std::pair{-3, -3}, {3, -3}, {-3, 3}, {3, 3}}) { P.put(cx + dx, yf + 1, cz + dz, chis); P.put(cx + dx, yf + 2, cz + dz, OT); }
}

// ==== Sunken temples =================================================================================================
static bool oceanPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (c.biome != BIOME_OCEAN || c.jsY > JS_SEA - 9) return false;
  Land L = sLand(t, x - 11, z - 11, x + 11, z + 11);
  if (L.jhi > JS_SEA - 6) return false;
  const int lo = L.lo;
  S.x0 = x - 11; S.z0 = z - 11; S.x1 = x + 11; S.z1 = z + 11; S.cx = x; S.cz = z; S.y = lo; S.hashDy = L.jlo - L.lo;
  S.name = "Sunken Temple";
  S.data = {{"residents", json::array({mobAt(x + 6, lo + 1, z, "drowned"), mobAt(x - 6, lo + 1, z, "drowned"), mobAt(x, lo + 1, z + 7, "drowned"), mobAt(x, lo + 1, z - 7, "drowned")})}};
  return true;
}

static void oceanBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int cx = S.cx, cz = S.cz, y = S.y;
  const char *wb = "prismarine_bricks", *pr = "prismarine", *dk = "dark_prismarine";
  auto fillAbove = [](int yy) { return yy <= SEA_LEVEL ? "water" : "air"; };
  for (int z = S.z0; z <= S.z1; z++) for (int x = S.x0; x <= S.x1; x++) {
    if (!P.in(x, z)) continue;
    const int lx = x - cx, lz = z - cz, ax = std::abs(lx), az = std::abs(lz), gh = P.ground(x, z);
    for (int yy = gh + 1; yy < y; yy++) P.put(x, yy, z, pr);
    for (int yy = y + 1; yy <= std::min(SEA_LEVEL, y + 12); yy++) P.put(x, yy, z, fillAbove(yy));
    P.put(x, y, z, (ax + az) % 4 == 0 ? dk : wb);
    const bool corner = ax >= 8 && az >= 8, edge = ax == 11 || az == 11;
    const int wallTop = std::min(y + 9, SEA_LEVEL + 1), towerTop = std::min(y + 12, SEA_LEVEL + 2);
    if (corner) {
      const bool tEdge = ax == 11 || az == 11 || ax == 8 || az == 8;
      for (int yy = y + 1; yy <= towerTop; yy++) P.put(x, yy, z, tEdge ? (yy == towerTop ? dk : (yy - y) % 4 == 0 ? "sea_lantern" : pr) : yy == towerTop ? dk : fillAbove(yy));
    } else if (edge) {
      const bool gate = ax <= 1 || az <= 1;
      for (int yy = y + 1; yy <= wallTop; yy++) { if (gate && yy <= y + 4) continue; P.put(x, yy, z, yy == wallTop ? dk : yy == y + 5 ? "sea_lantern" : wb); }
    } else {
      // the roof, a lantern in every fourth tile
      P.put(x, wallTop, z, (ax % 4 == 2 && az % 4 == 2) ? "sea_lantern" : pr);
      // pillars in the hall
      if (ax % 4 == 3 && az % 4 == 3 && !(ax <= 4 && az <= 4)) for (int yy = y + 1; yy < wallTop; yy++) P.put(x, yy, z, yy == wallTop - 1 ? "sea_lantern" : pr);
      // the core: dark walls round a heart of gold
      if (ax <= 4 && az <= 4) {
        const bool cEdge = ax == 4 || az == 4, door = cEdge && (ax <= 0 || az <= 0);
        for (int yy = y + 1; yy <= y + 6; yy++) {
          if (cEdge) { if (door && yy <= y + 2) continue; P.put(x, yy, z, dk); }
          else if (yy == y + 6) P.put(x, yy, z, dk);
        }
        if (ax <= 1 && az <= 1 && lx <= 0 && lz <= 0) { P.put(x, y + 1, z, "gold_block"); P.put(x, y + 2, z, "gold_block"); }
        if (ax == 3 && az == 3) P.put(x, y + 1, z, "sea_lantern");
      }
      // a sponge room in one wing
      if (lx >= 6 && lx <= 9 && lz >= 2 && lz <= 5) { if (lx == 6 || lz == 2 || lz == 5) { for (int yy = y + 1; yy <= y + 3; yy++) P.put(x, yy, z, wb); } else if (P.h(x, 0, z, 4) < 0.6) P.put(x, y + 1, z, "sponge"); }
    }
  }
  P.chest(cx + 2, y + 1, cz + 2, 0, "ocean"); P.chest(cx - 2, y + 1, cz + 2, 0, "ocean");
}

// ==== Shipwrecks =====================================================================================================
struct WreckExt { int L, axis, mast; bool sunk, flip; std::string wood; };

static bool wreckPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  const bool sunk = c.biome == BIOME_OCEAN && c.jsY <= JS_SEA - 6, beached = c.biome == BIOME_BEACH;
  if (!sunk && !beached) return false;
  const int L = 17 + jsFloor(rnd() * 6), axis = rnd() < 0.5 ? 0 : 1;   // 0: along z, 1: along x
  const int hx = axis ? L >> 1 : 4, hz = axis ? 4 : L >> 1;
  Land ld = sLand(t, x - hx, z - hz, x + hx, z + hz);
  const int yb = sunk ? ld.lo - 1 : std::max(SEA_LEVEL - 2, ld.mean - 2), jyb = sunk ? ld.jlo - 1 : std::max(JS_SEA - 2, ld.jmean - 2);
  static const char* woods[3] = {"spruce", "planks", "dark_oak"};
  auto e = std::make_shared<WreckExt>(); e->L = L; e->axis = axis; e->sunk = sunk;
  e->wood = woods[jsFloor(rnd() * 3)]; e->mast = 3 + jsFloor(rnd() * 7); e->flip = rnd() < 0.5;
  S.x0 = x - hx; S.z0 = z - hz; S.x1 = x + hx; S.z1 = z + hz; S.cx = x; S.cz = z; S.y = yb; S.hashDy = jyb - yb; S.surface = !sunk;
  S.name = "Shipwreck";
  S.data = {{"residents", sunk ? json::array({mobAt(x, yb + 2, z, "drowned")}) : json::array()}};
  S.ext = e;
  return true;
}

static void wreckBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const WreckExt& E = extOf<WreckExt>(S);
  const bool pl = E.wood == "planks";
  const std::string plank = pl ? "planks" : E.wood + "_planks", log = pl ? "log" : E.wood + "_log";
  const std::string fence = pl || !hasBlock(E.wood + "_fence") ? "oak_fence" : E.wood + "_fence", slab = pl || !hasBlock(E.wood + "_slab") ? "oak_slab" : E.wood + "_slab";
  const int keel = E.axis ? 1 : 2, L = E.L, half = L >> 1, yb = S.y;
  auto water = [](int yy) { return yy <= SEA_LEVEL ? "water" : "air"; };
  // local (a along the ship, c across) to world
  auto W = [&](int a, int c) { return E.axis ? std::pair{S.cx + (E.flip ? -a : a), S.cz + c} : std::pair{S.cx + c, S.cz + (E.flip ? -a : a)}; };
  for (int a = -half; a <= half; a++) {
    const double t = (a + half) / (double)L, prof = std::sin(std::min(1.0, t * 1.25 + 0.08) * M_PI);   // the bow is the sharp end
    const int hw = std::max(1, jsRound(3 * std::min(1.0, prof * 1.4))), bottom = yb + jsRound(2 * (1 - std::min(1.0, prof * 1.3)));
    const int deck = yb + 4; const bool cabin = a >= half - 4;
    for (int c = -hw - 1; c <= hw + 1; c++) {
      auto [x, z] = W(a, c); if (!P.in(x, z)) continue;
      auto broken = [&](int yy) { return P.hw(x, yy, z, 9) < 0.1 + (E.sunk ? 0.06 : 0); };
      if (std::abs(c) > hw) continue;
      for (int yy = bottom; yy <= deck + (cabin ? 3 : 1); yy++) {
        const bool side = std::abs(c) == hw;
        const std::string* id = nullptr;
        if (yy == bottom) id = c == 0 ? &log : &plank;
        else if (side && yy <= deck + 1) id = yy == deck + 1 ? &fence : &plank;
        else if (yy == deck) id = P.h(x, 7, z, 2) < 0.25 ? nullptr : (c == 0 ? &slab : &plank);
        else if (cabin && yy > deck) id = side || a == half - 4 || a == half ? (a == half - 4 && c == 0 && yy <= deck + 2 ? nullptr : &plank) : yy == deck + 3 ? &plank : nullptr;
        if (id && broken(yy)) id = nullptr;
        const int meta = yy == bottom && c == 0 ? keel : 0;
        if (id) P.put(x, yy, z, *id, meta); else P.put(x, yy, z, water(yy), 0);
      }
      // sand drifted in where it lies
      if (E.sunk && std::abs(c) < hw && P.h(x, 0, z, 6) < 0.3) P.put(x, bottom + 1, z, "sand");
      if (E.sunk && std::abs(c) == hw && P.h(x, 1, z, 7) < 0.3) { auto [ox, oz] = W(a, c + jsign(c)); if (P.in(ox, oz)) P.put(ox, yb, oz, "seagrass"); }
    }
    // the broken mast
    if (a == jsRound(L * 0.08)) {
      auto [x, z] = W(a, 0);
      if (P.in(x, z)) {
        for (int yy = yb + 1; yy <= yb + 4 + E.mast; yy++) P.put(x, yy, z, log, 0);
        if (E.mast > 5) for (int c : {-2, -1, 1, 2}) { auto [xx, zz] = W(a, c); if (P.in(xx, zz)) P.put(xx, yb + 2 + E.mast, zz, fence); }
      }
    }
  }
  // the hold's stores at the bow, the captain's chest in the cabin
  auto [bx, bz] = W(-half + 4, 0); auto [sx, sz] = W(half - 2, 0);
  const int fwd = E.axis ? (E.flip ? 1 : 3) : (E.flip ? 0 : 2);
  P.chest(bx, yb + 2, bz, fwd, "wreck_supply");
  P.chest(sx, yb + 5, sz, (fwd + 2) & 3, "wreck_treasure");
  auto [mx, mz] = W(half - 3, 1); P.put(mx, yb + 5, mz, "lantern");
}

// ==== Mineshafts =====================================================================================================
struct MineSeg { int x, z, ex, ez, dir, x0, x1, z0, z1; bool chest, spider; };
struct MineExt { std::vector<MineSeg> segs; int x, z, sp[3]; };

static bool minePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c)) return false;
  const int yo = 18 + jsFloor(rnd() * 14);
  if (c.jsY - yo < 16) return false;
  // corridors, branching out from the start
  auto e = std::make_shared<MineExt>(); auto& segs = e->segs;
  const int LIM = 56;
  struct Q { int x, z, dir, d; };
  std::vector<Q> q{{x, z, jsFloor(rnd() * 4), 0}}; size_t qi = 0;
  while (qi < q.size() && segs.size() < 28) {
    const Q s = q[qi++];
    const int len = 8 + jsFloor(rnd() * 18), ex = s.x + DXL[s.dir] * len, ez = s.z + DZL[s.dir] * len;
    if (std::abs(ex - x) > LIM || std::abs(ez - z) > LIM) continue;
    // not running into (or alongside) a corridor that's already there
    const int nx0 = std::min(s.x + DXL[s.dir] * 4, ex) - 1, nx1 = std::max(s.x + DXL[s.dir] * 4, ex) + 1, nz0 = std::min(s.z + DZL[s.dir] * 4, ez) - 1, nz1 = std::max(s.z + DZL[s.dir] * 4, ez) + 1;
    bool hit = false;
    for (const auto& o : segs) if (!(o.x1 + 1 < nx0 || o.x0 - 1 > nx1 || o.z1 + 1 < nz0 || o.z0 - 1 > nz1)) { hit = true; break; }
    if (hit) continue;
    MineSeg g{s.x, s.z, ex, ez, s.dir, std::min(s.x, ex), std::max(s.x, ex), std::min(s.z, ez), std::max(s.z, ez), false, false};
    g.chest = rnd() < 0.3;
    segs.push_back(g);
    if (s.d < 5) for (int nd : {s.dir, (s.dir + 1) & 3, (s.dir + 3) & 3}) if (rnd() < (nd == s.dir ? 0.6 : 0.45)) q.push_back({ex, ez, nd, s.d + 1});
  }
  if (segs.size() < 5) return false;
  MineSeg& sp = segs[1 + jsFloor(rnd() * (segs.size() - 1))]; sp.spider = true;
  int x0 = x, x1 = x, z0 = z, z1 = z;
  for (const auto& s : segs) { x0 = std::min(x0, s.x0 - 3); x1 = std::max(x1, s.x1 + 3); z0 = std::min(z0, s.z0 - 3); z1 = std::max(z1, s.z1 + 3); }
  const int y = owUnder(yo);
  e->x = x; e->z = z; e->sp[0] = (sp.x + sp.ex) >> 1; e->sp[1] = y; e->sp[2] = (sp.z + sp.ez) >> 1;
  S.x0 = x0; S.z0 = z0; S.x1 = x1; S.z1 = z1; S.cx = x; S.cz = z; S.y = y; S.hashDy = yo - y;
  S.name = "Mineshaft";
  S.data = {{"spawners", json::array({mobAt(e->sp[0], e->sp[1], e->sp[2], "cave_spider")})}};
  S.ext = e;
  return true;
}

static void mineBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const MineExt& M = extOf<MineExt>(S);
  const int y = S.y; const BlockId BED = bid("bedrock"), RAIL = bid("rail");
  for (const auto& s : M.segs) {
    if (s.x1 + 3 < P.bx || s.x0 - 3 > P.bx + 15 || s.z1 + 3 < P.bz || s.z0 - 3 > P.bz + 15) continue;
    const bool alongX = s.dir == 1 || s.dir == 3;
    const int n = std::abs(alongX ? s.ex - s.x : s.ez - s.z);
    for (int k = 0; k <= n; k++) {
      const int ax = alongX ? s.x + DXL[s.dir] * k : s.x, az = alongX ? s.z : s.z + DZL[s.dir] * k;
      for (int wv = -1; wv <= 1; wv++) {
        const int x = alongX ? ax : ax + wv, z = alongX ? az + wv : az;
        if (!P.in(x, z)) continue;
        for (int yy = y; yy <= y + 2; yy++) if (P.get(x, yy, z) != BED) P.put(x, yy, z, "air");
        // over a cave, a bridge of planks
        if (!tSolid(P.get(x, y - 1, z))) P.put(x, y - 1, z, "planks");
        if (wv == 0 && P.h(x, 0, z, 1) < 0.8) P.put(x, y, z, "rail", alongX ? 1 : 0);
        // timbers every fourth step: posts and a beam
        if (k % 4 == 0) {
          if (wv != 0) { P.put(x, y, z, "oak_fence"); P.put(x, y + 1, z, "oak_fence"); }
          P.put(x, y + 2, z, "planks");
        }
        if (P.h(x, 2, z, 3) < 0.06 && k % 4 != 0) P.put(x, y + 2, z, "cobweb");
        if (s.spider && P.h(x, 3, z, 4) < 0.3 && wv != 0) P.put(x, y + 1 + (k & 1), z, "cobweb");
      }
      if (s.chest && k == 5) { const int x = alongX ? ax : ax + 2, z = alongX ? az + 2 : az; if (P.in(x, z)) P.chest(x, y, z, alongX ? 0 : 3, "mineshaft"); }
    }
    // crossings
    for (auto [cx, cz] : {std::pair{s.x, s.z}, std::pair{s.ex, s.ez}}) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      const int x = cx + dx, z = cz + dz; if (!P.in(x, z)) continue;
      for (int yy = y; yy <= y + 3; yy++) if (P.get(x, yy, z) != BED && P.get(x, yy, z) != RAIL) P.put(x, yy, z, "air");
      if (!tSolid(P.get(x, y - 1, z))) P.put(x, y - 1, z, "planks");
    }
  }
  // the cage, wrapped in webs
  P.spawner(M.sp[0], M.sp[1], M.sp[2], "cave_spider"); P.put(M.sp[0], M.sp[1] - 1, M.sp[2], "planks");
  // the way in: a ladder in a timbered shaft from the land above
  const int gh = P.ground(M.x, M.z);
  for (int yy = y; yy <= gh + 3; yy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int x = M.x + dx, z = M.z + dz; if (!P.in(x, z)) continue;
    const bool ring = dx != 0 || dz != 0;
    if (yy > gh) { if (ring && std::abs(dx) + std::abs(dz) == 2 && yy <= gh + 2) P.put(x, yy, z, "oak_fence"); else if (yy == gh + 3 && ring) P.put(x, yy, z, "oak_slab"); else P.put(x, yy, z, "air"); continue; }
    if (ring) { if (yy >= y + 3) P.put(x, yy, z, yy == gh ? "planks" : (std::abs(dx) + std::abs(dz) == 2 ? "log" : "planks")); }
    else P.put(x, yy, z, "air");
  }
  if (P.in(M.x, M.z)) for (int yy = y; yy <= gh; yy++) P.put(M.x, yy, M.z, "ladder", 0);
  if (P.in(M.x, M.z - 1)) for (int yy = y; yy <= gh; yy++) P.put(M.x, yy, M.z - 1, "planks");
}

// ==== The Labyrinth ==================================================================================================
constexpr int MAZE_N = 13, MAZE_C = 3, MAZE_SIZE = MAZE_N * MAZE_C + 1;
struct MazeExt { std::vector<u8> open; std::vector<std::pair<int, int>> dead; int mx0, mz0, hx, hz, ex, ez, stairK; };

static bool mazePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c)) return false;
  const int x0 = x - (MAZE_SIZE >> 1), z0 = z - (MAZE_SIZE >> 1), x1 = x0 + MAZE_SIZE - 1, z1 = z0 + MAZE_SIZE - 1;
  Land L = sLand(t, x0, z0, x1, z1);
  const int yo = std::max(10, std::min(30, L.jlo - 16));
  if (L.jlo - yo < 12) return false;
  const int y = owUnder(yo);
  // carve the maze: cells are open, walls between them are knocked through as it wanders
  auto e = std::make_shared<MazeExt>();
  std::vector<u8>& open = e->open; open.assign(MAZE_SIZE * MAZE_SIZE, 0);
  auto O = [](int gx, int gz) { return gx * MAZE_SIZE + gz; };
  auto cellOpen = [&](int cx, int cz) { for (int a = 0; a < MAZE_C - 1; a++) for (int b = 0; b < MAZE_C - 1; b++) open[O(cx * MAZE_C + 1 + a, cz * MAZE_C + 1 + b)] = 1; };
  std::vector<u8> seen(MAZE_N * MAZE_N, 0); auto S0 = [](int cx, int cz) { return cx * MAZE_N + cz; };
  const int mid = MAZE_N >> 1;
  auto heart = [&](int cx, int cz) { return std::abs(cx - mid) <= 1 && std::abs(cz - mid) <= 1; };
  for (int cx = 0; cx < MAZE_N; cx++) for (int cz = 0; cz < MAZE_N; cz++) if (heart(cx, cz)) seen[S0(cx, cz)] = 1;
  auto knock = [&](int ax, int az, int bx, int bz) {
    cellOpen(bx, bz);
    const int wx = ax == bx ? ax * MAZE_C + 1 : std::max(ax, bx) * MAZE_C, wz = az == bz ? az * MAZE_C + 1 : std::max(az, bz) * MAZE_C;
    for (int t2 = 0; t2 < MAZE_C - 1; t2++) open[O(ax == bx ? wx + t2 : wx, az == bz ? wz + t2 : wz)] = 1;
  };
  const int st0 = mid, st1 = MAZE_N - 1;
  cellOpen(st0, st1); seen[S0(st0, st1)] = 1;
  std::vector<std::pair<int, int>> stack{{st0, st1}};
  while (!stack.empty()) {
    auto [cx, cz] = stack.back();
    std::vector<std::pair<int, int>> opts;
    for (int d = 0; d < 4; d++) { const int nx = cx + DXL[d], nz = cz + DZL[d]; if (nx >= 0 && nz >= 0 && nx < MAZE_N && nz < MAZE_N && !seen[S0(nx, nz)]) opts.push_back({nx, nz}); }
    if (opts.empty()) { stack.pop_back(); continue; }
    auto [nx, nz] = opts[jsFloor(rnd() * opts.size())]; seen[S0(nx, nz)] = 1; knock(cx, cz, nx, nz); stack.push_back({nx, nz});
  }
  // the heart: one great room, open on all four sides
  for (int gx = (mid - 1) * MAZE_C + 1; gx < (mid + 2) * MAZE_C; gx++) for (int gz = (mid - 1) * MAZE_C + 1; gz < (mid + 2) * MAZE_C; gz++) open[O(gx, gz)] = 1;
  for (auto [dx, dz] : {std::pair{0, -2}, {0, 2}, {-2, 0}, {2, 0}}) { const int cx = mid + dx, cz = mid + dz; cellOpen(cx, cz); knock(cx, cz, mid + jsign(dx), mid + jsign(dz)); }
  // dead ends get a chest or an old skeleton
  for (int cx = 0; cx < MAZE_N; cx++) for (int cz = 0; cz < MAZE_N; cz++) {
    if (heart(cx, cz) || (cx == st0 && cz == st1)) continue;
    const int gx = cx * MAZE_C + 1, gz = cz * MAZE_C + 1; int exits = 0;
    exits += open[O(gx - 1, gz)] + open[O(gx + MAZE_C - 1, gz)] + open[O(gx, gz - 1)] + open[O(gx, gz + MAZE_C - 1)];
    if (exits == 1) e->dead.push_back({x0 + gx, z0 + gz});
  }
  json res = json::array();
  for (auto [dx, dz] : e->dead) if (rnd() < 0.18) { const bool sk = rnd() < 0.5; res.push_back(mobAt(dx, y + 1, dz, sk ? "skeleton" : "ghoul")); }
  const int hx = x0 + mid * MAZE_C + 1, hz = z0 + mid * MAZE_C + 1, ex = x0 + st0 * MAZE_C + 1;
  // the stair climbs one step for each block south until it comes out on the land (in band heights: it is longer
  // than the original's where the underground is stretched)
  int stairK = 0; while (stairK < 60 && y + stairK <= t.terrainColumn(ex, z1 + 1 + stairK).y) stairK++;
  e->mx0 = x0; e->mz0 = z0; e->hx = hx; e->hz = hz; e->ex = ex; e->ez = z1; e->stairK = stairK;
  S.x0 = x0 - 1; S.z0 = z0 - 1; S.x1 = x1 + 1; S.z1 = z1 + stairK + 6; S.cx = hx; S.cz = hz; S.y = y; S.hashDy = yo - y;
  S.name = "The Labyrinth";
  S.data = {{"residents", res}, {"boss", {{"at", jat(hx, y + 1, hz)}, {"type", "minotaur"}, {"box", {hx - 3, y, hz - 3, hx + 4, y + 5, hz + 4}}}}};
  S.ext = e;
  return true;
}

static void mazeBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const MazeExt& M = extOf<MazeExt>(S);
  const int y = S.y;
  static const std::vector<const char*> walls = {"stone_bricks", "stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks", "stone_bricks", "mossy_stone_bricks"};
  auto wall = [&](int x, int yy, int z) { return P.pickw(walls, x, yy, z, 1); };
  for (int gz = 0; gz < MAZE_SIZE; gz++) for (int gx = 0; gx < MAZE_SIZE; gx++) {
    const int x = M.mx0 + gx, z = M.mz0 + gz; if (!P.in(x, z)) continue;
    P.put(x, y, z, P.h(x, 0, z, 2) < 0.25 ? "cracked_stone_bricks" : "stone_bricks");
    P.put(x, y + 5, z, "stone_bricks");
    const bool o = M.open[gx * MAZE_SIZE + gz];
    for (int yy = y + 1; yy <= y + 4; yy++) P.put(x, yy, z, o ? "air" : (yy == y + 4 && (gx + gz) % 6 == 0 ? "chiseled_stone_bricks" : wall(x, yy, z)));
    if (o && P.h(x, 3, z, 3) < 0.04) P.put(x, y + 4, z, "cobweb");
    if (o && P.h(x, 4, z, 4) < 0.012) P.put(x, y + 1, z, "bone_block");
  }
  // lamps in the heart, chests at its back
  const int hx = M.hx, hz = M.hz;
  for (auto [a, b] : {std::pair{-3, -3}, {4, -3}, {-3, 4}, {4, 4}}) { P.put(hx + a, y + 1, hz + b, "chiseled_stone_bricks"); P.put(hx + a, y + 2, hz + b, "soul_lantern"); }
  P.chest(hx - 1, y + 1, hz - 3, 2, "maze_heart"); P.chest(hx + 2, y + 1, hz - 3, 2, "maze_heart");
  P.lectern(hx, y + 1, hz + 4, 0, "labyrinth");
  // the dead ends
  for (auto [x, z] : M.dead) if (P.in(x, z)) { const double q = P.h(x, 7, z, 5); if (q < 0.22) P.chest(x, y + 1, z, 2, "maze"); else if (q < 0.4) P.put(x, y + 1, z, "old_torch", 0); }
  // the way down: a stair under a broken gate, south of the maze
  const int ex = M.ex, ez = M.ez;
  for (int k = 0; k <= M.stairK; k++) {
    const int z = ez + 1 + k, floor = y + k;   // one step up for every step south
    for (int dx = -1; dx <= 2; dx++) {
      const int x = ex + dx; if (!P.in(x, z)) continue;
      const bool side = dx == -1 || dx == 2; const int g = P.ground(x, z);
      for (int yy = floor - 1; yy <= floor + 4; yy++) {
        if (yy > g + 1 && side) continue;
        P.put(x, yy, z, side || yy == floor - 1 ? wall(x, yy, z) : yy == floor + 4 ? (floor + 4 > g ? "air" : "stone_bricks") : "air");
      }
      if (!side) P.put(x, floor, z, "stone_brick_stairs", 2);
    }
  }
  for (int dx = 0; dx <= 1; dx++) if (P.in(ex + dx, ez)) for (int yy = y + 1; yy <= y + 4; yy++) P.put(ex + dx, yy, ez, "air");
  // the gate: two worn pillars and a fallen lintel
  const int gz = ez + 1 + M.stairK + 2;
  for (int dx : {-2, 3}) { const int x = ex + dx; if (!P.in(x, gz)) continue; const int gy = P.ground(x, gz); for (int yy = gy + 1; yy <= gy + 4; yy++) P.put(x, yy, gz, yy == gy + 4 ? "chiseled_stone_bricks" : wall(x, yy, gz)); }
  for (int dx = -1; dx <= 2; dx++) { const int x = ex + dx; if (P.in(x, gz + 1) && P.h(x, 9, gz, 6) < 0.6) P.put(x, P.ground(x, gz + 1) + 1, gz + 1, "stone_brick_slab"); }
}

// ---- registration (the original's STRUCTS 0..6, in its order) ----
static void regPlace(const char* key, const char* name, int region, double chance, int margin, int jsIndex,
                decltype(StructureType::plan) plan, decltype(StructureType::build) build) {
  StructureType t; t.key = key; t.name = name; t.region = region; t.chance = chance; t.margin = margin; t.jsIndex = jsIndex;
  t.dims = 1u << DIM_OVERWORLD; t.plan = std::move(plan); t.build = std::move(build);
  registerStructure(std::move(t));
}
void registerStructurePlaces() {
  regPlace("ruin", "Ruins", 176, 0.5, 24, 0, ruinPlan, ruinBuild);
  regPlace("tower", "Mage Tower", 560, 0.6, 48, 1, towerPlan, towerBuild);
  regPlace("desert_temple", "Desert Temple", 420, 0.75, 40, 2, desertPlan, desertBuild);
  regPlace("ocean_temple", "Sunken Temple", 520, 0.65, 40, 3, oceanPlan, oceanBuild);
  regPlace("wreck", "Shipwreck", 300, 0.55, 32, 4, wreckPlan, wreckBuild);
  regPlace("mineshaft", "Mineshaft", 480, 0.55, 64, 5, minePlan, mineBuild);
  regPlace("labyrinth", "The Labyrinth", 720, 0.6, 60, 6, mazePlan, mazeBuild);
}

} // namespace dq
