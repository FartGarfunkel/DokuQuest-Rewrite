// gen/structures_deep.cpp — places under (and on the edge of) the Overworld:
//   game/stronghold.js  three strongholds in a ring 720..1080 blocks out, the End portal room (frames round lava)
//   game/deepdark.js    Ancient Cities in the Deep Dark (the carve keeps out of them: SF_NO_CARVE)
//   game/deephalls.js   the Deep Halls (three floors, keys, traps, the lever puzzle, the Lich King) and the Twin Vault
//   game/seas.js        pirate coves on lonely beaches
// Depths below the land go through vmap::owUnder; the parts that reach the surface (shafts, stairs) follow the band.
#include "gen/structures_common.h"
#include <optional>

namespace dq {
using namespace vmap;

static const int* DXL = DIRX;
static const int* DZL = DIRZ;

// ==== Strongholds (STRUCTS 7) ========================================================================================
struct HoldExt { int cx, cy, cz, top; };
static void strongholdGlobal(const TerrainQuery& t, std::vector<StructurePlan>& out) {
  const i32 seed = t.seed();
  Mulberry32 rnd((u32)(seed ^ 0x57A0D));
  const double a0 = rnd() * JTAU;
  for (int k = 0; k < 3; k++) {
    // (the original retries with a wobble when the spot's land has been changed; a new world's never has)
    const double a = a0 + k * JTAU / 3, r = 720 + rnd() * 360;
    const int x = jsRound(std::cos(a) * r), z = jsRound(std::sin(a) * r);
    const TerrainColumn c = t.terrainColumn(x, z);
    const int cyo = std::clamp(c.jsY - 30, 12, 34), cy = owUnder(cyo);
    StructurePlan S;
    S.id = "sh_" + std::to_string(k); S.name = "a Stronghold"; S.cx = x; S.cz = z; S.y = cy; S.hashDy = cyo - cy;
    S.x0 = x - 19; S.x1 = x + 19; S.z0 = z - 39; S.z1 = z + 9; S.seed = hash32(x, cyo, z, seed);
    S.data = {{"spawners", json::array({mobAt(x, cy + 1, z + 6, "cave_spider")})}, {"portal", {x + 0.5, cy + 3, z + 2.5}}, {"top", c.y}};
    auto e = std::make_shared<HoldExt>(); e->cx = x; e->cy = cy; e->cz = z; e->top = c.y; S.ext = e;
    out.push_back(std::move(S));
  }
}
static void strongholdBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const HoldExt& H = extOf<HoldExt>(S);
  const int cx = H.cx, cy = H.cy, cz = H.cz;
  static const std::vector<const char*> walls = {"stone_bricks", "stone_bricks", "stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks"};
  auto wallK = [&](int x, int y, int z) { return P.pickw(walls, x, y, z, 3); };
  // the rooms and halls (inside measurements); each gets a shell of brick, then they're hollowed out together
  const int rooms[7][6] = {
    {cx - 4, cy + 1, cz - 7, cx + 4, cy + 8, cz + 7},        // the portal room
    {cx - 1, cy + 1, cz - 24, cx + 1, cy + 4, cz - 8},       // the hall from it
    {cx - 4, cy + 1, cz - 33, cx + 4, cy + 6, cz - 25},      // the crossing
    {cx + 5, cy + 1, cz - 30, cx + 12, cy + 4, cz - 28},     // east hall
    {cx - 12, cy + 1, cz - 30, cx - 5, cy + 4, cz - 28},     // west hall
    {cx - 18, cy + 1, cz - 37, cx - 13, cy + 7, cz - 21},    // the library
    {cx + 13, cy + 1, cz - 34, cx + 17, cy + 4, cz - 24}};   // the storeroom
  for (auto& r : rooms) P.fillf(r[0] - 1, r[1] - 1, r[2] - 1, r[3] + 1, r[4] + 1, r[5] + 1, wallK);
  for (auto& r : rooms) P.fill(r[0], r[1], r[2], r[3], r[4], r[5], "air");
  // ---- the portal room: a raised dais, the frames round a pool of lava, a cage of spiders behind ----
  P.fill(cx - 3, cy + 1, cz - 1, cx + 3, cy + 2, cz + 5, "stone_bricks");
  P.fill(cx - 1, cy + 1, cz - 2, cx + 1, cy + 1, cz - 2, "stone_brick_slab");
  const int pz = cz + 2, fy = cy + 3;
  for (int i = -1; i <= 1; i++) for (auto [x, z] : {std::pair{cx + i, pz - 2}, {cx + i, pz + 2}, {cx - 2, pz + i}, {cx + 2, pz + i}})
    P.put(x, fy, z, P.hw(x, fy, z, 11) < 0.1 ? "end_portal_frame_eye" : "end_portal_frame");
  P.fill(cx - 1, fy - 1, pz - 1, cx + 1, fy - 1, pz + 1, "lava");
  P.fill(cx - 1, fy, pz - 1, cx + 1, fy, pz + 1, "air");
  P.spawner(cx, cy + 1, cz + 6, "cave_spider");
  for (auto [x, z] : {std::pair{cx - 4, cz - 7}, {cx + 4, cz - 7}, {cx - 4, cz + 7}, {cx + 4, cz + 7}}) P.put(x, cy + 1, z, "lantern");
  // iron bars in the walls looking in from the halls
  for (int x : {cx - 3, cx + 3}) P.fill(x, cy + 2, cz - 8, x, cy + 3, cz - 8, "iron_bars");
  // ---- the crossing: lanterns and a fountain of stone ----
  for (auto [x, z] : {std::pair{cx - 4, cz - 33}, {cx + 4, cz - 33}, {cx - 4, cz - 25}, {cx + 4, cz - 25}}) P.put(x, cy + 1, z, "lantern");
  P.fill(cx - 1, cy + 1, cz - 30, cx + 1, cy + 1, cz - 28, "stone_brick_slab");
  P.put(cx, cy + 2, cz - 29, "chiseled_stone_bricks");
  // ---- the library: walls of books, an old book on its stand, a chest ----
  for (int z = cz - 37; z <= cz - 21; z++) for (int y = cy + 1; y <= cy + 5; y++) {
    P.put(cx - 18, y, z, P.hw(cx - 18, y, z, 5) < 0.12 ? "cobweb" : "bookshelf");
    if (z < cz - 31 || z > cz - 27) P.put(cx - 13, y, z, "bookshelf");
  }
  P.fill(cx - 17, cy + 6, cz - 37, cx - 14, cy + 6, cz - 21, "planks");
  for (int z = cz - 35; z <= cz - 23; z += 4) P.put(cx - 15, cy + 5, z, "lantern");
  P.lectern(cx - 15, cy + 1, cz - 33, 2, "end");
  P.chest(cx - 16, cy + 1, cz - 24, 2, "stronghold_library");
  // ---- the storeroom ----
  P.chest(cx + 17, cy + 1, cz - 33, 3, "stronghold");
  P.chest(cx + 17, cy + 1, cz - 25, 3, "stronghold");
  for (auto [x, z] : {std::pair{cx + 13, cz - 34}, {cx + 16, cz - 30}, {cx + 14, cz - 26}}) P.put(x, cy + 1, z, "barrel");
  P.put(cx + 15, cy + 1, cz - 29, "lantern");
  for (auto [x, y, z] : {std::tuple{cx + 13, cy + 4, cz - 24}, {cx + 17, cy + 4, cz - 34}, {cx + 16, cy + 4, cz - 29}}) P.put(x, y, z, "cobweb");
  // ---- a stair up: a narrow shaft with a ladder, from the crossing towards the surface (it stops short; the
  // original's 32 rows above the stronghold, in band rows) ----
  const int top = std::min(H.top - 7, cy + owUnder(32));
  if (top > cy + 9) {
    P.fillf(cx + 1, cy + 6, cz - 33, cx + 3, top + 1, cz - 31, wallK);
    P.fill(cx + 2, cy + 7, cz - 32, cx + 2, top, cz - 32, "air");
    P.put(cx + 2, cy + 6, cz - 32, "air");
    P.ladder(cx + 2, cy + 1, top, cz - 32, 0);
    for (int y = cy + 1; y <= cy + 6; y++) P.put(cx + 2, y, cz - 33, wallK(cx + 2, y, cz - 33));
  }
}

