// gen/structures_far.cpp — the far places: game/places.js (the Sky Temple, cloud shrines, skyships, moa nests in the
// Skylands; Ember Bastions, obsidian spires, ruined forges, bone pits in the Ember Depths), game/nether2.js (Nether
// fortresses), game/batche.js (Floating Sanctums) and game/theend.js (End cities with their ships).
// Skylands heights are the original's + SKY_DY, the End's + END_DY; the Depths' through vmap::netherY, with the
// pillars that reach down into the lava stretched by NETHER_K so they still reach it.
#include "gen/structures_common.h"

namespace dq {
using namespace vmap;

static int kDepth(int n) { return (int)std::ceil(n * NETHER_K); }   // a pillar n original rows long, in band rows

// ---- the Skylands (places.js skyTop / skyFlat, band y) ----
static int skyTop(const SkylandsGen& g, int x, int z) { return g.islandMask(x, z) <= 0.12 ? -1 : g.hillTop(x, z); }
static int skyFlat(const SkylandsGen& g, int x, int z, int r) {
  const int c = skyTop(g, x, z); if (c < 0) return -1;
  for (auto [a, b] : {std::pair{r, 0}, {-r, 0}, {0, r}, {0, -r}, {r, r}, {-r, -r}}) { const int t = skyTop(g, x + a, z + b); if (t < 0 || std::abs(t - c) > 3) return -1; }
  return c;
}

static bool skyTemplePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const int yo = 92 + jsFloor(rnd() * 8), y = yo + SKY_DY;
  S.x0 = x - 14; S.x1 = x + 14; S.z0 = z - 14; S.z1 = z + 14; S.y = y; S.hashDy = -SKY_DY; S.cx = x; S.cz = z; S.surface = true;
  S.name = "the Sky Temple";
  S.data = {{"boss", {{"at", jat(x, y + 1, z + 4)}, {"type", "valkyrie_queen"}, {"box", {x - 8, y, z - 8, x + 8, y + 9, z + 8}}}}};
  return true;
}
static void skyTempleBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y, R = 9;
  const std::string Q = PLK("quartz_block", "smooth_stone"), QP = PLK("quartz_pillar", Q), GB = PLK("gold_block", "glowstone"), GP = PLK("glass_pane", "glass"), GL = PLK("glass", "air");
  // its own island under it
  for (int dz = -14; dz <= 14; dz++) for (int dx = -14; dx <= 14; dx++) {
    const double r = std::hypot(dx, dz); if (r > 13.5) continue;
    const int depth = jsRound((13.5 - r) * 0.9 + P.h(cx + dx, 0, cz + dz, 3) * 3);
    for (int k = 1; k <= depth; k++) P.put(cx + dx, y - k, cz + dz, k == depth && P.h(cx + dx, k, cz + dz, 4) < 0.3 ? "aether_crystal_ore" : "skystone");
    if (std::abs(dx) > R || std::abs(dz) > R) { P.put(cx + dx, y, cz + dz, "sky_grass"); if (P.h(cx + dx, 1, cz + dz, 5) < 0.12) P.put(cx + dx, y + 1, cz + dz, "tall_grass"); }
  }
  // the hall
  P.fill(cx - R, y, cz - R, cx + R, y, cz + R, Q);
  P.fillf(cx - R, y + 1, cz - R, cx + R, y + 8, cz + R, [&](int x, int yy, int z) -> std::string_view {
    if (!(x == cx - R || x == cx + R || z == cz - R || z == cz + R)) return "air";
    if ((std::abs(x - cx) % 4 == 1 && std::abs(z - cz) % 4 == 1) || ((x - cx) % 4 == 0 && (z - cz) % 4 == 0)) return QP;
    return (yy - y >= 3 && yy - y <= 5) && ((x - cx + z - cz) % 3 == 0) ? std::string_view(GP) : std::string_view(Q);
  });
  P.fill(cx - R, y + 8, cz - R, cx + R, y + 8, cz + R, Q);
  for (int dx = -R; dx <= R; dx += 2) { P.put(cx + dx, y + 9, cz - R, GB); P.put(cx + dx, y + 9, cz + R, GB); }
  for (int dz = -R; dz <= R; dz += 2) { P.put(cx - R, y + 9, cz + dz, GB); P.put(cx + R, y + 9, cz + dz, GB); }
  // a dome of gold and glass in the middle of the roof
  for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
    const double r = std::hypot(dx, dz);
    if (r <= 3.2) { P.put(cx + dx, y + 8, cz + dz, r < 2 ? GL : GB); if (r <= 2.2) P.put(cx + dx, y + 9, cz + dz, r < 1.2 ? GB : GL); }
  }
  // pillars inside, lights, the altar where she waits, the treasure behind it
  for (auto [a, b] : {std::pair{-5, -5}, {5, -5}, {-5, 5}, {5, 5}}) { P.fill(cx + a, y + 1, cz + b, cx + a, y + 7, cz + b, QP); P.put(cx + a, y + 1, cz + b - 1, "glowstone"); }
  P.fill(cx - 1, y + 1, cz + 6, cx + 1, y + 1, cz + 7, GB);
  P.chest(cx - 3, y + 1, cz + 8, 2, "sky_temple"); P.chest(cx + 3, y + 1, cz + 8, 2, "sky_temple");
  P.lectern(cx, y + 1, cz - 6, 0, "skylands_temple");
  // the door, facing north, with steps out onto the island
  P.fill(cx - 1, y + 1, cz - R, cx + 1, y + 4, cz - R, "air");
  for (int k = 1; k <= 3; k++) P.fill(cx - 1, y, cz - R - k, cx + 1, y, cz - R - k, Q);
}

