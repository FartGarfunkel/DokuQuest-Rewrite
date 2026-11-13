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
#include <map>

namespace dq {

namespace {
struct PoiPool { std::vector<std::pair<const VPPiece*, int>> els; };
struct PoiDef { std::string key, start; int depth = 4, y = 0, spacing = 32, reach = 80; bool surface = true; std::vector<std::string> lands; };
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
    for (auto& r : (*idx)["pieces"]) {
      if (!r.is_string()) continue;
      const std::string rel = r.get<std::string>();   // "ns/path.nbt"
      auto b = readBinary(dataDir() / "pois" / rel);
      if (!b) continue;
      const size_t sl = rel.find('/');
      const std::string ns = rel.substr(0, sl), path = rel.substr(sl + 1, rel.size() - sl - 5);
      VPPiece P;
      if (!makePiece(ns, "poi", path, *b, P)) continue;
      pk->pieces[ns + ":" + path] = std::move(P); n++;
    }
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
      if (pk->pools.count(d.start)) pk->defs.push_back(std::move(d));
    }
    DQ_LOG("points of interest: %zu structures, %d pieces, %zu pools", pk->defs.size(), n, pk->pools.size());
    return pk->defs.empty() ? nullptr : std::move(pk);
  }();
  return P.get();
}

// our lands at a column (the mods' biome tags were turned into these: tools/port/poi_import.py LAND)
std::vector<std::string> landsAt(const TerrainQuery& t, const TerrainColumn& c) {
  OwColumn oc; oc.h = c.jsY; oc.biome = (u8)std::max(0, c.biome); oc.temp = c.temp; oc.humid = c.humid;
  const BtaVariant bv = btaVariant(oc);
  std::vector<std::string> L;
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
struct PoiExt { std::vector<Placed> pcs; std::string key; };

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
    int lo = 1 << 20; for (int k = 0; k < 9; k++) lo = std::min(lo, t.surfaceY(s.x0 + s.w * (k % 3) / 2, s.z0 + s.d * (k / 3) / 2));
    s.y0 = lo + 1 + d.y;
  } else s.y0 = std::clamp(d.y + 1, 6, DIM_H - s0->sy - 2);
  if (s.y0 < SEA_LEVEL && d.surface) return false;   // (not under the sea)
  out.push_back(s);
  for (size_t qi = 0; qi < out.size() && out.size() < 160; qi++) {
    const Placed Q = out[qi];
    if (Q.depth >= d.depth) continue;
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
      for (const VPPiece* C : weighted(pit->second)) {
        int rs[4] = {0, 1, 2, 3}; for (int i = 3; i > 0; i--) std::swap(rs[i], rs[(int)(rnd() * (i + 1)) % (i + 1)]);
        for (int r : rs) {
          for (const VPJoint& B : C->joints) {
            if (B.name != A.target) continue;
            if (up ? B.front != 5 : down ? B.front != 4 : (B.front > 3 || ((B.front + r) & 3) != ((af + 2) & 3))) continue;
            auto [bx, bz] = vpTurn(*C, r, B.x, B.z);
            Placed q{}; q.p = C; q.r = r; q.w = r & 1 ? C->sz : C->sx; q.d = r & 1 ? C->sx : C->sz; q.depth = Q.depth + 1;
            q.x0 = Tx - bx; q.z0 = Tz - bz; q.y0 = Ty - B.y;
            if (C->terrain) q.y0 = t.surfaceY(Tx, Tz) + 1 - B.y;
            int x1, y1, z1; box(q, x1, y1, z1);
            if (std::max({std::abs(q.x0 - cx), std::abs(x1 - cx), std::abs(q.z0 - cz), std::abs(z1 - cz)}) > d.reach) continue;
            if (q.y0 < 2 || y1 >= DIM_H - 1) continue;
            if (inParent ? (q.x0 < Q.x0 || x1 > qx1 || q.y0 < Q.y0 || y1 > qy1 || q.z0 < Q.z0 || z1 > qz1 || overlaps(q.x0, q.y0, q.z0, x1, y1, z1, (int)qi))
                         : overlaps(q.x0, q.y0, q.z0, x1, y1, z1, -1)) continue;
            out.push_back(q); placed = true; break;
          }
          if (placed) break;
        }
        if (placed) break;
      }
    }
  }
  return true;
}

