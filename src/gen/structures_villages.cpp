// gen/structures_villages.cpp — villages, towns and cities: towns.js planVillage (terrain version 5: a square,
// main streets and side streets, lots along them, walls), townlife.js (washing lines, shop signs, bunting),
// villageflat.js (the land levelled under it all, blending back into the hills over 10 blocks), and the builder:
// towns.js buildVillageInto (streets, the square with its fountain or well, quest board, bell, benches, stalls,
// planters, banners, lamps, wayside things), townWall, towns3.js townGatesBuild and townlife.js townLifeBuild.
// Buildings are in structures_townbld.cpp. One structure type, "village" (the tier — village, town or city — is
// in the plan), placed one per 352-block region exactly like the original (hash32(seed, rx, rz, 4242)).
#include "gen/structures_town.h"
#include "gen/structures_api.h"
#include "common/holiday.h"
#include <map>
#include <set>

namespace dq {

static const int* DXL = DIRX;
static const int* DZL = DIRZ;
constexpr int VREG = 352, VF_R = 10;
static const char* TIER_NAMES[3] = {"village", "town", "city"};
static const char* GATE_NAMES[4] = {"North Gate", "East Gate", "South Gate", "West Gate"};

struct TownExt { Town v; json gates; };
struct GateCells { int d; std::vector<std::array<int, 3>> cells; int h0, inX, inZ, hw, cx, cz; std::string name; };

static std::array<int, 4> rectOf(const Street& S, int pad = 0) {
  const int ax = DXL[S.d], az = DZL[S.d];
  const int ex0 = S.ox + ax * S.t0 - std::abs(az) * S.hw, ex1 = S.oz + az * S.t0 - std::abs(ax) * S.hw;
  const int ey0 = S.ox + ax * S.L + std::abs(az) * S.hw, ey1 = S.oz + az * S.L + std::abs(ax) * S.hw;
  return {std::min(ex0, ey0) - pad, std::min(ex1, ey1) - pad, std::max(ex0, ey0) + pad, std::max(ex1, ey1) + pad};
}

// the gates of a walled town (towns3.js gatesOfPlan): per main street through the wall, the opening's cells
static std::vector<GateCells> gatesOf(const Town& v) {
  std::vector<GateCells> out;
  if (!v.hasWall) return out;
  const TownWall& W = v.wall;
  for (const Street& r : v.roads) {
    const int g = r.hw + 1, d = r.d; const bool along = d == 0 || d == 2;
    const int line = d == 0 ? W.z0 : d == 2 ? W.z1 : d == 1 ? W.x1 : W.x0;
    const int cx = along ? v.x : line, cz = along ? line : v.z, h0 = std::max(v.y, SEA_LEVEL);   // level ground (the wall's rects)
    GateCells G; G.d = d; G.h0 = h0; G.cx = cx; G.cz = cz; G.hw = r.hw; G.name = GATE_NAMES[d];
    for (int s = -g; s <= g; s++) { const int x = along ? v.x + s : line, z = along ? line : v.z + s; for (int k = 1; k <= 5; k++) G.cells.push_back({x, h0 + k, z}); }
    G.inX = d == 1 ? -1 : d == 3 ? 1 : 0; G.inZ = d == 0 ? 1 : d == 2 ? -1 : 0;
    out.push_back(std::move(G));
  }
  return out;
}

// ---- the plan (towns.js planVillage, townlife.js townLifePlan, villageflat.js vFlatten) ----
static bool townPlace(const StructureIndex&, const TerrainQuery& t, int rx, int rz, StructurePlan& S) {
  const i32 seed = t.seed();
  Mulberry32 rnd(hash32(seed, rx, rz, 4242));
  if (rnd() > 0.66) return false;
  const double q = rnd();
  const int tier = q < 0.12 ? TIER_CITY : q < 0.46 ? TIER_TOWN : TIER_VILLAGE;
  const json& TT = SD()["TOWN_TIERS"][TIER_NAMES[tier]];
  const int P = TT["P"], hw = TT["hw"];
  const int m = tier == TIER_CITY ? 140 : tier == TIER_TOWN ? 112 : 84;
  const int cx = rx * VREG + m + jsFloor(rnd() * (VREG - 2 * m)), cz = rz * VREG + m + jsFloor(rnd() * (VREG - 2 * m));
  const TerrainColumn c = t.terrainColumn(cx, cz);
  if (c.biome != BIOME_PLAINS && c.biome != BIOME_FOREST && c.biome != BIOME_TAIGA && c.biome != BIOME_DESERT) return false;
  if (c.jsY <= JS_SEA || c.jsY > JS_SEA + 30) return false;
  {
    int lo = c.jsY, hi = c.jsY, wet = 0;
    const std::vector<int> rings = tier == TIER_VILLAGE ? std::vector<int>{14, 28} : std::vector<int>{16, 32, 48};
    for (int a = 0; a < 12; a++) for (int d : rings) {
      const int h = t.terrainColumn(cx + jsRound(std::cos(a / 12.0 * JTAU) * d), cz + jsRound(std::sin(a / 12.0 * JTAU) * d)).jsY;
      lo = std::min(lo, h); hi = std::max(hi, h); if (h <= JS_SEA) wet++;
    }
    if (hi - lo > (tier == TIER_VILLAGE ? 11 : 14) || wet > (tier == TIER_VILLAGE ? 4 : 6)) return false;
  }
  const int style = c.biome == BIOME_DESERT ? VS_DESERT : c.biome == BIOME_TAIGA ? (c.temp < -0.38 ? VS_SNOWY : VS_TAIGA) : (c.temp > 0.28 && c.humid < 0.02 ? VS_SAVANNA : VS_PLAINS);
  const VStyle& st = vstyle(style);
  static const std::vector<std::string> SY0 = strList(SD()["VSYL"][0]), SY1 = strList(SD()["VSYL"][1]);
  std::string name = SY0[jsFloor(rnd() * SY0.size())]; name += SY1[jsFloor(rnd() * SY1.size())];
  const int y0 = c.y, y0js = c.jsY, fr = TT["fr"];
  auto e = std::make_shared<TownExt>(); Town& v = e->v;
  v.id = "t" + std::to_string(rx) + "_" + std::to_string(rz); v.name = name; v.style = style; v.tier = tier; v.P = P; v.hw = hw; v.fr = fr;
  v.x = cx; v.z = cz; v.y = y0; v.seed = hash32(seed, rx, rz, 4244);
  { const u32 r = hash32(seed, 91, 7, 3) % 10; v.wind = r < 6 ? 1 : r < 8 ? 2 : r < 9 ? 0 : 3; }   // towns3.js townWindOf
  int X0 = cx - P - 1, Z0 = cz - P - 1, X1 = cx + P + 1, Z1 = cz + P + 1;
  std::vector<std::array<int, 4>> taken{{cx - P - 1, cz - P - 1, cx + P + 1, cz + P + 1}};
  auto freeR = [&](const std::array<int, 4>& a) { for (auto& r : taken) if (a[0] <= r[2] && a[2] >= r[0] && a[1] <= r[3] && a[3] >= r[1]) return false; return true; };
  auto grow = [&](const std::array<int, 4>& a) { X0 = std::min(X0, a[0]); Z0 = std::min(Z0, a[1]); X1 = std::max(X1, a[2]); Z1 = std::max(Z1, a[3]); };
  // the main streets out of the square
  const int skip = tier == TIER_VILLAGE && rnd() < 0.25 ? jsFloor(rnd() * 4) : -1;
  const int L0 = TT["L"][0], L1 = TT["L"][1];
  for (int d = 0; d < 4; d++) {
    if (d == skip) continue;
    Street road; road.d = d; road.L = L0 + jsFloor(rnd() * (L1 - L0 + 1)); road.hw = hw; road.ox = cx; road.oz = cz; road.t0 = P + 1;
    v.roads.push_back(road); taken.push_back(rectOf(road));
  }
  // side streets off the main streets (a town's back lanes)
  const int sides = TT["sides"];
  if (sides) for (const Street& road : std::vector<Street>(v.roads)) for (int k = 0; k < sides; k++) {
    const int tb = P + 13 + k * 20; if (tb + 10 > road.L) break;
    for (int side : {1, -1}) {
      if (rnd() < 0.15) continue;
      const int d2 = (road.d + (side > 0 ? 1 : 3)) & 3, ox = cx + DXL[road.d] * tb, oz = cz + DZL[road.d] * tb;
      const int s0 = TT["sideLen"][0], s1 = TT["sideLen"][1];
      Street Sx; Sx.d = d2; Sx.L = s0 + jsFloor(rnd() * (s1 - s0 + 1)); Sx.hw = 1; Sx.ox = ox; Sx.oz = oz; Sx.t0 = hw + 1; Sx.side = true;
      v.streets.push_back(Sx); taken.push_back(rectOf(Sx));
    }
  }
  // the lots: along both sides of every street, nearest the square first
  struct Slot { int si; bool isSide; int side, t; };   // si: index into allStreets
  std::vector<const Street*> all; for (auto& s : v.roads) all.push_back(&s); for (auto& s : v.streets) all.push_back(&s);
  std::vector<Slot> slots;
  for (int i = 0; i < (int)all.size(); i++) for (int side : {1, -1}) slots.push_back({i, all[i]->side, side, all[i]->side ? all[i]->t0 + 1 : P + 2});
  struct Fit { int t, r, ox, oz; std::array<int, 4> box; };
  auto fit = [&](const Slot& slot, int W, int D, Fit& out) {
    const Street& Sx = *all[slot.si];
    const int ax = DXL[Sx.d], az = DZL[Sx.d], nx = -az * slot.side, nz = ax * slot.side, front = Sx.hw + 2;
    for (int tt = slot.t, tries = 0; tt + W - 1 <= Sx.L - 1 && tries < 40; tt += 2, tries++) {
      const double tc = tt + (W - 1) / 2.0;
      const double doorX = Sx.ox + ax * tc + nx * front, doorZ = Sx.oz + az * tc + nz * front;
      const int r = vDirOf(-nx, -nz); auto [ddx, ddz] = vRot(r, (W - 1) / 2, 0);
      const int ox = (int)(doorX - ddx), oz = (int)(doorZ - ddz);
      std::array<int, 2> cs[4];
      const int lx[4] = {0, W - 1, 0, W - 1}, lz[4] = {0, 0, D - 1, D - 1};
      for (int i = 0; i < 4; i++) { auto [a, b2] = vRot(r, lx[i], lz[i]); cs[i] = {ox + a, oz + b2}; }
      std::array<int, 4> box{cs[0][0], cs[0][1], cs[0][0], cs[0][1]};
      for (auto& p : cs) { box[0] = std::min(box[0], p[0]); box[1] = std::min(box[1], p[1]); box[2] = std::max(box[2], p[0]); box[3] = std::max(box[3], p[1]); }
      if (!freeR({box[0] - 1, box[1] - 1, box[2] + 1, box[3] + 1})) continue;
      bool bad = false;
      for (auto& p : cs) { const int h = t.terrainColumn(p[0], p[1]).jsY; if (h <= JS_SEA - 1 || std::abs(h - y0js) > 10) { bad = true; break; } }
      if (bad) continue;
      out = {tt, r, ox, oz, box};
      return true;
    }
    return false;
  };
  static const std::set<std::string> OUTSKIRTS = {"farm", "pen", "windmill", "stable"};
  const int gap0 = TT["gap"][0], gap1 = TT["gap"][1];
  auto place = [&](const std::string& type) {
    int W, D, H; if (!townTemplateSize(type, W, D, H)) return false;
    const bool outk = OUTSKIRTS.count(type) > 0;
    int bi = -1; Fit bf{}; double bs = 0;
    for (int i = 0; i < (int)slots.size(); i++) {
      Fit f; if (!fit(slots[i], W, D, f)) continue;
      const double mx = (f.box[0] + f.box[2]) / 2.0 - cx, mz = (f.box[1] + f.box[3]) / 2.0 - cz, d = std::hypot(mx, mz) + (slots[i].isSide ? 4 : 0);
      const double score = outk ? -d : d;
      if (bi < 0 || score < bs) { bi = i; bf = f; bs = score; }
    }
    if (bi < 0) return false;
    const TerrainColumn mid = t.terrainColumn((bf.box[0] + bf.box[2]) >> 1, (bf.box[1] + bf.box[3]) >> 1);
    Bld b; b.type = type; b.r = bf.r; b.ox = bf.ox; b.oz = bf.oz; b.W = W; b.D = D; b.H = H;
    b.y = y0;               // villageflat.js: every building on the square's level (its beds and seats with it)
    b.yRaw = mid.y;         // (townlife.js puts the shop signs at the lot's own height, before the levelling)
    b.x0 = bf.box[0]; b.z0 = bf.box[1]; b.x1 = bf.box[2]; b.z1 = bf.box[3];
    b.seed = hash32(seed, bf.ox, bf.oz, 4243); b.tier = tier;
    b.roofI = (int)(hash32((i32)b.seed, 3, 1, 7) % st.roofs.size());
    townBuildingPlan(b, rnd, st);
    v.blds.push_back(std::move(b)); taken.push_back(bf.box); grow({bf.box[0] - 3, bf.box[1] - 3, bf.box[2] + 3, bf.box[3] + 3});
    slots[bi].t = bf.t + W + gap0 + jsFloor(rnd() * (gap1 - gap0 + 1));
    return true;
  };
  static const std::map<int, std::vector<std::string>> WANT = {
    {TIER_VILLAGE, {"hall", "smithy", "farm", "house_m", "workshop", "house_s", "farm", "pen", "windmill", "house_s", "store", "workshop", "house_m", "stable", "farm"}},
    {TIER_TOWN, {"hall", "chapel", "smithy", "store", "library", "council", "tower", "tavern", "store", "workshop", "stable", "house_m", "house_l", "market", "house_m", "workshop", "house_s"}},
    {TIER_CITY, {"hall", "council", "chapel", "smithy", "library", "store", "store", "tavern", "tower", "sage", "market", "stable", "tower", "workshop", "house_l", "house_l", "house_m", "workshop", "store", "house_m", "library"}}};
  static const std::map<int, std::vector<std::pair<std::string, int>>> FILL = {
    {TIER_VILLAGE, {{"house_s", 35}, {"house_m", 18}, {"workshop", 15}, {"farm", 18}, {"pen", 8}, {"stable", 6}}},
    {TIER_TOWN, {{"house_m", 30}, {"house_s", 14}, {"house_l", 16}, {"workshop", 16}, {"store", 12}, {"stable", 4}, {"pen", 4}, {"farm", 4}}},
    {TIER_CITY, {{"house_l", 32}, {"house_m", 26}, {"workshop", 14}, {"store", 16}, {"house_s", 6}, {"stable", 3}, {"tavern", 3}}}};
  auto weighted = [](const std::vector<std::pair<std::string, int>>& list, double qq) {
    double tot = 0; for (auto& [k, w] : list) tot += w;
    double tt = tot * qq; for (auto& [k, w] : list) { if ((tt -= w) <= 0) return k; }
    return list[0].first;
  };
  for (const auto& k : WANT.at(tier)) place(k);
  const int cap = TT["cap"];
  for (int k = (int)v.blds.size(), tries = 0; k < cap && tries < cap * 2; tries++) { if (place(weighted(FILL.at(tier), rnd()))) k++; }
  if (v.blds.size() < 3) return false;
  // the streets end where the town does (and run on a little past its walls)
  int bx0 = cx - P, bz0 = cz - P, bx1 = cx + P, bz1 = cz + P;
  for (const Bld& b : v.blds) { bx0 = std::min(bx0, b.x0); bz0 = std::min(bz0, b.z0); bx1 = std::max(bx1, b.x1); bz1 = std::max(bz1, b.z1); }
  for (const Street& Sx : v.streets) { auto r = rectOf(Sx); bx0 = std::min(bx0, r[0]); bz0 = std::min(bz0, r[1]); bx1 = std::max(bx1, r[2]); bz1 = std::max(bz1, r[3]); }
  std::string wallKind = tier == TIER_CITY ? "stone" : tier == TIER_TOWN && rnd() < 0.6 ? "wood" : "";
  const int mg = 4;
  if (!wallKind.empty()) { v.hasWall = true; v.wall = {wallKind, bx0 - mg, bz0 - mg, bx1 + mg, bz1 + mg, wallKind == "stone" ? 6 : 5}; }
  const std::array<int, 4> edge = v.hasWall ? std::array<int, 4>{v.wall.x0, v.wall.z0, v.wall.x1, v.wall.z1} : std::array<int, 4>{bx0 - 2, bz0 - 2, bx1 + 2, bz1 + 2};
  auto cellsOf = [&](Street& Sx, int t0, int t1, int w) {
    const int ax = DXL[Sx.d], az = DZL[Sx.d];
    for (int tt = t0; tt <= t1; tt++) for (int s = -w; s <= w; s++) Sx.cells.push_back({Sx.ox + ax * tt + (-az) * s, Sx.oz + az * tt + ax * s, y0});   // level (villageflat.js)
  };
  for (Street& road : v.roads) {
    const int reach = road.d == 0 ? cz - edge[1] : road.d == 1 ? edge[2] - cx : road.d == 2 ? edge[3] - cz : cx - edge[0];
    road.L = std::max(P + 4, reach + (v.hasWall ? 10 : 4));
    cellsOf(road, P + 1, road.L, hw);
    grow(rectOf(road, 2));
  }
  for (Street& Sx : v.streets) { cellsOf(Sx, Sx.t0, Sx.L, 1); grow(rectOf(Sx, 2)); }
  if (v.hasWall) grow({v.wall.x0 - 4, v.wall.z0 - 4, v.wall.x1 + 4, v.wall.z1 + 4});
  // lamps and wayside things
  std::vector<std::array<int, 3>> doors;
  for (const Bld& b : v.blds) if (b.H > 4) doors.push_back(vWorld(b, (b.W - 1) / 2, 0, -1));
  auto inBld = [&](int x, int z) { for (const Bld& b : v.blds) if (x >= b.x0 - 1 && x <= b.x1 + 1 && z >= b.z0 - 1 && z <= b.z1 + 1) return true; return false; };
  std::vector<const Street*> sts; for (auto& s : v.roads) sts.push_back(&s); for (auto& s : v.streets) sts.push_back(&s);
  auto onStreet = [&](int x, int z) { for (auto* Sx : sts) { auto r = rectOf(*Sx); if (x >= r[0] && x <= r[2] && z >= r[1] && z <= r[3]) return true; } return false; };
  std::vector<std::array<int, 2>> lamps; std::vector<std::array<int, 3>> decor;
  for (const Street* Sx : sts) {
    const int ax = DXL[Sx->d], az = DZL[Sx->d], o = Sx->hw + 1, t0 = Sx->side ? Sx->t0 + 3 : P + 4;
    for (int tt = t0, i = 0; tt < Sx->L - 1; tt += Sx->side ? 9 : 8, i++) { const int s = i % 2 ? o : -o; lamps.push_back({Sx->ox + ax * tt + (-az) * s, Sx->oz + az * tt + ax * s}); }
    for (int tt = t0 + 2; tt < Sx->L - 1; tt += 2 + jsFloor(rnd() * 4)) if (rnd() < 0.35) {
      const int s = rnd() < 0.5 ? o : -o; const int k = jsFloor(rnd() * 9);
      decor.push_back({Sx->ox + ax * tt + (-az) * s, Sx->oz + az * tt + ax * s, k});
    }
  }
  auto nearDoor = [&](int x, int z) { for (auto& d : doors) if (std::abs(d[0] - x) + std::abs(d[2] - z) <= 2) return true; return false; };
  for (auto& l : lamps) if (!nearDoor(l[0], l[1]) && !inBld(l[0], l[1]) && !onStreet(l[0], l[1])) v.lamps.push_back(l);
  for (auto& d : decor) {
    if (nearDoor(d[0], d[1]) || inBld(d[0], d[1]) || onStreet(d[0], d[1])) continue;
    bool near = false; for (auto& l : v.lamps) if (std::abs(l[0] - d[0]) + std::abs(l[1] - d[1]) <= 1) { near = true; break; }
    if (!near) v.decor.push_back(d);
  }
  // the market square's furniture
  v.board = {cx + 4, y0 + 2, cz - (P - 1)};
  v.bell = {cx - 4, y0 + 1, cz - (P - 2)};
  std::array<int, 2> waySpot{0, 0}; bool hasWay = false;
  if (fr) {
    for (int dz : {-1, 0, 1}) { v.seats.push_back({cx - (fr + 3), y0 + 1, cz + dz, 1}); v.seats.push_back({cx + fr + 3, y0 + 1, cz + dz, 3}); }
    waySpot = {cx, cz - fr - 3}; hasWay = true;
  } else for (int dz : {-3, -4, -5}) v.seats.push_back({cx - 6, y0 + 1, cz + dz, 1});
  if (tier == TIER_VILLAGE) v.stalls = {{cx + 4, cz + 5, 0, "merchant", false}, {cx - 4, cz + 5, 0, "baker", false}};
  else {
    const int sx = P - 5, sz = P - 4;
    v.stalls = {{cx + sx, cz + sz, 0, "merchant", false}, {cx - sx, cz + sz, 0, "baker", false}, {cx + sx, cz - sz - 2, 2, "", true}, {cx - sx, cz - sz - 2, 2, "", true}};
    if (tier == TIER_CITY) { v.stalls.push_back({cx + sx - 4, cz + sz, 0, "", true}); v.stalls.push_back({cx - sx + 4, cz + sz, 0, "", true}); }
    if (tier == TIER_CITY) v.trees = {{cx + fr + 3, cz + fr + 3}, {cx - (fr + 3), cz + fr + 3}, {cx + fr + 3, cz - (fr + 3)}, {cx - (fr + 3), cz - (fr + 3)}};
    v.banners = {{cx + P - 1, cz + hw + 3}, {cx - (P - 1), cz + hw + 3}, {cx + P - 1, cz - hw - 3}, {cx - (P - 1), cz - hw - 3}};
  }
  for (const Bld& b : v.blds) for (const VSeat& s : b.seats) v.seats.push_back(s);
  // ---- townlife.js: washing lines, shop signs, bunting ----
  {
    Mulberry32 lr(hash32((i32)v.seed, 61, 3, 9));
    std::vector<std::array<int, 4>> rects; for (auto* Sx : sts) rects.push_back(rectOf(*Sx));
    auto onSt = [&](int x, int z) { for (auto& r : rects) if (x >= r[0] && x <= r[2] && z >= r[1] && z <= r[3]) return true; return false; };
    auto inB = [&](int x, int z, int mm) { for (const Bld& b : v.blds) if (x >= b.x0 - mm && x <= b.x1 + mm && z >= b.z0 - mm && z <= b.z1 + mm) return true; return false; };
    std::vector<std::array<int, 3>> adoors; for (const Bld& b : v.blds) adoors.push_back(vWorld(b, (b.W - 1) / 2, 0, -1));
    auto nDoor = [&](int x, int z) { for (auto& d : adoors) if (std::abs(d[0] - x) + std::abs(d[2] - z) <= 2) return true; return false; };
    auto lampNear = [&](int x, int z) { for (auto& l : v.lamps) if (std::abs(l[0] - x) + std::abs(l[1] - z) <= 1) return true; return false; };
    auto inWalls = [&](int x, int z) { return !v.hasWall || (x > v.wall.x0 + 2 && x < v.wall.x1 - 2 && z > v.wall.z0 + 2 && z < v.wall.z1 - 2); };
    auto plaza = [&](int x, int z) { return std::abs(x - v.x) <= v.P + 1 && std::abs(z - v.z) <= v.P + 1; };
    const int maxLines = tier == TIER_VILLAGE ? 2 : tier == TIER_TOWN ? 3 : 4;
    std::vector<std::array<int, 3>> keep;
    for (const auto& d : v.decor) {
      const int x = d[0], z = d[1];
      if ((int)v.lines.size() >= maxLines || lr() > 0.45) { keep.push_back(d); continue; }
      int si = -1;
      for (int i = 0; i < (int)rects.size(); i++) { auto& r = rects[i]; if (x >= r[0] - 2 && x <= r[2] + 2 && z >= r[1] - 2 && z <= r[3] + 2) { si = i; break; } }
      if (si < 0) { keep.push_back(d); continue; }
      const int ax = std::abs(DXL[sts[si]->d]), az = std::abs(DZL[sts[si]->d]);
      const TerrainColumn c0 = t.terrainColumn(x, z);
      bool ok = true;
      for (int j = -2; j <= 2 && ok; j++) {
        const int qx = x + ax * j, qz = z + az * j;
        if (inB(qx, qz, 0) || onSt(qx, qz) || nDoor(qx, qz) || lampNear(qx, qz) || !inWalls(qx, qz) || plaza(qx, qz) || t.terrainColumn(qx, qz).jsY != c0.jsY) ok = false;
        for (auto& kq : keep) if (kq[0] == qx && kq[1] == qz) ok = false;
      }
      if (!ok) { keep.push_back(d); continue; }
      v.lines.push_back({x, z, ax, az, c0.y, jsFloor(lr() * 4)});
    }
    v.decor = keep;
    static const std::set<std::string> SHOPS = {"smithy", "store", "tavern", "hall", "library", "sage", "stable", "market", "workshop", "chapel"};
    for (const Bld& b : v.blds) {
      if (!SHOPS.count(b.type) || b.H < 4) continue;
      // over the door of the building as built (the original used the lot's height before the levelling, so signs
      // floated over levelled lots or sank into the ground)
      auto p = vWorld(b, (b.W - 1) / 2 - 2, 3, -1);
      v.signs.push_back({p[0], p[1], p[2], (2 + b.r) & 3});
    }
    if (v.banners.size() >= 4) for (int zz : {v.z + v.hw + 3, v.z - v.hw - 3}) v.bunting.push_back({v.x - (v.P - 2), v.x + (v.P - 2), zz, v.y + 7});
    std::vector<std::pair<std::string, std::vector<const Stall*>>> pairs;
    for (const Stall& s : v.stalls) {
      const std::string k = std::to_string(s.f) + ":" + std::to_string(s.z);
      auto it = std::find_if(pairs.begin(), pairs.end(), [&](auto& p) { return p.first == k; });
      if (it == pairs.end()) pairs.push_back({k, {&s}}); else it->second.push_back(&s);
    }
    for (auto& [k, Pp] : pairs) if (Pp.size() >= 2) {
      std::stable_sort(Pp.begin(), Pp.end(), [](const Stall* a, const Stall* b) { return a->x < b->x; });
      const Stall *a = Pp.front(), *b = Pp.back();
      if (b->x - a->x >= 6) v.bunting.push_back({a->x + 2, b->x - 2, a->z + 1, v.y + 3});
    }
  }
  // ---- villageflat.js: the level ground ----
  v.flatRects.push_back({cx - P - 2, cz - P - 2, cx + P + 2, cz + P + 2});
  for (const Street* Sx : sts) {
    int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
    for (auto& c2 : Sx->cells) { x0 = std::min(x0, c2[0]); z0 = std::min(z0, c2[1]); x1 = std::max(x1, c2[0]); z1 = std::max(z1, c2[1]); }
    if (x0 <= x1) v.flatRects.push_back({x0 - 2, z0 - 2, x1 + 2, z1 + 2});
  }
  for (const Bld& b : v.blds) v.flatRects.push_back({b.x0 - 2, b.z0 - 2, b.x1 + 2, b.z1 + 2});
  if (v.hasWall) {
    const TownWall& W = v.wall; const int mm = 3;
    v.flatRects.push_back({W.x0 - mm, W.z0 - mm, W.x1 + mm, W.z0 + mm}); v.flatRects.push_back({W.x0 - mm, W.z1 - mm, W.x1 + mm, W.z1 + mm});
    v.flatRects.push_back({W.x0 - mm, W.z0 - mm, W.x0 + mm, W.z1 + mm}); v.flatRects.push_back({W.x1 - mm, W.z0 - mm, W.x1 + mm, W.z1 + mm});
  }
  {
    int fx0 = 1 << 30, fz0 = 1 << 30, fx1 = -(1 << 30), fz1 = -(1 << 30);
    for (auto& r : v.flatRects) { fx0 = std::min(fx0, r[0]); fz0 = std::min(fz0, r[1]); fx1 = std::max(fx1, r[2]); fz1 = std::max(fz1, r[3]); }
    v.fb = {fx0 - VF_R - 1, fz0 - VF_R - 1, fx1 + VF_R + 1, fz1 + VF_R + 1};
  }
  // ---- the plan, and what the villagers need from it (the original's plan fields; y band-local) ----
  S.x0 = X0; S.z0 = Z0; S.x1 = X1; S.z1 = Z1; S.cx = cx; S.cz = cz; S.y = y0; S.hashDy = y0js - y0;
  S.id = v.id; S.seed = v.seed; S.name = name; S.surface = true;
  auto spot = [](const VSpot& s, const char* f) { return json{{"at", jat(s.x, s.y, s.z)}, {f, s.f}}; };
  json blds = json::array(), folk = json::array(), seats = json::array(), stalls = json::array(), roads = json::array();
  std::vector<json> beds; std::vector<std::pair<std::string, json>> jobs;
  const Bld* hall = nullptr; for (const Bld& b : v.blds) if (b.type == "hall") { hall = &b; break; }
  if (hall) { jobs.push_back({"elder", spot(hall->elderAt, "face")}); jobs.push_back({"innkeeper", spot(hall->work, "face")}); }
  for (const Stall& s : v.stalls) if (!s.deco) jobs.push_back({s.wares, json{{"at", jat(s.x, y0 + 1, s.z + 1)}, {"face", 0}}});
  for (const Bld& b : v.blds) {
    json jb{{"type", b.type}, {"r", b.r}, {"ox", b.ox}, {"oz", b.oz}, {"W", b.W}, {"D", b.D}, {"H", b.H}, {"y", b.y}, {"x0", b.x0}, {"z0", b.z0}, {"x1", b.x1}, {"z1", b.z1},
            {"door", jat(vWorld(b, (b.W - 1) / 2, 1, 0)[0], b.y + 1, vWorld(b, (b.W - 1) / 2, 1, 0)[2])}, {"job", b.job.empty() ? json(nullptr) : json(b.job)},
            {"work", b.hasWork ? spot(b.work, "face") : json(nullptr)}, {"beds", json::array()}};
    for (const VSpot& bd : b.beds) { jb["beds"].push_back(spot(bd, "f")); beds.push_back(spot(bd, "f")); }
    if (!b.animals.empty()) { jb["animals"] = b.animals; jb["mid"] = jat(b.mid.x, b.mid.y, b.mid.z); }
    if (!b.station.empty()) jb["station"] = b.station;
    blds.push_back(jb);
    if (!b.job.empty() && b.type != "hall") jobs.push_back({b.job, b.hasWork ? spot(b.work, "face") : json(nullptr)});
  }
  const int nfolk = TT["folk"];
  for (int i = 0; i < (int)beds.size() && i < nfolk; i++) folk.push_back({{"home", beds[i]}, {"job", i < (int)jobs.size() ? jobs[i].first : "none"}, {"work", i < (int)jobs.size() ? jobs[i].second : json(nullptr)}});
  if (!hall && !folk.empty()) folk.back()["job"] = "elder";
  for (const VSeat& s : v.seats) {
    json j = json::array({s.x, s.y, s.z, s.f});
    if (s.walk) { j.push_back(json::array({s.ax, s.az})); j.push_back(s.held.empty() ? json(nullptr) : json(s.held)); }
    seats.push_back(j);
  }
  for (const Stall& s : v.stalls) { json j{{"x", s.x}, {"z", s.z}, {"f", s.f}}; if (s.deco) j["deco"] = true; else j["wares"] = s.wares; stalls.push_back(j); }
  for (const Street& r : v.roads) roads.push_back({{"d", r.d}, {"L", r.L}, {"hw", r.hw}});
  json gates = json::array();
  for (const GateCells& G : gatesOf(v)) {
    json cells = json::array(); for (auto& c2 : G.cells) cells.push_back(jat(c2[0], c2[1], c2[2]));
    const bool along = G.d == 0 || G.d == 2;
    json posts = json::array();
    for (int sd : {-1, 1}) {
      const double px = G.cx + (along ? sd * (G.hw + 1.5) : 0) + G.inX * 2.5, pz = G.cz + (along ? 0 : sd * (G.hw + 1.5)) + G.inZ * 2.5;
      posts.push_back({px + 0.5, std::max(y0, SEA_LEVEL) + 1, pz + 0.5});
    }
    const int s = G.hw + 1;
    gates.push_back({{"key", v.id + ":" + std::to_string(G.d)}, {"d", G.d}, {"stone", v.wall.kind == "stone"}, {"name", G.name}, {"cells", cells},
                     {"center", {G.cx + 0.5, G.h0 + 1, G.cz + 0.5}}, {"h0", G.h0}, {"inX", G.inX}, {"inZ", G.inZ}, {"hw", G.hw}, {"span", 2 * (G.hw + 1) + 1},
                     {"cx", G.cx}, {"cz", G.cz}, {"posts", posts}, {"lever", jat(G.cx + (along ? s : 0) + G.inX, G.h0 + 1, G.cz + (along ? 0 : s) + G.inZ)}});
  }
  S.data = {{"kind", "village"}, {"id", v.id}, {"name", name}, {"style", st.key}, {"tier", TIER_NAMES[tier]}, {"x", cx}, {"z", cz}, {"y", y0}, {"P", P}, {"hw", hw},
            {"r", std::max({X1 - cx, cx - X0, Z1 - cz, cz - Z0})}, {"board", jat(v.board[0], v.board[1], v.board[2])}, {"bell", jat(v.bell[0], v.bell[1], v.bell[2])},
            {"stalls", stalls}, {"seats", seats}, {"folk", folk}, {"guards", (int)TT["guards"]}, {"golems", 0}, {"blds", blds}, {"roads", roads}, {"gates", gates}};
  if (hasWay) S.data["waySpot"] = json::array({waySpot[0], waySpot[1]});
  if (v.hasWall) S.data["wall"] = {{"kind", v.wall.kind}, {"x0", v.wall.x0}, {"z0", v.wall.z0}, {"x1", v.wall.x1}, {"z1", v.wall.z1}, {"h", v.wall.h}};
  S.ext = e;
  return true;
}

// how strongly a column is pulled to the town's level (villageflat.js vFlatWeight)
static bool townFlatten(const StructurePlan& S, int x, int z, double& weight, int& targetY) {
  const Town& v = extOf<TownExt>(S).v;
  const auto& f = v.fb;
  if (x < f[0] || x > f[2] || z < f[1] || z > f[3]) return false;
  double d = 1e9;
  for (const auto& r : v.flatRects) {
    const int dx = std::max({r[0] - x, 0, x - r[2]}), dz = std::max({r[1] - z, 0, z - r[3]});
    if (dx == 0 && dz == 0) { weight = 1; targetY = v.y; return true; }
    d = std::min(d, std::hypot((double)dx, (double)dz));
  }
  if (d >= VF_R) return false;
  weight = 1 - jsSmooth(0, VF_R, d); targetY = v.y;
  return weight > 0;
}

// ---- building it into a chunk (towns.js buildVillageInto) ----
static std::string clothKey(const std::string& wool, const char* kind) {
  std::string c = wool.empty() || wool == "wool" ? "white" : wool;
  if (c.size() > 5 && c.compare(c.size() - 5, 5, "_wool") == 0) c.erase(c.size() - 5);
  const std::string k = c + "_" + kind;
  return hasBlock(k) ? k : std::string("white_") + kind;
}

static void townWall(SP& P, const Town& v, const VStyle& st);
static void townGates(SP& P, const Town& v, const VStyle& st);
static void townLife(SP& P, const Town& v, const VStyle& st);

static void townBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const Town& v = extOf<TownExt>(S).v; const VStyle& st = vstyle(v.style);
  const int bx = P.bx, bz = P.bz;
  auto inC = [&](int x, int z) { return P.in(x, z); };
  auto get = [&](int x, int y, int z) { return P.get(x, y, z); };
  auto put = [&](int x, int y, int z, std::string_view key, int meta = 0) { P.put(x, y, z, key, (u8)meta); };
  // October: about half the lamp posts carry a glowing jack-o'-lantern instead of a lantern
  const bool hw = halloween();
  auto lampTop = [&](int x, int z) -> std::string_view { return hw && hasBlock("jack_o_lantern") && ((x * 7 + z * 13) & 1) ? "jack_o_lantern" : "lantern"; };
  static const json& PAVE = SD()["TOWN_PAVE"];
  const std::vector<std::string> pave = v.style == VS_DESERT ? strList(PAVE["desert"]) : v.tier == TIER_VILLAGE ? st.path : strList(PAVE[TIER_NAMES[v.tier]]);
  const std::vector<std::string> lanePave = v.style == VS_DESERT ? strList(PAVE["desert"]) : v.tier == TIER_CITY ? strList(PAVE["town"]) : st.path;
  auto paveAt = [&](int x, int z, const std::vector<std::string>& list) -> const std::string& {
    if (list.empty()) return st.path[0];
    return list[jsFloor(hashFloat(x, z, (i32)v.seed, 7) * list.size())];
  };
  const int y0 = v.y, P0 = v.P;
  // the streets: following the land, bridged over water
  for (int pass = 0; pass < 2; pass++) for (const Street& road : pass ? v.streets : v.roads) {
    const auto& list = road.side ? lanePave : pave;
    for (const auto& [x, z, h] : road.cells) {
      if (!inC(x, z)) continue;
      if (h < SEA_LEVEL) { put(x, SEA_LEVEL, z, st.planks == "smooth_sandstone" ? std::string("jungle_planks") : st.planks); for (int y = SEA_LEVEL + 1; y <= SEA_LEVEL + 3; y++) put(x, y, z, "air"); continue; }
      put(x, h, z, paveAt(x, z, list));
      for (int y = h + 1; y <= h + 5; y++) if (softBlock(get(x, y, z))) put(x, y, z, "air");
      if (get(x, h - 1, z) == AIR || tLiquid(get(x, h - 1, z))) put(x, h - 1, z, "dirt");
    }
  }
  const std::string& kerb = !st.base3.empty() ? st.base3 : st.stone;
  // the square: level and paved, a stone kerb round it
  for (int dz = -P0; dz <= P0; dz++) for (int dx = -P0; dx <= P0; dx++) {
    const int x = v.x + dx, z = v.z + dz;
    if (!inC(x, z) || std::abs(dx) + std::abs(dz) > P0 * 2 - 3) continue;
    const bool rim = std::max(std::abs(dx), std::abs(dz)) == P0 && !(std::abs(dx) <= v.hw || std::abs(dz) <= v.hw);
    put(x, y0, z, rim ? kerb : paveAt(x, z, pave));
    for (int y = y0 + 1; y <= y0 + 8; y++) put(x, y, z, "air");
    for (int y = y0 - 1; y > 1 && softBlock(get(x, y, z)); y--) put(x, y, z, "dirt");
  }
  const int fr = v.fr;
  if (fr) {
    // the fountain: a broad basin, a statue on a column in the middle
    const std::string sb = PLK("stone_bricks", st.stone);
    for (int dz = -fr; dz <= fr; dz++) for (int dx = -fr; dx <= fr; dx++) {
      const int x = v.x + dx, z = v.z + dz;
      if (!inC(x, z) || std::abs(dx) + std::abs(dz) > fr * 2 - 1) continue;
      const int d = std::max(std::abs(dx), std::abs(dz)); const bool rim = d == fr || std::abs(dx) + std::abs(dz) == fr * 2 - 1, mid = d == 0;
      put(x, y0 - 2, z, sb); put(x, y0 - 1, z, rim || mid ? sb : std::string("water")); put(x, y0, z, rim || mid ? sb : std::string("water"));
      if (rim) put(x, y0 + 1, z, PLK("stone_brick_slab", st.stoneSlab));
      if (mid) {
        put(x, y0 + 1, z, PLK("chiseled_stone_bricks", sb));
        for (int y = y0 + 2; y <= y0 + 3; y++) put(x, y, z, PLK("polished_andesite", sb));
        put(x, y0 + 4, z, PLK("chiseled_quartz_block", "chiseled_stone_bricks"));
        put(x, y0 + 5, z, "lantern");
      }
      if (d == 1 && fr >= 4 && std::abs(dx) + std::abs(dz) == 2) put(x, y0, z, sb);
    }
  } else {
    // the well
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      const int x = v.x + dx, z = v.z + dz;
      if (!inC(x, z)) continue;
      const bool rim = std::abs(dx) == 2 || std::abs(dz) == 2;
      for (int y = y0 - 4; y <= y0; y++) put(x, y, z, rim ? st.stone : std::string("water"));
      put(x, y0 - 5, z, st.stone);
      if (rim) put(x, y0 + 1, z, st.stoneSlab);
      if (std::abs(dx) == 2 && std::abs(dz) == 2) { put(x, y0 + 1, z, st.stone); put(x, y0 + 2, z, st.fence); put(x, y0 + 3, z, st.fence); }
      put(x, y0 + 4, z, st.thatch[1]);
      if (dx == 0 && dz == 0) { put(x, y0 + 4, z, st.planks); put(x, y0 + 3, z, "lantern", 1); }
    }
  }
  // the quest board under its little roof, the bell, the benches
  for (int k : {3, 4}) { const int x = v.x + k, z = v.board[2]; put(x, y0 + 1, z, st.log); put(x, y0 + 2, z, "quest_board", (2 + 2) & 3); put(x, y0 + 3, z, st.slab); }
  for (int k : {2, 5}) { put(v.x + k, y0 + 1, v.board[2], st.fence); put(v.x + k, y0 + 2, v.board[2], "lantern"); }
  put(v.bell[0], y0 + 1, v.bell[2], st.log); put(v.bell[0], y0 + 2, v.bell[2], "bell");
  const std::string bench = PLK(st.fwood + "_bench", st.stairs);
  for (const VSeat& s : v.seats) if (!s.walk) put(s.x, y0 + 1, s.z, bench, (s.f + 2) & 3);
  // market stalls round the square
  for (int si = 0; si < (int)v.stalls.size(); si++) {
    const Stall& s = v.stalls[si];
    const auto& aw = si % 2 ? st.awning2 : st.awning; const int f = s.f;
    for (int k = 0; k <= 2; k++) for (int dx = -1; dx <= 1; dx++) {
      const int dz = f == 2 ? 2 - k : k, x = s.x + dx, z = s.z + dz;
      if (std::abs(dx) == 1 && (k == 0 || k == 2)) { put(x, y0 + 1, z, st.fence); put(x, y0 + 2, z, st.fence); }
      else if (k == 0) put(x, y0 + 1, z, st.slab, 1);
      else if (k == 2) put(x, y0 + 1, z, "barrel");
      put(x, y0 + 3, z, !aw.empty() ? aw[(dx + 1) % aw.size()] : st.planks);
    }
    // goods on the counter
    const int zc = s.z + (f == 2 ? 2 : 0);
    static const std::vector<std::string> BAKER = {"bread", "pumpkin_pie", "bread", "apple"}, MERCH = {"bowl", "candle", "string", "apple"};
    const std::vector<std::string>& goods = s.wares == "baker" ? BAKER : s.wares == "merchant" ? MERCH : (hash32((i32)v.seed, si, 3, 1) & 1 ? townDecor("food") : townDecor("goods"));
    if (inC(s.x, zc)) {
      const int i = colIdx(s.x - bx, y0 + 2, zc - bz);
      w.buf.blocks[i] = bid("item_pile"); w.buf.meta[i] = 0;
      json items = json::array();
      for (int k = 0; k < 4; k++) { const std::string id = goods.empty() ? "" : goods[(k + si) % goods.size()]; items.push_back(!id.empty() && hasItem(id) ? json{{"id", id}, {"n", 1}} : json(nullptr)); }
      P.setEntity(i, json{{"type", "pile"}, {"items", items}, {"rot", {0.3, -0.4, 0.8, 0.1}}});
    }
  }
  // trees in planters, banners on poles, lamps at the corners
  const std::string leaves = !st.leaves.empty() ? st.leaves : "leaves";
  for (const auto& [x, z] : v.trees) {
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (inC(x + dx, z + dz)) { put(x + dx, y0, z + dz, dx || dz ? kerb : std::string("grass")); if (dx || dz) put(x + dx, y0 + 1, z + dz, PLK("stone_brick_slab", st.stoneSlab)); }
    for (int y = y0 + 1; y <= y0 + 4; y++) put(x, y, z, st.log);
    for (int dy = 4; dy <= 6; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++)
      if (std::abs(dx) + std::abs(dz) + std::abs(dy - 5) < 4 && !(dx == 0 && dz == 0 && dy == 4) && inC(x + dx, z + dz) && get(x + dx, y0 + dy, z + dz) == AIR) put(x + dx, y0 + dy, z + dz, leaves);
  }
  for (const auto& [x, z] : v.banners) { if (!inC(x, z)) continue; for (int y = y0 + 1; y <= y0 + 6; y++) put(x, y, z, st.fence); put(x, y0 + 7, z, "lantern"); }
  for (const auto& [x, z] : v.banners) for (int dx : {1, -1}) {
    const int xx = x + dx; if (!inC(xx, z) || std::abs(xx - v.x) >= P0) continue;
    for (int y = y0 + 4; y <= y0 + 6; y++) { const int n = (int)st.banner.size(); put(xx, y, z, st.banner[(((y + S.hashDy + dx + 4) % n) + n) % n]); }
  }
  for (auto [dx, dz] : {std::pair{P0 - 2, P0 - 2}, {-(P0 - 2), P0 - 2}, {-(P0 - 2), -(P0 - 2)}, {P0 - 2, -(P0 - 2)}}) {
    const int x = v.x + dx, z = v.z + dz; if (std::abs(dx) + std::abs(dz) > P0 * 2 - 3) continue;
    put(x, y0 + 1, z, st.lampPost); put(x, y0 + 2, z, st.lampPost); put(x, y0 + 3, z, st.lampPost); put(x, y0 + 4, z, lampTop(x, z), (x + z) & 3);
  }
  for (const auto& [x, z] : v.lamps) {
    if (!inC(x, z)) continue;
    const int y = P.ground(x, z); if (y < SEA_LEVEL) continue;
    put(x, y, z, kerb); put(x, y + 1, z, st.lampPost); put(x, y + 2, z, st.lampPost); put(x, y + 3, z, st.lampPost); put(x, y + 4, z, lampTop(x, z), (x + z) & 3);
    for (int yy = y + 5; yy <= y + 6; yy++) if (softBlock(get(x, yy, z))) put(x, yy, z, "air");
  }
  static const std::vector<std::string> VFL = strList(SD()["VFLOWERS"]);
  for (const auto& [x, z, k] : v.decor) {
    if (!inC(x, z)) continue;
    const int y = P.ground(x, z); if (y < SEA_LEVEL) continue;
    for (int yy = y + 1; yy <= y + 3; yy++) if (softBlock(get(x, yy, z))) put(x, yy, z, "air");
    if (k <= 1) { put(x, y + 1, z, "hay_bale"); if (k == 0) put(x, y + 2, z, "hay_bale"); }
    else if (k <= 3) { put(x, y + 1, z, "barrel"); if (k == 2) put(x, y + 2, z, "barrel"); }
    else if (k == 4) put(x, y + 1, z, v.tier == TIER_VILLAGE ? std::string("composter") : PLK("azalea", "barrel"));
    else if (k == 5) { put(x, y + 1, z, st.fence); put(x, y + 2, z, PLK(st.fwood + "_slab", st.slab)); }
    else { put(x, y, z, v.style == VS_DESERT ? "sand" : "grass"); put(x, y + 1, z, v.style == VS_DESERT ? std::string("dead_bush") : k == 8 ? std::string("rose_bush") : VFL[(x * 7 + z) & 7]); }
  }
  // the buildings: ground cleared, a foundation, the building
  for (const Bld& b : v.blds) {
    if (b.x1 + 3 < bx || b.x0 - 3 > bx + 15 || b.z1 + 3 < bz || b.z0 - 3 > bz + 15) continue;
    const std::string found = b.type == "farm" || b.type == "pen" ? std::string("dirt") : !st.base2.empty() ? st.base2 : st.stone;
    for (int z = std::max(b.z0 - 1, bz); z <= std::min(b.z1 + 1, bz + 15); z++) for (int x = std::max(b.x0 - 1, bx); x <= std::min(b.x1 + 1, bx + 15); x++) {
      const bool inside = x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1;
      for (int y = b.y + 1; y <= std::min(DIM_H - 1, b.y + b.H + 1); y++) if (inside || softBlock(get(x, y, z))) put(x, y, z, "air");
      if (!inside) continue;
      put(x, b.y, z, found);
      for (int y = b.y - 1; y > 1 && softBlock(get(x, y, z)); y--) put(x, y, z, found);
    }
    townBuildingBuild(P, v, b);
  }
  if (v.hasWall) { townWall(P, v, st); townGates(P, v, st); }
  townLife(P, v, st);
}