static bool skyShrinePlan(const TerrainQuery& t, int x, int z, Mulberry32&, StructurePlan& S) {
  const SkylandsGen* g = skyOf(t); if (!g) return false;
  const int tp = skyFlat(*g, x, z, 4); if (tp < 0) return false;
  S.x0 = x - 6; S.x1 = x + 6; S.z0 = z - 6; S.z1 = z + 6; S.y = tp + 1; S.hashDy = -SKY_DY; S.cx = x; S.cz = z; S.surface = true;
  return true;
}
static void skyShrineBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y; const std::string Q = PLK("quartz_block", "smooth_stone");
  for (int dz = -5; dz <= 5; dz++) for (int dx = -5; dx <= 5; dx++) {
    const double r = std::hypot(dx, dz); if (r > 5.2) continue;
    P.put(cx + dx, y - 1, cz + dz, r < 2.2 ? std::string_view(Q) : "skystone");
    for (int k = 0; k < 5; k++) P.put(cx + dx, y + k, cz + dz, "air");
    for (int k = 2; k < 5; k++) P.put(cx + dx, y - k, cz + dz, "skystone");
  }
  for (int i = 0; i < 6; i++) { const double a = i / 6.0 * JTAU; const int x = jsRound(cx + std::cos(a) * 4), z = jsRound(cz + std::sin(a) * 4); P.fill(x, y, z, x, y + 2, z, PLK("quartz_pillar", Q)); P.put(x, y + 3, z, "glowstone"); }
  P.lectern(cx, y, cz, 0, P.hw(cx, y, cz, 1) < 0.5 ? "sky_islands" : "skylands_temple");
  P.chest(cx, y, cz + 2, 2, "sky_shrine");
}

static bool skyshipPlan(const TerrainQuery&, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const int y = 96 + jsFloor(rnd() * 8) + SKY_DY;
  S.x0 = x - 12; S.x1 = x + 12; S.z0 = z - 7; S.z1 = z + 7; S.y = y; S.hashDy = -SKY_DY; S.cx = x; S.cz = z; S.surface = true;
  return true;
}
static void skyshipBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y;
  const std::string Wd = PLK("spruce_planks", "planks"), L = PLK("spruce_log", "log"), wool = PLK("white_wool", "wool"), fence = PLK("spruce_fence", PLK("oak_fence", Wd));
  // the hull: a long boat, tapering at the ends, the deck at y
  for (int dx = -10; dx <= 10; dx++) {
    const int taper = std::abs(dx) > 6 ? std::abs(dx) - 6 : 0, half = 3 - std::min(2, taper);
    for (int dz = -half; dz <= half; dz++) {
      P.put(cx + dx, y, cz + dz, Wd);
      const int depth = 3 - std::min(2, taper);
      for (int k = 1; k <= depth; k++) P.put(cx + dx, y - k, cz + dz, std::abs(dz) == half || k == depth ? std::string_view(Wd) : "air");
      if (std::abs(dz) == half) P.put(cx + dx, y + 1, cz + dz, fence);
    }
  }
  // the hold: two chests below the deck, a hatch
  P.put(cx - 2, y - 2, cz, "air"); P.chest(cx - 3, y - 2, cz, 1, "skyship"); P.chest(cx + 3, y - 2, cz, 3, "skyship"); P.put(cx, y, cz, "air"); P.put(cx, y - 1, cz, "air"); P.ladder(cx, y - 2, y - 1, cz, 2);
  // the mast and its sail, and the great balloon above held by chains
  P.fill(cx + 1, y + 1, cz, cx + 1, y + 9, cz, L);
  P.fillf(cx + 1, y + 3, cz - 3, cx + 1, y + 7, cz + 3, [&](int, int, int z) -> std::string_view { return z == cz ? L : wool; });
  for (auto [a, b] : {std::pair{-6, -2}, {-6, 2}, {6, -2}, {6, 2}}) P.fill(cx + a, y + 1, cz + b, cx + a, y + 8, cz + b, "chain");
  const std::string lg = PLK("light_gray_wool", wool);
  for (int dz = -6; dz <= 6; dz++) for (int dx = -11; dx <= 11; dx++) for (int dy = 0; dy <= 6; dy++) {
    const double e = std::pow(dx / 11.0, 2) + std::pow(dz / 6.0, 2) + std::pow((dy - 3) / 3.4, 2);
    if (e <= 1 && e > 0.62) P.put(cx + dx, y + 9 + dy, cz + dz, P.h(cx + dx, dy, cz + dz, 6) < 0.15 ? lg : wool);
  }
  P.put(cx - 9, y + 1, cz, "lantern"); P.put(cx + 9, y + 1, cz, "lantern");
}