// ==== Ancient Cities (STRUCTS 8) =====================================================================================
static bool cityPlan(const TerrainQuery& t, int x0, int z0, Mulberry32& rnd, StructurePlan& S) {
  const OverworldGen* ow = owOf(t); if (!ow) return false;
  // somewhere in the region where the Deep Dark is deepest
  for (int tt = 0; tt < 24; tt++) {
    int x = x0, z = z0;
    if (tt) { x = x0 + jsRound((rnd() - 0.5) * 360); z = z0 + jsRound((rnd() - 0.5) * 360); }
    if (ow->deepZone(x, z) < 0.9 || ow->deepZone(x + 30, z) < 0.6 || ow->deepZone(x - 30, z) < 0.6 || ow->deepZone(x, z + 30) < 0.6 || ow->deepZone(x, z - 30) < 0.6) continue;
    if (t.terrainColumn(x, z).jsY < 40) continue;
    const int y = owUnder(14);
    S.x0 = x - 38; S.x1 = x + 38; S.z0 = z - 38; S.z1 = z + 38; S.y = y; S.hashDy = 14 - y; S.cx = x; S.cz = z; S.rot = jsFloor(rnd() * 2);
    S.name = "an Ancient City"; S.data = {{"spawners", json::array()}};
    return true;
  }
  return false;
}
static void cityBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const int cx = S.cx, cz = S.cz, y = S.y, fy = y - 1;
  static const std::vector<const char*> bricks = {"slate_bricks", "slate_bricks", "slate_tiles", "cracked_slate_bricks", "polished_slate"};
  auto brick = [&](int x, int yy, int z) { return P.pickw(bricks, x, yy, z, 4); };
  // the hall it stands in: a dome of dark air over a floor of deepslate tiles, sculk creeping in from the edges
  for (int z = cz - 38; z <= cz + 38; z++) for (int x = cx - 38; x <= cx + 38; x++) {
    if (!P.in(x, z)) continue;
    const double d = std::hypot(x - cx, z - cz) / 37;
    if (d > 1) continue;
    const int top = y + jsRound(16 - d * d * 11);
    P.put(x, fy - 1, z, "slate");
    P.put(x, fy, z, d > 0.82 || P.h(x, 0, z, 2) < d * 0.35 ? "sculk" : P.h(x, 1, z, 3) < 0.5 ? "slate_tiles" : "polished_slate");
    for (int yy = y; yy <= top; yy++) P.put(x, yy, z, "air");
    P.put(x, top + 1, z, "slate");
    if (d > 0.7 && P.h(x, 2, z, 5) < 0.25) P.put(x, y, z, "sculk_vein");
  }
  // the gate: a great arch of reinforced deepslate on a stepped dais
  auto G = [&](int a, int b) { return S.rot ? std::pair{cx + b, cz + a} : std::pair{cx + a, cz + b}; };   // a along the gate, b across it
  for (int a = -13; a <= 13; a++) for (int b = -5; b <= 5; b++) {
    auto [x, z] = G(a, b); const int step = std::max({std::abs(a) - 10, std::abs(b) - 3, 0});
    for (int yy = y; yy < y + 2 - std::min(2, step); yy++) P.put(x, yy, z, brick(x, yy, z));
  }
  const int gy = y + 2;
  for (int a = -9; a <= 9; a++) for (int b = -1; b <= 1; b++) {
    auto [x, z] = G(a, b);
    const bool pillar = std::abs(a) >= 7;
    if (pillar) for (int yy = gy; yy <= gy + 13; yy++) P.put(x, yy, z, "reinforced_deepslate");
    // the lintel, stepping down at its ends
    const int ly = gy + 11 + (std::abs(a) <= 4 ? 2 : std::abs(a) <= 6 ? 1 : 0);
    for (int yy = gy + 11; yy <= ly; yy++) P.put(x, yy, z, "reinforced_deepslate");
    if (!pillar && b == 0 && P.hw(x, gy, z, 9) < 0.4) P.put(x, gy, z, "sculk_vein");
  }
  // soul lanterns hung from the lintel, candles on the dais, chests before the gate
  for (int a : {-5, 0, 5}) { auto [x, z] = G(a, 0); P.put(x, gy + 10, z, "soul_lantern"); }
  for (auto [a, b] : {std::pair{-11, -4}, {11, -4}, {-11, 4}, {11, 4}}) { auto [x, z] = G(a, b); P.put(x, y + 1, z, "candle"); }
  { auto [x, z] = G(-3, -4); P.chest(x, gy, z, 2, "ancient_city"); }
  {
    auto [x, z] = G(3, -4); const int i = P.chest(x, gy, z, 2, "ancient_city");
    if (i >= 0) if (json* be = P.entity(i)) if (be->contains("items")) {
      json& items = (*be)["items"]; int s = 13; while (!items[s].is_null() && s < 26) s++;
      items[s] = json{{"id", "enchanted_book"}, {"n", 1}, {"en", {{"swift", 1 + jsFloor(P.hw(x, gy, z, 13) * 3)}}}};
    }
  }
  // the shriekers that guard it (these can call the Warden), and sensors all round
  for (int k = 0; k < 10; k++) {
    const double a = k / 10.0 * JTAU + 0.3, r = k % 2 ? 13 : 30;
    const int x = jsRound(cx + std::cos(a) * r), z = jsRound(cz + std::sin(a) * r);
    if (P.in(x, z)) { P.put(x, fy, z, "sculk"); P.put(x, y, z, "sculk_shrieker", 1); }
  }
  for (int k = 0; k < 14; k++) {
    const double a = k / 14.0 * JTAU + 0.9, r = 17 + (k % 3) * 5;
    const int x = jsRound(cx + std::cos(a) * r), z = jsRound(cz + std::sin(a) * r);
    if (P.in(x, z)) { P.put(x, fy, z, "sculk"); P.put(x, y, z, "sculk_sensor"); }
  }
  // the houses: a ring of six round the gate, each with a door towards it
  for (int k = 0; k < 6; k++) {
    const double a = k / 6.0 * JTAU + 0.52; const int hx = jsRound(cx + std::cos(a) * 24), hz = jsRound(cz + std::sin(a) * 24);
    const bool doorX = std::fabs(std::cos(a)) > std::fabs(std::sin(a));
    const int dsx = doorX ? -jsign(std::cos(a)) : 0, dsz = doorX ? 0 : -jsign(std::sin(a));
    P.fillf(hx - 4, y, hz - 4, hx + 4, y + 5, hz + 4, brick);
    P.fill(hx - 3, y, hz - 3, hx + 3, y + 4, hz + 3, "air");
    P.fillf(hx - 4, y + 6, hz - 4, hx + 4, y + 6, hz + 4, [&](int x, int, int z) { return (std::abs(x - hx) + std::abs(z - hz)) % 2 ? "slate_tiles" : "slate_brick_slab"; });
    P.fill(hx + dsx * 4, y, hz + dsz * 4, hx + dsx * 4, y + 2, hz + dsz * 4, "air");
    for (auto [wx, wz] : {std::pair{hx + 4 * (doorX ? 0 : 1), hz + 4 * (doorX ? 1 : 0)}, {hx - 4 * (doorX ? 0 : 1), hz - 4 * (doorX ? 1 : 0)}}) P.put(wx, y + 2, wz, "iron_bars");
    P.put(hx, y + 4, hz, "soul_lantern");
    P.chest(hx - dsx * 3 + (doorX ? 0 : 2), y, hz - dsz * 3 + (doorX ? 2 : 0), 0, "ancient_city");
    P.put(hx - dsx * 3 - (doorX ? 0 : 2), y, hz - dsz * 3 - (doorX ? 2 : 0), "candle");
    for (int t = 0; t < 4; t++) { const int x = hx - 2 + jsFloor(P.h(hx, t, hz, 21) * 5), z = hz - 2 + jsFloor(P.h(hx, t, hz, 22) * 5); if (P.get(x, y, z) == AIR) P.put(x, y, z, "sculk_vein"); }
    // a path of polished deepslate to the dais
    for (int s = 5; s < 14; s++) { const int x = jsRound(hx - std::cos(a) * s), z = jsRound(hz - std::sin(a) * s); P.put(x, fy, z, "polished_slate"); }
  }
}

