// gen/structures_places.cpp — game/structures.js: ruins, mage towers, desert temples, sunken temples, shipwrecks,
// mineshafts and the Labyrinth (STRUCTS 0..6), plus jungle temples, trail ruins and woodland mansions
// (gen/structures_overworld_more.cpp). The labyrinth's rooms follow the classic trap dungeon, written
// for this engine. The other places are built block for block as the original builds them.
// Plans decide with the original's heights (jsY) and build at band heights; underground depths go through
// vmap::owUnder. The residents/boss/spawners lists go in StructurePlan::data in the original's shape.
#include "gen/structures_common.h"
#include "gen/proc_rooms.h"
#include "core/itemstack.h"
#include <unordered_map>

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
struct RuinExt { int W, D, style, door, breach0, maxH; double breach1; bool cellar, lectern; int mx0 = 0, mz0 = 0, mx1 = 0, mz1 = 0, ax0 = 0, az0 = 0, aW = 0, aD = 0, aH = 0, aSide = 0; };

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
  e->mx0 = S.x0; e->mz0 = S.z0; e->mx1 = S.x1; e->mz1 = S.z1;
  // a second room, smaller and shorter, on a side wall: a ruin of two rooms, not one box
  if (rnd() < 0.8) {
    e->aSide = (e->door + 1 + jsFloor(rnd() * 2)) & 3;
    e->aW = 4 + jsFloor(rnd() * 2); e->aD = 3 + jsFloor(rnd() * 2); e->aH = std::max(2, e->maxH - 2);
    const int along = e->aW, out = e->aD - 1;
    if (e->aSide == 0) { e->ax0 = S.cx - along / 2; e->az0 = S.z0 - out; }
    else if (e->aSide == 1) { e->ax0 = S.x1; e->az0 = S.cz - along / 2; }
    else if (e->aSide == 2) { e->ax0 = S.cx - along / 2; e->az0 = S.z1; }
    else { e->ax0 = S.x0 - out; e->az0 = S.cz - along / 2; }
    const int ax1 = e->ax0 + ((e->aSide & 1) ? out : along - 1), az1 = e->az0 + ((e->aSide & 1) ? along - 1 : out);
    S.x0 = std::min(S.x0, e->ax0); S.z0 = std::min(S.z0, e->az0); S.x1 = std::max(S.x1, ax1); S.z1 = std::max(S.z1, az1);
  }
  S.y = L.mean; S.hashDy = L.jmean - L.mean; S.surface = true;
  S.name = style == 1 ? "Sandstone Ruins" : "Ruins";
  S.data = {{"spawners", json::array()}};
  S.ext = e;
  return true;
}

