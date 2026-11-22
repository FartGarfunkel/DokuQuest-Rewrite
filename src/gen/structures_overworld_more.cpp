// gen/structures_overworld_more.cpp — jungle temples, trail ruins and woodland mansions.
// Registered from registerStructurePlaces() (gen/structures_places.cpp). Surface places, existing blocks only.
#include "gen/structures_common.h"
#include "gen/proc_rooms.h"
#include "core/itemstack.h"
#include <algorithm>
#include <cstring>

namespace dq {
using namespace vmap;

static json arrowDisp(u32 salt) {
  json items = json::array();
  for (int s = 0; s < 9; s++) items.push_back(nullptr);
  Mulberry32 r(salt);
  items[jsFloor(r() * 9)] = ItemStack("arrow", 6 + jsFloor(r() * 9)).toJson();
  if (r() < 0.55) items[jsFloor(r() * 9)] = ItemStack("arrow", 3 + jsFloor(r() * 7)).toJson();
  return items;
}
static void putDisp(SP& P, int x, int y, int z, int face6, u32 salt) {
  const int i = P.put(x, y, z, "dispenser", (u8)face6);
  if (i >= 0) P.setEntity(i, json{{"type", "dispenser"}, {"items", arrowDisp(salt ^ (u32)(x * 17 + z * 31 + y))}});
}
// tripwire hall: hooks at the ends (strung), dispensers in the end walls and beside each hook
static void putTripHall(SP& P, int a0, int a1, int y, int b, bool alongX, u32 seed) {
  if (a1 < a0) std::swap(a0, a1);
  if (alongX) {
    P.put(a0, y, b, "tripwire_hook", 3 | 4);
    P.put(a1, y, b, "tripwire_hook", 1 | 4);
    for (int x = a0 + 1; x < a1; x++) P.put(x, y, b, "tripwire", 2);
    putDisp(P, a0 - 1, y, b, 5, seed);
    putDisp(P, a1 + 1, y, b, 4, seed);
    putDisp(P, a0, y, b - 1, 3, seed);
    putDisp(P, a1, y, b + 1, 2, seed);
  } else {
    P.put(b, y, a0, "tripwire_hook", 0 | 4);
    P.put(b, y, a1, "tripwire_hook", 2 | 4);
    for (int z = a0 + 1; z < a1; z++) P.put(b, y, z, "tripwire", 2);
    putDisp(P, b, y, a0 - 1, 3, seed);
    putDisp(P, b, y, a1 + 1, 2, seed);
    putDisp(P, b - 1, y, a0, 5, seed);
    putDisp(P, b + 1, y, a1, 4, seed);
  }
}

static std::string_view mossPick(SP& P, int x, int y, int z, int s) {
  const double h = P.h(x, y, z, s);
  return h < 0.48 ? std::string_view("mossy_cobblestone") : h < 0.82 ? std::string_view("mossy_stone_bricks") : std::string_view("cobblestone");
}

// ==== Jungle temple ==================================================================================================
bool jungleTemplePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c) || c.biome != BIOME_FOREST || c.temp <= 0.28 || c.humid <= 0.12) return false;
  Land L = sLand(t, x - 7, z - 7, x + 7, z + 7);
  if (L.jhi - L.jlo > 10 || L.mean < 12) return false;
  S.x0 = x - 7; S.z0 = z - 7; S.x1 = x + 7; S.z1 = z + 7; S.cx = x; S.cz = z;
  S.y = L.mean; S.hashDy = L.jmean - L.mean; S.surface = true;
  S.rot = rnd() < 0.5 ? 0 : 1;
  S.name = "Jungle Temple";
  S.data = {{"residents", json::array({mobAt(x, S.y + 1, z, "jungle_skeleton"),
                                       mobAt(x + 2, S.y + 1, z - 2, "jungle_skeleton")})}};
  return true;
}

void jungleTempleBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int cx = S.cx, cz = S.cz, y = S.y;
  const int yb = y - 8;
  for (int z = S.z0 - 1; z <= S.z1 + 1; z++) for (int x = S.x0 - 1; x <= S.x1 + 1; x++) {
    if (!P.in(x, z)) continue;
    const int lz = z - cz, ax = std::abs(x - cx), az = std::abs(lz), gh = P.ground(x, z);
    const bool out = x < S.x0 || x > S.x1 || z < S.z0 || z > S.z1;
    if (out) {
      if (gh < y) P.fill(x, gh + 1, z, x, y, z, "dirt");
      if (gh >= y) P.put(x, y, z, "grass");
      continue;
    }
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, mossPick(P, x, yy, z, 1));
    for (int yy = y + 1; yy <= std::max(gh, y) + 2; yy++) P.put(x, yy, z, "air");
    P.put(x, y, z, mossPick(P, x, 0, z, 2));
    const int ring = std::max(ax, az);
    int top = 0;
    if (ring <= 7) top = 5;
    if (ring <= 5) top = 9;
    if (ring <= 3) top = 12;
    const bool wall = (ax == 7 || az == 7) || ((ax == 5 || az == 5) && ring <= 5) || ((ax == 3 || az == 3) && ring <= 3);
    const bool door = lz == -7 && ax <= 1;
    if (wall) {
      for (int yy = 1; yy <= top; yy++) {
        if (door && yy <= 3) { P.put(x, y + yy, z, "air"); continue; }
        const bool win = !door && yy == 3 && top >= 5 && (ax % 2 == 0 || az % 2 == 0) && P.h(x, yy, z, 4) < 0.35;
        P.put(x, y + yy, z, win ? std::string_view("air") : mossPick(P, x, yy, z, 5));
      }
    } else {
      const int roof = ring <= 3 ? 12 : ring <= 5 ? 9 : 5;
      for (int yy = 1; yy < roof; yy++) P.put(x, y + yy, z, "air");
      if (ring > 1) P.put(x, y + roof, z, mossPick(P, x, roof, z, 6));
      if (P.h(x, 3, z, 8) < 0.08) P.put(x, y + 1, z, "cobweb");
    }
    // vines on the outer faces
    if ((x == S.x0 || x == S.x1 || z == S.z0 || z == S.z1) && P.h(x, 4, z, 9) < 0.6) {
      const int ox = x == S.x0 ? -1 : x == S.x1 ? 1 : 0, oz = z == S.z0 ? -1 : z == S.z1 ? 1 : 0;
      const int meta = x == S.x0 ? 1 : x == S.x1 ? 3 : z == S.z0 ? 2 : 0;
      const int len = 2 + jsFloor(P.h(x, 5, z, 10) * 7);
      for (int k = 1; k <= len && k <= top; k++) P.put(x + ox, y + k, z + oz, "vine", (u8)meta);
    }
  }
  // basement: moss box, 1-wide tripwire hall from the ladder (north) to the chests (south)
  P.fill(cx - 5, yb - 1, cz - 5, cx + 5, y - 1, cz + 5, "mossy_cobblestone");
  P.fill(cx - 4, yb + 1, cz - 4, cx + 4, y - 1, cz + 4, "air");
  P.fill(cx - 4, yb, cz - 4, cx + 4, yb, cz + 4, "mossy_cobblestone");
  const bool alongX = S.rot == 0;
  if (alongX) {
    // hall along x at cz; ladder west, chests east
    for (int x = cx - 3; x <= cx + 3; x++) for (int yy = yb + 1; yy <= yb + 3; yy++) {
      P.put(x, yy, cz - 1, mossPick(P, x, yy, cz - 1, 11));
      P.put(x, yy, cz + 1, mossPick(P, x, yy, cz + 1, 11));
    }
    P.fill(cx - 3, yb + 1, cz, cx + 3, yb + 3, cz, "air");
    putTripHall(P, cx - 3, cx + 3, yb + 1, cz, true, S.seed);
    for (int yy = yb + 1; yy < y; yy++) { P.put(cx - 4, yy, cz, "air"); P.put(cx - 5, yy, cz, "mossy_cobblestone"); }
    P.ladder(cx - 4, yb + 1, y - 1, cz, 3);
    P.put(cx - 4, y, cz, "air");
    P.chest(cx + 3, yb + 1, cz - 3, 1, "temple");
    P.chest(cx + 3, yb + 1, cz + 3, 1, "temple");
  } else {
    for (int z = cz - 3; z <= cz + 3; z++) for (int yy = yb + 1; yy <= yb + 3; yy++) {
      P.put(cx - 1, yy, z, mossPick(P, cx - 1, yy, z, 11));
      P.put(cx + 1, yy, z, mossPick(P, cx + 1, yy, z, 11));
    }
    P.fill(cx, yb + 1, cz - 3, cx, yb + 3, cz + 3, "air");
    putTripHall(P, cz - 3, cz + 3, yb + 1, cx, false, S.seed);
    for (int yy = yb + 1; yy < y; yy++) { P.put(cx, yy, cz - 4, "air"); P.put(cx, yy, cz - 5, "mossy_cobblestone"); }
    P.ladder(cx, yb + 1, y - 1, cz - 4, 0);
    P.put(cx, y, cz - 4, "air");
    P.chest(cx - 3, yb + 1, cz + 3, 0, "temple");
    P.chest(cx + 3, yb + 1, cz + 3, 0, "temple");
  }
  P.chest(cx + 4, y + 1, cz - 4, 3, "temple");
  P.put(cx, yb + 3, cz, "cobweb");
  P.put(cx - 2, yb + 3, cz + 2, "cobweb");
  furnishPlaced(P, cx, y + 1, cz, 4, 4, 4, "shrine", hash32(cx, y, cz, (i32)S.seed), PAL_MOSSY, 3);
  furnishPlaced(P, cx + (alongX ? 3 : 0), yb + 1, cz + (alongX ? 0 : 3), 1, 1, 3, "store",
                hash32(cx, yb, cz, (i32)S.seed), PAL_MOSSY, 2);
}