// ==== The Deep Halls (STRUCTS 17) ====================================================================================
constexpr int DH_C = 10, DH_N = 3, DH_F = 3, DH_FH = 9;
static const int DH_CORNERS[4][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
struct DHTheme { std::vector<std::string> wall, floor; std::string mob; };
static const DHTheme& dhTheme(int f) {
  static const std::array<DHTheme, 3> T = [] {
    std::array<DHTheme, 3> r;
    for (int i = 0; i < 3; i++) { const json& j = SD()["DH_THEMES"][i]; r[i].wall = strList(j["wall"]); r[i].floor = strList(j["floor"]); r[i].mob = j["mob"]; }
    return r;
  }();
  return T[f % 3];
}
struct DHDoor { std::string kind; int x0, z0, x1, z1, y0, y1; std::string key; };
struct DHFloor { int f, y; std::vector<std::pair<std::array<int, 2>, std::string>> roles; std::vector<DHDoor> doors; std::array<int, 2> entry; };
struct DHExt {
  int X0, Z0, W, Y0; std::vector<DHFloor> floors;
  struct Trap { std::vector<std::array<int, 3>> plates; std::vector<std::array<int, 5>> shooters; };
  std::vector<Trap> traps; std::vector<std::pair<std::array<int, 3>, std::string>> spawners;
  struct Key { std::array<int, 3> at; std::string key; bool mural; };
  std::vector<Key> keyroom; bool puzzle = false; std::array<int, 4> combo{}; std::vector<std::array<int, 3>> levers;
  struct Ladder { int x, z, top, bottom; }; std::vector<Ladder> ladders;
  struct Shaft { int x, z, top, bottom; } shaft;
};

static bool dhPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c)) return false;
  const int C = DH_C, W = DH_N * C + 1, X0 = x - (W >> 1), Z0 = z - (W >> 1);
  Land L = sLand(t, X0, Z0, X0 + W - 1, Z0 + W - 1);
  if (L.jhi - L.jlo > 14) return false;
  const int Y0o = std::max(28, std::min(44, L.jlo - 14));
  if (L.jlo - Y0o < 10) return false;
  const int Y0 = owUnder(Y0o);
  auto e = std::make_shared<DHExt>(); e->X0 = X0; e->Z0 = Z0; e->W = W; e->Y0 = Y0;
  using Q = std::array<int, 2>;
  auto corner = [](int i) { return Q{DH_CORNERS[i][0], DH_CORNERS[i][1]}; };
  Q entry = corner(jsFloor(rnd() * 4));
  bool hasPuz = false; Q puz{0, 0};
  for (int f = 0; f < DH_F; f++) {
    DHFloor F; F.f = f; F.y = Y0 - f * DH_FH; F.entry = entry;
    auto& roles = F.roles;   // in the order the original's Map has them
    auto has = [&](const Q& q) { for (auto& r : roles) if (r.first == q) return true; return false; };
    auto setR = [&](const Q& q, const std::string& role) { for (auto& r : roles) if (r.first == q) { r.second = role; return; } roles.push_back({q, role}); };
    auto roleOf = [&](const Q& q) -> std::string { for (auto& r : roles) if (r.first == q) return r.second; return ""; };
    auto freeL = [&]() { std::vector<Q> out; for (int cz = 0; cz < 3; cz++) for (int cx = 0; cx < 3; cx++) if (!has({cx, cz})) out.push_back({cx, cz}); return out; };
    auto take = [&](const std::string& role, const std::vector<Q>* list) -> std::optional<Q> {
      const std::vector<Q> l = list ? *list : freeL();
      if (l.empty()) return std::nullopt;
      const Q q = l[jsFloor(rnd() * l.size())]; setR(q, role); return q;
    };
    int ei = 0; for (int i = 0; i < 4; i++) if (corner(i) == entry) { ei = i; break; }
    setR(entry, "entry");
    std::optional<Q> stair;
    if (f < DH_F - 1) { stair = corner((ei + (f == 0 ? 1 : 2)) % 4); setR(*stair, "stair"); }
    if (f == 0) {
      // the secret vault in a free corner, the puzzle room beside it, the keeper's room (key and mural) elsewhere
      std::vector<Q> corners; for (int i = 0; i < 4; i++) if (!has(corner(i))) corners.push_back(corner(i));
      auto vault = take("secret", &corners);
      std::vector<Q> nb;
      if (vault) for (auto [a, b] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) { Q q{(*vault)[0] + a, (*vault)[1] + b}; if (q[0] >= 0 && q[1] >= 0 && q[0] < 3 && q[1] < 3 && !has(q)) nb.push_back(q); }
      auto p = take("puzzle", &nb);
      if (p) { hasPuz = true; puz = *p; }
      take("keyroom", nullptr); take("cage", nullptr); take("trap", nullptr); take("cage", nullptr);
      for (const Q& q : freeL()) setR(q, "hall");
    } else if (f == 1) {
      setR({1, 1}, "captain");
      take("cage", nullptr); take("trap", nullptr); take("trap", nullptr); take("cage", nullptr);
      for (const Q& q : freeL()) setR(q, "hall");
    } else {
      // the third floor: the Lich King's hall across the middle row, his vault in the far row
      for (int cx = 0; cx < 3; cx++) setR({cx, 1}, "boss");
      const int farRow = entry[1] == 0 ? 2 : 0;
      setR({2 - entry[0], farRow}, "vault");
      std::vector<Q> kl; for (Q q : {Q{1, entry[1]}, Q{2 - entry[0], entry[1]}}) if (!has(q)) kl.push_back(q);
      take("keyroom", &kl);
      for (const Q& q : freeL()) setR(q, q[1] == farRow ? "cage" : "trap");
    }
    // doorways: every pair of neighbours, but gates into the stair (or the boss hall), a wall round the vaults
    for (int cz = 0; cz < 3; cz++) for (int cx = 0; cx < 3; cx++) for (auto [dx, dz] : {std::pair{1, 0}, {0, 1}}) {
      const int nx = cx + dx, nz = cz + dz; if (nx > 2 || nz > 2) continue;
      const std::string a = roleOf({cx, cz}), b = roleOf({nx, nz});
      if (a == "boss" && b == "boss") continue;   // one long hall
      std::string kind = "open";
      if (a == "secret" || b == "secret") kind = (a == "puzzle" || b == "puzzle") ? "ward" : "wall";
      else if (a == "vault" || b == "vault") kind = (a == "boss" || b == "boss") ? "ward" : "wall";
      else if (a == "stair" || b == "stair") kind = "gate";
      else if ((a == "boss") != (b == "boss")) kind = "gate";
      const int wx = dx ? X0 + nx * C : X0 + cx * C + (C >> 1) - 1, wz = dz ? Z0 + nz * C : Z0 + cz * C + (C >> 1) - 1;
      F.doors.push_back({kind, wx, wz, dx ? wx : wx + 2, dz ? wz : wz + 2, F.y + 1, F.y + 4, f == 0 ? "key_bronze" : f == 1 ? "key_silver" : "key_gold"});
    }
    e->floors.push_back(F);
    if (stair) entry = *stair;
  }
  // dart traps, cages, the captain, the levers and their mural
  json residents = json::array(), spawners = json::array(), traps = json::array(), keyroom = json::array(), ladders = json::array(), captain;
  for (const DHFloor& F : e->floors) {
    const DHTheme& th = dhTheme(F.f);
    for (const auto& [q, role] : F.roles) {
      const int qx0 = X0 + q[0] * C, qz0 = Z0 + q[1] * C, mx = qx0 + (C >> 1), mz = qz0 + (C >> 1), y = F.y;
      if (role == "cage") { spawners.push_back(mobAt(mx, y + 1, mz, th.mob.c_str())); e->spawners.push_back({{mx, y + 1, mz}, th.mob}); }
      if (role == "trap") {
        DHExt::Trap T;
        for (int i = 0; i < 4; i++) { const int px = qx0 + 2 + jsFloor(rnd() * (C - 3)); const int pz = qz0 + 2 + jsFloor(rnd() * (C - 3)); T.plates.push_back({px, y + 1, pz}); }
        for (int s : {-3, 3}) { T.shooters.push_back({mx + s, y + 2, qz0, 0, 1}); T.shooters.push_back({mx + s, y + 2, qz0 + C, 0, -1}); }
        json jp = json::array(), js = json::array();
        for (auto& p : T.plates) jp.push_back(jat(p[0], p[1], p[2]));
        for (auto& s : T.shooters) js.push_back({s[0], s[1], s[2], s[3], s[4]});
        traps.push_back({{"plates", jp}, {"shooters", js}, {"room", {qx0, y, qz0, qx0 + C, y + 6, qz0 + C}}});
        e->traps.push_back(T);
        if (rnd() < 0.5) residents.push_back(mobAt(mx, y + 1, mz, th.mob.c_str()));
      }
      if (role == "hall" && rnd() < 0.6) residents.push_back(mobAt(mx + 1, y + 1, mz - 1, th.mob.c_str()));
      if (role == "captain") captain = {{"at", jat(mx, y + 1, mz)}, {"box", {qx0 + 1, y, qz0 + 1, qx0 + C - 1, y + 6, qz0 + C - 1}}};
      if (role == "keyroom") {
        DHExt::Key k{{qx0 + C - 2, y + 1, mz}, F.f == 0 ? "key_bronze" : "key_gold", F.f == 0};
        e->keyroom.push_back(k); keyroom.push_back({{"at", jat(k.at[0], k.at[1], k.at[2])}, {"key", k.key}, {"mural", k.mural}});
      }
      if (role == "stair") { e->ladders.push_back({qx0 + 1, qz0 + 1, F.y + 1, F.y - DH_FH + 1}); ladders.push_back({{"x", qx0 + 1}, {"z", qz0 + 1}, {"top", F.y + 1}, {"bottom", F.y - DH_FH + 1}}); }
    }
  }
  // the lever puzzle: four levers along the puzzle room's far wall; the mural in the keeper's room
  json puzzle;
  auto doorJ = [](const DHDoor& d) { return json{{"kind", d.kind}, {"x0", d.x0}, {"z0", d.z0}, {"x1", d.x1}, {"z1", d.z1}, {"y0", d.y0}, {"y1", d.y1}, {"key", d.key}}; };
  if (hasPuz) {
    const int qx0 = X0 + puz[0] * C, qz0 = Z0 + puz[1] * C, y = e->floors[0].y;
    for (int i = 0; i < 4; i++) e->combo[i] = rnd() < 0.5 ? 1 : 0;
    if (!e->combo[0] && !e->combo[1] && !e->combo[2] && !e->combo[3]) e->combo[jsFloor(rnd() * 4)] = 1;
    for (int i = 0; i < 4; i++) e->levers.push_back({qx0 + 2 + i * 2, y + 1, qz0 + C - 2});
    e->puzzle = true;
    json lv = json::array(); for (auto& l : e->levers) lv.push_back(jat(l[0], l[1], l[2]));
    puzzle = {{"levers", lv}, {"combo", {e->combo[0], e->combo[1], e->combo[2], e->combo[3]}}};
    for (auto& d : e->floors[0].doors) if (d.kind == "ward") { puzzle["door"] = doorJ(d); break; }
  }
  // the long hall on the third floor, and its master
  const int yb = e->floors[2].y, bz0 = Z0 + 1 * C;
  Q v2{0, 0}; for (auto& [q, role] : e->floors[2].roles) if (role == "vault") v2 = q;
  Q e0 = e->floors[0].entry;
  e->shaft = {X0 + e0[0] * C + 1, Z0 + e0[1] * C + 1, L.hi + 2, Y0 + 1};
  json floors = json::array();
  for (auto& F : e->floors) { json ds = json::array(); for (auto& d : F.doors) ds.push_back(doorJ(d)); floors.push_back({{"f", F.f}, {"y", F.y}, {"doors", ds}}); }
  json vault2 = {{"at", jat(X0 + v2[0] * C + (C >> 1), e->floors[2].y + 1, Z0 + v2[1] * C + (C >> 1))}};
  for (auto& d : e->floors[2].doors) if (d.kind == "ward") { vault2["door"] = doorJ(d); break; }
  S.x0 = X0 - 2; S.z0 = Z0 - 2; S.x1 = X0 + W + 1; S.z1 = Z0 + W + 1; S.cx = x; S.cz = z; S.y = Y0; S.hashDy = Y0o - Y0;
  S.name = "the Deep Halls";
  S.data = {{"floors", floors}, {"traps", traps}, {"spawners", spawners}, {"residents", residents}, {"captain", captain}, {"keyroom", keyroom}, {"puzzle", puzzle}, {"ladders", ladders},
            {"boss", {{"at", jat(X0 + (W >> 1), yb + 1, bz0 + (C >> 1))}, {"type", "lich_king"}, {"box", {X0 + 1, yb, bz0 + 1, X0 + W - 2, yb + 6, bz0 + C - 1}}}},
            {"vault2", vault2}, {"shaft", {{"x", e->shaft.x}, {"z", e->shaft.z}, {"top", e->shaft.top}, {"bottom", e->shaft.bottom}}}};
  S.ext = e;
  return true;
}