static bool moaPlan(const TerrainQuery& t, int x, int z, Mulberry32&, StructurePlan& S) {
  const SkylandsGen* g = skyOf(t); if (!g) return false;
  const int tp = skyFlat(*g, x, z, 3); if (tp < 0) return false;
  S.x0 = x - 4; S.x1 = x + 4; S.z0 = z - 4; S.z1 = z + 4; S.y = tp + 1; S.hashDy = -SKY_DY; S.cx = x; S.cz = z; S.surface = true;
  S.data = {{"residents", {mobAt(x + 2, tp + 1, z + 2, "moa"), mobAt(x - 2, tp + 1, z - 1, "moa")}}};
  return true;
}
static void moaBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y; const std::string H = PLK("hay_bale", PLK("hay_block", "planks")), D = PLK("coarse_dirt", "dirt");
  for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
    const double r = std::hypot(dx, dz); if (r > 3.3) continue;
    P.put(cx + dx, y - 1, cz + dz, r > 2.3 ? H : D);
    if (r > 2.3) P.put(cx + dx, y, cz + dz, P.hw(cx + dx, y, cz + dz, 2) < 0.5 ? std::string_view(H) : "air"); else P.put(cx + dx, y, cz + dz, "air");
  }
  P.chest(cx, y, cz, 0, "moa_nest");
}

// ---- the Ember Depths ----
static bool bastionPlan(const TerrainQuery&, int x, int z, Mulberry32&, StructurePlan& S) {
  const int y = netherY(31);
  S.x0 = x - 34; S.x1 = x + 34; S.z0 = z - 34; S.z1 = z + 34; S.y = y; S.hashDy = 31 - y; S.cx = x; S.cz = z; S.name = "an Ember Bastion";
  S.data = {{"boss", {{"at", jat(x, y + 1, z)}, {"type", "ember_lord"}, {"box", {x - 6, y, z - 6, x + 6, y + 9, z + 6}}}},
            {"spawners", {mobAt(x - 9, y + 1, z + 9, "charred_ghoul"), mobAt(x + 9, y + 1, z - 9, "charred_ghoul")}}};
  return true;
}
static void bastionBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y, R = 12;
  const std::string nb = PLK("nether_bricks", "blackstone"), bs = PLK("blackstone", nb), pb = PLK("polished_blackstone_bricks", PLK("polished_blackstone", bs)), nf = PLK("nether_brick_fence", nb);
  // room to stand: the cavern cleared round it
  P.fill(cx - R - 2, y + 1, cz - R - 2, cx + R + 2, y + 15, cz + R + 2, "air");
  // the floor, on pillars down into the lava
  P.fill(cx - R, y, cz - R, cx + R, y, cz + R, nb);
  for (int dz = -R; dz <= R; dz += 4) for (int dx = -R; dx <= R; dx += 4) for (int k = 1; k <= kDepth(12); k++) P.put(cx + dx, y - k, cz + dz, nb);
  // the walls, crenellated, with towers at the corners
  P.fillf(cx - R, y + 1, cz - R, cx + R, y + 8, cz + R, [&](int x, int yy, int z) -> std::string_view { return (x == cx - R || x == cx + R || z == cz - R || z == cz + R) ? (yy - y == 4 && (x + z) % 4 == 0 ? "air" : std::string_view(nb)) : "air"; });
  for (int d = -R; d <= R; d += 2) { P.put(cx + d, y + 9, cz - R, nb); P.put(cx + d, y + 9, cz + R, nb); P.put(cx - R, y + 9, cz + d, nb); P.put(cx + R, y + 9, cz + d, nb); }
  for (auto [a, b] : {std::pair{-R, -R}, {R, -R}, {-R, R}, {R, R}}) { P.fill(cx + a - 1, y + 1, cz + b - 1, cx + a + 1, y + 13, cz + b + 1, bs); P.put(cx + a, y + 12, cz + b, "glowstone"); }
  // the inner hall where he sits, magma glowing in the floor
  P.fillf(cx - 6, y + 1, cz - 6, cx + 6, y + 9, cz + 6, [&](int x, int yy, int z) -> std::string_view { return (std::abs(x - cx) == 6 || std::abs(z - cz) == 6) ? std::string_view(pb) : yy == y + 9 ? std::string_view(pb) : "air"; });
  for (int dz = -4; dz <= 4; dz += 4) for (int dx = -4; dx <= 4; dx += 4) P.put(cx + dx, y, cz + dz, PLK("magma_block", "glowstone"));
  P.fill(cx - 1, y + 1, cz - 6, cx + 1, y + 4, cz - 6, "air");
  P.chest(cx - 4, y + 1, cz + 5, 2, "ember_bastion"); P.chest(cx + 4, y + 1, cz + 5, 2, "ember_bastion"); P.chest(cx + R - 2, y + 1, cz - R + 2, 3, "ember_bastion");
  P.lectern(cx, y + 1, cz + 4, 2, "ember_lord");
  for (auto [dx, dz] : {std::pair{-9, 9}, {9, -9}}) P.spawner(cx + dx, y + 1, cz + dz, "charred_ghoul");
  for (auto [a, b] : {std::pair{-3, -3}, {3, -3}, {-3, 3}, {3, 3}}) P.put(cx + a, y + 8, cz + b, "soul_lantern");
  // gates on every side, and bridges out into the caverns
  for (auto [ax, az] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
    const int px = -az, pz = ax;
    for (int k = -1; k <= 1; k++) for (int h = 1; h <= 4; h++) P.put(cx + ax * R + px * k, y + h, cz + az * R + pz * k, "air");
    for (int d = R + 1; d <= 34; d++) for (int k = -1; k <= 1; k++) {
      const int bx = cx + ax * d + px * k, bz = cz + az * d + pz * k;
      P.put(bx, y, bz, nb);
      for (int h = 1; h <= 4; h++) P.put(bx, y + h, bz, std::abs(k) == 1 && h == 1 ? std::string_view(nf) : "air");
      if (d % 6 == 0 && k == 0) for (int q = 1; q <= kDepth(10); q++) P.put(bx, y - q, bz, nb);
    }
  }
}

