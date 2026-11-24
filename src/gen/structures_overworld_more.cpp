// gen/structures_overworld_more.cpp — jungle temples, trail ruins and woodland mansions.
// Registered from registerStructurePlaces() (gen/structures_places.cpp). Surface places, existing blocks only.
#include "gen/structures_common.h"
#include "world/dirs.h"
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
// one tripwire run (Minecraft jungle temple: two strings in the hall, dispensers in the side walls)
static void putTripRun(SP& P, int x0, int z, int x1, int y, u32 seed) {
  if (x1 < x0) std::swap(x0, x1);
  P.put(x0, y, z, "tripwire_hook", 1 | 4);   // facing east, strung
  P.put(x1, y, z, "tripwire_hook", 3 | 4);   // facing west, strung
  for (int x = x0 + 1; x < x1; x++) P.put(x, y, z, "tripwire", 2);
  putDisp(P, (x0 + x1) / 2, y, z - 1, 3, seed);       // south into the hall
  putDisp(P, (x0 + x1) / 2 + 1, y, z + 1, 2, seed ^ 17); // north into the hall
}

static std::string landName(const TerrainQuery& t, int x, int z) {
  const OverworldGen* ow = owOf(t);
  return (ow && t.genVersion() >= 15) ? ow->landBiomeName(x, z) : std::string();
}
static bool nameHas(const std::string& n, const char* k) { return n.find(k) != std::string::npos; }
static bool jungleLand(const TerrainQuery& t, int x, int z, const TerrainColumn& c) {
  const std::string n = landName(t, x, z);
  if (!n.empty()) return nameHas(n, "jungle") && !nameHas(n, "sparse");
  return c.biome == BIOME_FOREST && c.temp > 0.28 && c.humid > 0.12;
}
static bool darkForestLand(const TerrainQuery& t, int x, int z, const TerrainColumn& c) {
  const std::string n = landName(t, x, z);
  if (!n.empty()) return nameHas(n, "dark_forest") && !nameHas(n, "deep_dark");
  return c.biome == BIOME_FOREST && c.temp <= 0.28 && c.humid > 0.12;
}
static bool taigaLand(const TerrainQuery& t, int x, int z, const TerrainColumn& c) {
  const std::string n = landName(t, x, z);
  if (!n.empty()) return nameHas(n, "taiga");
  return c.biome == BIOME_TAIGA || (c.biome == BIOME_FOREST && c.temp <= 0.28);
}

static std::string_view mossPick(SP& P, int x, int y, int z, int s) {
  const double h = P.h(x, y, z, s);
  return h < 0.48 ? std::string_view("mossy_cobblestone") : h < 0.82 ? std::string_view("mossy_stone_bricks") : std::string_view("cobblestone");
}

// ==== Jungle temple ==================================================================================================
// Minecraft jungle pyramid: mossy cobble, front door, stairs down to a lever puzzle and a tripwire hall
// with two dispensers and one chest, a second chest behind the puzzle wall. No dumped furniture.
bool jungleTemplePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c) || !jungleLand(t, x, z, c)) return false;
  Land L = sLand(t, x - 7, z - 7, x + 7, z + 7);
  if (L.jhi - L.jlo > 10 || L.mean < 12) return false;
  S.x0 = x - 7; S.z0 = z - 7; S.x1 = x + 7; S.z1 = z + 7; S.cx = x; S.cz = z;
  S.y = L.hi; S.hashDy = L.jhi - L.hi; S.surface = true;
  S.rot = 0;
  S.name = "Jungle Temple";
  S.data = {{"residents", json::array({mobAt(x, S.y + 1, z, "jungle_skeleton"),
                                       mobAt(x + 2, S.y + 1, z - 2, "jungle_skeleton")})}};
  (void)rnd;
  return true;
}

void jungleTempleBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int cx = S.cx, cz = S.cz, y = S.y;
  const int yb = y - 5;
  auto moss = [&](int x, int yy, int z, int s) { return mossPick(P, x, yy, z, s); };
  for (int z = S.z0 - 1; z <= S.z1 + 1; z++) for (int x = S.x0 - 1; x <= S.x1 + 1; x++) {
    if (!P.in(x, z)) continue;
    const int lz = z - cz, ax = std::abs(x - cx), az = std::abs(lz), gh = P.ground(x, z);
    const bool out = x < S.x0 || x > S.x1 || z < S.z0 || z > S.z1;
    // skirt stays at local ground — no dirt terrace into the air
    if (out) continue;
    // moss footing from the land up to the floor, only under the box
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, moss(x, yy, z, 1));
    P.put(x, y, z, moss(x, 0, z, 2));
    const int ring = std::max(ax, az);
    int top = ring <= 3 ? 10 : ring <= 5 ? 7 : 5;
    const bool wall = (ax == 7 || az == 7) || ((ax == 5 || az == 5) && ring <= 5) || ((ax == 3 || az == 3) && ring <= 3);
    const bool door = lz == 7 && ax <= 1;   // south front
    if (wall) {
      for (int yy = 1; yy <= top; yy++) {
        if (door && yy <= 3) { P.put(x, y + yy, z, "air"); continue; }
        const bool win = !door && yy == 3 && top >= 5 && (ax % 2 == 0 || az % 2 == 0) && P.h(x, yy, z, 4) < 0.35;
        P.put(x, y + yy, z, win ? std::string_view("air") : moss(x, yy, z, 5));
      }
    } else {
      for (int yy = 1; yy < top; yy++) P.put(x, y + yy, z, "air");
      if (ring > 1) P.put(x, y + top, z, moss(x, top, z, 6));
    }
    if ((x == S.x0 || x == S.x1 || z == S.z0 || z == S.z1) && P.h(x, 4, z, 9) < 0.6) {
      const int ox = x == S.x0 ? -1 : x == S.x1 ? 1 : 0, oz = z == S.z0 ? -1 : z == S.z1 ? 1 : 0;
      const int meta = x == S.x0 ? 1 : x == S.x1 ? 3 : z == S.z0 ? 2 : 0;
      const int len = 2 + jsFloor(P.h(x, 5, z, 10) * 7);
      for (int k = 1; k <= len && k <= top; k++) P.put(x + ox, y + k, z + oz, "vine", (u8)meta);
    }
  }
  // main floor: open hall
  P.fill(cx - 5, y + 1, cz - 5, cx + 5, y + 4, cz + 5, "air");
  // basement box
  P.fill(cx - 6, yb - 1, cz - 6, cx + 6, y, cz + 6, "mossy_cobblestone");
  P.fill(cx - 5, yb + 1, cz - 5, cx + 5, y - 1, cz + 5, "air");
  P.fill(cx - 5, yb, cz - 5, cx + 5, yb, cz + 5, "mossy_cobblestone");
  // stairwell from the west of the hall down into the basement
  for (int s = 0; s < 4; s++) {
    const int zz = cz + 2 - s, yy = y - s;
    P.put(cx - 4, yy, zz, "air"); P.put(cx - 5, yy, zz, "air");
    P.put(cx - 4, yy, zz, "mossy_cobblestone_stairs", 0);   // facing north, walk north/down
    P.put(cx - 5, yy, zz, "mossy_cobblestone_stairs", 0);
    P.put(cx - 4, yy - 1, zz, mossPick(P, cx - 4, yy, zz, 11));
    P.put(cx - 5, yy - 1, zz, mossPick(P, cx - 5, yy, zz, 11));
  }
  // puzzle wall (west): three chiseled bricks with levers, sticky pistons behind
  for (int i = 0; i < 3; i++) {
    const int zz = cz - 2 + i * 2;
    P.put(cx - 5, yb + 2, zz, "chiseled_stone_bricks");
    P.put(cx - 4, yb + 2, zz, "lever", 5);   // on the east face of the west wall
    P.put(cx - 6, yb + 2, zz, "sticky_piston", 5);   // facing east
  }
  P.put(cx - 5, yb + 1, cz, "redstone_wire");
  // tripwire hall along +x: 1-wide corridor with moss walls so the dispensers sit in the sides
  for (int x = cx - 1; x <= cx + 4; x++) {
    P.put(x, yb + 1, cz - 1, mossPick(P, x, yb + 1, cz - 1, 12));
    P.put(x, yb + 2, cz - 1, mossPick(P, x, yb + 2, cz - 1, 12));
    P.put(x, yb + 1, cz + 1, mossPick(P, x, yb + 1, cz + 1, 12));
    P.put(x, yb + 2, cz + 1, mossPick(P, x, yb + 2, cz + 1, 12));
    P.put(x, yb + 1, cz, "air"); P.put(x, yb + 2, cz, "air"); P.put(x, yb + 3, cz, "air");
  }
  putTripRun(P, cx, cz, cx + 3, yb + 1, S.seed);
  P.put(cx + 4, yb + 1, cz - 1, "vine", 2);
  P.put(cx + 4, yb + 1, cz + 1, "vine", 0);
  P.chest(cx + 4, yb + 1, cz, 3, "temple");   // trap chest at the end of the hall
  P.chest(cx - 3, yb + 1, cz, 1, "temple");   // puzzle chest in the lever room
  P.put(cx - 4, y, cz + 2, "air"); P.put(cx - 5, y, cz + 2, "air");   // stair opening in the floor
  P.put(cx, yb + 3, cz - 3, "cobweb");
  P.put(cx + 3, yb + 3, cz + 3, "cobweb");
}