static void dhBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const DHExt& E = extOf<DHExt>(S);
  const int C = DH_C, W = E.W, X0 = E.X0, Z0 = E.Z0;
  for (const DHFloor& F : E.floors) {
    const DHTheme& T = dhTheme(F.f); const int y = F.y;
    auto wall = [&](int x, int yy, int z) -> const std::string& { return P.pickw(T.wall, x, yy, z, 1); };
    auto flo = [&](int x, int z) -> const std::string& { return P.pickw(T.floor, x, y, z, 2); };
    // the shell, then the rooms carved out of it
    for (int z = Z0; z < Z0 + W; z++) for (int x = X0; x < X0 + W; x++) {
      if (!P.in(x, z)) continue;
      P.put(x, y - 1, z, wall(x, y - 1, z)); P.put(x, y, z, flo(x, z)); P.put(x, y + 7, z, wall(x, y + 7, z));
      const int gx = (x - X0) % C, gz = (z - Z0) % C; const bool onWall = gx == 0 || gz == 0;
      for (int yy = y + 1; yy <= y + 6; yy++) P.put(x, yy, z, onWall ? std::string_view(wall(x, yy, z)) : std::string_view("air"));
    }
    // the long boss hall: the walls inside it come down
    for (const auto& [q, role] : F.roles) if (role == "boss" && q[0] > 0)
      for (int z = Z0 + q[1] * C + 1; z < Z0 + q[1] * C + C; z++) for (int yy = y + 1; yy <= y + 6; yy++) P.put(X0 + q[0] * C, yy, z, "air");
    // doorways
    for (const DHDoor& d : F.doors) {
      if (d.kind == "wall") continue;
      for (int z = d.z0; z <= d.z1; z++) for (int x = d.x0; x <= d.x1; x++) for (int yy = d.y0; yy <= d.y1; yy++)
        P.put(x, yy, z, d.kind == "gate" ? "dungeon_gate" : d.kind == "ward" ? "ward_stone" : "air");
    }
    // light: lanterns hanging in every room, a few old torches
    for (const auto& [q, role] : F.roles) {
      const int mx = X0 + q[0] * C + (C >> 1), mz = Z0 + q[1] * C + (C >> 1);
      P.put(mx, y + 6, mz, F.f == 2 ? "soul_lantern" : "lantern", 1);
      if (P.hw(mx, y, mz, 9) < 0.3) P.put(mx + 3, y + 1, mz + 3, "cobweb");
    }
  }
  // cages, plates and darts
  for (const auto& [at, mob] : E.spawners) P.spawner(at[0], at[1], at[2], mob);
  for (const auto& T : E.traps) {
    for (const auto& [x, y, z] : T.plates) P.put(x, y, z, "stone_pressure_plate");
    for (const auto& [x, y, z, dx, dz] : T.shooters) P.put(x, y, z, "dispenser", dz > 0 ? 3 : dz < 0 ? 2 : dx > 0 ? 5 : 4);
  }
  // chests: the keeper's room has its key (and on the first floor, the mural), the halls a little
  for (const auto& r : E.keyroom) {
    const int i = P.chest(r.at[0], r.at[1], r.at[2], 3, "deep");
    if (i >= 0) if (json* be = P.entity(i)) if (be->contains("items")) (*be)["items"][13] = json{{"id", r.key}, {"n", 1}};
    if (r.mural && E.puzzle) for (int k = 0; k < 4; k++) P.put(r.at[0] + 1, r.at[1] + 2, r.at[2] - 3 + k * 2, E.combo[k] ? "lime_wool" : "red_wool");
    if (r.mural) P.lectern(r.at[0] - 2, r.at[1], r.at[2] - 3, 1, "deep_halls");
  }
  for (const DHFloor& F : E.floors) for (const auto& [q, role] : F.roles) {
    const int x = X0 + q[0] * C + 2, z = Z0 + q[1] * C + C - 2;
    if (role == "hall" && P.hw(x, F.y, z, 4) < 0.5) P.chest(x, F.y + 1, z, 0, "deep");
    if (role == "secret") { P.chest(x, F.y + 1, z, 0, "deep_secret"); P.chest(x + 4, F.y + 1, z, 0, "deep_secret"); }
    if (role == "vault") { P.chest(x, F.y + 1, z, 0, "deep_vault"); P.chest(x + 2, F.y + 1, z, 0, "deep_vault"); P.chest(x + 4, F.y + 1, z, 0, "deep_vault"); }
  }
  // the levers (on the floor, all off)
  if (E.puzzle) for (const auto& [x, y, z] : E.levers) P.put(x, y, z, "lever", 0);
  // the ladders down between floors, and the shaft down from the stones on the land
  for (const auto& L : E.ladders) for (int yy = L.bottom; yy <= L.top; yy++) P.put(L.x, yy, L.z, "ladder", 3);
  const auto& sh = E.shaft;
  const int top = std::max(sh.top, P.in(sh.x, sh.z) ? P.ground(sh.x, sh.z) + 1 : 0);
  for (int yy = sh.bottom; yy <= top; yy++) P.put(sh.x, yy, sh.z, "ladder", 3);
  for (int yy = sh.bottom; yy <= top + 3; yy++) for (auto [dx, dz] : {std::pair{1, 0}, {0, 1}, {1, 1}})
    if (yy > E.Y0 + 7) P.put(sh.x + dx, yy, sh.z + dz, yy <= P.ground(sh.x + dx, sh.z + dz) ? "stone_bricks" : "air");
  // a ring of old stones round the way down
  for (int a = 0; a < 8; a++) {
    const int rx = jsRound(sh.x + std::cos(a * JPI / 4) * 4), rz = jsRound(sh.z + std::sin(a * JPI / 4) * 4);
    if (!P.in(rx, rz)) continue;
    const int g0 = P.ground(rx, rz), hgt = 2 + jsFloor(P.h(rx, 0, rz, 6) * 3);
    for (int yy = g0 + 1; yy <= g0 + hgt; yy++) P.put(rx, yy, rz, yy == g0 + hgt && a % 2 == 0 ? "chiseled_stone_bricks" : "mossy_stone_bricks");
  }
}