void buildPoi(ChunkWriter& w, const StructurePlan& S) {
  auto E = std::static_pointer_cast<const PoiExt>(S.ext);
  if (!E) return;
  const BlockId DIRT = reg().blockId("dirt");
  SP P(w);
  for (const Placed& q : E->pcs) {
    const VPPiece& p = *q.p;
    if (q.x0 > w.bx + 15 || q.x0 + q.w - 1 < w.bx || q.z0 > w.bz + 15 || q.z0 + q.d - 1 < w.bz) continue;
    for (const VPBlock& k : p.blocks) {
      auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
      const int x = q.x0 + tx, z = q.z0 + tz, y = q.y0 + k.y;
      if (!w.in(x, z) || y < 1 || y >= DIM_H) continue;
      const VPState& s = p.states[k.s];
      // the ground under a surface structure's floor: filled down to the land (no floating corners)
      if (k.y == 0 && S.surface && reg().tSolid[s.id]) for (int yy = y - 1; yy > y - 10 && yy > 1; yy--) { const BlockId g = w.get(x, yy, z); if (g != AIR && !reg().tLiquid[g]) break; w.put(x, yy, z, DIRT); }
      const int idx = w.put(x, y, z, s.id, s.meta[q.r]);
      if (idx < 0) continue;
      if (s.rug) w.rug(x, y, z, s.rug);
      if (s.kind == VPK_CHEST) {
        Mulberry32 cr(hash32(x, y, z, (i32)S.seed));
        P.setEntity(idx, json{{"type", "chest"}, {"items", structLoot("ruin", cr)}});
      }
    }
  }
}
}  // namespace

void registerStructurePois() {
  StructureType t;
  t.key = "poi"; t.name = "a place of note";
  t.region = 176; t.margin = 24; t.chance = 0.8; t.clearance = 10;
  t.flags = SF_SURFACE;
  t.plan = [](const TerrainQuery& tq, int x, int z, Mulberry32& rnd, StructurePlan& out) -> bool {
    const PoiPack* pk = poiPack();
    if (!pk || tq.dim() != DIM_OVERWORLD) return false;
    const TerrainColumn c = tq.terrainColumn(x, z);
    if (c.y < 0 || c.water >= 0) return false;
    const std::vector<std::string> lands = landsAt(tq, c);
    // the structures this land may have, the rarer ones (a wider spacing in their mod) less often
    std::vector<std::pair<double, const PoiDef*>> cand;
    for (const PoiDef& d : pk->defs) for (const std::string& l : d.lands) if (std::find(lands.begin(), lands.end(), l) != lands.end()) {
      cand.push_back({1.0 / d.spacing, &d}); break;
    }
    if (cand.empty()) return false;
    double tot = 0; for (auto& e : cand) tot += e.first;
    double u = rnd() * tot; const PoiDef* d = cand.back().second;
    for (auto& e : cand) { u -= e.first; if (u <= 0) { d = e.second; break; } }
    auto E = std::make_shared<PoiExt>(); E->key = d->key;
    if (!expand(*pk, *d, tq, x, z, rnd, E->pcs) || E->pcs.empty()) return false;
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
      if (hi - lo > std::max(8, (X1 - X0 + Z1 - Z0) / 8) || wet * 5 > n) return false;
    }
    out.x0 = X0; out.z0 = Z0; out.x1 = X1; out.z1 = Z1; out.cx = x; out.cz = z; out.y = E->pcs[0].y0;
    out.surface = d->surface;
    std::string nm = d->key.substr(d->key.find(':') + 1); for (auto& ch : nm) if (ch == '_') ch = ' ';
    out.name = nm; out.data = json{{"poi", d->key}};
    out.ext = E;
    if (std::getenv("DQ_POI_LOG")) DQ_LOG("poi %s at %d %d %d: %zu pieces", d->key.c_str(), x, out.y, z, E->pcs.size());
    return true;
  };
  t.build = buildPoi;
  registerStructure(std::move(t));
}

} // namespace dq