static bool spirePlan(const TerrainQuery&, int x, int z, Mulberry32&, StructurePlan& S) {
  const int y = netherY(30);
  S.x0 = x - 4; S.x1 = x + 4; S.z0 = z - 4; S.z1 = z + 4; S.y = y; S.hashDy = 30 - y; S.cx = x; S.cz = z; S.name = "an Obsidian Spire";
  return true;
}
static void spireBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y, top = netherY(74);
  const std::string co = PLK("crying_obsidian", "obsidian");
  for (int yy = y - 8; yy <= top + 4; yy++) {
    const double r = yy > top ? 3 - (yy - top) * 0.6 : 3;
    for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
      const int d = std::max(std::abs(dx), std::abs(dz));
      if (d > r) continue;
      const bool shell = d >= std::floor(r);
      P.put(cx + dx, yy, cz + dz, shell ? (P.hw(cx + dx, yy, cz + dz, 1) < 0.1 ? std::string_view(co) : "obsidian") : (yy == top - 6 || yy == y ? "obsidian" : "air"));
    }
  }
  P.fill(cx - 1, y + 1, cz - 3, cx + 1, y + 3, cz - 3, "air");   // the way in
  P.ladder(cx, y + 1, top - 7, cz + 2, 2);
  P.put(cx, top - 6, cz + 2, "air");
  P.chest(cx - 1, top - 5, cz - 1, 0, "ember_spire"); P.put(cx + 1, top - 5, cz - 1, "soul_lantern");
  for (auto [a, b] : {std::pair{0, -3}, {0, 3}, {-3, 0}, {3, 0}}) P.put(cx + a, top - 3, cz + b, PLK("glass", "air"));
}

static bool forgePlan(const TerrainQuery&, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const int yo = 40 + jsFloor(rnd() * 30), y = netherY(yo);
  S.x0 = x - 7; S.x1 = x + 7; S.z0 = z - 7; S.z1 = z + 7; S.y = y; S.hashDy = yo - y; S.cx = x; S.cz = z; S.name = "a Ruined Forge";
  return true;
}
static void forgeBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y;
  const std::string pb = PLK("polished_blackstone_bricks", PLK("blackstone", "obsidian")), bs = PLK("blackstone", pb);
  P.fillf(cx - 6, y, cz - 6, cx + 6, y + 6, cz + 6, [&](int x, int yy, int z) -> std::string_view {
    if (!(std::abs(x - cx) == 6 || std::abs(z - cz) == 6 || yy == y || yy == y + 6)) return "air";
    return P.hw(x, yy, z, 2) < 0.12 && yy > y && yy < y + 6 ? "air" : yy == y ? std::string_view(bs) : std::string_view(pb);
  });
  // the lava channel, the anvils and furnaces, the stores
  P.fill(cx - 4, y, cz, cx + 4, y, cz, "lava"); P.fill(cx - 4, y - 1, cz, cx + 4, y - 1, cz, bs);
  P.put(cx - 3, y + 1, cz - 3, PLK("anvil", "iron_block")); P.put(cx + 3, y + 1, cz - 3, PLK("anvil", "iron_block"));
  P.put(cx - 5, y + 1, cz + 2, PLK("blast_furnace", "furnace"), 1); P.put(cx - 5, y + 1, cz + 4, "furnace", 1);
  P.chest(cx + 4, y + 1, cz + 4, 3, "forge"); P.chest(cx + 4, y + 1, cz + 2, 3, "forge");
  P.put(cx, y + 5, cz - 4, "lantern", 1); P.put(cx, y + 5, cz + 4, "lantern", 1);
  P.fill(cx - 1, y + 1, cz - 6, cx + 1, y + 3, cz - 6, "air");
}

static bool bonePitPlan(const TerrainQuery&, int x, int z, Mulberry32&, StructurePlan& S) {
  const int y = netherY(32);
  S.x0 = x - 6; S.x1 = x + 6; S.z0 = z - 6; S.z1 = z + 6; S.y = y; S.hashDy = 32 - y; S.cx = x; S.cz = z; S.name = "a Bone Pit";
  S.data = {{"spawners", {mobAt(x, y - 3, z, "skeleton")}}};
  return true;
}
static void bonePitBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y; const std::string bone = PLK("bone_block", "blackstone"), ss = PLK("soul_sand", bone);
  P.fill(cx - 6, y, cz - 6, cx + 6, y + 6, cz + 6, "air");
  for (int dz = -6; dz <= 6; dz++) for (int dx = -6; dx <= 6; dx++) {
    const double r = std::hypot(dx, dz); if (r > 6.2) continue;
    const int depth = std::max(0, jsRound(4 - r * 0.6));
    P.put(cx + dx, y - 1 - depth, cz + dz, P.h(cx + dx, 0, cz + dz, 1) < 0.5 ? ss : bone);
    for (int k = 0; k < depth + 1; k++) P.put(cx + dx, y - k, cz + dz, "air");
    if (r > 5 && P.h(cx + dx, 2, cz + dz, 3) < 0.4) P.fill(cx + dx, y, cz + dz, cx + dx, y + 1 + jsFloor(P.h(cx + dx, 3, cz + dz, 4) * 3), cz + dz, bone);
  }
  P.spawner(cx, y - 3, cz, "skeleton");
  P.chest(cx + 2, y - 2, cz + 1, 1, "bone_pit");
  P.put(cx - 4, y, cz - 2, "soul_lantern"); P.put(cx + 4, y, cz + 3, "soul_lantern");
}