// ==== Trail ruins ====================================================================================================
struct TrailBit { int x0, z0, x1, z1, bury, h; bool chest; };
struct TrailExt { std::vector<TrailBit> bits; };

bool trailRuinsPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c) || !taigaLand(t, x, z, c)) return false;
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
    const int mx = (B.x0 + B.x1) >> 1, mz = (B.z0 + B.z1) >> 1;
    for (int z = B.z0; z <= B.z1; z++) for (int x = B.x0; x <= B.x1; x++) {
      if (!P.in(x, z)) continue;
      const int gh = P.ground(x, z);
      if (gh < 4) continue;
      const bool edge = x == B.x0 || x == B.x1 || z == B.z0 || z == B.z1;
      const bool corner = (x == B.x0 || x == B.x1) && (z == B.z0 || z == B.z1);
      const int poke = corner ? B.h + 1 : B.h;
      const int fy = std::max(2, gh - B.bury);
      for (int yy = fy - 1; yy < fy; yy++) P.put(x, yy, z, "packed_mud");
      P.put(x, fy, z, P.h(x, 0, z, 2) < 0.4 ? "gravel" : P.pick(floor, x, 0, z, 3));
      if (edge) {
        const double along = (x == B.x0 || x == B.x1) ? (z - B.z0) / (double)std::max(1, B.z1 - B.z0) : (x - B.x0) / (double)std::max(1, B.x1 - B.x0);
        int h = poke + B.bury;
        if (P.h(x, 1, z, 4) < 0.22 + along * 0.15) h -= 1 + jsFloor(P.h(x, 2, z, 5) * 2);
        if (h < 1) h = 1;
        const int maxWall = gh + (corner ? 2 : 1);
        for (int k = 1; k <= h; k++) {
          const int yy = fy + k;
          if (yy > maxWall) break;
          if (yy > gh && P.h(x, k, z, 6) < 0.28) continue;
          P.put(x, yy, z, P.h(x, k, z, 7) < 0.22 ? "gravel" : P.pick(wall, x, k, z, 8));
        }
        for (int yy = fy + 1; yy <= gh + 1; yy++) {
          const BlockId id = P.get(x, yy, z);
          if (id != AIR && id != NOBLOCK && !tSolid(id)) P.put(x, yy, z, "air");
        }
      } else {
        for (int yy = fy + 1; yy <= gh; yy++) P.put(x, yy, z, P.h(x, yy, z, 9) < 0.18 ? "gravel" : "air");
        if (P.h(x, 3, z, 10) < 0.12) P.put(x, fy + 1, z, "gravel");
        else if (P.h(x, 4, z, 11) < 0.08) P.put(x, fy + 1, z, "cobweb");
      }
    }
    if (B.chest) P.chest(mx, std::max(2, P.ground(mx, mz) - B.bury) + 1, mz, 2, "ruin");
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
// Minecraft: 3 floors, cobble foundation, birch floors, dark oak walls. Front foyer with a
// giant staircase. Corridors with a carpet runner connect the rooms. Rooms are themed, not dumped.
constexpr int MX = 5, MZ = 4, MCS = 9, MFH = 8, MFL = 3;