// ==== Trail ruins ====================================================================================================
struct TrailBit { int x0, z0, x1, z1, bury, h; bool chest; };
struct TrailExt { std::vector<TrailBit> bits; };

bool trailRuinsPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c)) return false;
  const bool taiga = c.biome == BIOME_TAIGA;
  const bool coolForest = c.biome == BIOME_FOREST && c.temp <= 0.28;
  if (!taiga && !coolForest) return false;
  const int R = 10;
  Land L = sLand(t, x - R, z - R, x + R, z + R);
  if (L.jhi - L.jlo > 12 || L.mean < 10) return false;
  auto e = std::make_shared<TrailExt>();
  const int n = 3 + jsFloor(rnd() * 3);
  const int want = 1 + (rnd() < 0.55 ? 1 : 0);
  int chests = 0;
  for (int i = 0; i < n; i++) {
    const int w = 4 + jsFloor(rnd() * 4), d = 4 + jsFloor(rnd() * 4);
    TrailBit b;
    b.x0 = x - R + 1 + jsFloor(rnd() * (2 * R - w - 1));
    b.z0 = z - R + 1 + jsFloor(rnd() * (2 * R - d - 1));
    b.x1 = b.x0 + w - 1; b.z1 = b.z0 + d - 1;
    b.bury = 2 + jsFloor(rnd() * 3);
    b.h = 1 + jsFloor(rnd() * 3);
    b.chest = chests < want && (i == n - 1 || rnd() < 0.5);
    if (b.chest) chests++;
    e->bits.push_back(b);
  }
  if (chests == 0 && !e->bits.empty()) e->bits.back().chest = true;
  S.x0 = x - R; S.z0 = z - R; S.x1 = x + R; S.z1 = z + R; S.cx = x; S.cz = z;
  S.y = L.mean; S.hashDy = L.jmean - L.mean; S.surface = true;
  S.name = "Trail Ruins";
  S.ext = e;
  return true;
}

void trailRuinsBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const TrailExt& E = extOf<TrailExt>(S);
  static const std::vector<const char*> wall = {"mud_bricks", "mud_bricks", "packed_mud", "bricks", "cracked_stone_bricks", "terracotta"};
  static const std::vector<const char*> floor = {"mud_bricks", "packed_mud", "gravel", "gravel", "bricks", "coarse_dirt"};
  const int y = S.y;
  for (const TrailBit& B : E.bits) {
    const int fy = y - B.bury;
    const int mx = (B.x0 + B.x1) >> 1, mz = (B.z0 + B.z1) >> 1;
    for (int z = B.z0; z <= B.z1; z++) for (int x = B.x0; x <= B.x1; x++) {
      if (!P.in(x, z)) continue;
      const int gh = P.ground(x, z);
      const bool edge = x == B.x0 || x == B.x1 || z == B.z0 || z == B.z1;
      const bool corner = (x == B.x0 || x == B.x1) && (z == B.z0 || z == B.z1);
      const int poke = corner ? B.h + 1 : B.h;
      const int top = y + poke;
      for (int yy = std::min(gh, fy) - 1; yy < fy; yy++) P.put(x, yy, z, "packed_mud");
      P.put(x, fy, z, P.h(x, 0, z, 2) < 0.4 ? "gravel" : P.pick(floor, x, 0, z, 3));
      if (edge) {
        const double along = (x == B.x0 || x == B.x1) ? (z - B.z0) / (double)std::max(1, B.z1 - B.z0) : (x - B.x0) / (double)std::max(1, B.x1 - B.x0);
        int h = poke + B.bury;
        if (P.h(x, 1, z, 4) < 0.22 + along * 0.15) h -= 1 + jsFloor(P.h(x, 2, z, 5) * 2);
        if (h < 1) h = 1;
        for (int k = 1; k <= h; k++) {
          const int yy = fy + k;
          if (yy > top) break;
          if (yy > y && P.h(x, k, z, 6) < 0.28) continue;
          P.put(x, yy, z, P.h(x, k, z, 7) < 0.22 ? "gravel" : P.pick(wall, x, k, z, 8));
        }
        for (int yy = fy + h + 1; yy <= std::max(gh, y) + 1; yy++) {
          const BlockId id = P.get(x, yy, z);
          if (id != AIR && id != NOBLOCK && !tSolid(id)) P.put(x, yy, z, "air");
        }
      } else {
        for (int yy = fy + 1; yy <= std::max(gh, y) + 1; yy++) P.put(x, yy, z, yy < y && P.h(x, yy, z, 9) < 0.18 ? "gravel" : "air");
        if (P.h(x, 3, z, 10) < 0.12) P.put(x, fy + 1, z, "gravel");
        else if (P.h(x, 4, z, 11) < 0.08) P.put(x, fy + 1, z, "cobweb");
      }
    }
    const int rx = std::max(1, (B.x1 - B.x0) / 2 - 1), rz = std::max(1, (B.z1 - B.z0) / 2 - 1);
    furnishPlaced(P, mx, fy + 1, mz, rx, rz, std::max(3, B.h + 1), "store",
                  hash32(mx, fy, mz, (i32)S.seed), PAL_MOSSY, 3);
    if (B.chest) P.chest(mx, fy + 1, mz, 2, "ruin");
  }
  for (int z = S.z0; z <= S.z1; z++) for (int x = S.x0; x <= S.x1; x++) {
    if (!P.in(x, z)) continue;
    if (P.h(x, 8, z, 12) > 0.11) continue;
    const int gh = P.ground(x, z);
    if (gh < y - 1 || gh > y + 2) continue;
    const BlockId below = P.get(x, gh, z);
    if (below == AIR || below == NOBLOCK) continue;
    P.put(x, gh + 1, z, "gravel");
    if (P.h(x, 9, z, 13) < 0.35) P.put(x, gh + 2, z, "gravel");
  }
}