// ---- Nether fortresses (nether2.js): long brick bridges across the lava, a keep at the crossing ----
static bool fortressPlan(const TerrainQuery&, int x, int z, Mulberry32&, StructurePlan& S) {
  const int y = netherY(34), A = 40;
  S.x0 = x - A; S.x1 = x + A; S.z0 = z - A; S.z1 = z + A; S.y = y; S.hashDy = 34 - y; S.cx = x; S.cz = z; S.name = "a Nether Fortress";
  S.data = {{"spawners", {mobAt(x + 6, y + 1, z - 6, "wither_skeleton"), mobAt(x - 6, y + 1, z + 6, "wither_skeleton"), mobAt(x + 24, y + 1, z, "cinder_wisp")}}};
  return true;
}
static void fortressBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y, A = 40, K = 8;
  const std::string nb = PLK("nether_bricks", "blackstone"), fence = PLK("nether_brick_fence", nb), stair = PLK("nether_brick_slab", nb);
  // four long bridges meeting at the keep, held up on arches down into the lava
  for (auto [ax, az] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
    const int px = -az, pz = ax;
    for (int d = K + 1; d <= A; d++) for (int k = -2; k <= 2; k++) {
      const int bx = cx + ax * d + px * k, bz = cz + az * d + pz * k;
      P.put(bx, y, bz, nb);
      for (int h = 1; h <= 5; h++) P.put(bx, y + h, bz, std::abs(k) == 2 ? (h <= 2 ? (h == 1 ? std::string_view(nb) : std::string_view(fence)) : "air") : "air");
      // a roofed stretch every so often, with windows
      if (d % 12 >= 8 && d % 12 <= 11) { if (std::abs(k) == 2) for (int h = 2; h <= 4; h++) P.put(bx, y + h, bz, h == 3 && d % 2 ? fence : nb); P.put(bx, y + 5, bz, nb); }
      if (d % 8 == 0 && std::abs(k) <= 1) for (int q = 1; q <= kDepth(14); q++) P.put(bx, y - q, bz, nb);
      if (d % 8 == 0 && std::abs(k) == 2) P.put(bx, y - 1, bz, stair);
    }
    // a watch post at the end of each bridge
    const int ex = cx + ax * A, ez = cz + az * A;
    P.fillf(ex - 2, y + 1, ez - 2, ex + 2, y + 1, ez + 2, [&](int x, int, int z) -> std::string_view { return (std::abs(x - ex) == 2 || std::abs(z - ez) == 2) ? std::string_view(fence) : "air"; });
  }
  // the keep: a walled courtyard round a hall
  P.fill(cx - K, y + 1, cz - K, cx + K, y + 9, cz + K, "air");
  P.fill(cx - K, y, cz - K, cx + K, y, cz + K, nb);
  for (int dz = -K; dz <= K; dz += 4) for (int dx = -K; dx <= K; dx += 4) for (int q = 1; q <= kDepth(14); q++) P.put(cx + dx, y - q, cz + dz, nb);
  P.fillf(cx - K, y + 1, cz - K, cx + K, y + 6, cz + K, [&](int x, int yy, int z) -> std::string_view { return (std::abs(x - cx) == K || std::abs(z - cz) == K) ? (yy - y == 3 && (x + z) % 3 == 0 ? std::string_view(fence) : std::string_view(nb)) : "air"; });
  for (int d = -K; d <= K; d += 2) { P.put(cx + d, y + 7, cz - K, nb); P.put(cx + d, y + 7, cz + K, nb); P.put(cx - K, y + 7, cz + d, nb); P.put(cx + K, y + 7, cz + d, nb); }
  for (auto [ax, az] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) for (int k = -1; k <= 1; k++) for (int h = 1; h <= 3; h++) P.put(cx + ax * K + -az * k, y + h, cz + az * K + ax * k, "air");
  // the hall at the heart, and the nether wart garden beside it
  P.fillf(cx - 3, y + 1, cz - 3, cx + 3, y + 5, cz + 3, [&](int x, int yy, int z) -> std::string_view { return (std::abs(x - cx) == 3 || std::abs(z - cz) == 3 || yy == y + 5) ? std::string_view(nb) : "air"; });
  for (int h = 1; h <= 2; h++) P.put(cx, y + h, cz - 3, "air");
  P.chest(cx - 2, y + 1, cz + 2, 2, "nether_fortress"); P.chest(cx + 2, y + 1, cz + 2, 2, "nether_fortress");
  P.put(cx, y + 4, cz, "glowstone");
  if (hasBlock("soul_sand")) for (int dz = -6; dz <= -5; dz++) for (int dx = -6; dx <= -2; dx++) { P.put(cx + dx, y, cz + dz, "soul_sand"); if (hasBlock("nether_wart")) P.put(cx + dx, y + 1, cz + dz, "nether_wart", 3); }
  for (auto [a, b] : {std::pair{-K, -K}, {K, -K}, {-K, K}, {K, K}}) { P.fill(cx + a - 1, y + 1, cz + b - 1, cx + a + 1, y + 9, cz + b + 1, nb); P.put(cx + a, y + 10, cz + b, "glowstone"); }
  P.chest(cx + 24, y + 1, cz + 1, 2, "nether_fortress");
  P.spawner(cx + 6, y + 1, cz - 6, "wither_skeleton"); P.spawner(cx - 6, y + 1, cz + 6, "wither_skeleton"); P.spawner(cx + 24, y + 1, cz, "cinder_wisp");
}

