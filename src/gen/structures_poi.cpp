// gen/structures_poi.cpp — points of interest from Soulrend's structure mods (Dungeons Arise, Moog's Voyager Structures,
// The Graveyard, Integrated Dungeons and Structures; tools/port/poi_import.py -> data/pois/index.json, git-ignored). One
// region in a few gets one, chosen by the land there (its lands, as the mod's biome tags say) and how rare the mod made
// it; put together Minecraft's jigsaw way in 3D (rigid pieces, joints up and down too, a piece inside its parent's box
// only when it fits there), from the pieces turned into our blocks as the village pieces are (gen/village_pieces.h).
#include "gen/structures.h"
#include "gen/structures_common.h"
#include "gen/village_pieces.h"
#include "gen/overworld.h"
#include "common/fs.h"
#include "core/registry.h"
#include "sim/creature_rig.h"
#include <map>
#include <functional>
#include <set>

namespace dq {

namespace {
struct PoiPool { std::vector<std::pair<const VPPiece*, int>> els; };
struct PoiDef { std::string key, start; int depth = 4, y = 0, spacing = 32, reach = 80; bool surface = true, beard = false; int flatR = 6; char ground = 0; std::vector<std::string> lands; };   // ground: 's' sand, 'g' grass, 'n' snow in its own pieces (0: none to speak of)
struct PoiPack {
  std::map<std::string, VPPiece> pieces;   // by location "ns:path"
  std::map<std::string, PoiPool> pools;
  std::vector<PoiDef> defs;
};

const PoiPack* poiPack() {
  static const std::unique_ptr<PoiPack> P = []() -> std::unique_ptr<PoiPack> {
    auto idx = readJson(dataDir() / "pois" / "index.json");
    if (!idx || !idx->is_object()) return nullptr;
    auto pk = std::make_unique<PoiPack>();
    int n = 0;
    std::map<std::string, const json*> rulesOf;   // a piece's swaps: from the first pool that names it with some
    for (auto& [name, els] : (*idx)["pools"].items()) for (auto& e : els) if (e.size() > 3 && !rulesOf.count(e[0].get<std::string>())) rulesOf[e[0].get<std::string>()] = &e[3];
    for (auto& r : (*idx)["pieces"]) {
      if (!r.is_string()) continue;
      const std::string rel = r.get<std::string>();   // "ns/path.nbt"
      auto b = readBinary(dataDir() / "pois" / rel);
      if (!b) continue;
      const size_t sl = rel.find('/');
      const std::string ns = rel.substr(0, sl), path = rel.substr(sl + 1, rel.size() - sl - 5);
      VPPiece P;
      auto ri = rulesOf.find(ns + ":" + path);
      if (!makePiece(ns, "poi", path, *b, P, ri == rulesOf.end() ? nullptr : ri->second)) continue;
      pk->pieces[ns + ":" + path] = std::move(P); n++;
    }
    if (idx->contains("placeAir")) for (auto& a : (*idx)["placeAir"]) if (a.is_string()) if (auto it = pk->pieces.find(a.get<std::string>()); it != pk->pieces.end()) it->second.placeAir = true;
    for (auto& [name, els] : (*idx)["pools"].items()) {
      PoiPool pl;
      for (auto& e : els) {
        auto it = pk->pieces.find(e[0].get<std::string>());
        if (it == pk->pieces.end()) continue;
        if (e.size() > 2 && e[2] == "terrain_matching") it->second.terrain = true;
        pl.els.push_back({&it->second, std::max(1, e[1].get<int>())});
      }
      if (!pl.els.empty()) pk->pools[name] = std::move(pl);
    }
    for (auto& s : (*idx)["structures"]) {
      PoiDef d; d.key = s.value("key", ""); d.start = s.value("start", ""); d.depth = s.value("depth", 4); d.y = s.value("y", 0);
      d.spacing = std::max(8, s.value("spacing", 32)); d.surface = s.value("surface", true); d.reach = s.value("reach", 80);
      for (auto& l : s["lands"]) d.lands.push_back(l.get<std::string>());
      // Minecraft's terrain adaptation: beard_thin / beard_box ease the land up to and down to the pieces' floors (no
      // pillars of dirt under them, no hillside through them); bury and none leave the land as it is
      { const std::string a = s.value("adapt", std::string("none")); d.beard = a == "beard_thin" || a == "beard_box" || a.rfind("beard:", 0) == 0;
        // Integrated API's (IDAS) blend reaches as far as its kernel says; Minecraft's beard_box a little further than its thin one
        if (a.rfind("beard:", 0) == 0) d.flatR = std::clamp(std::atoi(a.c_str() + 6) / 2, 6, 16); else if (a == "beard_box") d.flatR = 8; }
      // no ships and blimps floating in the sky (Dungeons Arise's heavenly ones): they don't belong in our world
      if (!d.surface && d.y > 90) continue;
      if (d.key.find("heavenly") != std::string::npos || d.key.find("blimp") != std::string::npos) continue;
      if (pk->pools.count(d.start)) pk->defs.push_back(std::move(d));
    }
    // each structure's own ground: what its pieces lay round themselves (sand, grass or snow), so it only stands where the land
    // is that ground (a castle courtyard of sand in a meadow, a lawn in the desert)
    {
      const Registry& R = reg();
      auto groundOf = [&](BlockId id) -> char {
        const std::string& k = R.block(id).key;
        if (k == "sand" || k == "red_sand") return 's';
        if (k == "grass" || k == "podzol" || k == "mycelium" || k == "coarse_dirt" || k == "rooted_dirt") return 'g';
        if (k == "snow_block" || k == "snow" || k == "snowy_grass" || k == "powder_snow") return 'n';
        return 0;
      };
      for (PoiDef& d : pk->defs) {
        std::map<char, int> n; std::set<std::string> seen; std::vector<std::string> todo{d.start};
        while (!todo.empty() && seen.size() < 60) {
          const std::string pn = todo.back(); todo.pop_back();
          if (!seen.insert(pn).second) continue;
          auto pit = pk->pools.find(pn); if (pit == pk->pools.end()) continue;
          for (auto& [pc, w] : pit->second.els) {
            for (const VPBlock& b : pc->blocks) if (char g = groundOf(pc->states[b.s].id)) n[g]++;
            for (const VPJoint& j : pc->joints) if (!j.pool.empty()) todo.push_back(j.pool);
          }
        }
        char best = 0; int bn = 0, tot = 0;
        for (auto& [g, c] : n) { tot += c; if (c > bn) { bn = c; best = g; } }
        if (bn >= 24 && bn * 3 >= tot * 2) d.ground = best;
        // its own lands say first when they all agree: a desert palace with a garden lawn is still the desert's
        auto all = [&](std::initializer_list<const char*> L) { for (auto& l : d.lands) if (std::find_if(L.begin(), L.end(), [&](const char* x) { return l == x; }) == L.end()) return false; return !d.lands.empty(); };
        if (all({"desert", "badlands", "beach"})) d.ground = 's';
        else if (all({"snowy"})) d.ground = 'n';
      }
    }
    DQ_LOG("points of interest: %zu structures, %d pieces, %zu pools; left out: %s", pk->defs.size(), n, pk->pools.size(), piecesMissing().c_str());
    return pk->defs.empty() ? nullptr : std::move(pk);
  }();
  return P.get();
}

// our lands at a column (the mods' biome tags were turned into these: tools/port/poi_import.py LAND)
std::vector<std::string> landsAt(const TerrainQuery& t, const TerrainColumn& c) {
  OwColumn oc; oc.h = c.jsY; oc.biome = (u8)std::max(0, c.biome); oc.temp = c.temp; oc.humid = c.humid; oc.mc = c.mc;
  const BtaVariant bv = btaVariant(oc);
  std::vector<std::string> L;
  if (c.special == 1) { L.push_back("mushroom"); return L; }   // (a mushroom island: its own)
  if (c.biome == BIOME_BEACH) L.push_back("beach");
  if (c.biome == BIOME_DESERT) L.push_back("desert");
  if (bv == BV_REDLANDS) L.push_back("badlands");
  if (bv == BV_MOSSMIRE) L.push_back("swamp");
  if (bv == BV_GLACIER || bv == BV_TUNDRA || (c.biome == BIOME_TAIGA && c.temp < -0.38)) L.push_back("snowy");
  if (c.biome == BIOME_MOUNTAINS || bv == BV_HIGHLANDS) L.push_back("mountains");
  if (c.biome == BIOME_TAIGA) L.push_back("taiga");
  if (c.biome == BIOME_FOREST) L.push_back(c.temp > 0.28 && c.humid > 0.12 ? "jungle" : "forest");
  if (c.biome == BIOME_PLAINS) L.push_back(c.temp > 0.28 && c.humid > 0.12 ? "jungle" : c.temp > 0.28 && c.humid < 0.02 ? "savanna" : "plains");
  (void)t;
  return L;
}

struct Placed { const VPPiece* p; int r, x0, y0, z0, w, d; int depth; };
struct PoiExt {
  std::vector<Placed> pcs; std::string key;
  std::vector<std::array<int, 6>> flat; int flatR = 6; bool beard = false, blend = false; char ground = 0;   // x0, z0, x1, z1, floor y, its skirt   // x0, z0, x1, z1, ground y: the land under each rigid piece that stands on it (beard)
};

const int DXJ[4] = {0, 1, 0, -1}, DZJ[4] = {-1, 0, 1, 0};   // north, east, south, west (VPJoint.front)

bool expand(const PoiPack& pk, const PoiDef& d, const TerrainQuery& t, int cx, int cz, Mulberry32& rnd, std::vector<Placed>& out) {
  auto weighted = [&](const PoiPool& pl) {
    std::vector<std::pair<double, const VPPiece*>> o;
    for (auto& [p, w] : pl.els) o.push_back({std::pow(rnd(), 1.0 / w), p});
    std::sort(o.begin(), o.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<const VPPiece*> r; for (auto& e : o) r.push_back(e.second); return r;
  };
  auto box = [](const Placed& q, int& x1, int& y1, int& z1) { x1 = q.x0 + q.w - 1; y1 = q.y0 + q.p->sy - 1; z1 = q.z0 + q.d - 1; };
  auto overlaps = [&](int x0, int y0, int z0, int x1, int y1, int z1, int skip) {
    for (int i = 0; i < (int)out.size(); i++) {
      if (i == skip) continue;
      int a1, b1, c1; box(out[i], a1, b1, c1);
      if (x0 <= a1 && x1 >= out[i].x0 && y0 <= b1 && y1 >= out[i].y0 && z0 <= c1 && z1 >= out[i].z0) return true;
    }
    return false;
  };
  const auto& start = pk.pools.at(d.start);
  const VPPiece* s0 = weighted(start)[0];
  Placed s{}; s.p = s0; s.r = (int)(rnd() * 4) & 3; s.w = s.r & 1 ? s0->sz : s0->sx; s.d = s.r & 1 ? s0->sx : s0->sz;
  s.x0 = cx - s.w / 2; s.z0 = cz - s.d / 2;
  // the start's height: on the ground (Minecraft's project_start_to_heightmap, plus its offset), else Minecraft's y in ours
  // (its sea at 63, ours at 64)
  if (d.surface) {
    int hs[9]; for (int k = 0; k < 9; k++) hs[k] = t.surfaceY(s.x0 + s.w * (k % 3) / 2, s.z0 + s.d * (k / 3) / 2);
    std::nth_element(hs, hs + 4, hs + 9);
    // with a beard: the median (the land is eased to the floor anyway). Without one: Minecraft's own rule, the ground at the
    // start (the lowest of the samples sank every building on a slope into the hill)
    s.y0 = (d.beard ? hs[4] : t.surfaceY(s.x0 + s.w / 2, s.z0 + s.d / 2)) + 1 + d.y;
  } else s.y0 = std::clamp(d.y + 1, 6, DIM_H - s0->sy - 2);
  if (d.surface && s.y0 - d.y - 1 < SEA_LEVEL) return false;   // (not where the land is under the sea; a start set into the ground on purpose is fine)
  out.push_back(s);
  for (size_t qi = 0; qi < out.size() && out.size() < 160; qi++) {
    const Placed Q = out[qi];
    if (Q.depth >= d.depth) {
      static const bool lj = std::getenv("DQ_POI_JOINTS") != nullptr;
      if (lj) for (const VPJoint& A : Q.p->joints) if (!A.pool.empty() && A.pool != "minecraft:empty" && pk.pools.count(A.pool)) DQ_LOG("poi joint %s: %s in %s cut off by depth %d", d.key.c_str(), A.pool.c_str(), Q.p->name.c_str(), d.depth);
      continue;
    }
    for (const VPJoint& A : Q.p->joints) {
      if (A.pool.empty() || A.pool == "minecraft:empty") continue;
      auto pit = pk.pools.find(A.pool);
      if (pit == pk.pools.end()) continue;
      auto [tx, tz] = vpTurn(*Q.p, Q.r, A.x, A.z);
      const int ax = Q.x0 + tx, ay = Q.y0 + A.y, az = Q.z0 + tz;
      const bool up = A.front == 4, down = A.front == 5;
      const int af = up || down ? -1 : (A.front + Q.r) & 3;
      const int Tx = ax + (af >= 0 ? DXJ[af] : 0), Ty = ay + (up ? 1 : down ? -1 : 0), Tz = az + (af >= 0 ? DZJ[af] : 0);
      int qx1, qy1, qz1; box(Q, qx1, qy1, qz1);
      const bool inParent = Tx >= Q.x0 && Tx <= qx1 && Ty >= Q.y0 && Ty <= qy1 && Tz >= Q.z0 && Tz <= qz1;
      bool placed = false;
      int rj[6] = {0, 0, 0, 0, 0, 0};   // why not: joint, reach, height, out of its parent, overlap, depth
      for (const VPPiece* C : weighted(pit->second)) {
        int rs[4] = {0, 1, 2, 3}; for (int i = 3; i > 0; i--) std::swap(rs[i], rs[(int)(rnd() * (i + 1)) % (i + 1)]);
        for (int r : rs) {
          for (const VPJoint& B : C->joints) {
            if (B.name != A.target) { rj[0]++; continue; }
            if (up ? B.front != 5 : down ? B.front != 4 : (B.front > 3 || ((B.front + r) & 3) != ((af + 2) & 3))) { rj[0]++; continue; }
            // an aligned up/down joint: the child turned so its top faces the way its parent's does (else the upper storey sat
            // across the one below, hanging out over the air)
            if ((up || down) && A.aligned && A.top >= 0 && B.top >= 0 && ((B.top + r) & 3) != ((A.top + Q.r) & 3)) { rj[0]++; continue; }
            auto [bx, bz] = vpTurn(*C, r, B.x, B.z);
            Placed q{}; q.p = C; q.r = r; q.w = r & 1 ? C->sz : C->sx; q.d = r & 1 ? C->sx : C->sz; q.depth = Q.depth + 1;
            q.x0 = Tx - bx; q.z0 = Tz - bz; q.y0 = Ty - B.y;
            if (C->terrain) q.y0 = t.surfaceY(Tx, Tz) + 1 - B.y;
            int x1, y1, z1; box(q, x1, y1, z1);
            if (std::max({std::abs(q.x0 - cx), std::abs(x1 - cx), std::abs(q.z0 - cz), std::abs(z1 - cz)}) > d.reach) { rj[1]++; continue; }
            if (q.y0 < 2 || y1 >= DIM_H - 1) { rj[2]++; continue; }
            if (inParent && (q.x0 < Q.x0 || x1 > qx1 || q.y0 < Q.y0 || y1 > qy1 || q.z0 < Q.z0 || z1 > qz1)) { rj[3]++; continue; }
            if (inParent ? overlaps(q.x0, q.y0, q.z0, x1, y1, z1, (int)qi) : overlaps(q.x0, q.y0, q.z0, x1, y1, z1, -1)) { rj[4]++; continue; }
            out.push_back(q); placed = true;
            if (std::getenv("DQ_POI_PIECES")) DQ_LOG("poi piece %s %s (%dx%dx%d) at %d,%d,%d from %s joint y%d %s", d.key.c_str(), C->name.c_str(), q.w, C->sy, q.d, q.x0, q.y0, q.z0,
                                                  Q.p->name.c_str(), A.y, up ? "up" : down ? "down" : "side");
            break;
          }
          if (placed) break;
        }
        if (placed) break;
      }
      static const bool lj = std::getenv("DQ_POI_JOINTS") != nullptr;
      if (lj && !placed) DQ_LOG("poi joint %s: %s in %s at y%d failed (joint %d reach %d height %d lot %d overlap %d)", d.key.c_str(), A.pool.c_str(),
                                 Q.p->name.c_str(), Ty, rj[0], rj[1], rj[2], rj[3], rj[4]);
    }
  }
  return true;
}

void buildPoi(ChunkWriter& w, const StructurePlan& S) {
  auto E = std::static_pointer_cast<const PoiExt>(S.ext);
  if (!E) return;
  const BlockId DIRT = reg().blockId("dirt");
  SP P(w);
  // the land's own ground in each column of this chunk, as it lay before anything was built: what the structure's own grass,
  // sand or snow becomes (a lawn of grass in the snow, a sand yard in a meadow: the builder's ground was for their own land)
  const Registry& RG = reg();
  static const std::vector<u8> isGround = [] {
    std::vector<u8> v(reg().blocks.size() + 1, 0);
    for (const char* k : {"grass", "snowy_grass", "sand", "red_sand"}) if (BlockId id = reg().blockId(k)) v[id] = 2;   // (the builder's plain ground: swapped)
    for (const char* k : {"podzol", "mycelium", "coarse_dirt", "mud", "moss_block", "snow_block", "silver_sand",
                          "desert_sand", "dirt_with_dry_grass", "dirt_with_coniferous_litter", "dirt_with_rainforest_litter", "scorched_dirt", "rooted_dirt", "packed_ice"})
      if (BlockId id = reg().blockId(k)) if (!v[id]) v[id] = 1;   // (the land's: what it may become)
    return v;
  }();
  static const BlockId SNOW = reg().blockId("snow_layer"), DIRTB = reg().blockId("dirt");
  BlockId landTop[256]; bool landSnow[256];
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    const int x = w.bx + lx, z = w.bz + lz, gy = w.ground(x, z);
    BlockId b = gy > 0 ? w.get(x, gy, z) : AIR;
    landTop[lz * 16 + lx] = b < isGround.size() && isGround[b] ? b : 0;
    landSnow[lz * 16 + lx] = SNOW && gy > 0 && gy + 1 < DIM_H && w.get(x, gy + 1, z) == SNOW;
  }
  auto groundAt = [&](int x, int z, BlockId own) -> BlockId {
    if (own >= isGround.size() || isGround[own] != 2) return own;   // (their moss, coarse dirt and snow walls are their design)
    const BlockId t = landTop[(z - w.bz) * 16 + (x - w.bx)];
    if (!t) return own;
    // on its own kind of land (a desert palace in the desert, its garden lawn and all) it stays as it was made
    const std::string& tk = RG.block(t).key;
    const char land = tk.find("sand") != std::string::npos ? 's' : (tk.find("snow") != std::string::npos || tk == "packed_ice") ? 'n' : 'g';
    if (E->ground && land == E->ground) return own;
    return t;
  };
  for (const Placed& q : E->pcs) {
    const VPPiece& p = *q.p;
    if (q.x0 > w.bx + 15 || q.x0 + q.w - 1 < w.bx || q.z0 > w.bz + 15 || q.z0 + q.d - 1 < w.bz) continue;
    // its air first (Minecraft's modern pieces: the land, grass and trees inside the building go). Not a path's: it lies on the land
    if (p.placeAir && !p.terrain && !p.air.empty())
      for (int ly = 0; ly < p.sy; ly++) for (int lz = 0; lz < p.sz; lz++) for (int lx = 0; lx < p.sx; lx++) {
        if (!p.airAt(lx, ly, lz)) continue;
        auto [tx, tz] = vpTurn(p, q.r, lx, lz);
        const int x = q.x0 + tx, z = q.z0 + tz, y = q.y0 + ly;
        if (!w.in(x, z) || y < 1 || y >= DIM_H) continue;
        if (w.get(x, y, z) != AIR) w.put(x, y, z, AIR);
      }
    static const bool audit = std::getenv("DQ_POI_AUDIT") != nullptr;
    std::vector<u8> filled;
    if (audit && !p.terrain) filled.assign((size_t)p.sx * p.sy * p.sz, 0);
    for (const VPBlock& k : p.blocks) {
      if (!filled.empty()) filled[((size_t)k.y * p.sz + k.z) * p.sx + k.x] = 1;
      auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
      const int x = q.x0 + tx, z = q.z0 + tz;
      // a terrain-matching piece (a path) follows the land column by column, its layer 0 in place of the ground's top
      // (Minecraft's gravity processor), not laid flat at the height where it joins
      const int y = p.terrain ? w.ground(x, z) + k.y : q.y0 + k.y;
      if (!w.in(x, z) || y < 1 || y >= DIM_H) continue;
      const VPState& s = p.states[k.s];
      // the ground under a surface structure's floor: filled down to the land (no floating corners)
      if (k.y == 0 && S.surface && !p.terrain && q.y0 <= w.ground(x, z) + 4 && reg().tSolid[s.id]) for (int yy = y - 1; yy > y - 10 && yy > 1; yy--) { const BlockId g = w.get(x, yy, z); if (g != AIR && (!reg().tLiquid[g] || yy > w.ground(x, z))) break; w.put(x, yy, z, DIRT); }   // (not the structure's own pools above the land)
      const BlockId sid = k.y <= 3 && !p.terrain ? groundAt(x, z, s.id) : p.terrain ? groundAt(x, z, s.id) : s.id;
      const int idx = w.put(x, y, z, sid, sid == s.id ? s.meta[q.r] : 0);
      if (sid != s.id && landSnow[(z - w.bz) * 16 + (x - w.bx)] && y + 1 < DIM_H && w.get(x, y + 1, z) == AIR) w.put(x, y + 1, z, SNOW);   // (and its snow)
      if (idx < 0) continue;
      if (s.rug) w.rug(x, y, z, s.rug);
      if (s.kind == VPK_SPAWNER) {
        P.setEntity(idx, json{{"type", "spawner"}, {"mob", s.items}, {"structure", S.id}});
        if (std::getenv("DQ_POI_LOG")) DQ_LOG("poi spawner %s at %d %d %d", s.items.c_str(), x, y, z);
      }
      if (s.kind == VPK_CHEST) {
        Mulberry32 cr(hash32(x, y, z, (i32)S.seed));
        P.setEntity(idx, json{{"type", "chest"}, {"items", structLoot("ruin", cr)}});
      }
    }
    // (the audit) the land left standing inside the piece over its floor where its template has no block and no air: ground
    // in the rooms
    if (!filled.empty()) {
      int ground = 0;
      for (int ly = 1; ly < p.sy; ly++) for (int lz = 0; lz < p.sz; lz++) for (int lx = 0; lx < p.sx; lx++) {
        if (filled[((size_t)ly * p.sz + lz) * p.sx + lx] || p.airAt(lx, ly, lz)) continue;
        auto [tx, tz] = vpTurn(p, q.r, lx, lz);
        const int x = q.x0 + tx, z = q.z0 + tz, y = q.y0 + ly;
        if (!w.in(x, z) || y < 1 || y >= DIM_H) continue;
        const BlockId g = w.get(x, y, z);
        if (g != AIR && reg().tSolid[g] && !reg().tLiquid[g]) ground++;
      }
      if (ground) DQ_LOG("poi audit %s ground-in %d piece %s", E->key.c_str(), ground, p.name.c_str());
      // standing on nothing: its lowest blocks over air or water (a structure that floats off the ground, or hangs over a drop)
      int over = 0, base = 0;
      for (const VPBlock& k : p.blocks) {
        if (k.y != 0 || !reg().tSolid[p.states[k.s].id]) continue;
        auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
        const int x = q.x0 + tx, z = q.z0 + tz, y = q.y0 - 1;
        if (!w.in(x, z) || y < 1) continue;
        base++;
        const BlockId g = w.get(x, y, z);
        if (g == AIR || reg().tLiquid[g]) over++;
      }
      if (over * 5 > base && base > 8) DQ_LOG("poi audit %s floats %d of %d piece %s y%d", E->key.c_str(), over, base, p.name.c_str(), q.y0);
    }
  }
}
}  // namespace

// (the checker, tools/port/gentool.cpp poi) every block a Soulrend place's pieces put down, where and what
void poiExpected(const StructurePlan& S, const std::function<void(int, int, int, BlockId)>& fn) {
  auto E = std::static_pointer_cast<const PoiExt>(S.ext);
  if (!E) return;
  for (const Placed& q : E->pcs) {   // (in build order: a piece's air, then its blocks, as buildPoi does)
    if (q.p->terrain) continue;
    const VPPiece& p = *q.p;
    if (p.placeAir && !p.air.empty())
      for (int ly = 0; ly < p.sy; ly++) for (int lz = 0; lz < p.sz; lz++) for (int lx = 0; lx < p.sx; lx++) {
        if (!p.airAt(lx, ly, lz)) continue;
        auto [tx, tz] = vpTurn(p, q.r, lx, lz);
        fn(q.x0 + tx, q.y0 + ly, q.z0 + tz, AIR);
      }
    for (const VPBlock& k : p.blocks) {
      auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
      fn(q.x0 + tx, q.y0 + k.y, q.z0 + tz, p.states[k.s].id);
    }
  }
}

void registerStructurePois() {
  StructureType t;
  t.key = "poi"; t.name = "a place of note";
  t.region = 112; t.margin = 16; t.chance = 0.9; t.clearance = 8;   // (about three for every one of our own places)
  t.flags = SF_SURFACE;
  t.plan = [](const TerrainQuery& tq, int x, int z, Mulberry32& rnd, StructurePlan& out) -> bool {
    const PoiPack* pk = poiPack();
    if (!pk || tq.dim() != DIM_OVERWORLD) return false;
    const TerrainColumn c = tq.terrainColumn(x, z);
    if (c.y < 0 || c.water >= 0) return false;
    const std::vector<std::string> lands = landsAt(tq, c);
    // the structures this land may have, the rarer ones (a wider spacing in their mod) less often
    std::vector<std::pair<double, const PoiDef*>> cand;
    static const char* only = std::getenv("DQ_POI_ONLY");   // (the audit: this one structure wherever it may stand)
    for (const PoiDef& d : pk->defs) if (!only || d.key == only) for (const std::string& l : d.lands) if (std::find(lands.begin(), lands.end(), l) != lands.end()) {
      cand.push_back({1.0 / d.spacing, &d}); break;
    }
    if (cand.empty()) return false;
    double tot = 0; for (auto& e : cand) tot += e.first;
    double u = rnd() * tot; const PoiDef* d = cand.back().second;
    for (auto& e : cand) { u -= e.first; if (u <= 0) { d = e.second; break; } }
    auto E = std::make_shared<PoiExt>(); E->key = d->key; E->flatR = d->flatR; E->beard = d->beard && d->surface; E->ground = d->ground; E->blend = tq.genVersion() >= 17;
    if (!expand(*pk, *d, tq, x, z, rnd, E->pcs) || E->pcs.empty()) { if (std::getenv("DQ_POI_WHY")) DQ_LOG("poi why %s: expand failed", d->key.c_str()); return false; }
    // the land under each rigid piece standing on it (not one buried well below the ground: a crypt keeps its hill)
    if (d->surface) for (auto& q : E->pcs) {   // (every place on the land, whatever its mod asked: no house stood on a bare plinth of earth)
      if (q.p->terrain) continue;
      const int qx1 = q.x0 + q.w - 1, qz1 = q.z0 + q.d - 1;
      // only a piece standing at the ground: Minecraft's beard reaches about a dozen blocks, so a tower's upper floors and
      // the pieces stacked on others get no earth under them (raising the land to every floor grew the castles' towers
      // of earth and put ground inside the rooms). Not one buried well below the ground either: a crypt keeps its hill
      const int g = tq.surfaceY((q.x0 + qx1) / 2, (q.z0 + qz1) / 2);
      if (q.y0 - 1 < g - 4 || q.y0 - 1 > g + 24) continue;
      bool under = false;   // (a piece below it in the plan: it stands on that, not on the land)
      for (auto& o : E->pcs) if (&o != &q && !o.p->terrain && o.y0 < q.y0 && o.y0 + o.p->sy >= q.y0 - 1 &&
                                 o.x0 <= qx1 && o.x0 + o.w - 1 >= q.x0 && o.z0 <= qz1 && o.z0 + o.d - 1 >= q.z0) { under = true; break; }
      if (under) continue;
      // the skirt: wide enough that the land never climbs more than about a block in two to the floor (Minecraft's beard_box
      // reaches 12 blocks; a floor raised well above the land had a sheer wall of earth under it)
      // (a floor below the land: the land comes down only on the piece itself and a step round it, never in a wide funnel)
      const int rise = q.y0 - 1 - g;
      E->flat.push_back({q.x0, q.z0, qx1, qz1, q.y0 - 1, rise < 0 ? std::min(14, -rise * 2 + 2) : std::max(E->flatR, std::min(28, rise * 2 + 2))});
    }
    int X0 = 1 << 30, Z0 = 1 << 30, X1 = -(1 << 30), Z1 = -(1 << 30);
    for (auto& q : E->pcs) { X0 = std::min(X0, q.x0); Z0 = std::min(Z0, q.z0); X1 = std::max(X1, q.x0 + q.w - 1); Z1 = std::max(Z1, q.z0 + q.d - 1); }
    // on the land: not over water, and level enough under it (a steep slope broke it into pieces up a ravine)
    if (d->surface) {
      int lo = 1 << 20, hi = -(1 << 20), wet = 0, n = 0;
      for (int zz = Z0; zz <= Z1; zz += 4) for (int xx = X0; xx <= X1; xx += 4) {
        const TerrainColumn cc = tq.terrainColumn(xx, zz); n++;
        if (cc.water >= 0) wet++;
        lo = std::min(lo, cc.y); hi = std::max(hi, cc.y);
      }
      static const bool why = std::getenv("DQ_POI_WHY") != nullptr;
      // level enough: a small place on nearly level land (a well on a hillside sat in a cut or a crater), a big one where the
      // land varies by no more than about a tenth of its size
      const int allowed = std::max(4, (X1 - X0 + Z1 - Z0) / 10);
      if (hi - lo > allowed || wet * 5 > n) { if (why) DQ_LOG("poi why %s: uneven %d (allowed %d) wet %d/%d", d->key.c_str(), hi - lo, allowed, wet, n); return false; }
      // its own ground matches the land's all over its footprint (sand in the desert and on the beach, snow in the snow, grass
      // in the green lands), not a courtyard of sand in a meadow half over the desert's edge
      int gOk = 0, gN = 0;
      if (d->ground) for (int zz = Z0; zz <= Z1; zz += 6) for (int xx = X0; xx <= X1; xx += 6) {
        const TerrainColumn cc = tq.terrainColumn(xx, zz);
        const std::vector<std::string> L = landsAt(tq, cc);
        auto has = [&](const char* l) { return std::find(L.begin(), L.end(), l) != L.end(); };
        const bool sandy = cc.biome == BIOME_DESERT || cc.biome == BIOME_BEACH || has("badlands"), snowy = has("snowy");
        gN++; if (!(d->ground == 's' ? !sandy : d->ground == 'n' ? !snowy : (sandy || snowy))) gOk++;
      }
      if (gN && gOk * 5 < gN * 4) { if (why) DQ_LOG("poi why %s: ground %c %d/%d", d->key.c_str(), d->ground, gOk, gN); return false; }   // (four in five of it on its own ground)
    }
    out.x0 = X0; out.z0 = Z0; out.x1 = X1; out.z1 = Z1; out.cx = x; out.cz = z; out.y = E->pcs[0].y0;
    out.surface = d->surface;
    std::string nm = d->key.substr(d->key.find(':') + 1); for (auto& ch : nm) if (ch == '_') ch = ' ';
    out.name = nm; out.data = json{{"poi", d->key}};
    // the bosses of Soulrend's places (sim/keepers.cpp wakes one when someone walks in; sim/boss_kit.cpp fights it): the Lich
    // in its walled graveyard, the Night Lord in the coliseum's arena, the Dead King in the ruined keep
    {
      static const std::map<std::string, std::string> BOSS = {{"graveyard:large_graveyard", "graveyard_lich"},
        {"dungeons_arise:coliseum", "night_lord"}, {"dungeons_arise:keep_kayra", "dead_king"}};
      if (auto it = BOSS.find(d->key); it != BOSS.end() && reg().mobs.contains(it->second) && rigModel(it->second)) {
        const Placed& q = E->pcs[0];
        out.data["boss"] = json{{"at", {q.x0 + q.w / 2, q.y0 + 1, q.z0 + q.d / 2}}, {"type", it->second}, {"box", {X0, q.y0 - 2, Z0, X1, q.y0 + 30, Z1}}};
      }
    }
    out.ext = E;
    if (std::getenv("DQ_POI_LOG")) DQ_LOG("poi %s at %d %d %d: %zu pieces", d->key.c_str(), x, out.y, z, E->pcs.size());
    return true;
  };
  t.build = buildPoi;
  t.flatten = [](const StructurePlan& S, int x, int z, double& weight, int& targetY) -> bool {
    auto E = std::static_pointer_cast<const PoiExt>(S.ext);
    if (!E || E->flat.empty()) return false;
    double best = 0, sw = 0, swy = 0; int inside = INT32_MAX, by = 0;
    for (const auto& r : E->flat) {
      const int dx = std::max({r[0] - x, 0, x - r[2]}), dz = std::max({r[1] - z, 0, z - r[3]});
      if (dx == 0 && dz == 0) { inside = std::min(inside, r[4]); continue; }   // (under two: the lower floor; the land never rises into a room)
      const double dd = std::hypot((double)dx, (double)dz);
      if (dd >= r[5]) continue;
      const double wv = 1 - jsSmooth(0, r[5], dd), k = wv * wv * wv;   // (round the pieces: their floors blended by nearness, no step between them)
      if (wv > best) { best = wv; by = r[4]; } sw += k; swy += k * r[4];
    }
    if (inside != INT32_MAX) { weight = 1; targetY = inside; return true; }
    if (best <= 0 || sw <= 0) return false;
    weight = best; targetY = E->blend ? (int)std::lround(swy / sw) : by;
    return true;
  };
  t.flattenPad = 29;   // (the widest blend: IDAS's kernels)
  registerStructure(std::move(t));
}

} // namespace dq