// ==== The Twin Vault (STRUCTS 18) ====================================================================================
struct TVExt { int X0, Z0, X1, Z1, x; std::array<int, 4> ante, arena, vault; std::array<std::array<int, 3>, 2> plates; std::array<std::array<int, 6>, 2> doors; int shX, shZ, shTop, shBottom; };
static bool tvPlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  TerrainColumn c = t.terrainColumn(x, z); if (!sLandy(c)) return false;
  const int X0 = x - 12, Z0 = z - 16, X1 = x + 12, Z1 = z + 16;
  Land L = sLand(t, X0, Z0, X1, Z1);
  if (L.jhi - L.jlo > 12) return false;
  const int yo = std::max(16, std::min(36, L.jlo - 14));
  if (L.jlo - yo < 10) return false;
  const int y = owUnder(yo);
  auto e = std::make_shared<TVExt>();
  e->X0 = X0; e->Z0 = Z0; e->X1 = X1; e->Z1 = Z1; e->x = x;
  // the antechamber (south), the arena (middle), the vault (north)
  e->ante = {X0 + 2, Z1 - 8, X1 - 2, Z1 - 2}; e->arena = {X0 + 1, Z0 + 8, X1 - 1, Z1 - 10}; e->vault = {x - 4, Z0 + 1, x + 4, Z0 + 6};
  e->plates = {{{e->ante[0] + 1, y + 1, e->ante[1] + 3}, {e->ante[2] - 1, y + 1, e->ante[1] + 3}}};
  e->doors = {{{x - 1, y + 1, Z1 - 9, x + 1, y + 4, Z1 - 9}, {x - 1, y + 1, Z0 + 7, x + 1, y + 4, Z0 + 7}}};
  e->shX = x; e->shZ = Z1 - 2; e->shTop = L.hi + 2; e->shBottom = y + 1;
  S.x0 = X0 - 2; S.z0 = Z0 - 2; S.x1 = X1 + 2; S.z1 = Z1 + 2; S.cx = x; S.cz = z; S.y = y; S.hashDy = yo - y;
  S.name = "the Twin Vault";
  auto dj = [](const std::array<int, 6>& d) { return json{{"x0", d[0]}, {"y0", d[1]}, {"z0", d[2]}, {"x1", d[3]}, {"y1", d[4]}, {"z1", d[5]}}; };
  S.data = {{"plates", {jat(e->plates[0][0], e->plates[0][1], e->plates[0][2]), jat(e->plates[1][0], e->plates[1][1], e->plates[1][2])}}, {"door", dj(e->doors[0])}, {"vdoor", dj(e->doors[1])},
            {"wardens", {jat(x - 5, y + 1, Z0 + 15), jat(x + 5, y + 1, Z0 + 15)}}, {"vault", {e->vault[0], e->vault[1], e->vault[2], e->vault[3]}}, {"arena", {e->arena[0], e->arena[1], e->arena[2], e->arena[3]}},
            {"shaft", {{"x", e->shX}, {"z", e->shZ}, {"top", e->shTop}, {"bottom", e->shBottom}}}};
  S.ext = e;
  return true;
}
static void tvBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const TVExt& E = extOf<TVExt>(S); const int y = S.y;
  static const std::vector<std::string> walls = [] { std::vector<std::string> v; for (const char* k : {"deepslate_bricks", "polished_blackstone_bricks", "stone_bricks", "deepslate_tiles"}) if (hasBlock(k)) v.push_back(k); return v; }();
  auto wall = [&](int x, int yy, int z) -> const std::string& { return P.pickw(walls, x, yy, z, 1); };
  auto room = [&](const std::array<int, 4>& r, int h) {
    for (int z = r[1] - 1; z <= r[3] + 1; z++) for (int x = r[0] - 1; x <= r[2] + 1; x++) {
      if (!P.in(x, z)) continue;
      const bool edge = x < r[0] || x > r[2] || z < r[1] || z > r[3];
      P.put(x, y, z, wall(x, y, z)); P.put(x, y + h + 1, z, wall(x, y + h + 1, z));
      for (int yy = y + 1; yy <= y + h; yy++) P.put(x, yy, z, edge ? std::string_view(wall(x, yy, z)) : std::string_view("air"));
    }
  };
  room(E.ante, 5); room(E.arena, 9); room(E.vault, 5);
  // the doors: the vault door between antechamber and arena, the inner door to the vault (both ward stone)
  for (const auto& d : E.doors) for (int z = d[2]; z <= d[5]; z++) for (int x = d[0]; x <= d[3]; x++) for (int yy = d[1]; yy <= d[4]; yy++) P.put(x, yy, z, "ward_stone");
  // the plates, each in a lit alcove; braziers in the arena
  for (const auto& [x, yy, z] : E.plates) { P.put(x, yy, z, "stone_pressure_plate"); P.put(x, yy + 3, z, "soul_lantern", 1); }
  for (auto [a, b] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) { const int x = a ? E.arena[2] - 2 : E.arena[0] + 2, z = b ? E.arena[3] - 2 : E.arena[1] + 2; P.put(x, y + 1, z, "chiseled_stone_bricks"); P.put(x, y + 2, z, "campfire"); }
  for (int k = 0; k < 4; k++) P.chest(E.vault[0] + 1 + k * 2, y + 1, E.vault[1] + 1, 2, "twin_vault");
  P.lectern(E.ante[0] + 4, y + 1, E.ante[3] - 1, 0, "twin_vault");
  // the way down, and two black obelisks on the land
  for (int yy = E.shBottom; yy <= E.shTop; yy++) P.put(E.shX, yy, E.shZ, "ladder", 2);
  for (int yy = y + 6; yy <= E.shTop + 3; yy++) for (auto [dx, dz] : {std::pair{-1, 0}, {1, 0}, {0, 1}, {-1, 1}, {1, 1}})
    if (P.in(E.shX + dx, E.shZ + dz)) P.put(E.shX + dx, yy, E.shZ + dz, yy <= P.ground(E.shX + dx, E.shZ + dz) ? std::string_view(wall(E.shX, yy, E.shZ)) : std::string_view("air"));
  for (int dx : {-3, 3}) {
    const int ox = E.shX + dx, oz = E.shZ; if (!P.in(ox, oz)) continue;
    const int g0 = P.ground(ox, oz);
    for (int yy = g0 + 1; yy <= g0 + 5; yy++) P.put(ox, yy, oz, yy == g0 + 5 ? "crying_obsidian" : "obsidian");
  }
}