// ==== Woodland mansion ===============================================================================================
constexpr int MX = 4, MZ = 3, MCS = 8, MFH = 6, MFL = 3;
static const char* kMansionRoom[3][12] = {
  {"store", "kitchen", "pantry", "workshop",
   "armoury", "hall", "dining", "office",
   "entry", "hall", "hall", "sleeping"},
  {"sleeping", "sleeping", "library", "archive",
   "hall", "gallery", "office", "bath",
   "sleeping", "scriptorium", "armoury", "store"},
  {"library", "gallery", "observatory", "sleeping",
   "hall", "archive", "store", "office",
   "sleeping", "greenhouse", "workshop", "pantry"}
};

bool woodlandMansionPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c) || c.biome != BIOME_FOREST || (c.temp > 0.28 && c.humid > 0.12)) return false;
  const int hx = (MX * MCS) / 2, hz = (MZ * MCS) / 2;
  Land L = sLand(t, x - hx, z - hz, x + hx, z + hz);
  if (L.jhi - L.jlo > 14 || L.mean < 12 || L.mean + MFL * MFH + 6 >= DIM_H - 2) return false;
  S.x0 = x - hx; S.z0 = z - hz; S.x1 = x - hx + MX * MCS; S.z1 = z - hz + MZ * MCS;
  S.cx = x; S.cz = z; S.y = L.mean; S.hashDy = L.jmean - L.mean; S.surface = true;
  S.name = "Woodland Mansion";
  const int y = S.y;
  json res = json::array();
  res.push_back(mobAt(x, y + 1, z + 4, "raider"));
  res.push_back(mobAt(x - 6, y + 1, z, "raider"));
  res.push_back(mobAt(x + 6, y + 1 + MFH, z - 4, "raider"));
  res.push_back(mobAt(x, y + 1 + MFH, z, "raider"));
  res.push_back(mobAt(x - 4, y + 1 + MFH * 2, z + 2, "warlock"));
  res.push_back(mobAt(x + 4, y + 1 + MFH * 2, z - 2, "warlock"));
  if (rnd() < 0.5) res.push_back(mobAt(x + 2, y + 1, z - 6, "raider"));
  S.data = {{"residents", std::move(res)}};
  return true;
}

void woodlandMansionBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int x0 = S.x0, z0 = S.z0, x1 = S.x1, z1 = S.z1, y = S.y, cx = S.cx;
  const std::string plank = PLK("dark_oak_planks", "planks"), log = PLK("dark_oak_log", "log");
  const std::string fence = PLK("dark_oak_fence", "oak_fence"), door = PLK("dark_oak_door", "oak_door");
  const std::string glass = PLK("glass_pane", "air");
  const BlockId rug = bid("red_carpet");
  const int sx = x0 + 2, sz = z0 + 10;   // ladder shaft
  const int top = y + MFL * MFH;
  for (int z = z0 - 1; z <= z1 + 2; z++) for (int x = x0 - 1; x <= x1 + 1; x++) {
    if (!P.in(x, z)) continue;
    const int gh = P.ground(x, z);
    const bool in = x >= x0 && x <= x1 && z >= z0 && z <= z1;
    if (!in) {
      if (z == z1 + 1 && x >= cx - 2 && x <= cx + 2) {
        for (int yy = std::min(gh, y) - 1; yy < y; yy++) P.put(x, yy, z, "cobblestone");
        P.put(x, y, z, "cobblestone");
        for (int yy = y + 1; yy <= std::max(gh, y) + 2; yy++) P.put(x, yy, z, "air");
      } else {
        if (gh < y) P.fill(x, gh + 1, z, x, y, z, "dirt");
        if (gh >= y) P.put(x, y, z, "grass");
      }
      continue;
    }
    const int lx = (x - x0) % MCS, lz = (z - z0) % MCS;
    const bool xw = x == x0 || x == x1 || lx == 0, zw = z == z0 || z == z1 || lz == 0;
    const bool wall = xw || zw;
    const bool corner = (x == x0 || x == x1) && (z == z0 || z == z1);
    const bool shaft = x == sx && z == sz;
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, "cobblestone");
    for (int yy = y; yy <= std::max(gh, top) + 2; yy++) P.put(x, yy, z, "air");
    for (int fl = 0; fl < MFL; fl++) {
      const int fy = y + fl * MFH;
      const bool hole = shaft && fl > 0;
      if (hole) P.put(x, fy, z, "air");
      else P.put(x, fy, z, wall ? log : plank);
      for (int yy = 1; yy <= 4; yy++) {
        if (!wall) { P.put(x, fy + yy, z, "air"); continue; }
        const bool front = z == z1 && x >= cx && x <= cx + 1 && fl == 0 && yy <= 2;
        if (front) { P.put(x, fy + yy, z, door, (u8)(2 | (yy == 2 ? 16 : 0) | (x == cx + 1 ? 8 : 0))); continue; }
        const bool win = (x == x0 || x == x1 || z == z0 || z == z1) && (yy == 2 || yy == 3) && ((x + z + fl) % 3 == 1) && !front;
        if (win) { P.put(x, fy + yy, z, glass); continue; }
        P.put(x, fy + yy, z, corner || yy == 4 ? log : plank);
      }
      P.put(x, fy + 5, z, hole ? "air" : plank);
      if (!wall && fl == 0 && rug != NOBLOCK && rug != AIR) {
        const int gx = std::clamp((x - x0) / MCS, 0, MX - 1), gz = std::clamp((z - z0) / MCS, 0, MZ - 1);
        if (std::strcmp(kMansionRoom[0][gz * MX + gx], "hall") == 0) P.rug(x, fy, z, rug);
      }
    }
    P.put(x, top, z, plank);
    if (x == x0 || x == x1 || z == z0 || z == z1) P.put(x, top + 1, z, fence);
  }
  // inner doorways on shared walls
  for (int z = z0 + 1; z < z1; z++) for (int x = x0 + 1; x < x1; x++) {
    if (!P.in(x, z)) continue;
    const int lx = (x - x0) % MCS, lz = (z - z0) % MCS;
    const bool xw = lx == 0 && x != x0 && x != x1, zw = lz == 0 && z != z0 && z != z1;
    if (!xw && !zw) continue;
    const bool open = (xw && lz >= 3 && lz <= 5) || (zw && lx >= 3 && lx <= 5);
    if (!open) continue;
    for (int fl = 0; fl < MFL; fl++) {
      const int fy = y + fl * MFH;
      P.put(x, fy + 1, z, "air"); P.put(x, fy + 2, z, "air");
    }
  }
  for (int yy = y; yy < top; yy++) {
    P.put(sx, yy, sz, "air");
    P.put(sx, yy, sz - 1, log);
  }
  P.ladder(sx, y + 1, top - 1, sz, 0);
  // chimneys
  for (auto [dx, dz] : {std::pair{6, -4}, {-8, 5}}) {
    const int x = cx + dx, z = S.cz + dz;
    for (int k = 0; k <= 3; k++) P.put(x, top + k, z, "cobblestone");
    P.put(x, top + 4, z, PLK("cobblestone_wall", "cobblestone"));
  }
  auto chest = [&](int x, int yy, int z, const char*, bool) { P.chest(x, yy, z, 2, "ruin"); };
  for (int fl = 0; fl < MFL; fl++) for (int gz = 0; gz < MZ; gz++) for (int gx = 0; gx < MX; gx++) {
    const int rx = x0 + gx * MCS + MCS / 2, rz = z0 + gz * MCS + MCS / 2;
    const int fy = y + fl * MFH + 1;
    const char* kind = kMansionRoom[fl][gz * MX + gx];
    const int pal = std::strcmp(kind, "library") == 0 || std::strcmp(kind, "archive") == 0 || std::strcmp(kind, "scriptorium") == 0
                    ? PAL_SLATE : PAL_OAK;
    const int clut = std::strcmp(kind, "hall") == 0 || std::strcmp(kind, "entry") == 0 ? 1 : 2;
    furnishPlaced(P, rx, fy, rz, 3, 3, 4, kind, hash32(rx, fy, rz, (i32)S.seed), pal, clut, chest);
    if (std::strcmp(kind, "store") == 0 || std::strcmp(kind, "armoury") == 0)
      P.chest(rx - 1, fy, rz - 1, 2, "ruin");
  }
}

} // namespace dq