// the town's wall: stone with a walkway, crenels, corner towers and gatehouses (or a palisade of logs with watchtowers)
static std::vector<std::array<int, 2>> wallTowers(const Town& v) {
  const TownWall& W = v.wall; const bool stone = W.kind == "stone";
  std::vector<std::array<int, 2>> towers{{W.x0, W.z0}, {W.x1, W.z0}, {W.x0, W.z1}, {W.x1, W.z1}};
  for (const Street& r : v.roads) {
    const int g = r.hw + (stone ? 4 : 3);
    if (r.d == 0) { towers.push_back({v.x - g, W.z0}); towers.push_back({v.x + g, W.z0}); }
    else if (r.d == 2) { towers.push_back({v.x - g, W.z1}); towers.push_back({v.x + g, W.z1}); }
    else if (r.d == 1) { towers.push_back({W.x1, v.z - g}); towers.push_back({W.x1, v.z + g}); }
    else { towers.push_back({W.x0, v.z - g}); towers.push_back({W.x0, v.z + g}); }
  }
  return towers;
}
static void townWall(SP& P, const Town& v, const VStyle& st) {
  const TownWall& W = v.wall; const bool stone = W.kind == "stone";
  const std::string sb = PLK("stone_bricks", st.stone);
  std::vector<std::string> mixes; for (const char* k : {"stone_bricks", "stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks", "stone_bricks"}) if (hasBlock(k)) mixes.push_back(k);
  const std::string log = PLK("spruce_log", st.log), fence = PLK("spruce_fence", st.fence), planks = PLK("spruce_planks", st.planks);
  auto wb = [&](int x, int y, int z) -> const std::string& { return mixes.empty() ? sb : mixes[jsFloor(hashFloat(x * 3 + y + P.S.hashDy, z, (i32)v.seed, 11) * mixes.size())]; };
  auto ground = [&](int x, int z) { return std::max(P.ground(x, z), SEA_LEVEL); };
  auto gate = [&](int x, int z) {
    for (const Street& r : v.roads) {
      const int g = r.hw + 1;
      if (r.d == 0) { if (z == W.z0 && std::abs(x - v.x) <= g) return true; }
      else if (r.d == 2) { if (z == W.z1 && std::abs(x - v.x) <= g) return true; }
      else if (r.d == 1) { if (x == W.x1 && std::abs(z - v.z) <= g) return true; }
      else if (x == W.x0 && std::abs(z - v.z) <= g) return true;
    }
    return false;
  };
  const int H = W.h;
  auto wallAt = [&](int x, int z, int ix, int iz) {
    if (!P.in(x, z)) return;
    const int h = ground(x, z);
    for (int y = h; y > h - 4 && softBlock(P.get(x, y, z)); y--) P.put(x, y, z, stone ? sb : std::string("dirt"));
    if (gate(x, z)) {
      // the gateway: an arch over the road
      if (stone) { for (int y = h + 6; y <= h + H + 1; y++) P.put(x, y, z, wb(x, y, z)); if ((x + z) % 2 == 0) P.put(x, h + H + 2, z, sb); }
      else P.put(x, h + 6, z, log, (W.x0 == x || W.x1 == x) ? 0 : 1);
      return;
    }
    if (stone) {
      for (int y = h + 1; y <= h + H; y++) P.put(x, y, z, wb(x, y, z));
      if ((x + z) % 2 == 0) P.put(x, h + H + 1, z, wb(x, h + H + 1, z));
      // the walkway behind the parapet
      const int xi = x + ix, zi = z + iz;
      if (P.in(xi, zi)) { const int hi = ground(xi, zi); for (int y = hi + 1; y <= h + H - 1; y++) P.put(xi, y, zi, wb(xi, y, zi)); }
    } else {
      for (int y = h + 1; y <= h + H; y++) P.put(x, y, z, log);
      if ((x + z) % 2 == 0) P.put(x, h + H + 1, z, fence);
    }
  };
  for (int x = W.x0; x <= W.x1; x++) { wallAt(x, W.z0, 0, 1); wallAt(x, W.z1, 0, -1); }
  for (int z = W.z0 + 1; z < W.z1; z++) { wallAt(W.x0, z, 1, 0); wallAt(W.x1, z, -1, 0); }
  // towers at the corners, and either side of every gate
  for (const auto& [tx, tz] : wallTowers(v)) {
    const int h = ground(tx, tz), R = stone ? 2 : 1, TH = H + 4;
    for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
      const int x = tx + dx, z = tz + dz; if (!P.in(x, z)) continue;
      const bool edge = std::abs(dx) == R || std::abs(dz) == R;
      for (int y = h; y > h - 4 && softBlock(P.get(x, y, z)); y--) P.put(x, y, z, stone ? sb : std::string("dirt"));
      if (stone) {
        for (int y = h + 1; y <= h + TH; y++) P.put(x, y, z, edge ? wb(x, y, z) : (y == h + H ? planks : std::string("air")));
        P.put(x, h + TH, z, sb);
        if (edge && (dx + dz) % 2 == 0) P.put(x, h + TH + 1, z, sb);
        if (edge && (dx == 0 || dz == 0) && dx != dz) P.put(x, h + H - 2, z, PLK("iron_bars", "glass_pane"));
      } else {
        const bool post = std::abs(dx) == R && std::abs(dz) == R;
        for (int y = h + 1; y <= h + TH + 2; y++) P.put(x, y, z, post ? log : y == h + TH - 1 ? planks : std::string("air"));
        if (edge && !post) P.put(x, h + TH, z, fence);
        P.put(x, h + TH + 3, z, PLK("spruce_slab", st.slab));
      }
    }
    // a way in from the town side, a ladder up to the top
    const int ix = tx < v.x ? 1 : -1, iz = tz < v.z ? 1 : -1; const bool sideX = std::abs(tx - v.x) > std::abs(tz - v.z);
    const int dx0 = sideX ? ix * R : 0, dz0 = sideX ? 0 : iz * R;
    if (P.in(tx + dx0, tz + dz0)) { P.put(tx + dx0, h + 1, tz + dz0, "air"); P.put(tx + dx0, h + 2, tz + dz0, "air"); }
    if (stone && P.in(tx, tz)) {
      const int lx = tx - (sideX ? ix : 0), lz = tz - (sideX ? 0 : iz);
      if (P.in(lx, lz)) for (int y = h + 1; y <= h + TH; y++) P.put(lx, y, lz, "ladder", sideX ? (ix > 0 ? 3 : 1) : (iz > 0 ? 0 : 2));
    }
    if (P.in(tx, tz)) P.put(tx, h + TH + (stone ? 1 : 0), tz, "lantern");
  }
}