// ==== Pirate coves (STRUCTS 19) ======================================================================================
struct CoveExt { double sea; };
static bool covePlan(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  // the nearest stretch of beach to the spot picked
  TerrainColumn c = t.terrainColumn(x, z);
  for (int r = 4; r <= 60 && (c.biome != BIOME_BEACH || c.jsY <= JS_SEA); r += 4) for (int k = 0; k < 12; k++) {
    const double a = k / 12.0 * JTAU; const int bx = jsFloor(x + std::cos(a) * r), bz = jsFloor(z + std::sin(a) * r);
    const TerrainColumn q = t.terrainColumn(bx, bz);
    if (q.biome == BIOME_BEACH && q.jsY > JS_SEA) { x = bx; z = bz; c = q; break; }
  }
  if (c.biome != BIOME_BEACH || c.jsY <= JS_SEA) return false;
  // which way the sea is (the original's loop runs on while it's 0: a sea found due east can be overwritten)
  bool found = false; double sea = 0;
  for (int k = 0; k < 8 && !(found && sea != 0); k++) {
    const double a = k / 8.0 * JTAU;
    for (int d = 6; d <= 20; d += 2) { if (t.terrainColumn(jsFloor(x + std::cos(a) * d), jsFloor(z + std::sin(a) * d)).biome == BIOME_OCEAN) { sea = a; found = true; break; } }
  }
  if (!found) return false;
  const int cx = jsFloor(x - std::cos(sea) * 3), cz = jsFloor(z - std::sin(sea) * 3);
  Land L = sLand(t, cx - 6, cz - 6, cx + 6, cz + 6);
  if (L.jhi - L.jlo > 4 || L.jlo <= JS_SEA - 1) return false;
  const TerrainColumn cc = t.terrainColumn(cx, cz); const int y = cc.y;
  S.x0 = cx - 8; S.z0 = cz - 8; S.x1 = cx + 8; S.z1 = cz + 8; S.cx = cx; S.cz = cz; S.y = y; S.hashDy = cc.jsY - y; S.surface = true;
  S.name = "a Pirate Cove";
  json res = json::array(); for (int i = 0; i < 3; i++) res.push_back(mobAt(cx - 3 + i * 3, y + 1, cz + 3, "pirate"));
  S.data = {{"residents", res}, {"boss", {{"at", jat(cx, y + 1, cz - 2)}, {"type", "pirate_captain"}, {"box", {cx - 7, y - 1, cz - 7, cx + 7, y + 6, cz + 7}}}}};
  auto e = std::make_shared<CoveExt>(); e->sea = sea; S.ext = e;
  return true;
}
static void coveBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const double sea = extOf<CoveExt>(S).sea; const int cx = S.cx, cz = S.cz;
  auto gy = [&](int x, int z) { return P.ground(x, z); };
  // two tents (red and white canvas over a pole)
  for (auto [tx, tz, col] : {std::tuple{cx - 4, cz - 3, "red_wool"}, {cx + 3, cz - 3, "white_wool"}}) {
    const std::string ck = hasBlock(col) ? col : "wool";
    const int y0 = gy(tx, tz);
    for (int dz = -1; dz <= 1; dz++) for (int k = 0; k < 3; k++) P.put(tx - 1 + k, y0 + 1 + (k == 1 ? 2 : 1), tz + dz, ck);
    for (int dz = -1; dz <= 1; dz++) { P.put(tx - 2, y0 + 1, tz + dz, ck); P.put(tx + 2, y0 + 1, tz + dz, ck); }
    P.put(tx, y0 + 1, tz - 1, "oak_fence"); P.put(tx, y0 + 2, tz - 1, "oak_fence");
  }
  // the fire, the hoard, the barrels, the flag
  const int y = gy(cx, cz);
  P.put(cx, y + 1, cz, "campfire");
  P.chest(cx - 2, gy(cx - 2, cz + 1) + 1, cz + 1, 2, "pirate"); P.chest(cx + 2, gy(cx + 2, cz + 1) + 1, cz + 1, 2, "pirate"); P.chest(cx, gy(cx, cz - 5) + 1, cz - 5, 2, "pirate");
  for (auto [bx, bz] : {std::pair{cx + 5, cz + 1}, {cx + 5, cz + 2}, {cx - 5, cz + 2}}) P.put(bx, gy(bx, bz) + 1, bz, "barrel");
  const int fx = cx + 6, fz = cz - 5, fy = gy(fx, fz);
  for (int k = 1; k <= 6; k++) P.put(fx, fy + k, fz, "oak_fence");
  for (int k = 0; k < 3; k++) for (int j = 0; j < 2; j++) P.put(fx + 1 + k, fy + 5 + j, fz, hasBlock("black_wool") ? "black_wool" : "wool");
  // a short jetty out over the water
  for (int d = 4; d <= 14; d++) {
    const int x = jsFloor(cx + std::cos(sea) * d), z = jsFloor(cz + std::sin(sea) * d);
    if (!P.in(x, z)) continue;
    const int g0 = gy(x, z); if (g0 > SEA_LEVEL + 1) continue;
    P.put(x, SEA_LEVEL + 1, z, "planks"); if (d % 4 == 0) for (int yy = g0 + 1; yy <= SEA_LEVEL; yy++) P.put(x, yy, z, "log");
  }
}