static void ruinBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const RuinExt& R = extOf<RuinExt>(S); const RuinPal& pal = ruinPal(R.style);
  const bool sand = R.style == 1;
  const int x0m = R.mx1 > R.mx0 ? R.mx0 : S.x0, z0m = R.mz1 > R.mz0 ? R.mz0 : S.z0, x1m = R.mx1 > R.mx0 ? R.mx1 : S.x1, z1m = R.mz1 > R.mz0 ? R.mz1 : S.z1;
  const int cx = (x0m + x1m) >> 1, cz = (z0m + z1m) >> 1;
  for (int z = z0m - 1; z <= z1m + 1; z++) for (int x = x0m - 1; x <= x1m + 1; x++) {
    if (!P.in(x, z)) continue;
    const bool out = x < x0m || x > x1m || z < z0m || z > z1m; const int gh = P.ground(x, z);
    if (out) { if (gh < S.y) P.fill(x, gh + 1, z, x, S.y, z, sand ? "sand" : "dirt"); if (gh >= S.y) P.put(x, S.y, z, sand ? "sand" : "grass"); continue; }
    // the floor, on a footing down to the land
    for (int y = std::min(gh, S.y) - 2; y < S.y; y++) P.put(x, y, z, pal.found);
    for (int y = S.y + 1; y <= std::max(gh, S.y) + 3; y++) P.put(x, y, z, "air");
    const double fl = P.h(x, 0, z, 1);
    P.put(x, S.y, z, fl < 0.15 ? (sand ? "sand" : "grass") : std::string_view(P.pick(pal.floor, x, 0, z, 2)));
    const bool edge = x == x0m || x == x1m || z == z0m || z == z1m, corner = (x == x0m || x == x1m) && (z == z0m || z == z1m);
    if (edge) {
      // the walls, broken down to stumps where they fell, worst at the breach
      const int side = z == z0m ? 0 : x == x1m ? 1 : z == z1m ? 2 : 3;
      const double along = side == 0 || side == 2 ? (x - x0m) / (double)(R.W - 1) : (z - z0m) / (double)(R.D - 1);
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
  } else P.chest(x0m + 1, S.y + 1, z0m + 1, 2, "ruin");
  // the smaller room off one wall, with a door through into the main hall
  if (R.aW > 0) {
    const bool odd = R.aSide & 1;
    const int ax0 = R.ax0, az0 = R.az0, ax1 = R.ax0 + (odd ? R.aD : R.aW) - 1, az1 = R.az0 + (odd ? R.aW : R.aD) - 1;
    for (int z = az0; z <= az1; z++) for (int x = ax0; x <= ax1; x++) {
      if (!P.in(x, z)) continue;
      const int gh = P.ground(x, z);
      const bool edge = x == ax0 || x == ax1 || z == az0 || z == az1;
      const bool shared = (R.aSide == 0 && z == z0m) || (R.aSide == 2 && z == z1m) || (R.aSide == 1 && x == x1m) || (R.aSide == 3 && x == x0m);
      for (int y = std::min(gh, S.y) - 1; y < S.y; y++) P.put(x, y, z, pal.found);
      for (int y = S.y + 1; y <= std::max(gh, S.y) + R.aH + 1; y++) P.put(x, y, z, "air");
      P.put(x, S.y, z, P.pick(pal.floor, x, 0, z, 2));
      if (!edge || shared) continue;
      const bool corner = (x == ax0 || x == ax1) && (z == az0 || z == az1);
      for (int y = 1; y <= (corner ? R.aH : std::max(1, R.aH - (P.h(x, 3, z, 5) < 0.4 ? 1 : 0))); y++)
        P.put(x, S.y + y, z, corner ? std::string_view(pal.corner) : std::string_view(P.pick(pal.wall, x, y, z, 5)));
    }
    const int mx = (ax0 + ax1) >> 1, mz = (az0 + az1) >> 1;
    const int doorX = odd ? (R.aSide == 1 ? x1m : x0m) : mx;
    const int doorZ = odd ? mz : (R.aSide == 0 ? z0m : z1m);
    for (int y = 1; y <= 2; y++) P.put(doorX, S.y + y, doorZ, "air");
    P.chest(mx, S.y + 1, mz, R.aSide, "ruin");
    const int arx = std::max(1, (ax1 - ax0) / 2 - 1), arz = std::max(1, (az1 - az0) / 2 - 1);
    Mulberry32 ra(hash32(mx, S.y, mz, 11));
    furnishPlaced(P, mx, S.y + 1, mz, arx, arz, std::max(3, R.aH), pickProcKind(ra, "", 0),
                  hash32(mx, S.y, mz, (i32)S.seed),
                  R.style == 2 ? PAL_ICE : R.style == 1 ? PAL_SAND : PAL_MOSSY, 3);
  }
  const int hrx = std::max(2, (x1m - x0m) / 2 - 1), hrz = std::max(2, (z1m - z0m) / 2 - 1);
  Mulberry32 rh(hash32(cx, S.y, cz, 7));
  furnishPlaced(P, cx, S.y + 1, cz, hrx, hrz, std::max(3, R.maxH), pickProcKind(rh, "", 1),
                hash32(cx, S.y, cz, (i32)S.seed),
                R.style == 2 ? PAL_ICE : R.style == 1 ? PAL_SAND : PAL_MOSSY, 3);
  if (R.cellar)
    furnishPlaced(P, cx, S.y - 4, cz, 1, 1, 3, "store", hash32(cx, S.y, cz, 19),
                  R.style == 2 ? PAL_ICE : R.style == 1 ? PAL_SAND : PAL_MOSSY, 3);
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
  // floors are different rooms: a wide workroom, a small library, a study, then the master's room back out to the full width.
  // The ladder sits inside the smallest of them.
  auto radOf = [&](int level) { return level == 1 ? R - 2 : level == 2 ? R - 1 : R; };
  const int la0 = cx - dx0 * 2, la1 = cz - dz0 * 2;   // up the wall opposite the door, inside every floor
  for (int z = S.z0; z <= S.z1; z++) for (int x = S.x0; x <= S.x1; x++) {
    if (!P.in(x, z)) continue;
    const double d = std::hypot(x - cx, z - cz); const int gh = P.ground(x, z);
    if (d > R + 1.6) continue;
    if (d > R + 0.5) { if (gh < y) P.fill(x, gh + 1, z, x, y - 1, z, "cobblestone"); P.put(x, y, z, "grass"); for (int yy = y + 1; yy <= std::max(gh, y) + 2; yy++) P.put(x, yy, z, "air"); continue; }
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, "cobblestone");
    for (int yy = y; yy <= top; yy++) {
      const int lv = (yy - y) % TOWER_LV, level = (yy - y) / TOWER_LV;
      const int rNow = radOf(level), rFloor = std::max(rNow, level > 0 ? radOf(level - 1) : rNow);
      if (d > rFloor + 0.5) { P.put(x, yy, z, "air"); continue; }   // the setback above a wider floor
      const bool ledge = d > rNow + 0.5;   // still on the wider floor's roof, outside this room
      const bool ring = d > rNow - 0.5 && !ledge;
      if (ledge) { P.put(x, yy, z, lv == 0 ? (yy == y ? "stone_bricks" : P.pick(floors, x, 0, z, 2)) : "air"); continue; }
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
    const int rLib = R - 2;   // the library is the small floor
    if (d > rLib - 1.5 && d <= rLib - 0.5 && !(x == la0 && z == la1) && !(std::abs(x - la0) <= 1 && std::abs(z - la1) <= 1)) {
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
  const int tr = std::max(2, R - 2);
  furnishPlaced(P, cx, L(0), cz, tr, tr, 4, "workshop", hash32(cx, L(0), cz, (i32)S.seed), PAL_SLATE, 1);
  furnishPlaced(P, cx, L(1), cz, std::max(2, R - 3), std::max(2, R - 3), 4, "library", hash32(cx, L(1), cz, (i32)S.seed), PAL_SLATE, 1);
  furnishPlaced(P, cx, L(2), cz, tr, tr, 4, "office", hash32(cx, L(2), cz, (i32)S.seed), PAL_SLATE, 2);
  // the top is the archmage's arena: crystal and chest only
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
      if (ax == 8 && lz == -8)
        furnishPlaced(P, x, y + 1, z, 1, 1, 3, "alcove", hash32(x, y, z, (i32)S.seed), PAL_SAND, 1);
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
  Mulberry32 rd(hash32(cx, y, cz, 3));
  furnishPlaced(P, cx, y + 1, cz, 6, 5, 6, pickProcKind(rd, "", 2),
                hash32(cx, y, cz, (i32)S.seed), PAL_SAND, 1);
  furnishPlaced(P, cx, yf + 1, cz, 2, 2, 3, "vault", hash32(cx, yf, cz, (i32)S.seed), PAL_SAND, 2);
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
  furnishPlaced(P, cx, y + 1, cz, 7, 7, 5, "shrine", hash32(cx, y, cz, (i32)S.seed), PAL_PRISM, 1);
  for (auto [dx, dz] : {std::pair{9, -9}, {9, 9}, {-9, -9}, {-9, 9}})
    furnishPlaced(P, cx + dx, y + 1, cz + dz, 1, 1, 3, "alcove",
                  hash32(cx + dx, y, cz + dz, (i32)S.seed), PAL_PRISM, 1);
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
  furnishPlaced(P, sx, yb + 5, sz, 2, 2, 3, "office", hash32(sx, yb, sz, (i32)S.seed),
                E.wood == "spruce" ? 5 : 4, E.sunk ? 3 : 1);
  furnishPlaced(P, bx, yb + 2, bz, 2, 1, 3, "store", hash32(bx, yb, bz, (i32)S.seed),
                E.wood == "spruce" ? 5 : 4, E.sunk ? 3 : 2);
}

// ==== Mineshafts =====================================================================================================
struct MineSeg { int x, z, ex, ez, dir, x0, x1, z0, z1; bool chest, spider; };
struct MineExt { std::vector<MineSeg> segs; int x, z, sp[3]; };

static bool minePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const OverworldGen* deepOw = owOf(t); if (deepOw && !deepOw->deepStructs()) deepOw = nullptr;
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c) && !deepOw) return false;   // (a deep world's go under the sea too)
  const int yo = 18 + jsFloor(rnd() * 14);
  if (c.jsY - yo < 16 && !deepOw) return false;
  // corridors, branching out from the start
  auto e = std::make_shared<MineExt>(); auto& segs = e->segs;
  const int LIM = 48 + jsFloor(rnd() * 33);   // 48..80
  struct Q { int x, z, dir, d; };
  std::vector<Q> q{{x, z, jsFloor(rnd() * 4), 0}}; size_t qi = 0;
  const int wantSegs = 18 + jsFloor(rnd() * 23);   // 18..40
  while (qi < q.size() && (int)segs.size() < wantSegs) {
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
  for (const auto& s : segs) { x0 = std::min(x0, s.x0 - 10); x1 = std::max(x1, s.x1 + 10); z0 = std::min(z0, s.z0 - 10); z1 = std::max(z1, s.z1 + 10); }
  int y = owUnder(yo);
  // (a deep world, terrain 21: anywhere from the imported data's y -55 to 30, kept 16 under the land)
  if (const OverworldGen* ow = owOf(t); ow && ow->deepStructs()) y = std::min(c.y - 16, -54 + (int)(hash32(t.seed() ^ 0x3A1, x, z, 77) % 86u));
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
    if (s.x1 + 10 < P.bx || s.x0 - 10 > P.bx + 15 || s.z1 + 10 < P.bz || s.z0 - 10 > P.bz + 15) continue;
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
      if (k % 4 == 1) {
        const int wv = ((k / 4) & 1) ? 1 : -1;
        const int x = alongX ? ax : ax + wv, z = alongX ? az + wv : az;
        const int back = (s.dir + 2) & 3;
        static const u8 T[4] = {3, 2, 4, 1};
        if (P.in(x, z) && P.get(x, y + 1, z) == AIR && P.get(x, y, z) != RAIL)
          P.put(x, y + 1, z, s.spider ? "soul_torch" : "torch", T[back]);
      }
      if (s.spider && k % 8 == 4 && P.in(ax, az) && P.get(ax, y + 2, az) == AIR)
        P.put(ax, y + 2, az, "soul_lantern", 1);
      if (s.chest && k == 5) {
        const int side = P.h(ax, 1, az, 7) < 0.5 ? -1 : 1;
        const int ox = alongX ? 0 : side, oz = alongX ? side : 0;
        const int x2 = ax + ox * 2, z2 = az + oz * 2, x3 = ax + ox * 3, z3 = az + oz * 3;
        const int face = alongX ? (side > 0 ? 0 : 2) : (side > 0 ? 3 : 1);
        if (P.in(x3, z3) && P.get(x2, y, z2) != RAIL && P.get(x3, y, z3) != RAIL) {
          for (int yy = y; yy <= y + 1; yy++) {
            if (P.get(x2, yy, z2) != BED && P.get(x2, yy, z2) != RAIL) P.put(x2, yy, z2, "air");
            if (P.get(x3, yy, z3) != BED && P.get(x3, yy, z3) != RAIL) P.put(x3, yy, z3, "air");
          }
          if (!tSolid(P.get(x3, y - 1, z3))) P.put(x3, y - 1, z3, "planks");
          if (P.get(x3, y, z3) != RAIL) P.chest(x3, y, z3, face, "mineshaft");
        }
      }
    }
    for (auto [cx, cz] : {std::pair{s.x, s.z}, std::pair{s.ex, s.ez}}) {
      if (cx == M.x && cz == M.z) continue;
      int deg = 0, inDir = -1;
      for (const auto& o : M.segs) {
        if (o.ex == cx && o.ez == cz) { deg++; inDir = o.dir; }
        else if (o.x == cx && o.z == cz) { deg++; inDir = (o.dir + 2) & 3; }
      }
      if (deg != 1 || inDir < 0) continue;
      const int side = (inDir + (P.h(cx, 2, cz, 13) < 0.5 ? 1 : 3)) & 3;
      const int ox = cx + DXL[side] * 5, oz = cz + DZL[side] * 5;
      const int nx = cx + DXL[side] * 2, nz = cz + DZL[side] * 2;
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
        const int x = ox + dx, z = oz + dz; if (!P.in(x, z) || P.get(x, y, z) == RAIL) continue;
        for (int yy = y; yy <= y + 2; yy++) if (P.get(x, yy, z) != BED) P.put(x, yy, z, "air");
        if (!tSolid(P.get(x, y - 1, z))) P.put(x, y - 1, z, "planks");
      }
      if (P.in(nx, nz) && P.get(nx, y, nz) != RAIL) {
        P.put(nx, y, nz, "air"); P.put(nx, y + 1, nz, "air");
        if (!tSolid(P.get(nx, y - 1, nz))) P.put(nx, y - 1, nz, "planks");
      }
      ProcRoom pr;
      static const char* MK[] = {"office", "store", "forge", "workshop", "cells"};
      pr.kind = MK[jsFloor(P.h(ox, 0, oz, 11) * 5)];
      pr.x = ox; pr.y = y; pr.z = oz; pr.rx = 2; pr.rz = 2; pr.H = 3;
      pr.seed = hash32(ox, y, oz, (i32)S.seed); pr.wood = "oak"; pr.light = "torch"; pr.hangLight = "lantern";
      pr.pillar = "log"; pr.slab = "oak_slab"; pr.density = 0.55f;
      applyProcLayout(pr, ProcLayout{(int)(pr.seed % 4), (int)((pr.seed >> 2) % PROC_PATTERN_N), P.h(ox, 1, oz, 17) < 0.5 ? PAL_OAK : PAL_SPRUCE, 1});
      furnishProcRoom(P, pr);
    }
  }
  // the cage, wrapped in webs
  P.spawner(M.sp[0], M.sp[1], M.sp[2], "cave_spider"); P.put(M.sp[0], M.sp[1] - 1, M.sp[2], "planks");
  // the way in: a ladder in a timbered shaft from the land above
  const int gh = P.ground(M.x, M.z);
  if (const OverworldGen* ow = owOf(w.terrain); ow && ow->deepStructs() && gh <= SEA_LEVEL + 1) return;   // (a deep world's under the sea or a lake: no shaft up into the water)
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
// Buried trap rooms and corridors. No surface stair. A walk stops after 32 corridor segments, 64 calls deep,
// 30000 cells, or 72 blocks from that network's origin. Three mistakes of the source are not repeated: the saved room's
// z uses dz, the library and trophy test the z bound against z, and shaft spikes are placed after the carve.
// Stand-ins this game does not have: no armoured zombie, ice skates, gold arrows, sandstone or permafrost
// plate, mossy log, or permafrost slab.
enum LabKind : u8 {
  LK_AIR = 1, LK_A, LK_B, LK_BA, LK_BB, LK_SLAB, LK_PLATE, LK_SENSOR, LK_DISP,
  LK_CHEST, LK_TCHEST, LK_SPAWN, LK_BOOK, LK_LOG, LK_WEB, LK_SPIKE, LK_LAVA, LK_OBSIDIAN, LK_PLANK
};
constexpr int LD_UP = 1, LD_SOUTH = 3, LD_EAST = 5;

