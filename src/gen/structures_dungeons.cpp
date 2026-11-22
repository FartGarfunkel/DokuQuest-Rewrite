// gen/structures_dungeons.cpp — game/dungeons.js (crypts, spider nests and hollows under a ruined shrine: rooms and
// corridors on a 4x4 grid, cages, libraries, chests — some with teeth — and the keeper's hall) and the dragon lairs
// of game/dragons.js (a nest in a ring of boulders with the egg and a chest of plunder, in every wild place of the
// Overworld, on the floor of the open sea, in a cavern of the Ember Depths, on top of the Skylands' islands).
// Both are placed per region with the original's own seeds; the dragons and keepers themselves are the creatures
// workstream's (the plan's data: boss, spawners, lair type).
#include "gen/structures_common.h"
#include "gen/structures_vault.h"
#include "gen/proc_rooms.h"
#include "core/registry.h"
#include <algorithm>

namespace dq {
using namespace vmap;

static const int* DXL = DIRX;
static const int* DZL = DIRZ;

// ==== Dungeons =======================================================================================================
constexpr int DREG = 288, HSPAN = 8;
struct DTheme { std::string key, name, pillar, stairs, slab, light; std::vector<std::string> wall, floor, mobs, bosses; bool webs = false; };
static const DTheme& dtheme(int k) {
  static const std::array<DTheme, 3> T = [] {
    std::array<DTheme, 3> r;
    const char* names[3] = {"crypt", "nest", "hollow"};
    for (int i = 0; i < 3; i++) {
      const json& j = SD()["DTHEME"][names[i]];
      r[i].key = names[i]; r[i].name = j["name"]; r[i].pillar = j["pillar"]; r[i].stairs = j["stairs"]; r[i].slab = j["slab"]; r[i].light = j["light"];
      r[i].wall = strList(j["wall"]); r[i].floor = strList(j["floor"]); r[i].mobs = strList(j["mobs"]); r[i].bosses = strList(j["bosses"]);
      r[i].webs = j.contains("webs") && j["webs"].is_boolean() && j["webs"].get<bool>();
      // bosses added since (mobs.json "dungeonBoss": the theme they keep, e.g. the Giant in the crypts: tools/port/classic_extras.py)
      if (reg().mobs.is_object()) for (auto& [k, v] : reg().mobs.items())
        if (v.is_object() && v.value("dungeonBoss", std::string()) == names[i] && std::find(r[i].bosses.begin(), r[i].bosses.end(), k) == r[i].bosses.end()) r[i].bosses.push_back(k);
    }
    return r;
  }();
  return T[k];
}
struct DRoom { std::string kind; int x, z, y, R, H; u32 seed; std::string key; int rx = 0, rz = 0, fl = 0; int yaw = 0, pattern = 0; };
struct DungeonExt {
  int theme = 0, entX = 0, entZ = 0, surf = 0, grid = 6, cell = 11, floors = 2;
  std::string wood = "oak"; float density = 0.65f;
  int pal = 0, clutter = 1;
  std::vector<DRoom> rooms;
  std::vector<std::pair<int, int>> corridors, stairs;
  std::vector<std::pair<std::array<int, 3>, std::string>> spawners;
};

static bool dungeonPlace(const StructureIndex& idx, const TerrainQuery& t, int rx, int rz, StructurePlan& S) {
  const i32 seed = t.seed();
  Mulberry32 rnd(hash32(seed, rx, rz, 5151));
  if (rnd() > 0.8) return false;
  const int grid = DUNGEON_GRID_MIN + jsFloor(rnd() * (DUNGEON_GRID_MAX - DUNGEON_GRID_MIN + 1));
  const int cell = DUNGEON_CELL_MIN + 2 * jsFloor(rnd() * ((DUNGEON_CELL_MAX - DUNGEON_CELL_MIN) / 2 + 1));
  const int floors = DUNGEON_FLOORS_MIN + jsFloor(rnd() * (DUNGEON_FLOORS_MAX - DUNGEON_FLOORS_MIN + 1));
  const int SZ = grid * cell;
  const int span = DREG - 80 - SZ; if (span < 8) return false;
  const int ox = rx * DREG + 40 + jsFloor(rnd() * span), oz = rz * DREG + 40 + jsFloor(rnd() * span);
  { const int vk = structureTypeIndex("village"); std::vector<const StructurePlan*> v; if (vk >= 0) idx.near(ox + SZ / 2, oz + SZ / 2, 40, v, vk); if (!v.empty()) return false; }
  const bool deep = rnd() < 0.3;
  const int theme = deep ? 2 : rnd() < 0.35 ? 1 : 0;
  const int s0 = jsFloor(rnd() * grid), s1 = jsFloor(rnd() * grid);
  struct Cell { int gx, gz, fl, d; };
  std::vector<Cell> cells;
  auto find = [&](int gx, int gz, int fl) {
    for (int i = 0; i < (int)cells.size(); i++) if (cells[i].gx == gx && cells[i].gz == gz && cells[i].fl == fl) return i;
    return -1;
  };
  std::vector<std::pair<int, int>> edges, stairs;
  cells.push_back({s0, s1, 0, 0});
  const int want = DUNGEON_ROOMS_MIN + jsFloor(rnd() * (DUNGEON_ROOMS_MAX - DUNGEON_ROOMS_MIN + 1));
  for (int tries = 0; (int)cells.size() < want && tries < 400; tries++) {
    const int fi = jsFloor(rnd() * cells.size()); const Cell from = cells[fi];
    const int dir = jsFloor(rnd() * 6);
    int nx = from.gx, nz = from.gz, nfl = from.fl;
    if (dir < 4) { nx += DXL[dir]; nz += DZL[dir]; }
    else if (dir == 4) nfl++;
    else nfl--;
    if (nx < 0 || nz < 0 || nx >= grid || nz >= grid || nfl < 0 || nfl >= floors || find(nx, nz, nfl) >= 0) continue;
    cells.push_back({nx, nz, nfl, from.d + 1});
    if (nfl == from.fl) edges.push_back({fi, (int)cells.size() - 1});
    else stairs.push_back({fi, (int)cells.size() - 1});
  }
  if ((int)cells.size() < DUNGEON_ROOMS_MIN) return false;
  for (int ci = 0; ci < (int)cells.size(); ci++) for (int dir = 1; dir <= 2; dir++) {
    const int n = find(cells[ci].gx + DXL[dir], cells[ci].gz + DZL[dir], cells[ci].fl);
    if (n < 0) continue;
    if (rnd() < 0.18) {
      bool has = false; for (auto& e : edges) if ((e.first == n && e.second == ci) || (e.second == n && e.first == ci)) { has = true; break; }
      if (!has) edges.push_back({ci, n});
    }
  }
  const int entX = ox + s0 * cell + cell / 2, entZ = oz + s1 * cell + cell / 2;
  const TerrainColumn sc = t.terrainColumn(entX + 2, entZ + 2);
  if (sc.jsY <= JS_SEA) return false;
  const int yo = std::max(8, std::min(sc.jsY - 18 - floors * HSPAN, (deep ? 14 : 24) + jsFloor(rnd() * 10)));
  if (sc.jsY - yo < 10 + floors * 4) return false;
  int y = owUnder(yo);
  if (const OverworldGen* ow = owOf(t); ow && ow->deepStructs()) y = std::min(sc.y - 14 - floors * HSPAN, -49 + (int)(hash32(seed, rx, rz, 5153) % 101u));
  auto e = std::make_shared<DungeonExt>();
  e->theme = theme; e->entX = entX; e->entZ = entZ; e->surf = sc.y;
  e->grid = grid; e->cell = cell; e->floors = floors;
  e->pal = pickDungeonPal(theme, rnd);
  e->clutter = rnd() < 0.18 ? 0 : rnd() < 0.55 ? 1 : rnd() < 0.82 ? 2 : 3;
  e->density = clutterDensity(e->clutter);
  e->wood = procWood(palette(e->pal).wood);
  int far = 0; for (int i = 0; i < (int)cells.size(); i++) if (cells[i].d > cells[far].d) far = i;
  std::vector<std::string> roles(cells.size());
  for (int i = 0; i < (int)cells.size(); i++) {
    if (i == far) roles[i] = "boss";
    else if (cells[i].gx == s0 && cells[i].gz == s1 && cells[i].fl == 0) roles[i] = "entry";
  }
  std::vector<char> floorCage(floors, 0);
  for (int fl = 0; fl < floors; fl++)
    for (int i = 0; i < (int)cells.size(); i++)
      if (roles[i].empty() && cells[i].fl == fl) { roles[i] = "spawner"; floorCage[fl] = 1; break; }
  int extraCages = 1 + jsFloor(rnd() * 3);
  for (int i = 0; i < (int)cells.size() && extraCages > 0; i++)
    if (roles[i].empty() && rnd() < 0.22) { roles[i] = "spawner"; extraCages--; }
  static const char* REST[] = {
    "hall", "loot", "library", "barracks", "dining", "chapel", "kitchen", "armoury",
    "workshop", "gallery", "shrine", "store", "sleeping", "refectory", "scriptorium",
    "crypt", "office", "forge", "well", "vault"
  };
  std::vector<std::string> bag;
  for (const char* k : REST) bag.emplace_back(k);
  for (int i = (int)bag.size() - 1; i > 0; i--) std::swap(bag[i], bag[jsFloor(rnd() * (i + 1))]);
  // loot and armoury first so a 12-room crypt always has both
  auto takeKind = [&](const char* want) {
    for (size_t i = 0; i < bag.size(); i++) if (bag[i] == want) { std::swap(bag[i], bag[0]); break; }
  };
  takeKind("armoury"); takeKind("loot");
  size_t bagAt = 0;
  std::vector<std::string> used;
  for (int i = 0; i < (int)cells.size(); i++) {
    const Cell& c = cells[i];
    std::string kind;
    if (roles[i] == "boss") kind = "boss";
    else if (roles[i] == "entry") kind = "entry";
    else if (roles[i] == "spawner") kind = pickProcKind(rnd, "spawner", i);
    else {
      do {
        kind = bag[bagAt % bag.size()];
        bagAt++;
      } while (bagAt < bag.size() * 2 && std::find(used.begin(), used.end(), kind) != used.end());
      used.push_back(kind);
    }
    int rxr = 3, rzr = 3, H = 4, R = 3;
    if (kind == "boss" || kind == "nave") { rxr = rzr = R = std::min(5, cell / 2 - 1); H = 6; }
    else if (kind == "entry") { rxr = rzr = R = 2; H = 3; }
    else if (kind == "library" || kind == "chapel" || kind == "dining" || kind == "refectory") {
      rxr = std::min(4, cell / 2 - 1); rzr = std::min(4, cell / 2 - 1); H = 5; R = std::min(rxr, rzr);
    } else switch (jsFloor(rnd() * 5)) {
      case 0: rxr = 2; rzr = 2; H = 3; break;
      case 1: rxr = std::min(4, cell / 2 - 1); rzr = 2; H = 4; break;
      case 2: rxr = 2; rzr = std::min(4, cell / 2 - 1); H = 4; break;
      case 3: rxr = 3; rzr = 3; H = 6; break;
      default: rxr = std::min(4, cell / 2 - 1); rzr = 3; H = 4; break;
    }
    if (kind != "boss" && kind != "nave" && kind != "entry") R = std::min(rxr, rzr);
    const int ry = y + c.fl * HSPAN;
    DRoom room{kind, ox + c.gx * cell + cell / 2, oz + c.gz * cell + cell / 2, ry, R, H,
               hash32(seed, c.gx, c.gz, c.fl * 17 + rx * 7 + rz),
               std::to_string(c.gx) + "," + std::to_string(c.gz) + "," + std::to_string(c.fl)};
    room.rx = rxr; room.rz = rzr; room.fl = c.fl;
    room.yaw = jsFloor(rnd() * PROC_YAW_N);
    room.pattern = jsFloor(rnd() * PROC_PATTERN_N);
    e->rooms.push_back(room);
  }
  e->corridors = edges; e->stairs = stairs;
  const DTheme& th = dtheme(theme);
  const DRoom* boss = nullptr; for (auto& r : e->rooms) if (r.kind == "boss") { boss = &r; break; }
  if (!boss && !e->rooms.empty()) boss = &e->rooms.back();
  const std::string bossType = th.bosses[jsFloor(rnd() * th.bosses.size())];
  const int brx = boss->rx > 0 ? boss->rx : boss->R, brz = boss->rz > 0 ? boss->rz : boss->R;
  auto onDais = [&](int x, int y, int z) {
    return x >= boss->x - brx && x <= boss->x + brx && z >= boss->z - brz && z <= boss->z + brz
        && y >= boss->y && y <= boss->y + boss->H - 1;
  };
  json spawners = json::array();
  auto addCage = [&](int x, int y, int z) {
    if (th.mobs.empty() || onDais(x, y, z)) return;
    const std::string mob = th.mobs[jsFloor(rnd() * th.mobs.size())];
    spawners.push_back(mobAt(x, y, z, mob.c_str()));
    e->spawners.push_back({{x, y, z}, mob});
  };
  auto cageKind = [](const std::string& k) { return k == "spawner" || k == "cells" || k == "prison"; };
  for (auto& r : e->rooms) if (cageKind(r.kind)) addCage(r.x, r.y, r.z);
  int patrols = 2 + jsFloor(rnd() * 3);
  for (auto& r : e->rooms) {
    if (patrols <= 0) break;
    if (r.kind != "hall") continue;
    addCage(r.x + 1, r.y, r.z - 1);
    patrols--;
  }
  for (auto [ai, bi] : e->corridors) {
    if (patrols <= 0) break;
    const DRoom &a = e->rooms[ai], &b = e->rooms[bi];
    addCage((a.x + b.x) / 2, a.y, (a.z + b.z) / 2);
    patrols--;
  }
  S.id = "d" + std::to_string(rx) + "_" + std::to_string(rz); S.seed = hash32(seed, rx, rz, 5152);
  S.x0 = ox - 3; S.z0 = oz - 3; S.x1 = ox + SZ + 3; S.z1 = oz + SZ + 3; S.cx = ox + SZ / 2; S.cz = oz + SZ / 2; S.y = y; S.hashDy = yo - y;
  S.name = th.name;
  S.data = {{"theme", th.key}, {"ent", json::array({entX, entZ})}, {"surf", sc.y}, {"floors", floors}, {"grid", grid},
            {"boss", {{"at", jat(boss->x, boss->y, boss->z)}, {"type", bossType},
                      {"box", {boss->x - brx, boss->y, boss->z - brz, boss->x + brx, boss->y + boss->H - 1, boss->z + brz}}}},
            {"spawners", spawners}};
  S.ext = e;
  return true;
}

static void dungeonBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const DungeonExt& d = extOf<DungeonExt>(S); const DTheme& th = dtheme(d.theme);
  auto pick = [&](const std::vector<std::string>& list, int x, int y, int z, int salt) -> const std::string& { return list[jsFloor(hashFloat(x * 3 + salt, y + S.hashDy, z, (i32)S.seed) * list.size())]; };
  auto wall = [&](int x, int y, int z) -> const std::string& {
    const float h = hashFloat(x * 3 + 1, y + S.hashDy, z, (i32)S.seed);
    if (th.key == "crypt") {
      static const std::string chi = "chiseled_stone_bricks", crk = "cracked_stone_bricks", mos = "mossy_stone_bricks";
      if (h < 0.10f && hasBlock("chiseled_stone_bricks")) return chi;
      if (h < 0.22f && hasBlock("cracked_stone_bricks")) return crk;
      if (h < 0.32f && hasBlock("mossy_stone_bricks")) return mos;
    } else if (th.key == "hollow") {
      static const std::string chi = "chiseled_slate_bricks", crk = "cracked_slate_bricks";
      if (h < 0.10f && hasBlock("chiseled_slate_bricks")) return chi;
      if (h < 0.22f && hasBlock("cracked_slate_bricks")) return crk;
    } else if (th.key == "nest") {
      static const std::string mos = "mossy_stone_bricks", cob = "mossy_cobblestone";
      if (h < 0.12f && hasBlock("mossy_stone_bricks")) return mos;
      if (h < 0.22f) return cob;
    }
    return pick(th.wall, x, y, z, 1);
  };
  auto flo = [&](int x, int y, int z) -> const std::string& { return pick(th.floor, x, y, z, 2); };
  std::vector<std::array<int, 6>> spaces;
  for (const auto& r : d.rooms) {
    const int rxr = r.rx > 0 ? r.rx : r.R, rzr = r.rz > 0 ? r.rz : r.R;
    spaces.push_back({r.x - rxr, r.y, r.z - rzr, r.x + rxr, r.y + r.H - 1, r.z + rzr});
  }
  for (auto [ai, bi] : d.corridors) {
    const DRoom &a = d.rooms[ai], &b = d.rooms[bi];
    const int x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), z0 = std::min(a.z, b.z), z1 = std::max(a.z, b.z);
    if (a.x == b.x) spaces.push_back({a.x - 1, a.y, z0, a.x + 1, a.y + 2, z1}); else spaces.push_back({x0, a.y, a.z - 1, x1, a.y + 2, a.z + 1});
  }
  auto stairCol = [](const DRoom& a, const DRoom& b, int& x, int& z) {
    const int rx = std::min(a.rx > 0 ? a.rx : a.R, b.rx > 0 ? b.rx : b.R);
    const int rz = std::min(a.rz > 0 ? a.rz : a.R, b.rz > 0 ? b.rz : b.R);
    x = a.x - rx;                     // west interior, against the wall
    z = a.z - (rz >= 2 ? 2 : 0);      // north of the 3-wide east-west door
  };
  for (auto [ai, bi] : d.stairs) {
    const DRoom &a = d.rooms[ai], &b = d.rooms[bi];
    int x, z; stairCol(a, b, x, z);
    const int y0 = std::min(a.y, b.y), y1 = std::max(a.y + 2, b.y + 2);
    spaces.push_back({x, y0, z, x, y1, z});   // one column, not a 3x3 hole
  }
  const int ex = d.entX + 2, ez = d.entZ + 2;
  spaces.push_back({ex, S.y, ez, ex, d.surf + 1, ez});
  for (const auto& [x0, y0, z0, x1, y1, z1] : spaces)
    for (int z = z0 - 1; z <= z1 + 1; z++) for (int x = x0 - 1; x <= x1 + 1; x++) {
      if (!P.in(x, z)) continue;
      for (int y = y0 - 1; y <= y1 + 1; y++) { if (y > d.surf - 2 && x == ex && z == ez) continue; P.put(x, y, z, wall(x, y, z)); }
    }
  for (const auto& [x0, y0, z0, x1, y1, z1] : spaces)
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
      if (!P.in(x, z)) continue;
      for (int y = y0; y <= y1; y++) P.put(x, y, z, "air");
      if (!(x == ex && z == ez)) P.put(x, y0 - 1, z, flo(x, y0 - 1, z));
    }
  for (int y = S.y; y <= d.surf; y++) P.put(ex, y, ez, "ladder", 2);
  for (auto [ai, bi] : d.stairs) {
    const DRoom &a = d.rooms[ai], &b = d.rooms[bi];
    int x, z; stairCol(a, b, x, z);
    const int y0 = std::min(a.y, b.y), y1 = std::max(a.y + a.H - 1, b.y + b.H - 1);
    for (int y = y0; y <= y1; y++) P.put(x, y, z, "ladder", 3);   // against the west wall
  }
  for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
    const int x = ex + dx, z = ez + dz;
    if (!P.in(x, z)) continue;
    const int h = P.ground(x, z), m = std::max(std::abs(dx), std::abs(dz)); const bool edge = m == 3, mid = m <= 1;
    if (dx == 0 && dz == 0) { for (int y = h + 1; y <= h + 3; y++) P.put(x, y, z, "air"); continue; }
    if (mid) {
      P.put(x, h, z, wall(x, h, z)); for (int y = h + 1; y <= h + 3; y++) P.put(x, y, z, "air");
      if (dx && dz) { P.put(x, h + 1, z, th.pillar); if (hashFloat(x, z, (i32)S.seed, 9) < 0.6) P.put(x, h + 2, z, th.pillar); }
      continue;
    }
    P.put(x, h, z, flo(x, h, z));
    if (edge) {
      const int tall = jsFloor(hashFloat(x, z, (i32)S.seed, 8) * 4);
      for (int y = h + 1; y <= h + tall; y++) P.put(x, y, z, wall(x, y, z));
      for (int y = h + tall + 1; y <= h + 4; y++) { const BlockId id = P.get(x, y, z); if (id != AIR && id != NOBLOCK && !tSolid(id)) P.put(x, y, z, "air"); }
    } else for (int y = h + 1; y <= h + 3; y++) {
      const BlockId id = P.get(x, y, z);
      if (id != AIR && id != NOBLOCK && (!tSolid(id) || blockKey(id).find("leaves") != std::string::npos || blockKey(id).find("log") != std::string::npos)) P.put(x, y, z, "air");
    }
  }
  { const int h = P.ground(ex, ez); P.put(ex + 1, h + 1, ez - 1, th.light == "soul_torch" ? "soul_lantern" : "lantern"); }
  const std::string hang = th.light == "soul_torch" ? "soul_lantern" : "lantern";
  for (const auto& [at, mob] : d.spawners) P.spawner(at[0], at[1], at[2], mob);
  for (const DRoom& r : d.rooms) {
    Mulberry32 rr(r.seed);
    auto chest = [&](int x, int y, int z, const char* kind, bool mimic) {
      const int dx = r.x - x, dz = r.z - z;
      const int f = std::abs(dx) >= std::abs(dz) ? (dx >= 0 ? 1 : 3) : (dz >= 0 ? 2 : 0);
      const int i = P.put(x, y, z, "chest", (u8)((2 - f) & 3));
      if (i >= 0) { json be{{"type", "chest"}, {"items", dungeonLoot(kind, rr)}, {"lk", std::string("d:") + kind}}; if (mimic) be["mimic"] = 1; P.setEntity(i, be); }
    };
    ProcRoom pr;
    pr.kind = r.kind; pr.x = r.x; pr.y = r.y; pr.z = r.z;
    pr.rx = r.rx > 0 ? r.rx : r.R; pr.rz = r.rz > 0 ? r.rz : r.R; pr.H = r.H;
    pr.seed = r.seed; pr.wood = d.wood; pr.light = th.light; pr.hangLight = hang;
    pr.pillar = th.pillar; pr.slab = th.slab; pr.stairs = th.stairs; pr.webs = th.webs; pr.density = d.density;
    applyProcLayout(pr, ProcLayout{r.yaw, r.pattern, d.pal, d.clutter});
    pr.light = th.light; pr.hangLight = hang;   // theme lamps, palette furniture
    pr.webs = th.webs || pr.webs;
    furnishProcRoom(P, pr, chest);
  }
}

