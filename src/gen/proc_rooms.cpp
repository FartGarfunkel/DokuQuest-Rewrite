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
    "crypt", "office", "forge", "well", "vault"
  };
  return REST[jsFloor(rnd() * 20)];
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

void putFloor(SP& P, int x, int y, int z, std::string_view k, u8 meta = 0) {
  if (k.empty() || !floorOk(P, x, y, z)) return;
  P.put(x, y, z, k, meta);
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
    const int reach = (dir == 0 || dir == 2) ? r.rx : r.rz;
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

void tableAgainst(SP& P, const ProcRoom& r, int dir) {
  int x, z; wallCell(r, dir, 0, x, z);
  putFloor(P, x, r.y, z, furn(r, "table"));
  const int cx = x + DIRX[(dir + 2) & 3], cz = z + DIRZ[(dir + 2) & 3];
  putFloor(P, cx, r.y, cz, furn(r, "chair"), faceMeta(dir));
}

void deskAgainst(SP& P, const ProcRoom& r, int dir) {
  int x, z; wallCell(r, dir, 0, x, z);
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

} // namespace

void furnishProcRoom(SP& P, const ProcRoom& r, const ProcChestFn& chest) {
  Mulberry32 rnd(r.seed ? r.seed : 1);
  auto ch = [&](int x, int y, int z, const char* k, bool mimic) {
    if (!chest || !floorOk(P, x, y, z)) return;
    chest(x, y, z, k, mimic);
  };
  wallLights(P, r, rnd);
  clutterCorners(P, r, rnd);

  const std::string& k = r.kind;
  const int y = r.y;
  const int var = jsFloor(rnd() * 4);
  const int side = jsFloor(rnd() * 4);
  const int back = (side + 2) & 3;
  int wx, wz, bx, bz;
  wallCell(r, side, 0, wx, wz);
  wallCell(r, back, 0, bx, bz);

  if (k == "entry") {
    putFloor(P, r.x + r.rx, y, r.z + r.rz, "decorated_pot");
    putFloor(P, wx, y, wz, furn(r, "stool"));
    if (var & 1) putFloor(P, r.x - r.rx, y, r.z - r.rz, "barrel");
  } else if (k == "hall") {
    cornerPillars(P, r, true);
    int x0, z0;
    wallCell(r, side, -1, x0, z0);
    for (int i : {-1, 1}) {
      int x, z; wallCell(r, side, i, x, z);
      putFloor(P, x, y, z, furn(r, "bench"), faceMeta(back));
    }
    if (var == 0) putFloor(P, r.x - 1, y, r.z - 1, furn(r, "table"));
    else if (var == 1) { int x, z; wallCell(r, (side + 1) & 3, 0, x, z); putFloor(P, x, y, z, "brazier"); }
    else if (var == 2) wallBanner(P, r, back);
  } else if (k == "loot") {
    ch(bx, y, bz, var == 0 ? "rare" : "common", rnd() < 0.08);
    int x, z; wallCell(r, back, 1, x, z);
    putFloor(P, x, y, z, "barrel");
    putFloor(P, wx, y, wz, furn(r, "standing_shelf"));
    putFloor(P, r.x + r.rx, y, r.z - r.rz, "decorated_pot");
  } else if (k == "vault") {
    for (int i = -1; i <= 1; i++) {
      int x, z; wallCell(r, back, i, x, z);
      P.put(x, y, z, r.slab, 0);
      if (i == 0) ch(x, y + 1, z, "rare", false);
    }
    putFloor(P, wx, y, wz, "armor_stand");
    int bx1, bz1; wallCell(r, 3, 0, bx1, bz1);
    putWall(P, bx1, y + 1, bz1, "iron_bars", 3);
  } else if (k == "spawner") {
    P.put(r.x, y - 1, r.z, hasBlock("chiseled_stone_bricks") ? "chiseled_stone_bricks" : r.pillar);
    putFloor(P, r.x - r.rx, y, r.z - r.rz, "bone_block");
    putFloor(P, r.x + r.rx, y, r.z + r.rz, "bone_block");
    int x, z; wallCell(r, 3, 0, x, z); putWall(P, x, y + 1, z, "iron_bars", 3);
    wallCell(r, 1, 0, x, z); putWall(P, x, y + 1, z, "iron_bars", 1);
  } else if (k == "library" || k == "scriptorium") {
    for (int t = -(r.rz - 1); t <= (r.rz - 1); t++) {
      if (std::abs(t) < 1) continue;
      for (int yy = y; yy < y + std::min(3, r.H); yy++) {
        P.put(r.x - r.rx, yy, r.z + t, PLK(furn(r, "bookcase"), "bookshelf"));
        P.put(r.x + r.rx, yy, r.z + t, PLK(furn(r, "bookcase"), "bookshelf"));
      }
    }
    if (k == "scriptorium" || var >= 2) deskAgainst(P, r, back);
    else tableAgainst(P, r, back);
    int lx, lz; wallCell(r, back, 1, lx, lz);
    putFloor(P, lx, y, lz, "lectern", faceMeta(side));
    wallCell(r, back, -1, lx, lz);
    putFloor(P, lx, y, lz, "candle");
  } else if (k == "boss" || k == "nave") {
    cornerPillars(P, r, true);
    const int daisZ = r.z + r.rz;
    for (int x = r.x - 2; x <= r.x + 2; x++) {
      if (floorOk(P, x, y, daisZ) || solidAt(P, x, y, daisZ)) P.put(x, y, daisZ, r.slab, 0);
      chiseledBase(P, r, x, daisZ);
    }
    const std::string throne = hasBlock("altar") && k == "nave" ? "altar" : furn(r, "chair");
    putFloor(P, r.x, y + 1, daisZ, throne, k == "nave" && hasBlock("altar") ? (u8)2 : (u8)faceMeta(2));
    putFloor(P, r.x - 2, y, daisZ - 1, "brazier");
    putFloor(P, r.x + 2, y, daisZ - 1, "brazier");
    if (k == "boss") ch(r.x, y + 1, daisZ, "boss", false);
    else {
      for (int i = -1; i <= 1; i++) putFloor(P, r.x + i * 2, y, r.z - 1, furn(r, "bench"), faceMeta(0));
    }
    wallBanner(P, r, 3);
  } else if (k == "barracks" || k == "sleeping") {
    const std::string bed = PLK("bed", "red_bed");
    // heads toward the walls, aisle down the middle
    placeBed(P, r.x - r.rx + 1, y, r.z - 1, 3, bed);
    placeBed(P, r.x - r.rx + 1, y, r.z + 1, 3, bed);
    placeBed(P, r.x + r.rx - 1, y, r.z - 1, 1, bed);
    placeBed(P, r.x + r.rx - 1, y, r.z + 1, 1, bed);
    int x, z; wallCell(r, 2, 0, x, z); putFloor(P, x, y, z, "barrel");
    wallCell(r, 0, 0, x, z); putFloor(P, x, y, z, "armor_stand");
    if (var == 0) { wallCell(r, 2, 1, x, z); putFloor(P, x, y, z, furn(r, "stool")); }
  } else if (k == "dining" || k == "refectory") {
    const int span = std::min(2, r.rz - 2);
    if (var & 1) {
      // tables along the west, aisle down the east
      for (int t = -span; t <= span; t++) {
        putFloor(P, r.x - 1, y, r.z + t, furn(r, "table"));
        putFloor(P, r.x - 2, y, r.z + t, furn(r, "bench"), faceMeta(1));
      }
    } else {
      for (int t = -span; t <= span; t++) {
        putFloor(P, r.x + t, y, r.z - 1, furn(r, "table"));
        putFloor(P, r.x + t, y, r.z - 2, furn(r, "bench"), faceMeta(0));
      }
    }
    putFloor(P, r.x - r.rx, y, r.z, "barrel");
    putFloor(P, r.x + r.rx, y, r.z, "barrel");
  } else if (k == "chapel" || k == "shrine") {
    for (int row = 0; row < 2; row++) {
      const int zz = r.z - r.rz + 1 + row * 2;
      for (int i = -1; i <= 1; i++) putFloor(P, r.x + i, y, zz, furn(r, "bench"), faceMeta(0));
    }
    int azx, azz; wallCell(r, 2, 0, azx, azz);
    if (hasBlock("chiseled_stone_bricks")) P.put(azx, y, azz, "chiseled_stone_bricks");
    const std::string top = hasBlock("altar") ? "altar" : "lectern";
    putFloor(P, azx, y + (hasBlock("chiseled_stone_bricks") ? 1 : 0), azz, top, hasBlock("altar") ? (u8)2 : (u8)faceMeta(2));
    putFloor(P, azx - 1, y, azz, "candle");
    putFloor(P, azx + 1, y, azz, "candle");
    wallBanner(P, r, 3);
    if (var == 3) putFloor(P, r.x - r.rx, y, r.z, "brazier");
  } else if (k == "kitchen") {
    putFloor(P, wx, y, wz, PLK("smoker", "furnace"));
    int x, z; wallCell(r, side, 1, x, z); putFloor(P, x, y, z, "cauldron");
    tableAgainst(P, r, back);
    putFloor(P, r.x - r.rx, y, r.z - r.rz, "barrel");
    putFloor(P, r.x + r.rx, y, r.z - r.rz, furn(r, "cabinet"));
    if (var == 0) putFloor(P, r.x + r.rx, y, r.z + r.rz, "crafting_table");
  } else if (k == "armoury") {
    putFloor(P, wx, y, wz, PLK("weapon_rack", "armor_stand"));
    int x, z; wallCell(r, side, 1, x, z); putFloor(P, x, y, z, "armor_stand");
    int nx, nz; wallCell(r, 0, 0, nx, nz);
    putWall(P, nx, y + 1, nz, PLK("wall_weapon_rack", ""), 0);
    putFloor(P, bx, y, bz, furn(r, "shelf"));
    wallCell(r, back, 1, x, z);
    ch(x, y, z, "rare", false);
  } else if (k == "cells" || k == "prison") {
    auto bars = [&](int x, int z) {
      if (!P.in(x, z) || isLadder(P, x, y, z)) return;
      if (isAir(P, x, y, z) || floorOk(P, x, y, z)) P.put(x, y, z, "iron_bars", 0);
      if (isAir(P, x, y + 1, z)) P.put(x, y + 1, z, "iron_bars", 0);
    };
    for (int t = -(r.rz - 1); t <= (r.rz - 1); t++) {
      if (t == 0) continue;
      bars(r.x - r.rx + 1, r.z + t);
      bars(r.x + r.rx - 1, r.z + t);
    }
    putFloor(P, r.x - r.rx, y, r.z, furn(r, "stool"));
    putFloor(P, r.x + r.rx, y, r.z, furn(r, "stool"));
    putHang(P, r.x - r.rx, y + r.H - 1, r.z, "chain");
    putHang(P, r.x - r.rx, y + r.H - 1, r.z - r.rz, "cobweb");
    if (var == 2) putFloor(P, r.x - r.rx, y, r.z + r.rz, "bone_block");
  } else if (k == "workshop" || k == "forge" || k == "office") {
    if (k == "forge") {
      putFloor(P, wx, y, wz, "anvil");
      int x, z; wallCell(r, side, 1, x, z); putFloor(P, x, y, z, PLK("blast_furnace", "furnace"));
      putFloor(P, bx, y, bz, "brazier");
    } else if (k == "office") {
      deskAgainst(P, r, side);
      int x, z; wallCell(r, side, 1, x, z);
      putFloor(P, x, y, z, "lectern", faceMeta(back));
      if (var >= 2) { wallCell(r, back, 0, x, z); putFloor(P, x, y, z, furn(r, "bookcase").empty() ? "bookshelf" : furn(r, "standing_shelf")); }
    } else {
      putFloor(P, wx, y, wz, "crafting_table");
      tableAgainst(P, r, back);
      putFloor(P, r.x + r.rx, y, r.z, "barrel");
    }
  } else if (k == "crypt") {
    const std::string coffin = hasBlock("oak_coffin") ? "oak_coffin" : PLK("grave", "bone_block");
    putFloor(P, r.x - r.rx, y, r.z, coffin, faceMeta(1));
    putFloor(P, r.x + r.rx, y, r.z, coffin, faceMeta(3));
    if (var >= 2) putFloor(P, r.x, y, r.z - 1, coffin, faceMeta(0));
    P.put(r.x, y, r.z + r.rz, r.slab, 0);
    putHang(P, r.x - std::max(1, r.rx / 2), y + r.H - 1, r.z - std::max(1, r.rz / 2), "soul_lantern");
  } else if (k == "well") {
    putFloor(P, r.x, y, r.z, "cauldron");
    for (auto [dx, dz] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
      P.put(r.x + dx, y, r.z + dz, r.slab, 0);
    putHang(P, r.x - std::max(1, r.rx / 2), y + r.H - 1, r.z - std::max(1, r.rz / 2), r.hangLight);
  } else if (k == "gallery") {
    wallBanner(P, r, 3);
    wallBanner(P, r, 1);
    putFloor(P, r.x - 1, y, r.z - 1, furn(r, "bench"), faceMeta(1));
    if (var == 0) putFloor(P, r.x - r.rx, y, r.z, furn(r, "stool"));
  } else if (k == "store") {
    for (int i = 0; i < 2; i++) {
      putFloor(P, r.x - r.rx, y, r.z - 1 + i, "barrel");
      putFloor(P, r.x + r.rx, y, r.z - 1 + i, furn(r, "standing_shelf"));
    }
    if (var == 1) putFloor(P, r.x, y, r.z + r.rz, furn(r, "table"));
  } else {
    tableAgainst(P, r, side);
  }
}

} // namespace dq