// ---- registration, in the original's STRUCTS order ----
static void regDeep(const char* key, const char* name, int region, double chance, int margin, int jsIndex, decltype(StructureType::plan) plan,
                    decltype(StructureType::build) build, u32 flags = 0) {
  StructureType t; t.key = key; t.name = name; t.region = region; t.chance = chance; t.margin = margin; t.jsIndex = jsIndex; t.flags = flags;
  t.dims = 1u << DIM_OVERWORLD; t.plan = std::move(plan); t.build = std::move(build);
  registerStructure(std::move(t));
}
void registerStructureDeepA() {
  {
    StructureType t; t.key = "stronghold"; t.name = "a stronghold"; t.region = 1 << 20; t.chance = 0; t.margin = 0; t.jsIndex = 7;
    t.dims = 1u << DIM_OVERWORLD; t.global = strongholdGlobal; t.build = strongholdBuild;
    registerStructure(std::move(t));
  }
  regDeep("ancient_city", "an Ancient City", 512, 0.85, 70, 8, cityPlan, cityBuild, SF_NO_CARVE);
}
void registerStructureDeepB() {
  regDeep("deep_halls", "the Deep Halls", 760, 0.6, 70, 17, dhPlan, dhBuild);
  regDeep("twin_vault", "the Twin Vault", 900, 0.5, 70, 18, tvPlan, tvBuild);
  regDeep("pirate_cove", "a Pirate Cove", 600, 0.6, 40, 19, covePlan, coveBuild);
}

} // namespace dq