// ==== Dragon lairs ===================================================================================================
constexpr int LAIR_REG = 480;
struct LairExt { std::string type; int x, z; };

static bool lairPlace(const StructureIndex& idx, const TerrainQuery& t, int rx, int rz, StructurePlan& S) {
  const i32 seed = t.seed(); const int jd = jsFromDim(t.dim());
  Mulberry32 rnd(lairSeed(seed, rx, rz, 7171 + jd));
  if (rnd() > (jd == JS_OVERWORLD ? 0.8 : 0.6)) return false;
  const int x = rx * LAIR_REG + 80 + jsFloor(rnd() * (LAIR_REG - 160)), z = rz * LAIR_REG + 80 + jsFloor(rnd() * (LAIR_REG - 160));
  std::string type; int y = 0, yo = 0;
  if (jd == JS_OVERWORLD) {
    const TerrainColumn c = t.terrainColumn(x, z);
    std::vector<const StructurePlan*> v;
    const int vk = structureTypeIndex("village"), dk = structureTypeIndex("dungeon");
    if (vk >= 0) idx.near(x, z, 60, v, vk); if (!v.empty()) return false;
    if (dk >= 0) idx.near(x, z, 30, v, dk); if (!v.empty()) return false;
    // the Tide Dragon sleeps on the floor of the open sea
    if (c.biome == BIOME_OCEAN && c.jsY <= JS_SEA - 9) {
      if (rnd() > 0.3) return false;   // the open sea is wide; its dragons are few
      double sum = c.y, jsum = c.jsY; int n = 1;
      for (int k = 0; k < 8; k++) {
        const double a = k / 8.0 * JTAU;
        const TerrainColumn q = t.terrainColumn(jsRound(x + std::cos(a) * 9), jsRound(z + std::sin(a) * 9));
        sum += std::min(SEA_LEVEL - 4, q.y); jsum += std::min(JS_SEA - 4, q.jsY); n++;
      }
      type = "tide"; y = std::min(SEA_LEVEL - 8, jsRound(sum / n)); yo = std::min(JS_SEA - 8, jsRound(jsum / n));
    } else {
      if (c.jsY <= JS_SEA + 1) return false;
      if ((c.biome == BIOME_FOREST || c.biome == BIOME_PLAINS) && c.humid > 0.38 && c.jsY <= JS_SEA + 5) type = "bog";
      else if (c.biome == BIOME_FOREST || (c.biome == BIOME_PLAINS && c.humid > 0)) type = "verdant";
      else if (c.biome == BIOME_DESERT) type = "sand";
      else if ((c.biome == BIOME_MOUNTAINS || c.biome == BIOME_TAIGA) && c.temp < -0.15) type = "frost";
      if (type.empty()) return false;
      // the nest sits at the lie of the land around it, not in a pit or on a post
      double sum = c.y, jsum = c.jsY; int n = 1;
      for (int k = 0; k < 8; k++) {
        const double a = k / 8.0 * JTAU;
        const TerrainColumn q = t.terrainColumn(jsRound(x + std::cos(a) * 9), jsRound(z + std::sin(a) * 9));
        sum += q.y; jsum += q.jsY; n++;
      }
      y = std::max(SEA_LEVEL + 2, jsRound(sum / n)); yo = std::max(JS_SEA + 2, jsRound(jsum / n));
    }
  } else if (jd == JS_DEPTHS) { type = "ember"; yo = 44; y = netherY(44); }
  else if (jd == JS_SKYLANDS) {
    // on top of an island big enough to hold it
    const SkylandsGen* sk = skyOf(t); if (!sk) return false;
    if (sk->islandMask(x, z) < 0.2) return false;
    type = "storm"; y = sk->hillTop(x, z); yo = y - SKY_DY;
  } else return false;
  auto e = std::make_shared<LairExt>(); e->type = type; e->x = x; e->z = z;
  S.id = "L" + std::to_string(jd) + "_" + std::to_string(rx) + "_" + std::to_string(rz); S.seed = lairSeed(seed, rx, rz, 7172);
  S.x0 = x - 14; S.z0 = z - 14; S.x1 = x + 14; S.z1 = z + 14; S.cx = x; S.cz = z; S.y = y; S.hashDy = yo - y;
  S.name = "a dragon's lair";
  S.data = {{"type", type}, {"egg", jat(x, y + 2, z)}, {"at", jat(x, y, z)}};
  S.ext = e;
  return true;
}