// towns3.js townGatesBuild: the openings shut with gate barriers as they're made, the levers, the boards and signs
static void townGates(SP& P, const Town& v, const VStyle& st) {
  const std::string signKey = hasBlock("oak_sign") ? "oak_sign" : "spruce_sign";
  auto signAt = [&](int x, int y, int z, int meta, std::vector<std::string> lines) {
    if (!P.in(x, z) || y < 1 || y >= DIM_H) return;
    const int i = P.put(x, y, z, signKey, (u8)meta);
    json L = json::array(); for (int k = 0; k < 4; k++) L.push_back(k < (int)lines.size() ? lines[k].substr(0, 16) : std::string());
    P.setEntity(i, json{{"type", "sign"}, {"lines", L}});
  };
  const BlockId BAR = bid("gate_barrier");
  for (const GateCells& G : gatesOf(v)) {
    for (const auto& [x, y, z] : G.cells) if (P.in(x, z)) {
      const BlockId id = P.get(x, y, z);
      if (id == AIR || !tSolid(id) || reg().tShape[id] == ShapeKind::Gate) P.putId(x, y, z, BAR);
      if (y == G.h0 + 1) { const BlockId below = P.get(x, y - 1, z); if (below == AIR || !tSolid(below)) P.put(x, y - 1, z, !st.path.empty() ? st.path[0] : std::string("gravel")); }
    }
    const int cx = G.cx, cz = G.cz, h = G.h0; const bool along = G.d == 0 || G.d == 2;
    {
      const int s = G.hw + 1, lx = cx + (along ? s : 0) + G.inX, ly = h + 1, lz = cz + (along ? 0 : s) + G.inZ;
      if (P.in(lx, lz)) {
        const BlockId below = P.get(lx, ly - 1, lz);
        if (below == AIR || !tSolid(below)) P.put(lx, ly - 1, lz, !st.base3.empty() ? st.base3 : st.stone);
        P.put(lx, ly, lz, "lever", along ? 16 : 0); P.put(lx, ly + 1, lz, "air");
      }
    }
    // the board over the arch, outside and in
    const int ox = cx - G.inX, oz = cz - G.inZ, ix = cx + G.inX, iz = cz + G.inZ;
    auto toWall = [](int dx, int dz) { return dz < 0 ? 0 : dx > 0 ? 1 : dz > 0 ? 2 : 3; };
    if (P.in(ox, oz) && P.get(ox, h + 6, oz) == AIR) signAt(ox, h + 6, oz, 16 | toWall(G.inX, G.inZ), {"", G.name, v.name, ""});
    if (P.in(ix, iz) && P.get(ix, h + 6, iz) == AIR) signAt(ix, h + 6, iz, 16 | toWall(-G.inX, -G.inZ), {"", G.name, "", ""});
    // a sign by the road outside, and one inside, at eye height (the original names no destinations: game/roads.js isn't part of it)
    const int rot = along ? 0 : 4, side = G.hw + 1;
    struct Spot { int x, z; std::vector<std::string> lines; };
    const Spot spots[2] = {{cx + (along ? side : 0) - G.inX * 3, cz + (along ? 0 : side) - G.inZ * 3, {G.name, "of " + v.name, "Shut dusk-dawn", ""}},
                           {cx - (along ? side : 0) + G.inX * 3, cz - (along ? 0 : side) + G.inZ * 3, {G.name, "the road out", "of " + v.name}}};
    for (const Spot& sp : spots) {
      if (!P.in(sp.x, sp.z)) continue;
      const int gy = std::max(P.ground(sp.x, sp.z), SEA_LEVEL);
      int y = gy + 1;
      while (y < DIM_H - 2 && P.get(sp.x, y, sp.z) != AIR && tSolid(P.get(sp.x, y, sp.z)) && y < gy + 4) y++;
      if (P.get(sp.x, y, sp.z) != AIR || !tSolid(P.get(sp.x, y - 1, sp.z))) continue;
      signAt(sp.x, y, sp.z, rot, sp.lines);
    }
  }
}