static const char* mansionKind(int fl, int gx, int gz) {
  if (fl >= 2 && gz == 0) return "none";                 // top floor is the front half
  if (gz == MZ - 2 && fl < 2) return "hall";             // E-W corridor
  if (gx == 2 && gz < MZ - 2 && fl < 2) return "hall";   // N-S spur
  if (fl == 0 && gz == MZ - 1 && (gx == 1 || gx == 2)) return "foyer";
  if (fl == 1 && gz == MZ - 1 && (gx == 1 || gx == 2)) return "hall";
  if (fl == 1 && gx == 2 && gz == MZ - 3) return "stairs";
  static const char* extra[3][4][5] = {
    { // floor 0, gz 0..3 × gx 0..4
      {"library", "office", "hall", "bedroom", "store"},
      {"dining", "flower", "hall", "store", "office"},
      {"hall", "hall", "hall", "hall", "hall"},
      {"flower", "foyer", "foyer", "dining", "store"},
    },
    {
      {"bedroom", "library", "hall", "bedroom", "flower"},
      {"store", "office", "stairs", "flower", "library"},
      {"hall", "hall", "hall", "hall", "hall"},
      {"bedroom", "hall", "hall", "store", "office"},
    },
    {
      {"none", "none", "none", "none", "none"},
      {"library", "bedroom", "hall", "store", "flower"},
      {"hall", "hall", "hall", "hall", "hall"},
      {"flower", "office", "dining", "bedroom", "store"},
    }
  };
  return extra[fl][gz][gx];
}

static void putBed2(SP& P, int x, int y, int z, int facing, const char* key) {
  const int x2 = x + DIRX[facing & 3], z2 = z + DIRZ[facing & 3];
  P.put(x, y, z, key, (u8)(facing & 3));
  P.put(x2, y, z2, key, (u8)((facing & 3) | 4));
}
static void mBedroom(SP& P, int x0, int z0, int x1, int z1, int y, u32 seed) {
  const char* bed = hasBlock("red_bed") ? "red_bed" : "bed";
  putBed2(P, x0 + 1, y, z0 + 2, 1, bed);
  if (z1 - z0 >= 6) putBed2(P, x0 + 1, y, z1 - 2, 1, bed);
  P.chest(x1 - 1, y, (z0 + z1) / 2, 3, "ruin");
  P.put(x1 - 1, y, z0 + 1, "flower_pot");
  P.hang((x0 + x1) / 2, y + 5, (z0 + z1) / 2, "lantern");
  (void)seed;
}
static void mLibrary(SP& P, int x0, int z0, int x1, int z1, int y) {
  for (int z = z0 + 1; z <= z1 - 1; z++) {
    P.put(x0 + 1, y, z, "bookshelf"); P.put(x0 + 1, y + 1, z, "bookshelf");
    P.put(x1 - 1, y, z, "bookshelf"); P.put(x1 - 1, y + 1, z, "bookshelf");
  }
  P.put((x0 + x1) / 2, y, (z0 + z1) / 2, PLK("oak_table", "planks"));
  P.put((x0 + x1) / 2, y + 1, (z0 + z1) / 2, "flower_pot");
  P.hang((x0 + x1) / 2, y + 5, z0 + 2, "lantern");
}
static void mStore(SP& P, int x0, int z0, int x1, int z1, int y) {
  int n = 0;
  for (int z = z0 + 1; z <= z1 - 1; z += 2) {
    P.chest(x0 + 1, y, z, 1, n++ % 2 == 0 ? "ruin" : "ruin");
    if (x1 - x0 >= 5) P.chest(x1 - 1, y, z, 3, "ruin");
  }
  P.hang((x0 + x1) / 2, y + 5, (z0 + z1) / 2, "lantern");
}
static void mDining(SP& P, int x0, int z0, int x1, int z1, int y) {
  const std::string st = PLK("dark_oak_stairs", "oak_stairs");
  const int mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
  for (int dx = -2; dx <= 2; dx++) P.put(mx + dx, y, mz, st, 0);
  for (int dx = -2; dx <= 2; dx++) P.put(mx + dx, y, mz + 1, st, 2);
  P.put(mx, y + 1, mz, "flower_pot");
  P.put(mx - 2, y, mz - 1, PLK("oak_chair", "oak_stairs"));
  P.put(mx + 2, y, mz + 2, PLK("oak_chair", "oak_stairs"));
  P.hang(mx, y + 5, mz, "lantern");
}
static void mFlower(SP& P, int x0, int z0, int x1, int z1, int y) {
  for (int z = z0 + 2; z <= z1 - 2; z += 2) for (int x = x0 + 2; x <= x1 - 2; x += 2)
    P.put(x, y, z, "flower_pot");
  P.hang((x0 + x1) / 2, y + 5, (z0 + z1) / 2, "lantern");
}
static void mOffice(SP& P, int x0, int z0, int x1, int z1, int y) {
  P.put((x0 + x1) / 2, y, z0 + 1, PLK("oak_desk", "oak_table"));
  P.put((x0 + x1) / 2, y, z0 + 2, PLK("oak_chair", "oak_stairs"), 0);
  P.put(x0 + 1, y, z1 - 1, "flower_pot");
  P.put(x1 - 1, y, z1 - 1, "flower_pot");
  P.hang((x0 + x1) / 2, y + 5, (z0 + z1) / 2, "lantern");
}