// ---- Floating Sanctums (batche.js): a quartz shrine on its own islet in open sky near the Skylands ----
static bool sanctumPlan(const TerrainQuery& t, int x0, int z0, Mulberry32& rnd, StructurePlan& S) {
  const SkylandsGen* g = skyOf(t); if (!g) return false;
  auto ok = [&](int x, int z) {
    if (skyTop(*g, x, z) >= 0) return false;
    for (auto [a, b] : {std::pair{8, 0}, {-8, 0}, {0, 8}, {0, -8}}) if (skyTop(*g, x + a, z + b) >= 0) return false;
    for (double d : {22.0, 34.0}) for (auto [a, b] : {std::pair{d, 0.0}, {-d, 0.0}, {0.0, d}, {0.0, -d}, {d * 0.7, d * 0.7}, {-d * 0.7, -d * 0.7}, {d * 0.7, -d * 0.7}, {-d * 0.7, d * 0.7}})
      if (skyTop(*g, x + jsRound(a), z + jsRound(b)) >= 0) return true;
    return false;
  };
  for (int i = 0; i < 16; i++) {
    const int bx = jsFloor(x0 / 340.0) * 340, bz = jsFloor(z0 / 340.0) * 340;
    auto cl = [](int v, int b) { return std::max(b + 10, std::min(b + 330, v)); };
    const int x = cl(x0 + (i ? jsFloor((rnd() - 0.5) * 240) : 0), bx);
    const int z = cl(z0 + (i ? jsFloor((rnd() - 0.5) * 240) : 0), bz);
    if (ok(x, z)) {
      const int y = 84 + jsFloor(rnd() * 10) + SKY_DY;
      S.x0 = x - 6; S.x1 = x + 6; S.z0 = z - 6; S.z1 = z + 6; S.y = y; S.hashDy = -SKY_DY; S.cx = x; S.cz = z;
      return true;
    }
  }
  return false;
}
static void sanctumBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const int cx = S.cx, cz = S.cz, y = S.y; const std::string Q = PLK("quartz_block", "smooth_stone"), QP = PLK("quartz_pillar", Q);
  for (int dz = -6; dz <= 6; dz++) for (int dx = -6; dx <= 6; dx++) {
    const double r = std::hypot(dx, dz);
    for (int k = 0; k < 6; k++) P.put(cx + dx, y + k, cz + dz, "air");
    // the islet: grass on top, tapering to a ragged point of skystone underneath
    for (int k = 0; k < 7; k++) {
      if (r > 5.3 - k * 0.8 + P.h(cx + dx, k, cz + dz, 3) * 0.7) break;
      P.put(cx + dx, y - 1 - k, cz + dz, k == 0 ? (r < 2.6 ? std::string_view(Q) : "sky_grass") : k == 1 ? "dirt" : "skystone");
    }
    if (r >= 2.6 && r < 4.6 && P.hw(cx + dx, y, cz + dz, 5) < 0.18) P.put(cx + dx, y, cz + dz, P.hw(cx + dx, y, cz + dz, 6) < 0.5 ? "flower_yellow" : "tall_grass");
  }
  for (auto [dx, dz] : {std::pair{-3, -3}, {3, -3}, {-3, 3}, {3, 3}}) { P.fill(cx + dx, y, cz + dz, cx + dx, y + 3, cz + dz, QP); P.put(cx + dx, y + 4, cz + dz, Q); }
  for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (std::abs(dx) + std::abs(dz) < 4) P.put(cx + dx, y + 4, cz + dz, dx == 0 && dz == 0 ? std::string_view("glowstone") : std::string_view(Q));
  P.chest(cx, y, cz, 2, "sky_sanctum");
}