struct LabPos {
  int x, y, z;
  bool operator==(const LabPos& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct LabPosHash { size_t operator()(const LabPos& p) const { return (size_t)hash32(p.x, p.y, p.z, 0x4C4142); } };
struct LabCell { u8 kind = 0, meta = 0, mob = 0; u32 salt = 0; };

struct LabFurnish { int x = 0, y = 0, z = 0, rx = 3, rz = 3, H = 3; std::string kind; };
struct LabExt {
  std::unordered_map<LabPos, LabCell, LabPosHash> cells;
  std::string wallA, wallB, brickA, brickB, slab, plate, plank, log;
  bool cold = false, hasBoss = false;
  int bossX = 0, bossY = 0, bossZ = 0;
  std::vector<LabFurnish> rooms;
};

struct LabGen {
  LabExt& e;
  Mulberry32& rnd;
  int ox = 0, oz = 0;
  int dungeonSize = 0, dungeonLimit = 1, dungeonCount = 0, depth = 0;
  bool treasure = false, library = false, hasLast = false, hasPlatform = false;
  int lastRot = 0, lastX = 0, lastY = 0, lastZ = 0;
  int platRot = 0, platX = 0, platY = 0, platZ = 0;
  struct Pop { int& d; explicit Pop(int& d) : d(d) { ++d; } ~Pop() { --d; } Pop(const Pop&) = delete; };

  LabGen(LabExt& e, Mulberry32& r) : e(e), rnd(r) {}
  int nextInt(int n) { return n <= 0 ? 0 : jsFloor(rnd() * n); }
  bool far(int x, int z) const { return std::max(std::abs(x - ox), std::abs(z - oz)) > 72; }
  bool kept(u8 k) const { return k == LK_BA || k == LK_BB || k == LK_PLANK || k == LK_WEB || k == LK_BOOK || k == LK_SPAWN; }
  bool isKind(int x, int y, int z, u8 k) const {
    auto it = e.cells.find(LabPos{x, y, z});
    return it != e.cells.end() && it->second.kind == k;
  }
  // Unplaced stone is replaceable. Air is only a cell we already opened.
  bool canReplace(int x, int y, int z) {
    if (y <= 11 || y >= 248 || e.cells.size() > 30000) return false;
    auto it = e.cells.find(LabPos{x, y, z});
    if (it == e.cells.end()) return true;
    const u8 k = it->second.kind;
    if (kept(k)) return false;
    if (k == LK_SENSOR || k == LK_DISP || k == LK_A || k == LK_B || k == LK_AIR || k == LK_SLAB) {
      if (k == LK_DISP && (it->second.meta & 7) == LD_UP) {
        LabPos up{x, y + 1, z};
        auto a = e.cells.find(up);
        if (a != e.cells.end() && a->second.kind == LK_PLATE) e.cells[up] = LabCell{LK_AIR, 0, 0, 0};
      }
      return true;
    }
    return false;
  }
  void place(int x, int y, int z, u8 kind, u8 meta = 0, u8 mob = 0, u32 salt = 0) {
    if (y < 0 || y >= 248) return;
    e.cells[LabPos{x, y, z}] = LabCell{kind, meta, mob, salt};
  }
  void placeWallTrap(int x, int y, int z, int dir) {
    place(x, y, z, LK_SENSOR, (u8)((dir & 7) << 4));
    place(x, y - 1, z, LK_DISP, (u8)(dir & 7), 0, hash32(x, y - 1, z, 0xD15B));
  }
  void placeFloorTrap(int x, int y, int z) {
    place(x, y - 1, z, LK_DISP, LD_UP, 0, hash32(x, y - 1, z, 0xD15B));
    place(x, y, z, LK_PLATE);
  }
  void generateBranch(int blockX, int blockY, int blockZ) {
    if (far(blockX, blockZ) || e.cells.size() > 30000 || depth > 64) return;
    Pop pop(depth);
    int xDir = LD_EAST;
    for (int x = blockX - 2; x <= blockX + 2; ++x) {
      const bool xWall = x == blockX - 2 || x == blockX + 2;
      if (x == blockX) xDir ^= 1;   // flips once at the centre and stays flipped for the rest of this sweep
      for (int y = blockY - 2; y <= blockY + 1; ++y) {
        const bool yWall = y == blockY - 2;
        int zDir = LD_SOUTH;        // reset every row; the x facing is not
        for (int z = blockZ - 2; z <= blockZ + 2; ++z) {
          const bool zWall = z == blockZ - 2 || z == blockZ + 2;
          if (z == blockZ) zDir ^= 1;
          if (!canReplace(x, y, z)) continue;
          u8 kind = LK_AIR;
          if (xWall && zWall) kind = nextInt(4) == 0 ? LK_BB : LK_BA;
          else if (xWall || zWall || yWall) kind = LK_B;
          place(x, y, z, kind);
          if (y == blockY) {
            if (kind != LK_AIR && nextInt(32) == 0) placeWallTrap(x, y, z, xWall ? xDir : zDir);
          } else if (y == blockY - 1) {
            if (kind == LK_AIR && nextInt(64) == 0) placeFloorTrap(x, y, z);
          }
        }
      }
    }
    if (dungeonSize < 10) {
      ++dungeonSize;
      const int n = nextInt(4);                 // 0..3, and the loop is inclusive: 1 to 4 corridors
      for (int i = 0; i <= n; ++i) createCorridor(blockX, blockY, blockZ, nextInt(4), 0);
    }
  }

  void generateDrop(int blockX, int blockY, int blockZ) {
    if (far(blockX, blockZ) || e.cells.size() > 30000 || depth > 64) return;
    Pop pop(depth);
    if (rnd() < 0.5) generateDungeon(blockX, blockY, blockZ, false);
    const int dropHeight = nextInt(10) + 10;
    for (int x = blockX - 2; x <= blockX + 2; ++x)
      for (int y = blockY - dropHeight; y <= blockY + 1; ++y)
        for (int z = blockZ - 2; z <= blockZ + 2; ++z) {
          const bool xWall = x == blockX - 2 || x == blockX + 2;
          const bool zWall = z == blockZ - 2 || z == blockZ + 2;
          const bool yWall = y == blockY - dropHeight;
          if (!canReplace(x, y, z)) continue;
          if (xWall || zWall || yWall) { place(x, y, z, LK_B); continue; }
          if (x != blockX && z != blockZ && nextInt(20) == 0 && !isKind(x, y + 1, z, LK_SLAB)) {
            place(x, y, z, LK_SLAB, 0);
            continue;
          }
          place(x, y, z, LK_AIR);
        }
    // Spikes go on after the shaft. Placing them in the loop lets the next row carve them back out.
    if (dungeonSize >= 10) {
      const int fy = blockY - dropHeight + 1;
      for (int x = blockX - 1; x <= blockX + 1; ++x)
        for (int z = blockZ - 1; z <= blockZ + 1; ++z) {
          if (fy <= 11 || fy >= 248) continue;
          auto it = e.cells.find(LabPos{x, fy, z});
          if (it != e.cells.end() && kept(it->second.kind)) continue;
          place(x, fy, z, LK_SPIKE, 0);
        }
    }
    if (dungeonSize < 10) {
      ++dungeonSize;
      const int n = nextInt(4) + 1;
      const int y0 = blockY - (dropHeight - 2);
      for (int i = 1; i <= n; ++i) createCorridor(blockX, y0, blockZ, nextInt(4), 0);
    }
  }

  void generateCorridor(int blockX, int blockY, int blockZ, int rot, int corridorIteration) {
    if (corridorIteration > 32 || far(blockX, blockZ) || e.cells.size() > 30000 || depth > 64) return;
    Pop pop(depth);
    const int xOrZ = rot & 1;
    int xzWidth = 0;
    if (rot == 0) xzWidth = 2;
    else if (rot == 1 || rot == 2) xzWidth = -2;
    else if (rot == 3) xzWidth = 2;
    const int center = xOrZ ? blockX : blockZ;
    int xDir = LD_EAST;
    for (int x = blockX - 2; x <= blockX + 2; ++x) {
      if (x == blockX) xDir ^= 1;
      const bool xWall = std::abs(x - blockX) == 2;
      for (int y = blockY - 2; y <= blockY + 1; ++y) {
        const bool yWall = y == blockY - 2;
        int zDir = LD_SOUTH;
        for (int z = blockZ - 2; z <= blockZ + 2; ++z) {
          if (z == blockZ) zDir ^= 1;
          const bool zWall = std::abs(z - blockZ) == 2;
          if (!canReplace(x, y, z)) continue;
          if ((xWall || zWall || yWall) && isKind(x, y + 1, z, LK_AIR) && nextInt(3) > 0) continue;
          const bool sideWall = xOrZ ? zWall : xWall;
          const int along = xOrZ ? x : z;
          u8 kind = LK_AIR;
          if (yWall) kind = nextInt(3) == 0 ? LK_B : LK_A;
          else if (sideWall || along == center + xzWidth) kind = LK_A;
          const bool hollow = kind == LK_AIR;
          if (hollow && !xWall && !zWall && y == blockY + 1 && nextInt(20) == 0) kind = LK_WEB;
          place(x, y, z, kind);
          if (y == blockY) {
            if (!hollow && nextInt(32) == 0) placeWallTrap(x, y, z, xOrZ ? zDir : xDir);
          } else if (y == blockY - 1) {
            if (hollow && nextInt(64) == 0) placeFloorTrap(x, y, z);
          }
        }
      }
    }
    // && binds tighter than ||. The left-hand nextInt runs whenever this test is reached.
    if (nextInt(2) == 0 && corridorIteration > 1) {
      if (nextInt(2) == 0) generateBranch(blockX, blockY, blockZ);
      else generateDrop(blockX, blockY, blockZ);
    } else if ((nextInt(2) == 0 && corridorIteration > 1 && dungeonSize > 3) || (dungeonSize >= 10 && dungeonCount < dungeonLimit)) {
      createDungeon(blockX, blockY, blockZ, rot);
      ++dungeonCount;
    } else {
      if (nextInt(10) == 0 && corridorIteration > 1 && dungeonSize > 5) return;
      createCorridor(blockX, blockY, blockZ, rot, corridorIteration + 1);
    }
  }

  void createCorridor(int blockX, int blockY, int blockZ, int rot, int iteration) {
    if (rot == 0) generateCorridor(blockX, blockY, blockZ + 4, 0, iteration);
    else if (rot == 1) generateCorridor(blockX - 4, blockY, blockZ, 1, iteration);
    else if (rot == 2) generateCorridor(blockX, blockY, blockZ - 4, 2, iteration);
    else if (rot == 3) generateCorridor(blockX + 4, blockY, blockZ, 3, iteration);
  }

  void generateDungeon(int blockX, int blockY, int blockZ, bool doSpawner) {
    if (blockY < 10 || far(blockX, blockZ)) return;
    const int size = 4;
    for (int x = blockX - size; x <= blockX + size; ++x)
      for (int y = blockY - 2; y <= blockY + 2; ++y)
        for (int z = blockZ - size; z <= blockZ + size; ++z) {
          const bool xWall = x == blockX - size || x == blockX + size;
          const bool zWall = z == blockZ - size || z == blockZ + size;
          if (!canReplace(x, y, z)) continue;
          if (xWall || zWall) { place(x, y, z, LK_B); continue; }
          if (y == blockY - 2) { place(x, y, z, nextInt(5) == 0 ? LK_B : LK_A); continue; }
          place(x, y, z, LK_AIR);
        }
    const int chestX = blockX + nextInt(3) - 3, chestZ = blockZ + nextInt(3) - 3;
    if (canReplace(chestX, blockY - 2, chestZ)) place(chestX, blockY - 1, chestZ, LK_CHEST);
    if (doSpawner) {
      // Warm: skeleton and ghoul, even. Cold adds a stray. No armoured zombie (this game has no such mob).
      const u8 mob = (u8)nextInt(e.cold ? 3 : 2);
      place(blockX, blockY - 1, blockZ, LK_SPAWN, 0, mob);
    }
    static const char* DK[] = {"crypt", "armoury", "store", "office"};
    e.rooms.push_back({blockX, blockY - 1, blockZ, 3, 3, 4, DK[nextInt(4)]});
  }

  void cornerPost(int x, int y, int z, int blockX, int blockZ, int zRoom) {
    if (zRoom % 2 == 0 && (x == blockX - 2 || x == blockX + 2) && (z == blockZ - 2 || z == blockZ + 2))
      place(x, y, z, LK_BA);
  }

  void generateLibrary(int blockX, int blockY, int blockZ) {
    if (blockY < 10 || far(blockX, blockZ)) return;
    const int size = 10;
    for (int x = blockX - size; x <= blockX + size; ++x)
      for (int y = blockY - 2; y <= blockY + 3; ++y)
        for (int z = blockZ - size; z <= blockZ + size; ++z) {
          const int xRoom = x - blockX + size, zRoom = z - blockZ + size;
          const bool xWall = x == blockX - size || x == blockX + size;
          const bool zWall = z == blockZ - size || z == blockZ + size;
          if (!canReplace(x, y, z)) continue;
          if (xWall) place(x, y, z, zRoom % 4 != 0 ? LK_B : LK_BA);
          else if (zWall) place(x, y, z, xRoom % 4 != 0 ? LK_B : LK_BA);
          else if (y == blockY - 2) {
            if ((x > blockX - 2 && x < blockX + 2) || (z > blockZ - 2 && z < blockZ + 2)) place(x, y, z, LK_PLANK);
            else place(x, y, z, nextInt(5) == 0 ? LK_B : LK_A);
            if (x > blockX - 3 && x < blockX + 3 && z > blockZ - 3 && z < blockZ + 3) place(x, y, z, LK_B);
          } else if (y == blockY + 3) {
            place(x, y, z, nextInt(5) == 0 ? LK_B : LK_A);
          } else if ((x > blockX - size && x < blockX + size) || (z > blockZ - size && z < blockZ + size)) {
            const bool aisle = xRoom % 4 == 0 || zRoom % 2 != 0 || (x > blockX - 2 && x < blockX + 2) ||
                               (z > blockZ - 1 && z < blockZ + 1) || y >= blockY + 2;
            if (!aisle) {
              if (xRoom % 2 == 0) place(x, y, z, LK_BOOK);
              else { nextInt(5); place(x, y, z, LK_LOG); }   // mossy log and plain log are both this game's log
            } else if (y == blockY - 1 && std::min(std::abs(x - blockX), std::abs(z - blockZ)) >= 2 && nextInt(64) == 0) {
              placeFloorTrap(x, y, z);
            } else place(x, y, z, LK_AIR);
          } else place(x, y, z, LK_AIR);
          cornerPost(x, y, z, blockX, blockZ, zRoom);
        }
    generateDrop(blockX, blockY, blockZ);
    // shelves already stand; furnishLibrary would land on aisle plates
  }

  void generateTrophy(int blockX, int blockY, int blockZ, int rot) {
    if (blockY < 10 || far(blockX, blockZ)) return;
    const int size = 8;
    for (int x = blockX - size; x <= blockX + size; ++x)
      for (int y = blockY - 2; y <= blockY + 6; ++y)
        for (int z = blockZ - size; z <= blockZ + size; ++z) {
          const int xRoom = x - blockX + size, zRoom = z - blockZ + size;
          const bool xWall = x == blockX - size || x == blockX + size;
          const bool zWall = z == blockZ - size || z == blockZ + size;
          if (!canReplace(x, y, z)) continue;
          if (xWall) place(x, y, z, zRoom % 4 != 0 ? LK_B : LK_BA);
          else if (zWall) place(x, y, z, xRoom % 4 != 0 ? LK_B : LK_BA);
          else if (y == blockY - 2) {
            if ((x > blockX - 2 && x < blockX + 2) || (z > blockZ - 2 && z < blockZ + 2)) {
              if (x == blockX || z == blockZ) place(x, y, z, e.cold ? LK_OBSIDIAN : LK_LAVA);
              else place(x, y, z, LK_SLAB, 1);
            } else place(x, y, z, nextInt(5) == 0 ? LK_B : LK_A);
            if (x > blockX - 3 && x < blockX + 3 && z > blockZ - 3 && z < blockZ + 3) place(x, y, z, LK_B);
          } else if (y == blockY + 6) {
            place(x, y, z, nextInt(5) == 0 ? LK_B : LK_A);
          } else place(x, y, z, LK_AIR);
          cornerPost(x, y, z, blockX, blockZ, zRoom);
        }
    e.rooms.erase(std::remove_if(e.rooms.begin(), e.rooms.end(),
        [&](const LabFurnish& r) { return r.x == blockX && r.z == blockZ && r.y == blockY - 1; }),
        e.rooms.end());
    static const char* TK[] = {"chapel", "armoury", "crypt"};
    e.rooms.push_back({blockX, blockY - 1, blockZ, 6, 6, 6, TK[nextInt(3)]});
    hasPlatform = true;
    platRot = rot; platX = blockX; platY = blockY; platZ = blockZ;
  }

  void generateStatue(int rot, int blockX, int blockY, int blockZ) {
    (void)rot;   // the source turned a statue with this. The minotaur has no facing block.
    if (e.hasBoss) return;
    for (int x = blockX - 1; x <= blockX + 1; ++x)
      for (int z = blockZ - 1; z <= blockZ + 1; ++z)
        place(x, blockY - 1, z, LK_SLAB, (x == blockX && z == blockZ) ? 1 : 0);
    // The east slab becomes the chest. Nothing is placed in the centre at the boss's feet.
    place(blockX + 1, blockY - 1, blockZ, e.cold ? LK_CHEST : LK_TCHEST);
    e.hasBoss = true;
    e.bossX = blockX; e.bossY = blockY; e.bossZ = blockZ;
  }

  void createDungeon(int blockX, int blockY, int blockZ, int rot) {
    int dx = 0, dz = 0;
    if (rot == 0) dz = 1;
    else if (rot == 1) dx = -1;
    else if (rot == 2) dz = -1;
    else if (rot == 3) dx = 1;
    const int rx = blockX + dx * 4, rz = blockZ + dz * 4;
    if (!canReplace(blockX + dx * 5, blockY, blockZ + dz * 5) || far(rx, rz)) return;
    if (!treasure && dungeonSize == 10) {
      treasure = true;
      generateTrophy(rx, blockY, rz, rot);
    } else if (!library && nextInt(3) == 0) {
      library = true;
      generateLibrary(rx, blockY, rz);
    } else {
      generateDungeon(rx, blockY, rz, true);
      hasLast = true; lastRot = rot; lastX = rx; lastY = blockY; lastZ = rz;
    }
  }

  void run(int x, int y, int z) {
    ox = x; oz = z;
    dungeonSize = 0; dungeonCount = 0; dungeonLimit = 1; depth = 0;
    treasure = library = hasLast = hasPlatform = false;
    if (y <= 11 || y >= 248 || !canReplace(x, y, z)) return;
    generateBranch(x, y, z);
    if (!treasure && hasLast) generateTrophy(lastX, lastY, lastZ, lastRot);   // does not set treasure first
    if (hasPlatform) generateStatue(platRot, platX, platY, platZ);
  }
};

static bool mazePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c)) return false;
  Land L = sLand(t, x - 16, z - 16, x + 16, z + 16);
  const int yo = std::max(10, std::min(30, L.jlo - 16));
  const int y = owUnder(yo);
  if (y <= 11) return false;
  auto e = std::make_shared<LabExt>();
  OwColumn oc; oc.h = c.jsY; oc.temp = c.temp; oc.humid = c.humid; oc.landBiome = c.landBiome;
  if (c.biome >= 0 && c.biome < BIOME_COUNT) oc.biome = (u8)c.biome;
  const LandVariant bv = landVariant(oc);
  const bool desert = c.biome == BIOME_DESERT;
  const bool cold = c.biome == BIOME_TAIGA || bv == BV_GLACIER || (bv == BV_TUNDRA && (t.genVersion() < 19 || c.temp < -0.38));
  if (desert) {
    e->wallA = e->wallB = "sandstone";
    e->brickA = e->brickB = PLK("desert_sandstone_bricks", "cut_sandstone");
    e->slab = "sandstone_slab";
  } else if (cold) {
    e->wallA = "permafrost"; e->wallB = "cobbled_permafrost";
    e->brickA = e->brickB = "permafrost_bricks";
    e->slab = hasBlock("polished_permafrost_slab") ? "polished_permafrost_slab" : PLK("stone_slab", "smooth_stone_slab");
    e->cold = true;
  } else {
    e->wallA = "cobblestone"; e->wallB = "mossy_cobblestone";
    e->brickA = "stone_bricks"; e->brickB = "mossy_stone_bricks";
    e->slab = "smooth_stone_slab";
  }
  e->plate = "stone_pressure_plate";   // no sandstone or permafrost plate in this game
  e->plank = "planks"; e->log = "log";
  LabGen g(*e, rnd);
  g.run(x, y, z);
  if (rnd() < 0.2) {
    const int y2 = y - (30 + jsFloor(rnd() * 10));
    if (y2 > 11) g.run(x, y2, z);       // its own origin for the 72-block cap; the boss flag is kept
  }
  if (e->cells.empty()) return false;
  int x0 = 0, x1 = 0, z0 = 0, z1 = 0; bool any = false;
  for (const auto& [p, cell] : e->cells) {
    (void)cell;
    if (!any) { x0 = x1 = p.x; z0 = z1 = p.z; any = true; }
    else { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); z0 = std::min(z0, p.z); z1 = std::max(z1, p.z); }
  }
  S.x0 = x0; S.z0 = z0; S.x1 = x1; S.z1 = z1; S.cx = x; S.cz = z; S.y = y; S.hashDy = yo - y;
  S.name = "The Labyrinth";
  if (e->hasBoss)
    S.data = {{"boss", {{"at", jat(e->bossX, e->bossY, e->bossZ)}, {"type", "minotaur"},
                        {"box", {e->bossX - 8, e->bossY, e->bossZ - 8, e->bossX + 8, e->bossY + 8, e->bossZ + 8}}}}};
  S.ext = e;
  return true;
}