bool woodlandMansionPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z);
  if (!sLandy(c) || !darkForestLand(t, x, z, c)) return false;
  const int hx = (MX * MCS) / 2, hz = (MZ * MCS) / 2;
  Land L = sLand(t, x - hx, z - hz, x + hx, z + hz);
  if (L.jhi - L.jlo > 14 || L.mean < 12 || L.hi + MFL * MFH + 6 >= DIM_H - 2) return false;
  S.x0 = x - hx; S.z0 = z - hz; S.x1 = x - hx + MX * MCS; S.z1 = z - hz + MZ * MCS;
  S.cx = x; S.cz = z; S.y = L.hi; S.hashDy = L.jhi - L.hi; S.surface = true;
  S.name = "Woodland Mansion";
  const int y = S.y;
  json res = json::array();
  res.push_back(mobAt(x, y + 1, z + 8, "raider"));
  res.push_back(mobAt(x - 8, y + 1, z, "raider"));
  res.push_back(mobAt(x + 8, y + 1 + MFH, z - 4, "raider"));
  res.push_back(mobAt(x, y + 1 + MFH, z, "raider"));
  res.push_back(mobAt(x - 4, y + 1 + MFH * 2, z + 2, "warlock"));
  res.push_back(mobAt(x + 4, y + 1 + MFH * 2, z - 2, "warlock"));
  if (rnd() < 0.5) res.push_back(mobAt(x + 2, y + 1, z - 8, "raider"));
  S.data = {{"residents", std::move(res)}};
  return true;
}

void woodlandMansionBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int x0 = S.x0, z0 = S.z0, x1 = S.x1, z1 = S.z1, y = S.y, cx = S.cx;
  const std::string wallP = PLK("dark_oak_planks", "planks"), log = PLK("dark_oak_log", "log");
  const std::string floorP = PLK("birch_planks", "planks");
  const std::string fence = PLK("dark_oak_fence", "oak_fence"), door = PLK("dark_oak_door", "oak_door");
  const std::string glass = PLK("glass_pane", "air");
  const std::string cobbleSt = PLK("cobblestone_stairs", "cobblestone");
  const int top = y + MFL * MFH;
  const int foyerX0 = x0 + 1 * MCS, foyerX1 = x0 + 3 * MCS;
  auto isFoyer = [&](int x, int z, int fl) {
    return fl == 0 && z >= z0 + (MZ - 1) * MCS && x >= foyerX0 && x < foyerX1;
  };
  auto isHall = [&](int gx, int gz, int fl) {
    const char* k = mansionKind(fl, gx, gz);
    return std::strcmp(k, "hall") == 0 || std::strcmp(k, "foyer") == 0 || std::strcmp(k, "stairs") == 0;
  };

  for (int z = z0 - 1; z <= z1 + 2; z++) for (int x = x0 - 1; x <= x1 + 1; x++) {
    if (!P.in(x, z)) continue;
    const int gh = P.ground(x, z);
    const bool in = x >= x0 && x <= x1 && z >= z0 && z <= z1;
    if (!in) {
      if (z == z1 + 1 && x >= cx - 2 && x <= cx + 2) {
        for (int yy = std::min(gh, y) - 1; yy < y; yy++) P.put(x, yy, z, "cobblestone");
        P.put(x, y, z, "cobblestone");
        for (int yy = y + 1; yy <= y + 2; yy++) P.put(x, yy, z, "air");
      }
      continue;
    }
    const int gx = std::clamp((x - x0) / MCS, 0, MX - 1), gz = std::clamp((z - z0) / MCS, 0, MZ - 1);
    const int lx = (x - x0) % MCS, lz = (z - z0) % MCS;
    const bool xw = x == x0 || x == x1 || lx == 0, zw = z == z0 || z == z1 || lz == 0;
    const bool wall = xw || zw;
    const bool corner = (x == x0 || x == x1) && (z == z0 || z == z1);
    for (int yy = std::min(gh, y) - 3; yy < y; yy++) P.put(x, yy, z, "cobblestone");
    const int carveTop = (gz == 0 ? y + 2 * MFH : top) + 2;
    for (int yy = y; yy <= carveTop; yy++) P.put(x, yy, z, "air");
    for (int fl = 0; fl < MFL; fl++) {
      if (fl >= 2 && gz == 0) continue;   // no back rooms on the top floor
      const int fy = y + fl * MFH;
      const bool foyer = isFoyer(x, z, fl);
      P.put(x, fy, z, wall ? log : floorP);
      for (int yy = 1; yy <= 6; yy++) {
        if (!wall) { P.put(x, fy + yy, z, "air"); continue; }
        const bool front = z == z1 && x >= cx - 1 && x <= cx && fl == 0 && yy <= 3;
        if (front) { P.put(x, fy + yy, z, door, (u8)(2 | (yy == 2 ? 16 : 0) | (x == cx ? 8 : 0))); continue; }
        const bool win = (x == x0 || x == x1 || z == z0 || z == z1) && (yy == 3 || yy == 4)
                         && ((x + z + fl) % 3 == 1) && !front;
        if (win) { P.put(x, fy + yy, z, glass); continue; }
        P.put(x, fy + yy, z, corner || yy == 6 ? log : wallP);
      }
      P.put(x, fy + 7, z, wallP);
      if (!wall && isHall(gx, gz, fl)) {
        const bool runner = std::strcmp(mansionKind(fl, gx, gz), "hall") == 0;
        const BlockId wcar = bid("white_carpet"), rcar = bid("red_carpet");
        const BlockId use = runner && rcar != NOBLOCK && (lx == MCS / 2 || lz == MCS / 2)
                            ? rcar : (wcar != NOBLOCK ? wcar : rcar);
        if (use != NOBLOCK && use != AIR) P.rug(x, fy, z, use);
        if (foyer && rcar != NOBLOCK) P.rug(x, fy, z, rcar);
      }
    }
    const int localTop = gz == 0 ? y + 2 * MFH : top;   // top floor is the front half
    P.put(x, localTop, z, wallP);
    if (x == x0 || x == x1 || z == z0 || z == z1) P.put(x, localTop + 1, z, fence);
  }

  // doorways: rooms open onto the corridor (3-wide arch at mid-wall)
  for (int z = z0 + 1; z < z1; z++) for (int x = x0 + 1; x < x1; x++) {
    if (!P.in(x, z)) continue;
    const int lx = (x - x0) % MCS, lz = (z - z0) % MCS;
    const bool xw = lx == 0 && x != x0 && x != x1, zw = lz == 0 && z != z0 && z != z1;
    if (!xw && !zw) continue;
    const bool open = (xw && lz >= 3 && lz <= 5) || (zw && lx >= 3 && lx <= 5);
    if (!open) continue;
    for (int fl = 0; fl < MFL; fl++) {
      const int gz = std::clamp((z - z0) / MCS, 0, MZ - 1);
      if (fl >= 2 && gz == 0) continue;
      const int fy = y + fl * MFH;
      P.put(x, fy + 1, z, "air"); P.put(x, fy + 2, z, "air"); P.put(x, fy + 3, z, "air");
    }
  }

  // foyer staircase: 3-wide cobble stairs from the front door, north into the house, up to floor 1
  {
    const int fx = (foyerX0 + foyerX1) / 2;
    const int fz = z1 - 2;
    for (int s = 0; s < MFH - 1; s++) {
      const int zz = fz - 1 - s, yy = y + 1 + s;
      for (int dx = -1; dx <= 1; dx++) {
        P.put(fx + dx, yy, zz, cobbleSt, 0);   // facing north
        for (int k = y; k < yy; k++) P.put(fx + dx, k, zz, "cobblestone");
        P.put(fx + dx, y + MFH, zz, "air");     // hole through the first ceiling
        P.put(fx + dx, y + MFH + 1, zz, "air");
        P.put(fx + dx, y + MFH + 2, zz, "air");
      }
    }
    // landing on floor 1
    for (int dx = -1; dx <= 1; dx++) for (int dz = 0; dz <= 2; dz++)
      P.put(fx + dx, y + MFH, fz - MFH - dz, PLK("birch_planks", "planks"));
  }

  // second-to-third floor stairs in the dedicated stair cell
  {
    const int sx = x0 + 2 * MCS + MCS / 2, sz = z0 + 1 * MCS + 2;
    const int fy = y + MFH;
    for (int s = 0; s < MFH - 1; s++) {
      const int zz = sz + s, yy = fy + 1 + s;
      for (int dx = -1; dx <= 1; dx++) {
        P.put(sx + dx, yy, zz, cobbleSt, 2);   // facing south
        for (int k = fy; k < yy; k++) P.put(sx + dx, k, zz, "cobblestone");
        P.put(sx + dx, fy + MFH, zz, "air");
        P.put(sx + dx, fy + MFH + 1, zz, "air");
      }
    }
  }

  for (auto [dx, dz] : {std::pair{10, -6}, {-12, 8}}) {
    const int x = cx + dx, z = S.cz + dz;
    for (int k = 0; k <= 3; k++) P.put(x, top + k, z, "cobblestone");
    P.put(x, top + 4, z, PLK("cobblestone_wall", "cobblestone"));
  }

  for (int fl = 0; fl < MFL; fl++) for (int gz = 0; gz < MZ; gz++) for (int gx = 0; gx < MX; gx++) {
    const char* kind = mansionKind(fl, gx, gz);
    if (std::strcmp(kind, "none") == 0 || std::strcmp(kind, "hall") == 0
        || std::strcmp(kind, "foyer") == 0 || std::strcmp(kind, "stairs") == 0) continue;
    const int ix0 = x0 + gx * MCS + 1, iz0 = z0 + gz * MCS + 1;
    const int ix1 = x0 + (gx + 1) * MCS - 1, iz1 = z0 + (gz + 1) * MCS - 1;
    const int fy = y + fl * MFH + 1;
    if (std::strcmp(kind, "bedroom") == 0) mBedroom(P, ix0, iz0, ix1, iz1, fy, S.seed);
    else if (std::strcmp(kind, "library") == 0) mLibrary(P, ix0, iz0, ix1, iz1, fy);
    else if (std::strcmp(kind, "store") == 0) mStore(P, ix0, iz0, ix1, iz1, fy);
    else if (std::strcmp(kind, "dining") == 0) mDining(P, ix0, iz0, ix1, iz1, fy);
    else if (std::strcmp(kind, "flower") == 0) mFlower(P, ix0, iz0, ix1, iz1, fy);
    else if (std::strcmp(kind, "office") == 0) mOffice(P, ix0, iz0, ix1, iz1, fy);
  }
}

} // namespace dq