static void lairBuild(ChunkWriter& w, const StructurePlan& S) {
  SP P(w); const LairExt& L = extOf<LairExt>(S);
  const int dim = P.w.terrain.dim(), Ly = S.y;
  const bool deep = dim == DIM_NETHER, sky = dim == DIM_SKYLANDS, sea = L.type == "tide";
  auto hf = [&](int x, int z, int s) { return hashFloat(x, z, (i32)S.seed, s); };
  auto above = [&](int y) { return sea && y <= SEA_LEVEL ? "water" : "air"; };   // what fills the space over the nest
  using V = std::vector<const char*>;
  const V rock = sea ? V{"prismarine", "dark_prismarine", "prismarine_bricks", "prismarine"} : deep ? V{"blackstone", "basalt", "blackstone", "magma_block"} : sky ? V{"skystone", "skystone", "cobblestone"}
               : L.type == "sand" ? V{"sandstone", "cut_sandstone", "sandstone"} : L.type == "frost" ? V{"packed_ice", "stone", "snow_block"} : V{"mossy_cobblestone", "cobblestone", "stone"};
  const V floor = sea ? V{"sand", "gravel", "sand", "prismarine"} : deep ? V{"blackstone", "magma_block", "basalt"} : sky ? V{"sky_grass", "coarse_dirt", "gravel"} : L.type == "sand" ? V{"sand", "coarse_dirt", "gravel"}
                : L.type == "frost" ? V{"snow_block", "gravel", "coarse_dirt"} : L.type == "bog" ? V{"mud", "coarse_dirt", "gravel"} : V{"coarse_dirt", "gravel", "podzol"};
  auto pick = [&](const V& l, int x, int z, int s) { return l[jsFloor(hf(x, z, s) * l.size())]; };
  for (int dz = -14; dz <= 14; dz++) for (int dx = -14; dx <= 14; dx++) {
    const int x = L.x + dx, z = L.z + dz;
    if (!P.in(x, z)) continue;
    const double d = std::hypot(dx, dz);
    if (d > 13.5) continue;
    if (deep) {
      // a great round cavern carved in the rock, lava glowing in cracks of the floor
      for (int y = Ly - 3; y <= Ly + 16; y++) { const double ry = (y - Ly - 5) / 11.0; if (dx * dx / 169.0 + dz * dz / 169.0 + ry * ry < 1) P.put(x, y, z, "air"); }
      if (d < 12.5) { P.put(x, Ly, z, pick(floor, x, z, 1)); for (int y = Ly - 1; y > Ly - 4; y--) P.put(x, y, z, "blackstone"); if (hf(x, z, 9) < 0.04 && d > 4) P.put(x, Ly, z, "lava"); }
    } else {
      const int h = sky ? Ly : P.ground(x, z);
      if (d < 10.5) {
        // the floor of the nest: level, hard-packed, bones and plunder in it
        P.put(x, Ly, z, pick(floor, x, z, 1));
        for (int y = Ly - 1; y > Ly - 6 && y > 1; y--) P.put(x, y, z, sky ? "skystone" : pick(rock, x, z, y + S.hashDy));
        for (int y = Ly + 1; y <= Ly + 16; y++) P.put(x, y, z, above(y));
      } else {
        // a ring of great boulders
        const int top = std::max(h, Ly) + jsFloor(hf(x, z, 3) * 5) + (hf(x, z, 4) < 0.5 ? 1 : 0);
        for (int y = std::min(h, Ly) - 2; y <= top; y++) P.put(x, y, z, pick(rock, x, z, y + S.hashDy + 7));
        for (int y = top + 1; y <= Ly + 16; y++) P.put(x, y, z, above(y));
      }
    }
    if (d < 10.5) {
      const double q = hf(x, z, 5);
      if (q < 0.05) P.put(x, Ly + 1, z, "bone_block");
      else if (q < 0.056 && d > 3) P.put(x, Ly + 1, z, "gravel");
      else if (q < 0.075 && d > 3) P.put(x, Ly + 1, z, sea ? "sea_lantern" : deep ? "magma_block" : L.type == "frost" ? "packed_ice" : "gravel");
      else if (sea && q < 0.2 && d > 2) {
        static const char* COR[5] = {"tube", "brain", "bubble", "fire", "horn"};
        const std::string cor = COR[jsFloor(hf(x, z, 6) * 5)];
        P.put(x, Ly + 1, z, hf(x, z, 7) < 0.4 ? cor + "_coral_block" : q < 0.15 ? cor + "_coral" : std::string("seagrass"), 0);
      }
    }
  }
  // the nest itself, the egg on it, and a chest of plunder
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) P.put(L.x + dx, Ly + 1, L.z + dz, sea ? "dark_prismarine" : deep ? "magma_block" : L.type == "frost" ? "snow_block" : "hay_bale");
  P.put(L.x, Ly + 2, L.z, "dragon_egg_" + L.type);
  const int ci = P.put(L.x + 4, Ly + 1, L.z - 3, "chest", 2);
  if (ci >= 0) { Mulberry32 r(S.seed); P.setEntity(ci, json{{"type", "chest"}, {"items", dungeonLoot("rare", r)}, {"lk", "d:rare"}}); }
}

void registerStructureDungeons() {
  {
    StructureType t; t.key = "dungeon"; t.name = "a dungeon"; t.region = DREG; t.clearance = 8; t.dims = 1u << DIM_OVERWORLD;
    t.place = dungeonPlace; t.build = dungeonBuild; t.treePad = 0; t.flags |= SF_DEEP;
    // no tree growing out of the shrine
    t.treeOff = [](const StructurePlan& S, int x, int z) { const auto& d = extOf<DungeonExt>(S); return std::abs(x - d.entX - 2) < 7 && std::abs(z - d.entZ - 2) < 7; };
    registerStructure(std::move(t));
  }
  {
    StructureType t; t.key = "dragon_lair"; t.name = "a dragon's lair"; t.region = LAIR_REG; t.clearance = 6;
    t.dims = (1u << DIM_OVERWORLD) | (1u << DIM_NETHER) | (1u << DIM_SKYLANDS);
    t.place = lairPlace; t.build = lairBuild; t.treePad = 2;
    t.treeOff = [](const StructurePlan&, int, int) { return true; };
    registerStructure(std::move(t));
  }
  registerStructureVaults();   // after the lairs, so those two indices stay
}

} // namespace dq