// townlife.js townLifeBuild: banners on the square's poles, bunting, washing lines, shop signs, flags on the walls
static void townLife(SP& P, const Town& v, const VStyle& st) {
  auto air = [&](int x, int y, int z) { return P.get(x, y, z) == AIR; };
  auto cross = [&](BlockId id) { return id != NOBLOCK && id != AIR && reg().block(id).render == Render::Cross; };
  const int wind = v.wind, y0 = v.y;
  const std::vector<std::string> cols = !st.banner.empty() ? st.banner : std::vector<std::string>{"red_wool"};
  for (const auto& [x, z] : v.banners) for (int dx : {1, -1}) {
    const int xx = x + dx; if (!P.in(xx, z) || std::abs(xx - v.x) >= v.P) continue;
    const std::string key = clothKey(cols[((dx + 1) / 2) % cols.size()], "banner");
    P.put(xx, y0 + 4, z, "air"); P.put(xx, y0 + 5, z, key, 0 | 4); P.put(xx, y0 + 6, z, key, 0);
  }
  for (const Bunting& bu : v.bunting) {
    const int n = bu.x1 - bu.x0;
    for (int x = bu.x0; x <= bu.x1; x++) {
      if (!P.in(x, bu.z) || !air(x, bu.y, bu.z)) continue;
      const double u = n ? ((x - bu.x0) / (double)n) * 2 - 1 : 0; const int sag = jsRound(7 * (1 - u * u));
      P.put(x, bu.y, bu.z, "bunting", (u8)(0 | (sag << 2) | ((x & 3) << 6)));
    }
  }
  const std::string lfence = !st.fence.empty() ? st.fence : "oak_fence";
  for (const WashLine& ln : v.lines) for (int j = -2; j <= 2; j++) {
    // the ground as levelled (the plan only knows the raw terrain: lines floated over levelled streets)
    const int x = ln.x + ln.ax * j, z = ln.z + ln.az * j, y = P.ground(ln.x, ln.z);
    if (!P.in(x, z)) continue;
    if (std::abs(j) == 2) {
      const int g = std::min(y, P.ground(x, z));   // the post stands on its own ground, up to the line
      for (int k = g + 1; k <= y + 3; k++) if (air(x, k, z) || cross(P.get(x, k, z))) P.put(x, k, z, lfence);
    }
    else {
      for (int k = 1; k <= 2; k++) if (cross(P.get(x, y + k, z))) P.put(x, y + k, z, "air");
      if (air(x, y + 3, z)) P.put(x, y + 3, z, "washing_line", (u8)((ln.ax ? 0 : 1) | ((std::abs(j) == 1 ? 2 : 3) << 2) | (((ln.v + j + 4) & 3) << 6)));
    }
  }
  for (const auto& [x, y, z, rf] : v.signs) {
    if (!P.in(x, z) || !air(x, y, z)) continue;
    const int wx = x + DIRX[rf], wz = z + DIRZ[rf];
    if (!P.in(wx, wz) || !tSolid(P.get(wx, y, wz))) continue;
    if (!air(x, y - 1, z)) continue;
    P.put(x, y, z, "hanging_shop_sign", (u8)rf);
  }
  if (v.hasWall) {
    const TownWall& W = v.wall; const bool stone = W.kind == "stone"; const int H = W.h, TH = H + 4;
    auto ground = [&](int x, int z) { return std::max(P.ground(x, z), SEA_LEVEL); };
    const std::string flagKey = clothKey(cols[0], "flag"), fence = PLK("spruce_fence", !st.fence.empty() ? st.fence : "oak_fence");
    for (const auto& [tx, tz] : wallTowers(v)) {
      const int h = ground(tx, tz), top = stone ? h + TH + 1 : h + TH + 4;
      const int px = tx + (stone ? (tx < v.x ? 1 : -1) : 0), pz = tz;
      if (!P.in(px, pz)) continue;
      for (int y = top; y <= top + 2; y++) if (air(px, y, pz)) P.put(px, y, pz, fence);
      const int fx = px + DIRX[wind], fz = pz + DIRZ[wind];
      if (P.in(fx, fz) && air(fx, top + 2, fz)) P.put(fx, top + 2, fz, flagKey, (u8)wind);
    }
    for (const Street& r : v.roads) {
      const int g = r.hw + (stone ? 4 : 3), R = stone ? 2 : 1, d = r.d; const bool along = d == 0 || d == 2;
      const int line = d == 0 ? W.z0 : d == 2 ? W.z1 : d == 1 ? W.x1 : W.x0;
      const int outX = d == 1 ? 1 : d == 3 ? -1 : 0, outZ = d == 0 ? -1 : d == 2 ? 1 : 0;
      for (int sd : {-1, 1}) {
        const int tx = along ? v.x + sd * g : line, tz = along ? line : v.z + sd * g;
        const int x = tx + outX * (R + 1), z = tz + outZ * (R + 1), h = ground(tx, tz);
        if (!P.in(x, z)) continue;
        const std::string key = clothKey(cols[((sd + 1) / 2) % cols.size()], "banner"); const int rf = along ? 0 : 1;
        const int yt = h + H - 1;
        if (air(x, yt, z) && air(x, yt - 1, z)) { P.put(x, yt, z, key, (u8)rf); P.put(x, yt - 1, z, key, (u8)(rf | 4)); }
      }
    }
  }
}