static json labyrinthDispenser(u32 salt) {
  json items = json::array();
  for (int s = 0; s < 9; s++) items.push_back(nullptr);
  Mulberry32 r(salt);
  for (int n = 0; n < 3; n++) {
    const double w = r() * 600.0;
    ItemStack st;
    if (w < 300.0) st = ItemStack("arrow", 5 + jsFloor(r() * 3));
    else if (w < 310.0) st = ItemStack("arrow", 1);     // the gold arrow
    else if (w < 310.5) st = ItemStack("tnt", 1);       // the explosive charge
    else continue;
    items[jsFloor(r() * 9)] = st.toJson();
  }
  return items;
}

static void mazeBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const LabExt& M = extOf<LabExt>(S);
  for (const auto& [pos, cell] : M.cells) {
    if (!P.in(pos.x, pos.z)) continue;
    const int x = pos.x, y = pos.y, z = pos.z;
    switch (cell.kind) {
    case LK_AIR: P.put(x, y, z, "air"); break;
    case LK_A: P.put(x, y, z, M.wallA); break;
    case LK_B: P.put(x, y, z, M.wallB); break;
    case LK_BA: P.put(x, y, z, M.brickA); break;
    case LK_BB: P.put(x, y, z, M.brickB); break;
    case LK_SLAB: P.put(x, y, z, M.slab, cell.meta); break;
    case LK_PLATE: P.put(x, y, z, M.plate); break;
    case LK_SENSOR: P.put(x, y, z, "motion_sensor", cell.meta); break;
    case LK_DISP: {
      const int i = P.put(x, y, z, "dispenser", cell.meta);
      if (i >= 0) P.setEntity(i, json{{"type", "dispenser"}, {"items", labyrinthDispenser(cell.salt)}});
      break;
    }
    case LK_CHEST: P.chest(x, y, z, 2, "maze"); break;
    case LK_TCHEST: {
      const int i = P.chest(x, y, z, 2, "maze");
      if (i >= 0 && hasItem("golden_quiver"))
        if (json* be = P.entity(i))
          if (be->contains("items") && (*be)["items"].is_array() && !(*be)["items"].empty())
            (*be)["items"][0] = ItemStack("golden_quiver", 1).toJson();
      break;
    }
    case LK_SPAWN: P.spawner(x, y, z, cell.mob == 2 ? "stray" : cell.mob == 1 ? "ghoul" : "skeleton"); break;
    case LK_BOOK: P.put(x, y, z, "bookshelf"); break;
    case LK_LOG: P.put(x, y, z, M.log); break;
    case LK_WEB: P.put(x, y, z, "cobweb"); break;
    case LK_SPIKE: P.put(x, y, z, "spike_trap", 0); break;
    case LK_LAVA: P.put(x, y, z, "lava"); break;
    case LK_OBSIDIAN: P.put(x, y, z, "obsidian"); break;
    case LK_PLANK: P.put(x, y, z, M.plank); break;
    default: break;
    }
  }
  for (const auto& r : M.rooms) {
    if (r.kind == "library") continue;
    if (r.rx >= 6) continue;   // trophy hall stays empty for the fight
    furnishPlaced(P, r.x, r.y, r.z, r.rx, r.rz, r.H, r.kind,
                  hash32(r.x, r.y, r.z, (i32)S.seed),
                  M.cold ? PAL_ICE : M.wallA == "sandstone" ? PAL_SAND : PAL_MOSSY, 1);
  }
}