// ---- End cities (theend.js): purpur towers out on the islands, some with a ship moored beside them ----
constexpr int END_CITY_REG = 360;
struct CityExt { int floors, dir; bool ship; };
static bool endCityPlace(const StructureIndex&, const TerrainQuery& t, int rx, int rz, StructurePlan& S) {
  const EndGen* g = endOf(t); if (!g) return false;
  Mulberry32 rnd(lairSeed(t.seed(), rx, rz, 7701));
  if (rnd() >= 0.6) return false;
  const int x0 = rx * END_CITY_REG + 60 + jsFloor(rnd() * (END_CITY_REG - 120)), z0 = rz * END_CITY_REG + 60 + jsFloor(rnd() * (END_CITY_REG - 120));
  if (std::hypot((double)x0, (double)z0) <= EndGen::OUTER + 90) return false;
  for (int tt = 0; tt < 12; tt++) {
    const int x = x0 + jsRound((rnd() - 0.5) * 60), z = z0 + jsRound((rnd() - 0.5) * 60);
    EndGen::Isle c; if (!g->island(x, z, c) || c.top - c.bot < 8) continue;
    bool ok = true;
    for (auto [dx, dz] : {std::pair{5, 5}, {-5, 5}, {5, -5}, {-5, -5}}) { EndGen::Isle o; if (!g->island(x + dx, z + dz, o) || std::abs(o.top - c.top) > 3) ok = false; }
    if (!ok) continue;
    auto e = std::make_shared<CityExt>(); e->floors = 3 + jsFloor(rnd() * 3); e->dir = jsFloor(rnd() * 4); e->ship = rnd() < 0.65;
    const int reach = e->ship ? 26 : 7;
    S.id = "e" + std::to_string(rx) + "_" + std::to_string(rz); S.seed = lairSeed(t.seed(), rx, rz, 7702); S.name = "an End city";
    S.cx = x; S.cz = z; S.y = c.top + 1; S.hashDy = -END_DY; S.x0 = x - reach; S.x1 = x + reach; S.z0 = z - reach; S.z1 = z + reach;
    S.data = {{"roofY", S.y + e->floors * 6}, {"ship", e->ship}, {"dir", e->dir}, {"floors", e->floors}};
    S.ext = e;
    return true;
  }
  return false;
}
static void endCityBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const CityExt& E = extOf<CityExt>(S); const int x = S.cx, z = S.cz, y = S.y, floors = E.floors;
  auto wall = [&](int xx, int, int zz) { return std::abs(xx - x) == 3 && std::abs(zz - z) == 3 ? "purpur_pillar" : "purpur_block"; };
  // footing: a broad purpur base, its legs reaching down into the island
  P.fill(x - 5, y - 1, z - 5, x + 5, y - 1, z + 5, "purpur_block");
  for (auto [dx, dz] : {std::pair{-5, -5}, {5, -5}, {-5, 5}, {5, 5}}) for (int yy = y - 2; yy > y - 10; yy--) { const BlockId id = P.get(x + dx, yy, z + dz); if (id != AIR && id != NOBLOCK && tSolid(id)) break; P.put(x + dx, yy, z + dz, "purpur_pillar"); }
  // the tower: a hollow column with a floor every six blocks, lit with end rods, windows of magenta glass
  const int topY = y + floors * 6;
  P.fillf(x - 3, y, z - 3, x + 3, topY, z + 3, wall);
  P.fill(x - 2, y, z - 2, x + 2, topY - 1, z + 2, "air");
  for (int f = 0; f < floors; f++) {
    const int fy = y + f * 6;
    if (f > 0) P.fill(x - 2, fy, z - 2, x + 2, fy, z + 2, "purpur_block");
    P.put(x, fy + 5, z, "end_rod");
    for (auto [dx, dz] : {std::pair{0, -3}, {3, 0}, {0, 3}, {-3, 0}}) if (!(f == 0 && dz == 3)) { P.put(x + dx, fy + 2, z + dz, "magenta_stained_glass"); P.put(x + dx, fy + 3, z + dz, "magenta_stained_glass"); }
  }
  // a ladder up the north wall, through a gap in each floor
  for (int f = 1; f <= floors; f++) P.put(x, y + f * 6, z - 2, "air");
  P.ladder(x, y, topY, z - 2, 0);
  // the door
  P.fill(x, y, z + 3, x, y + 1, z + 3, "air");
  P.put(x, y - 1, z + 4, "purpur_block"); P.put(x, y - 1, z + 5, "purpur_block");
  // the room at the top: open to the sky, a low wall of end stone bricks, its chests
  P.fill(x - 5, topY, z - 5, x + 5, topY, z + 5, "purpur_block");
  P.fillf(x - 5, topY + 1, z - 5, x + 5, topY + 1, z + 5, [&](int xx, int, int zz) { return std::abs(xx - x) == 5 || std::abs(zz - z) == 5 ? "end_stone_bricks" : "air"; });
  P.fill(x - 4, topY + 2, z - 4, x + 4, topY + 6, z + 4, "air");
  P.put(x, topY, z - 2, "ladder", 0);
  for (auto [dx, dz] : {std::pair{-5, -5}, {5, -5}, {-5, 5}, {5, 5}}) { for (int yy = topY + 1; yy <= topY + 4; yy++) P.put(x + dx, yy, z + dz, "purpur_pillar"); P.put(x + dx, topY + 5, z + dz, "end_rod"); }
  P.chest(x - 4, topY + 1, z + 4, 0, "end_city"); P.chest(x + 4, topY + 1, z + 4, 0, "end_city"); P.chest(x + 4, topY + 1, z - 4, 2, "end_city");
  // the ship, moored off one side at the height of the roof, with a bridge across to it
  if (!E.ship) return;
  static const int D[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
  const int ax = D[E.dir][0], az = D[E.dir][1], sx = -az, sz = ax;   // along the ship, and across it
  const int cx = x + ax * 18, cz = z + az * 18, yS = topY;
  auto at = [&](int i, int j) { return std::pair{cx + ax * i + sx * j, cz + az * i + sz * j}; };
  for (int k = 6; k <= 13; k++) { const int bx = x + ax * k, bz = z + az * k; P.put(bx, yS, bz, "purpur_block"); P.put(bx + sx, yS, bz + sz, "purpur_slab"); P.put(bx - sx, yS, bz - sz, "purpur_slab"); }
  P.put(x + ax * 5, yS + 1, z + az * 5, "air");
  // the hull: pointed at the bow, square at the stern
  for (int i = -7; i <= 7; i++) {
    const int hw = i >= 6 ? 0 : i >= 4 || i <= -6 ? 1 : 2;
    for (int j = -hw; j <= hw; j++) {
      auto [hx, hz] = at(i, j);
      P.put(hx, yS - 2, hz, std::abs(j) < hw || hw == 0 ? "purpur_block" : "air");
      P.put(hx, yS - 1, hz, std::abs(j) == hw ? "purpur_block" : "end_stone_bricks");
      P.put(hx, yS, hz, std::abs(j) == hw || std::abs(i) == 7 ? "purpur_block" : "purpur_slab");
      for (int yy = yS + 1; yy <= yS + 9; yy++) P.put(hx, yy, hz, "air");
      if (std::abs(j) == hw && std::abs(i) < 7) P.put(hx, yS + 1, hz, "purpur_slab");
    }
  }
  // the gap in the rail where the bridge comes aboard
  { auto [gx, gz] = at(-8, 0); P.put(gx, yS, gz, "purpur_block"); auto [hx, hz] = at(-7, 0); P.put(hx, yS, hz, "purpur_slab"); P.put(hx, yS + 1, hz, "air"); }
  // the mast and its sail
  for (int yy = yS + 1; yy <= yS + 9; yy++) { auto [mx, mz] = at(1, 0); P.put(mx, yy, mz, "purpur_pillar"); }
  for (int yy = yS + 4; yy <= yS + 8; yy++) for (int j = -2; j <= 2; j++) if (j) { auto [ux, uz] = at(1, j); P.put(ux, yy, uz, "magenta_wool"); }
  { auto [lx, lz] = at(3, 0); P.put(lx, yS + 1, lz, "end_rod"); }
  // the hold: down a hatch at the stern, the chest with the wings
  auto [cx2, cz2] = at(-4, 0);
  P.put(cx2, yS, cz2, "air");
  const int i = P.chest(cx2, yS - 1, cz2, E.dir, "end_city");
  if (i >= 0) if (json* be = P.entity(i)) if (be->contains("items")) { json& it = (*be)["items"]; int s = 13; while (!it[s].is_null() && s < 26) s++; it[s] = json{{"id", "elytra"}, {"n", 1}}; }
  auto [k1x, k1z] = at(-3, 0); P.chest(k1x, yS - 1, k1z, E.dir, "end_city");
}

