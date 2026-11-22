// gen/structures_vault.cpp — one buried vault. Three rooms share a plan the test and the builder both use.
#include "gen/structures_vault.h"
#include "gen/structures_common.h"
#include "gen/proc_rooms.h"
#include <algorithm>

namespace dq {

namespace {
constexpr int kReg = 192;
constexpr int kFloor = 0;     // mossy cobblestone
constexpr int kAir0 = 1;      // lever, door lower, chest, spawner
constexpr int kAir1 = 4;
constexpr int kCover = 6;     // land left over the ceiling, before the shaft opens it

// Closed door is a full slab on +Z when facing 0. Turn that slab onto the vault side of the cell
// so the passage (lever -> door -> vault) is the thin axis.
int doorFacing(int dx, int dz) {
  if (dx > 0) return 3;
  if (dx < 0) return 1;
  if (dz > 0) return 0;
  return 2;
}
} // namespace

bool planWayvault(uint32_t seed, Wayvault& out) {
  out = Wayvault{};
  // Mirrors keep the half-extents. Rotating would swap the hall's 6 and 2.
  const int mx = (seed & 1u) ? -1 : 1;
  const int mz = (seed & 2u) ? -1 : 1;
  auto X = [&](int x) { return x * mx; };
  auto Z = [&](int z) { return z * mz; };
  // Hall centred on the origin, vault past a one-block wall to the east, ante a pocket off the
  // north-east corner. The lever cell is that corner: inside the hall and inside the ante, one
  // step from the door, so a pull reaches it (sim/redstone.cpp, case DOOR).
  Wayvault::Room ante; ante.x = X(4); ante.z = Z(4); ante.rx = 2; ante.rz = 2; ante.role = "ante";
  Wayvault::Room hall; hall.x = X(0); hall.z = Z(0); hall.rx = 6; hall.rz = 2; hall.role = "hall";
  Wayvault::Room vault; vault.x = X(12); vault.z = Z(0); vault.rx = 4; vault.rz = 4; vault.role = "vault";
  out.rooms.push_back(ante);
  out.rooms.push_back(hall);
  out.rooms.push_back(vault);
  out.lever[0] = X(6); out.lever[1] = kAir0; out.lever[2] = Z(2);
  out.door[0] = X(7); out.door[1] = kAir0; out.door[2] = Z(2);
  out.chest[0] = X(12); out.chest[1] = kAir0; out.chest[2] = Z(0);
  out.spawner[0] = X(-3); out.spawner[1] = kAir0; out.spawner[2] = Z(0);   // off the hall's centre, still inside it
  // The column stands on the ante. Its top is relative here (no world surface yet): at least 4 above the ceiling.
  // place raises shaft[1] to the land it sampled.
  out.shaft[0] = ante.x; out.shaft[1] = kWayvaultCeil + 4; out.shaft[2] = ante.z;
  return true;
}

static bool wayvaultPlace(const StructureIndex& idx, const TerrainQuery& t, int rx, int rz, StructurePlan& S) {
  Mulberry32 rnd(hash32(t.seed(), rx, rz, 9241));
  if (rnd() >= 0.2) return false;   // about one try in five
  const int margin = 24;
  const int ox = rx * kReg + margin + jsFloor(rnd() * (kReg - 2 * margin));
  const int oz = rz * kReg + margin + jsFloor(rnd() * (kReg - 2 * margin));
  const u32 layout = hash32(t.seed(), rx, rz, 9243);
  Wayvault v;
  if (!planWayvault(layout, v)) return false;
  int x0 = ox, x1 = ox, z0 = oz, z1 = oz;
  for (const auto& r : v.rooms) {
    x0 = std::min(x0, ox + r.x - r.rx - 1);
    x1 = std::max(x1, ox + r.x + r.rx + 1);
    z0 = std::min(z0, oz + r.z - r.rz - 1);
    z1 = std::max(z1, oz + r.z + r.rz + 1);
  }
  const int mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
  const TerrainColumn col = t.terrainColumn(mx, mz);
  if (!sLandy(col)) return false;   // land above the sea, not a beach
  {
    const int vk = structureTypeIndex("village");
    std::vector<const StructurePlan*> near;
    if (vk >= 0) idx.near(mx, mz, 48, near, vk);
    if (!near.empty()) return false;
  }
  const int y = col.y - (kWayvaultCeil + kCover);
  if (y < 2 || y + kWayvaultCeil >= col.y) return false;
  // shaft[1] is local to the floor. The sampled surface is at least 4 above the ante ceiling, or we lift it there.
  v.shaft[1] = std::max(v.shaft[1], kWayvaultCeil + 4);
  v.shaft[1] = std::max(v.shaft[1], col.y - y);
  S.id = "wv" + std::to_string(rx) + "_" + std::to_string(rz);
  S.seed = layout;   // build replans with this, so the world matches planWayvault
  S.x0 = x0; S.z0 = z0; S.x1 = x1; S.z1 = z1;
  S.cx = ox; S.cz = oz;   // local (0, 0): the hall's centre
  S.y = y;
  S.name = "a wayvault";
  const int sx = ox + v.spawner[0], sy = y + v.spawner[1], sz = oz + v.spawner[2];
  S.data = {{"spawners", json::array({mobAt(sx, sy, sz, "skeleton")})}, {"shaftTop", v.shaft[1]}};
  return true;
}

static void wayvaultBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  Wayvault v;
  if (!planWayvault(S.seed, v)) return;
  const int ox = S.cx, oz = S.cz, oy = S.y;
  auto shellRoom = [&](const Wayvault::Room& r) {
    const int x0 = ox + r.x - r.rx, x1 = ox + r.x + r.rx, z0 = oz + r.z - r.rz, z1 = oz + r.z + r.rz;
    for (int z = z0 - 1; z <= z1 + 1; z++) for (int x = x0 - 1; x <= x1 + 1; x++) {
      if (!P.in(x, z)) continue;
      for (int y = kFloor; y <= kWayvaultCeil; y++) P.put(x, oy + y, z, "cobblestone");
    }
  };
  auto carveRoom = [&](const Wayvault::Room& r) {
    const int x0 = ox + r.x - r.rx, x1 = ox + r.x + r.rx, z0 = oz + r.z - r.rz, z1 = oz + r.z + r.rz;
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
      if (!P.in(x, z)) continue;
      P.put(x, oy + kFloor, z, "mossy_cobblestone");
      for (int y = kAir0; y <= kAir1; y++) P.put(x, oy + y, z, "air");
    }
  };
  for (const auto& r : v.rooms) shellRoom(r);
  for (const auto& r : v.rooms) carveRoom(r);
  const int face = doorFacing(v.door[0] - v.lever[0], v.door[2] - v.lever[2]);
  const int dx = ox + v.door[0], dy = oy + v.door[1], dz = oz + v.door[2];
  P.put(dx, dy, dz, "iron_door", (u8)face);
  P.put(dx, dy + 1, dz, "iron_door", (u8)(face | 16));
  P.put(ox + v.lever[0], oy + v.lever[1], oz + v.lever[2], "lever", 0);
  const int cx = ox + v.chest[0], cy = oy + v.chest[1], cz = oz + v.chest[2];
  const int ci = P.put(cx, cy, cz, "chest", 0);
  if (ci >= 0) {
    Mulberry32 r(hash32(cx, cy, cz, (i32)S.seed));
    P.setEntity(ci, json{{"type", "chest"}, {"items", dungeonLoot("rare", r)}, {"lk", "d:rare"}});
  }
  P.spawner(ox + v.spawner[0], oy + v.spawner[1], oz + v.spawner[2], "skeleton");
  for (const auto& r : v.rooms) {
    const char* k = r.role == "vault" ? "vault" : r.role == "ante" ? "entry" : "hall";
    furnishPlaced(P, ox + r.x, oy + kAir0, oz + r.z, std::max(1, r.rx - 1), std::max(1, r.rz - 1), 4, k,
                  hash32(ox + r.x, oy, oz + r.z, (i32)S.seed), 1, r.role == "vault" ? 2 : 1);
  }
  // Air from the ante ceiling up through the shaft top, cobblestone around it. The sleeve starts at the
  // ceiling, so it does not fill the ante. place stored the sampled surface; a higher column is opened too.
  const Wayvault::Room* ante = nullptr;
  for (const auto& r : v.rooms) if (r.role == "ante") ante = &r;
  int topLocal = v.shaft[1];
  if (S.data.contains("shaftTop") && S.data["shaftTop"].is_number_integer())
    topLocal = std::max(topLocal, S.data["shaftTop"].get<int>());
  if (topLocal < kWayvaultCeil + 4) topLocal = kWayvaultCeil + 4;
  const int sx = ox + v.shaft[0], sz = oz + v.shaft[2];
  int y1 = oy + topLocal;
  const int g = P.ground(sx, sz);
  if (g > y1) y1 = g;
  const int y0 = oy + kWayvaultCeil;
  auto anteInside = [&](int x, int y, int z) {
    if (!ante) return false;
    const int ly = y - oy;
    if (ly < kAir0 || ly > kAir1) return false;
    int dx = x - (ox + ante->x); if (dx < 0) dx = -dx;
    int dz = z - (oz + ante->z); if (dz < 0) dz = -dz;
    return dx <= ante->rx && dz <= ante->rz;
  };
  for (int y = y0; y <= y1; y++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int x = sx + dx, z = sz + dz;
    if (!P.in(x, z)) continue;
    if (dx == 0 && dz == 0) { P.put(x, y, z, "air"); continue; }
    if (anteInside(x, y, z)) continue;
    P.put(x, y, z, "cobblestone");
  }
}

void registerStructureVaults() {
  StructureType t;
  t.key = "wayvault";
  t.name = "a wayvault";
  t.region = kReg;
  t.chance = 0.2;
  t.margin = 24;
  t.clearance = 8;
  t.dims = 1u << DIM_OVERWORLD;
  t.place = wayvaultPlace;
  t.build = wayvaultBuild;
  registerStructure(std::move(t));
}

} // namespace dq