// a new world starts at the edge of a village (villages.js placeAtSpawn), facing it
bool villageStartPoint(const WorldGen& wg, vec3& pos, float& yaw) {
  const StructureIndex* idx = structureIndexOf(wg.forDim(DIM_OVERWORLD));
  const int k = structureTypeIndex("village");
  if (!idx || k < 0) return false;
  const StructurePlan* best = nullptr; double bd = 1e9;
  for (int rz = -3; rz <= 2; rz++) for (int rx = -3; rx <= 2; rx++) {
    const StructurePlan* S = idx->at(k, rx, rz);
    if (!S) continue;
    const double d = std::hypot((double)S->cx, (double)S->cz);
    if (d < bd) { bd = d; best = S; }
  }
  if (!best) return false;
  const Town& v = extOf<TownExt>(*best).v;
  const Street* road = nullptr; for (const Street& r : v.roads) if (!road || r.L > road->L) road = &r;
  if (!road) return false;
  const int t = road->L + 3, x = v.x + DXL[road->d] * t, z = v.z + DZL[road->d] * t;
  const int y = idx->terrain().finalSurfaceY(x, z);
  pos = vec3(x + 0.5f, dimBase(DIM_OVERWORLD) + y + 1, z + 0.5f);
  yaw = (float)std::atan2(-(double)(v.x - x), -(double)(v.z - z));
  return true;
}

void registerStructureVillages() {
  StructureType t; t.key = "village"; t.name = "a village"; t.region = VREG; t.clearance = 24; t.dims = 1u << DIM_OVERWORLD;
  t.place = townPlace; t.build = townBuild; t.flatten = townFlatten; t.flattenPad = VF_R + 2;
  t.treeOff = [](const StructurePlan&, int, int) { return true; }; t.treePad = 4;   // villages.js: no trees within 4 of a village
  registerStructure(std::move(t));
}

} // namespace dq
