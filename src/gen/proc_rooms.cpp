// gen/proc_rooms.cpp — furnished rooms. Furniture sits on the floor, against walls, with a walkway.
// Beds are two blocks. Lights need a wall or a ceiling. Ladders and holes are left clear.
#include "gen/proc_rooms.h"
#include "world/dirs.h"
#include <algorithm>

namespace dq {

std::string procWood(const std::string& want) {
  const std::string w = want.empty() ? "oak" : want;
  return hasBlock(w + "_chair") ? w : "oak";
}

std::string pickProcKind(Mulberry32& rnd, const std::string& role, int salt) {
  (void)salt;
  if (role == "entry") return "entry";
  if (role == "boss") return "boss";
  if (role == "spawner") {
    static const char* S[] = {"spawner", "cells", "prison"};
    return S[jsFloor(rnd() * 3)];
  }
  static const char* REST[] = {
    "hall", "loot", "library", "barracks", "dining", "chapel", "kitchen", "armoury",
    "workshop", "gallery", "shrine", "store", "sleeping", "refectory", "scriptorium",
    "crypt", "office", "forge", "well", "vault", "nave"
  };
  return REST[jsFloor(rnd() * 21)];
}

static const ProcPal PROC_PAL[PROC_PAL_N] = {
  {"stone_bricks",      "stone_bricks",      "stone_bricks",      "stone_brick_slab",   "stone_brick_stairs", "old_torch",  "lantern",      "oak"},
  {"mossy_stone_bricks","mossy_cobblestone", "mossy_stone_bricks","stone_brick_slab",   "stone_brick_stairs", "old_torch",  "lantern",      "oak"},
  {"slate_bricks",      "slate_tiles",       "slate_bricks",      "slate_brick_slab",   "slate_brick_stairs", "lantern",    "soul_lantern", "dark_oak"},
  {"nether_bricks",     "nether_bricks",     "nether_bricks",     "nether_brick_slab",  "nether_brick_stairs","soul_torch", "soul_lantern", "crimson"},
  {"planks",            "planks",            "log",               "oak_slab",           "oak_stairs",         "torch",      "lantern",      "oak"},
  {"spruce_planks",     "spruce_planks",     "spruce_log",        "spruce_slab",        "spruce_stairs",      "lantern",    "lantern",      "spruce"},
};

const ProcPal& palette(int pal) {
  return PROC_PAL[std::clamp(pal, 0, PROC_PAL_N - 1)];
}
void applyPalette(ProcRoom& r) {
  const ProcPal& p = palette(r.pal);
  r.pillar = p.pillar; r.slab = p.slab; r.stairs = p.stairs;
  r.light = p.light; r.hangLight = p.hang;
  r.wood = procWood(p.wood);
}
static const float CLUTTER_DENSITY[4] = {0.35f, 0.55f, 0.80f, 0.45f};
float clutterDensity(int c) { return CLUTTER_DENSITY[std::clamp(c, 0, 3)]; }

int rotDir(int dir, int yaw) { return (dir + yaw) & 3; }

void rotOffset(int lx, int lz, int yaw, int& ox, int& oz) {
  switch (yaw & 3) {
    case 0: ox =  lx; oz =  lz; break;
    case 1: ox = -lz; oz =  lx; break;
    case 2: ox = -lx; oz = -lz; break;
    default: ox = lz; oz = -lx; break;
  }
}
void rotCell(const ProcRoom& r, int lx, int lz, int& x, int& z) {
  int ox, oz; rotOffset(lx, lz, r.yaw, ox, oz);
  x = r.x + std::clamp(ox, -r.rx, r.rx);
  z = r.z + std::clamp(oz, -r.rz, r.rz);
}

int pickDungeonPal(int theme, Mulberry32& rnd) {
  const double u = rnd();
  if (theme == 0) return u < 0.45 ? 0 : u < 0.75 ? 1 : u < 0.88 ? 2 : u < 0.94 ? 4 : u < 0.97 ? 5 : 3;
  if (theme == 1) return u < 0.40 ? 1 : u < 0.70 ? 4 : u < 0.85 ? 5 : u < 0.93 ? 0 : 2;
  return              u < 0.50 ? 2 : u < 0.70 ? 5 : u < 0.85 ? 0 : u < 0.93 ? 1 : 4;
}
ProcLayout pickProcLayout(Mulberry32& rnd, int dungeonPal, int dungeonClutter) {
  ProcLayout L;
  L.yaw = jsFloor(rnd() * PROC_YAW_N);
  L.pattern = jsFloor(rnd() * PROC_PATTERN_N);
  L.pal = std::clamp(dungeonPal, 0, PROC_PAL_N - 1);
  L.clutter = std::clamp(dungeonClutter, 0, PROC_CLUTTER_N - 1);
  return L;
}
void applyProcLayout(ProcRoom& r, const ProcLayout& L) {
  r.yaw = L.yaw & 3; r.pattern = L.pattern & 7;
  r.pal = std::clamp(L.pal, 0, PROC_PAL_N - 1);
  r.clutter = std::clamp(L.clutter, 0, PROC_CLUTTER_N - 1);
  r.density = clutterDensity(r.clutter);
  if (r.clutter == 3) r.webs = true;
  applyPalette(r);
}

namespace {

bool solidAt(SP& P, int x, int y, int z) {
  const BlockId id = P.get(x, y, z);
  return id != AIR && id != NOBLOCK && tSolid(id) && !tLiquid(id);
}
bool isAir(SP& P, int x, int y, int z) {
  return P.get(x, y, z) == AIR;
}
bool isLadder(SP& P, int x, int y, int z) {
  const std::string& k = blockKey(P.get(x, y, z));
  return k.find("ladder") != std::string::npos;
}
bool floorOk(SP& P, int x, int y, int z) {
  return P.in(x, z) && isAir(P, x, y, z) && !isLadder(P, x, y, z) && solidAt(P, x, y - 1, z);
}
std::string furn(const ProcRoom& r, const char* piece) {
  return PLK(r.wood + "_" + piece, std::string("oak_") + piece);
}
int faceMeta(int f) { return (f + 2) & 3; }

// interior cell against wall `dir` (0 N, 1 E, 2 S, 3 W), offset `along` along that wall
void wallCell(const ProcRoom& r, int dir, int along, int& x, int& z) {
  dir &= 3;
  const int reach = (dir == 0 || dir == 2) ? r.rz : r.rx;
  const int tx = DIRX[(dir + 1) & 3], tz = DIRZ[(dir + 1) & 3];
  x = r.x + DIRX[dir] * reach + tx * along;
  z = r.z + DIRZ[dir] * reach + tz * along;
}
void wallAt(const ProcRoom& r, int localDir, int along, int& x, int& z) {
  const int d = rotDir(localDir, r.yaw);
  const int span = (d == 0 || d == 2) ? r.rx : r.rz;
  wallCell(r, d, std::clamp(along, -span, span), x, z);
}
void inFrom(const ProcRoom& r, int localDir, int along, int steps, int& x, int& z) {
  const int d = rotDir(localDir, r.yaw);
  wallAt(r, localDir, along, x, z);
  x -= DIRX[d] * steps; z -= DIRZ[d] * steps;
}
bool skipPiece(const ProcRoom& r, Mulberry32& rnd) {
  if (r.clutter == 0) return rnd() < 0.55;
  if (r.clutter == 1) return rnd() < 0.08;
  if (r.clutter == 3) return rnd() < 0.40;
  return false;
}

void putFloor(SP& P, int x, int y, int z, std::string_view k, u8 meta = 0) {
  if (k.empty() || !floorOk(P, x, y, z)) return;
  P.put(x, y, z, k, meta);
}
// back of a north-default model (shelf, bookcase, desk, coffin) against wall `wallToward`
void putAgainst(SP& P, int x, int y, int z, std::string_view k, int wallToward) {
  putFloor(P, x, y, z, k, (u8)(wallToward & 3));
}
void putBarrel(SP& P, int x, int y, int z, int front, const char* table = "common") {
  if (!floorOk(P, x, y, z)) return;
  P.barrel(x, y, z, front & 3, table);
}
int frontTo(int x, int z, int cx, int cz) {
  const int dx = cx - x, dz = cz - z;
  if (std::abs(dx) >= std::abs(dz)) return dx >= 0 ? 1 : 3;
  return dz >= 0 ? 2 : 0;
}
void putHang(SP& P, int x, int y, int z, std::string_view k) {
  if (k.empty() || !P.in(x, z) || !isAir(P, x, y, z) || isLadder(P, x, y, z)) return;
  if (!solidAt(P, x, y + 1, z)) return;
  P.put(x, y, z, k, 1);
}
void putWall(SP& P, int x, int y, int z, std::string_view k, int wallToward) {
  wallToward &= 3;
  if (k.empty() || !P.in(x, z) || !isAir(P, x, y, z) || isLadder(P, x, y, z)) return;
  if (!solidAt(P, x + DIRX[wallToward], y, z + DIRZ[wallToward])) return;
  const std::string ks(k);
  if (ks.find("banner") != std::string::npos) {
    if (!isAir(P, x, y - 1, z) || isLadder(P, x, y - 1, z)) return;
    if (!solidAt(P, x + DIRX[wallToward], y - 1, z + DIRZ[wallToward])) return;
    const u8 f = (u8)(((wallToward + 2) & 3) | 8);
    P.put(x, y, z, k, f);
    P.put(x, y - 1, z, k, (u8)(f | 4));
    return;
  }
  if (ks == "iron_bars") { P.put(x, y, z, k, 0); return; }
  if (ks == "chain") { putHang(P, x, y, z, k); return; }
  static const u8 T[4] = {3, 2, 4, 1};   // torch/soul_torch meta toward the supporting block
  const bool torch = ks.find("torch") != std::string::npos;
  P.put(x, y, z, k, torch ? T[wallToward] : (u8)(wallToward & 3));
}
void placeBed(SP& P, int x, int y, int z, int facing, std::string_view key) {
  const int x2 = x + DIRX[facing & 3], z2 = z + DIRZ[facing & 3];
  if (!floorOk(P, x, y, z) || !floorOk(P, x2, y, z2)) return;
  P.put(x, y, z, key, (u8)(facing & 3));
  P.put(x2, y, z2, key, (u8)((facing & 3) | 4));
}

const char* wallLightOf(const ProcRoom& r) {
  if (hasBlock("sconce") && r.light.find("soul") == std::string::npos) return "sconce";
  return r.light.c_str();
}

void wallLights(SP& P, const ProcRoom& r, Mulberry32& rnd) {
  const int y = r.y + std::min(2, std::max(1, r.H - 2));
  const char* light = wallLightOf(r);
  for (int dir = 0; dir < 4; dir++) {
    if (rnd() >= 0.55 + r.density * 0.3) continue;
    const int reach = (dir == 0 || dir == 2) ? r.rz : r.rx;
    const int along = reach >= 2 ? (rnd() < 0.5 ? -1 : 1) : 0;
    int x, z; wallCell(r, dir, along, x, z);
    putWall(P, x, y, z, light, dir);
  }
  // hanging lights at the four quarter-points, never the centre (ladders and tables live there)
  const int qx = std::max(1, r.rx / 2), qz = std::max(1, r.rz / 2);
  if (rnd() < 0.55) putHang(P, r.x - qx, r.y + r.H - 1, r.z - qz, r.hangLight);
  if (rnd() < 0.45) putHang(P, r.x + qx, r.y + r.H - 1, r.z + qz, r.hangLight);
}

void clutterCorners(SP& P, const ProcRoom& r, Mulberry32& rnd) {
  for (auto [sx, sz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
    const int x = r.x + sx * r.rx, z = r.z + sz * r.rz;
    if (r.webs || rnd() < 0.22) putHang(P, x, r.y + r.H - 1, z, "cobweb");
    else if (rnd() < 0.28 * r.density) putFloor(P, x, r.y, z, rnd() < 0.5 ? "bone_block" : "decorated_pot");
  }
}

void stockBooks(SP& P, int i) {
  if (i < 0) return;
  json items = json::array();
  for (int k = 0; k < 6; k++) items.push_back(json{{"id", "book"}, {"n", 1}});
  P.setEntity(i, json{{"type", "bookcase"}, {"items", items}});
}

void tableAgainst(SP& P, const ProcRoom& r, int dir) {
  int x, z; wallCell(r, dir, 0, x, z);
  putFloor(P, x, r.y, z, furn(r, "table"));
  const int cx = x + DIRX[(dir + 2) & 3], cz = z + DIRZ[(dir + 2) & 3];
  putFloor(P, cx, r.y, cz, furn(r, "chair"), faceMeta(dir));
}

void deskAgainst(SP& P, const ProcRoom& r, int dir) {
  int x, z; wallCell(r, dir, 1, x, z);
  const std::string desk = hasBlock("oak_desk") ? "oak_desk" : furn(r, "table");
  putFloor(P, x, r.y, z, desk, (u8)(dir & 3));   // backboard against the wall
  const int cx = x + DIRX[(dir + 2) & 3], cz = z + DIRZ[(dir + 2) & 3];
  putFloor(P, cx, r.y, cz, furn(r, "chair"), faceMeta(dir));
}

void chiseledBase(SP& P, const ProcRoom& r, int x, int z) {
  if (!hasBlock("chiseled_stone_bricks")) return;
  if (P.in(x, z) && solidAt(P, x, r.y - 1, z)) P.put(x, r.y - 1, z, "chiseled_stone_bricks");
}

void cornerPillars(SP& P, const ProcRoom& r, bool chisel) {
  const int px = std::max(1, r.rx - 1), pz = std::max(1, r.rz - 1);
  for (auto [sx, sz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
    const int x = r.x + sx * px, z = r.z + sz * pz;
    for (int yy = r.y; yy < r.y + r.H; yy++) {
      const bool cap = chisel && (yy == r.y || yy == r.y + r.H - 1) && hasBlock("chiseled_stone_bricks");
      P.put(x, yy, z, cap ? "chiseled_stone_bricks" : r.pillar);
    }
  }
}

void wallBanner(SP& P, const ProcRoom& r, int dir) {
  if (!hasBlock("red_banner")) return;
  const int reach = (dir == 0 || dir == 2) ? r.rx : r.rz;
  int x, z; wallCell(r, dir, reach >= 2 ? 1 : 0, x, z);
  putWall(P, x, r.y + 2, z, "red_banner", dir);
}

bool placeAgainst(SP& P, const ProcRoom& r, Mulberry32& rnd, int localDir, int along, std::string_view k, bool against = true) {
  if (skipPiece(r, rnd)) return false;
  int x, z; wallAt(r, localDir, along, x, z);
  const int wd = rotDir(localDir, r.yaw);
  if (against) putAgainst(P, x, r.y, z, k, wd);
  else putFloor(P, x, r.y, z, k);
  return true;
}
void pewRow(SP& P, const ProcRoom& r, Mulberry32& rnd, int localDir, int along0, int along1, int faceLocal) {
  const int face = rotDir(faceLocal, r.yaw);
  for (int a = along0; a <= along1; a++) {
    if (skipPiece(r, rnd)) continue;
    int x, z; inFrom(r, localDir, a, 1, x, z);
    putFloor(P, x, r.y, z, furn(r, "bench"), faceMeta((face + 2) & 3));
  }
}
void daisSlab(SP& P, const ProcRoom& r, int localDir, int along) {
  int x, z; wallAt(r, localDir, along, x, z);
  if (floorOk(P, x, r.y, z) || solidAt(P, x, r.y, z)) P.put(x, r.y, z, r.slab, 0);
  chiseledBase(P, r, x, z);
}
void extraBarrels(SP& P, const ProcRoom& r, Mulberry32& rnd) {
  for (int d = 0; d < 4; d++) {
    if (rnd() >= 0.45) continue;
    int x, z; wallAt(r, d, rnd() < 0.5 ? -1 : 1, x, z);
    putBarrel(P, x, r.y, z, frontTo(x, z, r.x, r.z));
  }
}

void furnishChapel(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const std::string top = hasBlock("altar") ? "altar" : "lectern";
  const int y = r.y;
  auto altar = [&](int d) {
    daisSlab(P, r, d, 0); daisSlab(P, r, d, -1); daisSlab(P, r, d, 1);
    int x, z; wallAt(r, d, 0, x, z);
    putFloor(P, x, y, z, top, (u8)rotDir(d, r.yaw));
    inFrom(r, d, -1, 0, x, z); putFloor(P, x, y, z, "candle");
    inFrom(r, d,  1, 0, x, z); putFloor(P, x, y, z, "candle");
  };
  switch (pat) {
    case PL_LONG:    altar(2); pewRow(P, r, rnd, 0, -1, 1, 2); break;
    case PL_SHELVES: altar(2);
                     placeAgainst(P, r, rnd, 1, 0, furn(r, "standing_shelf"));
                     placeAgainst(P, r, rnd, 3, 0, furn(r, "standing_shelf")); break;
    case PL_DAIS_N:  altar(0); pewRow(P, r, rnd, 2, -1, 1, 0); pewRow(P, r, rnd, 2, -1, 1, 0); break;
    case PL_DAIS_S:  altar(2); pewRow(P, r, rnd, 0, -1, 1, 2); pewRow(P, r, rnd, 0, -1, 1, 2); break;
    case PL_BEDS_NS: altar(2); pewRow(P, r, rnd, 1, -1, 1, 3); pewRow(P, r, rnd, 3, -1, 1, 1); break;
    case PL_BEDS_EW: altar(2); pewRow(P, r, rnd, 0, -1, 1, 2); pewRow(P, r, rnd, 2, -1, 1, 0); break;
    case PL_L:       altar(2); placeAgainst(P, r, rnd, 3, -1, furn(r, "standing_shelf"));
                     placeAgainst(P, r, rnd, 3,  1, furn(r, "standing_shelf"));
                     pewRow(P, r, rnd, 0, 0, 1, 2); break;
    default:         altar(2); pewRow(P, r, rnd, 0, -1, 1, 2);
                     placeAgainst(P, r, rnd, 1, 0, "brazier", false);
                     placeAgainst(P, r, rnd, 3, 0, "brazier", false);
                     wallBanner(P, r, rotDir(3, r.yaw)); break;
  }
}

void furnishLibrary(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const std::string books = PLK(furn(r, "bookcase"), "bookshelf");
  auto wallBooks = [&](int d) {
    const int span = ((rotDir(d, r.yaw) == 0 || rotDir(d, r.yaw) == 2) ? r.rx : r.rz) - 1;
    for (int a = -span; a <= span; a++) {
      if (std::abs(a) < 1) continue;
      if (skipPiece(r, rnd)) continue;
      int x, z; wallAt(r, d, a, x, z);
      const int wd = rotDir(d, r.yaw);
      for (int yy = r.y; yy < r.y + std::min(3, r.H); yy++)
        stockBooks(P, P.put(x, yy, z, books, (u8)wd));
    }
  };
  auto desk = [&](int d) {
    deskAgainst(P, r, rotDir(d, r.yaw));
    int x, z; wallAt(r, d, 1, x, z);
    putFloor(P, x, r.y, z, "lectern", (u8)rotDir(d, r.yaw));
  };
  switch (pat) {
    case PL_LONG:    desk(2); for (int t = -1; t <= 1; t++) {
                       int x, z; rotCell(r, t, 0, x, z); putFloor(P, x, r.y, z, furn(r, "table")); } break;
    case PL_SHELVES: wallBooks(0); wallBooks(2); desk(1); break;
    case PL_DAIS_N:  desk(0); wallBooks(1); wallBooks(3); break;
    case PL_DAIS_S:  desk(2); wallBooks(1); wallBooks(3); break;
    case PL_BEDS_NS: wallBooks(1); wallBooks(3); desk(2); break;
    case PL_BEDS_EW: wallBooks(0); wallBooks(2); desk(1); break;
    case PL_L:       wallBooks(2); wallBooks(3); desk(1); break;
    default:         wallBooks(0); wallBooks(1); wallBooks(2); wallBooks(3);
                     deskAgainst(P, r, rotDir(2, r.yaw)); break;
  }
}

void furnishBarracks(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const std::string bed = PLK("bed", "red_bed");
  auto bedsOn = [&](int wall, int headDir) {
    const int wd = rotDir(headDir, r.yaw);
    for (int a : {-1, 1}) {
      if (skipPiece(r, rnd)) continue;
      int x, z; inFrom(r, wall, a, 1, x, z);
      placeBed(P, x, r.y, z, wd, bed);
    }
  };
  switch (pat) {
    case PL_LONG:    bedsOn(3, 3); placeAgainst(P, r, rnd, 1, 0, furn(r, "table"), false); break;
    case PL_SHELVES: bedsOn(1, 1); bedsOn(3, 3);
                     placeAgainst(P, r, rnd, 0, 0, furn(r, "standing_shelf")); break;
    case PL_DAIS_N:  bedsOn(0, 0); bedsOn(2, 2); break;
    case PL_DAIS_S:  bedsOn(2, 2); placeAgainst(P, r, rnd, 0, 0, "armor_stand", false); break;
    case PL_BEDS_NS: bedsOn(1, 1); bedsOn(3, 3); break;
    case PL_BEDS_EW: bedsOn(0, 0); bedsOn(2, 2); break;
    case PL_L:       bedsOn(2, 2); bedsOn(3, 3); break;
    default:         bedsOn(0, 0); bedsOn(1, 1); bedsOn(2, 2); bedsOn(3, 3);
                     int x, z; wallAt(r, 2, 0, x, z); putBarrel(P, x, r.y, z, rotDir(0, r.yaw)); break;
  }
}

void furnishHall(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  cornerPillars(P, r, true);
  switch (pat) {
    case PL_LONG:    pewRow(P, r, rnd, 0, -1, 1, 2); break;
    case PL_SHELVES: placeAgainst(P, r, rnd, 0, 0, furn(r, "standing_shelf"));
                     placeAgainst(P, r, rnd, 2, 0, furn(r, "standing_shelf")); break;
    case PL_DAIS_N:  placeAgainst(P, r, rnd, 0, 0, "brazier", false); wallBanner(P, r, rotDir(0, r.yaw)); break;
    case PL_DAIS_S:  placeAgainst(P, r, rnd, 2, 0, "brazier", false); wallBanner(P, r, rotDir(2, r.yaw)); break;
    case PL_BEDS_NS: pewRow(P, r, rnd, 1, -1, 1, 3); pewRow(P, r, rnd, 3, -1, 1, 1); break;
    case PL_BEDS_EW: pewRow(P, r, rnd, 0, -1, 1, 2); pewRow(P, r, rnd, 2, -1, 1, 0); break;
    case PL_L:       placeAgainst(P, r, rnd, 2, 0, furn(r, "bench"), false);
                     placeAgainst(P, r, rnd, 3, 0, furn(r, "bench"), false); break;
    default:         wallBanner(P, r, rotDir(1, r.yaw)); wallBanner(P, r, rotDir(3, r.yaw));
                     placeAgainst(P, r, rnd, 0, 0, furn(r, "stool"), false); break;
  }
}

void furnishDining(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  auto tables = [&](int alongLocal, int faceLocal) {
    const int span = std::min(2, std::max(1, ((alongLocal == 0 || alongLocal == 2) ? r.rx : r.rz) - 2));
    const int face = rotDir(faceLocal, r.yaw);
    for (int t = -span; t <= span; t++) {
      if (skipPiece(r, rnd)) continue;
      int x, z;
      if (alongLocal == 0 || alongLocal == 2) rotCell(r, t, alongLocal == 2 ? -1 : 1, x, z);
      else rotCell(r, alongLocal == 1 ? -1 : 1, t, x, z);
      putFloor(P, x, r.y, z, furn(r, "table"));
      int cx = x + DIRX[(face + 2) & 3], cz = z + DIRZ[(face + 2) & 3];
      putFloor(P, cx, r.y, cz, furn(r, "bench"), faceMeta(face));
    }
  };
  switch (pat) {
    case PL_LONG:    tables(1, 1); break;
    case PL_SHELVES: tables(0, 0); break;
    case PL_DAIS_N:  tables(0, 2); placeAgainst(P, r, rnd, 0, 0, furn(r, "cabinet")); break;
    case PL_DAIS_S:  tables(2, 0); break;
    case PL_BEDS_NS: tables(1, 3); tables(3, 1); break;
    case PL_BEDS_EW: tables(0, 2); tables(2, 0); break;
    case PL_L:       tables(2, 0); tables(3, 1); break;
    default:         tables(0, 2); tables(1, 3); break;
  }
  int x, z; wallAt(r, 3, 0, x, z); putBarrel(P, x, r.y, z, rotDir(1, r.yaw));
  wallAt(r, 1, 0, x, z); putBarrel(P, x, r.y, z, rotDir(3, r.yaw));
}

void furnishKitchen(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const int cook = (pat == PL_DAIS_N) ? 0 : (pat == PL_L) ? 3 : 2;
  placeAgainst(P, r, rnd, cook, 0, PLK("smoker", "furnace"), false);
  placeAgainst(P, r, rnd, cook, 1, "cauldron", false);
  deskAgainst(P, r, rotDir((cook + 2) & 3, r.yaw));
  int x, z; wallAt(r, 3, -1, x, z); putBarrel(P, x, r.y, z, rotDir(1, r.yaw));
  placeAgainst(P, r, rnd, 1, 0, furn(r, "cabinet"));
  if (pat == PL_PACK || pat == PL_SHELVES) placeAgainst(P, r, rnd, 0, 0, "crafting_table", false);
}

void furnishArmoury(SP& P, const ProcRoom& r, Mulberry32& rnd, const ProcChestFn& chest, int pat) {
  auto ch = [&](int x, int y, int z, const char* k, bool mimic) {
    if (!chest || !floorOk(P, x, y, z)) return;
    chest(x, y, z, k, mimic);
  };
  const int rack = (pat == PL_DAIS_N) ? 0 : (pat == PL_DAIS_S) ? 2 : 1;
  placeAgainst(P, r, rnd, rack, 0, PLK("weapon_rack", "armor_stand"), false);
  placeAgainst(P, r, rnd, rack, 1, "armor_stand", false);
  int x, z; wallAt(r, 0, 0, x, z);
  putWall(P, x, r.y + 1, z, PLK("wall_weapon_rack", ""), rotDir(0, r.yaw));
  placeAgainst(P, r, rnd, (rack + 2) & 3, 0, furn(r, "shelf"));
  wallAt(r, (rack + 2) & 3, 1, x, z);
  ch(x, r.y, z, "rare", false);
}

void furnishCells(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  auto bars = [&](int x, int z) {
    if (!P.in(x, z) || isLadder(P, x, r.y, z)) return;
    if (isAir(P, x, r.y, z) || floorOk(P, x, r.y, z)) P.put(x, r.y, z, "iron_bars", 0);
    if (isAir(P, x, r.y + 1, z)) P.put(x, r.y + 1, z, "iron_bars", 0);
  };
  const int d0 = (pat == PL_BEDS_EW || pat == PL_DAIS_N) ? 0 : 1;
  const int d1 = (d0 + 2) & 3;
  const int span = ((rotDir(d0, r.yaw) == 0 || rotDir(d0, r.yaw) == 2) ? r.rx : r.rz) - 1;
  for (int t = -span; t <= span; t++) {
    if (t == 0) continue;
    int x, z; inFrom(r, d0, t, 1, x, z); bars(x, z);
    inFrom(r, d1, t, 1, x, z); bars(x, z);
  }
  placeAgainst(P, r, rnd, d0, 0, furn(r, "stool"), false);
  placeAgainst(P, r, rnd, d1, 0, furn(r, "stool"), false);
  int x, z; wallAt(r, 3, 0, x, z);
  putHang(P, x, r.y + r.H - 1, z, "chain");
  if (pat == PL_PACK || r.clutter == 3) {
    wallAt(r, 2, 1, x, z); putFloor(P, x, r.y, z, "bone_block");
  }
}

void furnishWorkshop(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const int side = (pat == PL_DAIS_N) ? 0 : (pat == PL_L) ? 3 : 1;
  if (r.kind == "forge") {
    placeAgainst(P, r, rnd, side, 0, "anvil", false);
    placeAgainst(P, r, rnd, side, 1, PLK("blast_furnace", "furnace"), false);
    placeAgainst(P, r, rnd, (side + 2) & 3, 0, "brazier", false);
  } else if (r.kind == "office") {
    deskAgainst(P, r, rotDir(side, r.yaw));
    int x, z; wallAt(r, side, 1, x, z);
    putFloor(P, x, r.y, z, "lectern", (u8)rotDir((side + 2) & 3, r.yaw));
    if (pat >= PL_L) placeAgainst(P, r, rnd, (side + 2) & 3, 0, furn(r, "standing_shelf"));
  } else {
    placeAgainst(P, r, rnd, side, 0, "crafting_table", false);
    deskAgainst(P, r, rotDir((side + 2) & 3, r.yaw));
    int x, z; wallAt(r, 1, 0, x, z); putBarrel(P, x, r.y, z, rotDir(3, r.yaw));
  }
}

void furnishCrypt(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const std::string coffin = hasBlock("oak_coffin") ? "oak_coffin" : PLK("grave", "bone_block");
  auto coffins = [&](int d) { placeAgainst(P, r, rnd, d, 0, coffin); };
  switch (pat) {
    case PL_LONG:    coffins(1); coffins(3); break;
    case PL_SHELVES: coffins(0); coffins(2); break;
    case PL_DAIS_N:  coffins(0); coffins(1); coffins(3); break;
    case PL_DAIS_S:  coffins(2); coffins(1); coffins(3); break;
    case PL_BEDS_NS: coffins(1); coffins(3); break;
    case PL_BEDS_EW: coffins(0); coffins(2); break;
    case PL_L:       coffins(2); coffins(3); break;
    default:         coffins(0); coffins(1); coffins(2); coffins(3); break;
  }
  int x, z; wallAt(r, 2, 0, x, z); P.put(x, r.y, z, r.slab, 0);
  putHang(P, r.x - std::max(1, r.rx / 2), r.y + r.H - 1, r.z - std::max(1, r.rz / 2), "soul_lantern");
}

void furnishStore(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  const int wall = (pat == PL_DAIS_N) ? 0 : (pat == PL_BEDS_NS) ? 1 : 3;
  for (int i : {-2, -1, 1}) {
    if (std::abs(i) > ((rotDir(wall, r.yaw) == 0 || rotDir(wall, r.yaw) == 2) ? r.rx : r.rz)) continue;
    int x, z; wallAt(r, wall, i, x, z);
    putBarrel(P, x, r.y, z, rotDir((wall + 2) & 3, r.yaw));
    placeAgainst(P, r, rnd, (wall + 2) & 3, i, furn(r, "standing_shelf"));
  }
  if (pat == PL_PACK || pat == PL_LONG) placeAgainst(P, r, rnd, 2, 0, furn(r, "table"), false);
}

void furnishLoot(SP& P, const ProcRoom& r, Mulberry32& rnd, const ProcChestFn& chest, int pat) {
  auto ch = [&](int x, int y, int z, const char* k, bool mimic) {
    if (!chest || !floorOk(P, x, y, z)) return;
    chest(x, y, z, k, mimic);
  };
  const int back = (pat == PL_DAIS_N) ? 0 : (pat == PL_L) ? 3 : 2;
  int x, z; wallAt(r, back, 0, x, z);
  ch(x, r.y, z, pat == PL_PACK ? "rare" : "common", rnd() < 0.08);
  wallAt(r, back, 1, x, z);
  putBarrel(P, x, r.y, z, frontTo(x, z, r.x, r.z));
  placeAgainst(P, r, rnd, (back + 2) & 3, 0, furn(r, "standing_shelf"));
  placeAgainst(P, r, rnd, 1, 0, "decorated_pot", false);
}

void furnishVault(SP& P, const ProcRoom& r, Mulberry32& rnd, const ProcChestFn& chest, int pat) {
  auto ch = [&](int x, int y, int z, const char* k, bool mimic) {
    if (!chest || !floorOk(P, x, y, z)) return;
    chest(x, y, z, k, mimic);
  };
  const int back = (pat == PL_DAIS_N) ? 0 : 2;
  for (int i = -1; i <= 1; i++) {
    int x, z; wallAt(r, back, i, x, z);
    P.put(x, r.y, z, r.slab, 0);
    if (i == 0) ch(x, r.y + 1, z, "rare", false);
  }
  placeAgainst(P, r, rnd, (back + 2) & 3, 0, "armor_stand", false);
  int x, z; wallAt(r, 3, 0, x, z);
  putWall(P, x, r.y + 1, z, "iron_bars", rotDir(3, r.yaw));
}

void furnishBoss(SP& P, const ProcRoom& r, Mulberry32& rnd, const ProcChestFn& chest, int pat) {
  auto ch = [&](int x, int y, int z, const char* k, bool mimic) {
    if (!chest || !floorOk(P, x, y, z)) return;
    chest(x, y, z, k, mimic);
  };
  cornerPillars(P, r, true);
  const int dais = (pat == PL_DAIS_N) ? 0 : 2;
  for (int a = -2; a <= 2; a++) daisSlab(P, r, dais, a);
  int x, z; wallAt(r, dais, 0, x, z);
  const std::string throne = hasBlock("altar") && r.kind == "nave" ? "altar" : furn(r, "chair");
  const int face = rotDir(dais, r.yaw);
  putFloor(P, x, r.y + 1, z, throne, r.kind == "nave" && hasBlock("altar") ? (u8)face : (u8)faceMeta(face));
  inFrom(r, dais, -2, 1, x, z); putFloor(P, x, r.y, z, "brazier");
  inFrom(r, dais,  2, 1, x, z); putFloor(P, x, r.y, z, "brazier");
  if (r.kind == "boss") {
    wallAt(r, dais, 0, x, z);
    ch(x, r.y + 1, z, "boss", false);
  } else {
    pewRow(P, r, rnd, (dais + 2) & 3, -1, 1, dais);
  }
  wallBanner(P, r, rotDir(3, r.yaw));
}

void furnishSpawner(SP& P, const ProcRoom& r, int pat) {
  P.put(r.x, r.y - 1, r.z, hasBlock("chiseled_stone_bricks") ? "chiseled_stone_bricks" : r.pillar);
  putFloor(P, r.x - r.rx, r.y, r.z - r.rz, "bone_block");
  putFloor(P, r.x + r.rx, r.y, r.z + r.rz, "bone_block");
  const int a = (pat & 1) ? 0 : 1;
  int x, z; wallAt(r, a, 0, x, z); putWall(P, x, r.y + 1, z, "iron_bars", rotDir(a, r.yaw));
  wallAt(r, a + 2, 0, x, z); putWall(P, x, r.y + 1, z, "iron_bars", rotDir(a + 2, r.yaw));
}

void furnishWell(SP& P, const ProcRoom& r) {
  putFloor(P, r.x, r.y, r.z, "cauldron");
  for (auto [dx, dz] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
    P.put(r.x + dx, r.y, r.z + dz, r.slab, 0);
  putHang(P, r.x - std::max(1, r.rx / 2), r.y + r.H - 1, r.z - std::max(1, r.rz / 2), r.hangLight);
}

void furnishEntry(SP& P, const ProcRoom& r, Mulberry32& rnd, int pat) {
  placeAgainst(P, r, rnd, 2, 1, "decorated_pot", false);
  placeAgainst(P, r, rnd, (pat & 1) ? 1 : 3, 0, furn(r, "stool"), false);
  if (pat >= PL_L) {
    int x, z; wallAt(r, 0, 0, x, z);
    putBarrel(P, x, r.y, z, frontTo(x, z, r.x, r.z));
  }
}

} // namespace

void furnishProcRoom(SP& P, const ProcRoom& r, const ProcChestFn& chest) {
  Mulberry32 rnd(r.seed ? r.seed : 1);
  wallLights(P, r, rnd);
  clutterCorners(P, r, rnd);
  const int pat = r.pattern & 7;
  const std::string& k = r.kind;
  if (k == "chapel" || k == "shrine") furnishChapel(P, r, rnd, pat);
  else if (k == "library" || k == "scriptorium") furnishLibrary(P, r, rnd, pat);
  else if (k == "barracks" || k == "sleeping") furnishBarracks(P, r, rnd, pat);
  else if (k == "hall" || k == "gallery") furnishHall(P, r, rnd, pat);
  else if (k == "dining" || k == "refectory") furnishDining(P, r, rnd, pat);
  else if (k == "kitchen") furnishKitchen(P, r, rnd, pat);
  else if (k == "armoury") furnishArmoury(P, r, rnd, chest, pat);
  else if (k == "cells" || k == "prison") furnishCells(P, r, rnd, pat);
  else if (k == "workshop" || k == "forge" || k == "office") furnishWorkshop(P, r, rnd, pat);
  else if (k == "crypt") furnishCrypt(P, r, rnd, pat);
  else if (k == "store") furnishStore(P, r, rnd, pat);
  else if (k == "loot") furnishLoot(P, r, rnd, chest, pat);
  else if (k == "vault") furnishVault(P, r, rnd, chest, pat);
  else if (k == "boss" || k == "nave") furnishBoss(P, r, rnd, chest, pat);
  else if (k == "spawner") furnishSpawner(P, r, pat);
  else if (k == "well") furnishWell(P, r);
  else if (k == "entry") furnishEntry(P, r, rnd, pat);
  else furnishHall(P, r, rnd, pat);
  if (r.clutter >= 2) extraBarrels(P, r, rnd);
}

void furnishPlaced(SP& P, int x, int y, int z, int rx, int rz, int H,
                   const std::string& kind, u32 seed, int pal, int clutter,
                   const ProcChestFn& chest) {
  if (rx < 1 || rz < 1 || H < 2) return;
  ProcRoom pr;
  pr.kind = kind.empty() ? "hall" : kind;
  pr.x = x; pr.y = y; pr.z = z; pr.rx = rx; pr.rz = rz; pr.H = H;
  pr.seed = seed ? seed : 1;
  Mulberry32 rnd(pr.seed);
  applyProcLayout(pr, pickProcLayout(rnd, pal, clutter));
  furnishProcRoom(P, pr, chest);
}

} // namespace dq