// ---- registration ----
static void regFar(const char* key, const char* name, int region, double chance, int margin, int jsIndex, int dim, decltype(StructureType::plan) plan, decltype(StructureType::build) build) {
  StructureType t; t.key = key; t.name = name; t.region = region; t.chance = chance; t.margin = margin; t.jsIndex = jsIndex;
  t.dims = 1u << dim; t.plan = std::move(plan); t.build = std::move(build);
  registerStructure(std::move(t));
}
void registerStructureEndCities() {
  StructureType t; t.key = "end_city"; t.name = "an End city"; t.region = END_CITY_REG; t.dims = 1u << DIM_END;
  t.place = endCityPlace; t.build = endCityBuild;
  registerStructure(std::move(t));
}
void registerStructureFarA() {   // places.js, STRUCTS 9..16
  regFar("sky_temple", "the Sky Temple", 640, 0.8, 70, 9, DIM_AETHER, skyTemplePlan, skyTempleBuild);
  regFar("sky_shrine", "a Cloud Shrine", 260, 0.6, 30, 10, DIM_AETHER, skyShrinePlan, skyShrineBuild);
  regFar("skyship", "a Skyship", 480, 0.55, 50, 11, DIM_AETHER, skyshipPlan, skyshipBuild);
  regFar("moa_nest", "a Moa Nest", 300, 0.5, 30, 12, DIM_AETHER, moaPlan, moaBuild);
  regFar("ember_bastion", "an Ember Bastion", 600, 0.8, 70, 13, DIM_NETHER, bastionPlan, bastionBuild);
  regFar("ember_spire", "an Obsidian Spire", 360, 0.55, 30, 14, DIM_NETHER, spirePlan, spireBuild);
  regFar("ruined_forge", "a Ruined Forge", 320, 0.6, 30, 15, DIM_NETHER, forgePlan, forgeBuild);
  regFar("bone_pit", "a Bone Pit", 280, 0.5, 20, 16, DIM_NETHER, bonePitPlan, bonePitBuild);
}
void registerStructureFarB() {   // nether2.js (20), batche.js (21)
  regFar("nether_fortress", "a Nether Fortress", 520, 0.75, 60, 20, DIM_NETHER, fortressPlan, fortressBuild);
  regFar("sky_sanctum", "a Floating Sanctum", 340, 0.55, 30, 21, DIM_AETHER, sanctumPlan, sanctumBuild);
}

} // namespace dq