bool jungleTemplePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S);
void jungleTempleBuild(ChunkWriter& w, const StructurePlan& S);
bool trailRuinsPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S);
void trailRuinsBuild(ChunkWriter& w, const StructurePlan& S);
bool woodlandMansionPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S);
void woodlandMansionBuild(ChunkWriter& w, const StructurePlan& S);

// ---- registration (the original's STRUCTS 0..6, in its order) ----
static void regPlace(const char* key, const char* name, int region, double chance, int margin, int jsIndex,
                decltype(StructureType::plan) plan, decltype(StructureType::build) build) {
  StructureType t; t.key = key; t.name = name; t.region = region; t.chance = chance; t.margin = margin; t.jsIndex = jsIndex;
  t.dims = 1u << DIM_OVERWORLD; t.plan = std::move(plan); t.build = std::move(build);
  if (t.key.rfind("mineshaft", 0) == 0) t.flags |= SF_DEEP;
  registerStructure(std::move(t));
}
void registerStructurePlaces() {
  regPlace("ruin", "Ruins", 176, 0.5, 24, 0, ruinPlan, ruinBuild);
  regPlace("tower", "Mage Tower", 560, 0.6, 48, 1, towerPlan, towerBuild);
  regPlace("desert_temple", "Desert Temple", 420, 0.75, 40, 2, desertPlan, desertBuild);
  regPlace("ocean_temple", "Sunken Temple", 520, 0.65, 40, 3, oceanPlan, oceanBuild);
  regPlace("wreck", "Shipwreck", 300, 0.55, 32, 4, wreckPlan, wreckBuild);
  // a deep world (terrain 21) has many more (0.3% of chunks, about one per 290 blocks square): its own
  // kind on a 256-block grid; the old one stands aside there (other worlds keep theirs where they were)
  auto deepOn = [](const TerrainQuery& t) { const OverworldGen* ow = owOf(t); return ow && ow->deepStructs(); };
  regPlace("mineshaft", "Mineshaft", 480, 0.55, 64, 5, [deepOn](const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) { return !deepOn(t) && minePlan(t, x, z, rnd, S); }, mineBuild);
  regPlace("labyrinth", "The Labyrinth", 720, 0.6, 60, 6, mazePlan, mazeBuild);
  // Minecraft jungle pyramid: 32-chunk spacing → 512 blocks (chance is high; biome plan() is the filter)
  regPlace("jungle_temple", "Jungle Temple", 512, 0.70, 48, -1, jungleTemplePlan, jungleTempleBuild);
  // Minecraft trail ruins: ~34-chunk spacing → 544 blocks; taiga-family, uncommon
  regPlace("trail_ruins", "Trail Ruins", 544, 0.40, 40, -1, trailRuinsPlan, trailRuinsBuild);
  // Minecraft woodland mansion: 80-chunk spacing → 1280 blocks; rarest overworld structure
  regPlace("woodland_mansion", "Woodland Mansion", 1280, 0.80, 80, -1, woodlandMansionPlan, woodlandMansionBuild);
}

// registered after every other kind, so their indices (their seeds) stay as they were
void registerStructureDeepMines() {
  regPlace("mineshaft_deep", "Mineshaft", 256, 0.85, 40, 5, [](const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
    const OverworldGen* ow = owOf(t); return ow && ow->deepStructs() && minePlan(t, x, z, rnd, S); }, mineBuild);
}

} // namespace dq
