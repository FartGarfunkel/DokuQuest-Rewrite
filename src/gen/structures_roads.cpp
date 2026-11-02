// gen/structures_roads.cpp — roads between neighbouring villages: a path of the villages' own street blocks from the end
// of one's main street to the end of the other's, winding round hills and lakes (a greedy walk that keeps level and dry),
// plank bridges with railings and posts over water, and a lamp post now and then. One plan per 3x3 village regions,
// joining villages to their nearest neighbours.
// Registered after every other structure, so nothing else keeps clear of a road's (large) footprint.
#include "gen/structures_town.h"
#include "gen/structures_api.h"
#include "gen/gendefs.h"
#include <cstdlib>

namespace dq {

namespace {
constexpr int VREG = 352, RREG = VREG * 3, STEP = 12;   // structures_villages.cpp's village region; a road plan's; route step
struct RoadSeg { std::vector<std::array<int, 2>> pts; std::vector<float> at; int style = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0; std::array<int, 4> skipA{}, skipB{}; std::string from, to; };
struct RoadExt { std::vector<RoadSeg> segs; };

int villageType() { static const int k = structureTypeIndex("village"); return k; }

// nearest point of the road to (x, z): distance, distance along it, which side
double nearest(const RoadSeg& g, double x, double z, double* along = nullptr, double* side = nullptr) {
  double bd = 1e9;
  for (size_t i = 0; i + 1 < g.pts.size(); i++) {
    const double ax = g.pts[i][0] + 0.5, az = g.pts[i][1] + 0.5, bx = g.pts[i + 1][0] + 0.5, bz = g.pts[i + 1][1] + 0.5;
    if (x < std::min(ax, bx) - 6 || x > std::max(ax, bx) + 6 || z < std::min(az, bz) - 6 || z > std::max(az, bz) + 6) continue;
    const double dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz;
    const double t = L2 > 0 ? std::clamp(((x - ax) * dx + (z - az) * dz) / L2, 0.0, 1.0) : 0;
    const double qx = ax + dx * t - x, qz = az + dz * t - z, d = std::sqrt(qx * qx + qz * qz);
    if (d < bd) { bd = d; if (along) *along = g.at[i] + t * std::sqrt(L2); if (side) *side = dx * (z - az) - dz * (x - ax); }
  }
  return bd;
}
bool inRect(const std::array<int, 4>& r, int x, int z) { return x >= r[0] && x <= r[2] && z >= r[1] && z <= r[3]; }

// the way from a to b: every STEP blocks, the sideways offset (within a band that narrows to nothing at both ends)
// that changes height least and stays out of water; false when it would climb a mountain or cross a sea
bool route(const TerrainQuery& t, std::array<int, 2> a, std::array<int, 2> b, u32 seed, RoadSeg& g) {
  const double dx = b[0] - a[0], dz = b[1] - a[1], len = std::hypot(dx, dz);
  if (len < 30 || len > 940) return false;
  const int n = std::max(2, (int)std::lround(len / STEP));
  const double px = -dz / len, pz = dx / len;
  auto col = [&](double x, double z, int& h, bool& wet) { const TerrainColumn c = t.terrainColumn(jsFloor(x), jsFloor(z)); h = c.y; wet = c.water > c.y; };
  int hPrev; bool w0; col(a[0], a[1], hPrev, w0);
  double off = 0; int wetRun = 0, wetMax = 0, climb = 0;
  g.pts.clear(); g.pts.push_back(a);
  for (int i = 1; i < n; i++) {
    const double s = (double)i / n, cx = a[0] + dx * s, cz = a[1] + dz * s;
    const double room = std::min(1.0, std::min(s, 1 - s) * len / 60.0) * 30;
    double best = 1e18, bo = 0; int bh = hPrev; bool bw = false;
    for (int k = -3; k <= 3; k++) {
      const double o = std::clamp(off + k * 6.0, -room, room);
      int h; bool wet; col(cx + px * o, cz + pz * o, h, wet);
      if (h < 0) continue;
      const double c = std::abs(h - hPrev) * 3.0 + (wet ? 12 : 0) + std::abs(o - off) * 0.3 + std::abs(o) * 0.06 + hashFloat(i, k, 77, (i32)seed) * 0.5;
      if (c < best) { best = c; bo = o; bh = h; bw = wet; }
    }
    if (best > 1e17) return false;
    climb = std::max(climb, std::abs(bh - hPrev));
    wetRun = bw ? wetRun + STEP : 0; wetMax = std::max(wetMax, wetRun);
    off = bo; hPrev = bw ? hPrev : bh;
    g.pts.push_back({jsFloor(cx + px * off), jsFloor(cz + pz * off)});
  }
  g.pts.push_back(b);
  if (wetMax > 160 || climb > 14) return false;   // no mountain passes, no crossing the sea (a long bridge over a lake is fine)
  g.at.assign(g.pts.size(), 0);
  g.x0 = g.x1 = a[0]; g.z0 = g.z1 = a[1];
  for (size_t i = 0; i < g.pts.size(); i++) {
    if (i) g.at[i] = g.at[i - 1] + (float)std::hypot(g.pts[i][0] - g.pts[i - 1][0], g.pts[i][1] - g.pts[i - 1][1]);
    g.x0 = std::min(g.x0, g.pts[i][0]); g.x1 = std::max(g.x1, g.pts[i][0]); g.z0 = std::min(g.z0, g.pts[i][1]); g.z1 = std::max(g.z1, g.pts[i][1]);
  }
  g.x0 -= 4; g.z0 -= 4; g.x1 += 4; g.z1 += 4;
  return true;
}

// One road plan per block of 3x3 village regions (RREG): it holds the roads from each village in it to its two nearest
// neighbours within 900 blocks (or to any village that has this one among its two nearest), each pair built once, by the
// village that sorts first. Plans this big are still found from anywhere along their roads (StructureIndex::near looks
// one region about).
struct VRef { const StructurePlan* p = nullptr; int rx = 0, rz = 0; };
bool before(const VRef& a, const VRef& b) { return a.rx != b.rx ? a.rx < b.rx : a.rz < b.rz; }
std::vector<VRef> nearestTwo(const StructureIndex& idx, int vt, const VRef& A) {
  std::vector<std::pair<double, VRef>> c;
  for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (dx || dz)
    if (const StructurePlan* B = idx.at(vt, A.rx + dx, A.rz + dz)) {
      const double d = std::hypot(B->cx - A.p->cx, B->cz - A.p->cz);
      if (d < 900) c.push_back({d, VRef{B, A.rx + dx, A.rz + dz}});
    }
  std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<VRef> out; for (size_t i = 0; i < c.size() && i < 2; i++) out.push_back(c[i].second);
  return out;
}
bool roadPlace(const StructureIndex& idx, const TerrainQuery& t, int Rx, int Rz, StructurePlan& S) {
  const int vt = villageType();
  if (vt < 0) return false;
  auto e = std::make_shared<RoadExt>();
  static const bool dbg = std::getenv("DQ_ROADLOG") != nullptr;
  auto join = [&](const VRef& a, const VRef& b) {
    const StructurePlan* A = a.p; const StructurePlan* B = b.p;
    const auto ea = townStreetEnds(*A), eb = townStreetEnds(*B);
    std::array<int, 2> pa{ea[0][0], ea[0][1]}, pb{eb[0][0], eb[0][1]};
    double best = 1e18;
    for (auto& p : ea) for (auto& q : eb) { const double d = std::hypot(p[0] - q[0], p[1] - q[1]); if (d < best) { best = d; pa = {p[0], p[1]}; pb = {q[0], q[1]}; } }
    RoadSeg g;
    const bool ok = route(t, pa, pb, hash32(t.seed(), a.rx * 7 + b.rx, a.rz * 7 + b.rz, 5150), g);
    if (dbg) DQ_LOG("road %s -> %s: %.0f apart, %s", A->id.c_str(), B->id.c_str(), best, ok ? "made" : "no way");
    if (!ok) return;
    g.style = townStyleOf(*A);
    g.skipA = {A->x0, A->z0, A->x1, A->z1}; g.skipB = {B->x0, B->z0, B->x1, B->z1}; g.from = A->id; g.to = B->id;
    e->segs.push_back(std::move(g));
  };
  int y = 0;
  for (int vz = Rz * 3; vz < Rz * 3 + 3; vz++) for (int vx = Rx * 3; vx < Rx * 3 + 3; vx++) {
    const StructurePlan* Ap = idx.at(vt, vx, vz);
    if (!Ap) continue;
    const VRef A{Ap, vx, vz}; y = Ap->y;
    const auto mine = nearestTwo(idx, vt, A);
    for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (dx || dz) {
      const StructurePlan* Bp = idx.at(vt, vx + dx, vz + dz);
      if (!Bp) continue;
      const VRef B{Bp, vx + dx, vz + dz};
      if (!before(A, B)) continue;   // the pair is built once, from the village that sorts first
      bool linked = false;
      for (auto& q : mine) if (q.p == Bp) linked = true;
      if (!linked) for (auto& q : nearestTwo(idx, vt, B)) if (q.p == Ap) linked = true;
      if (linked) join(A, B);
    }
  }
  if (e->segs.empty()) return false;
  S.x0 = S.z0 = INT32_MAX; S.x1 = S.z1 = INT32_MIN;
  for (auto& g : e->segs) { S.x0 = std::min(S.x0, g.x0); S.z0 = std::min(S.z0, g.z0); S.x1 = std::max(S.x1, g.x1); S.z1 = std::max(S.z1, g.z1); }
  S.cx = (S.x0 + S.x1) / 2; S.cz = (S.z0 + S.z1) / 2; S.y = y; S.surface = true;
  S.seed = hash32(t.seed(), Rx, Rz, 5151); S.name = "a road";   // no id: not a place to discover
  S.ext = e;
  return true;
}

void roadBuild(ChunkWriter& w, const StructurePlan& S) {
  static const BlockId WATER = reg().blockId("water"), ICE = reg().blockId("ice"), LANTERN = reg().blockId("lantern");
  const RoadExt& R = extOf<RoadExt>(S);
  for (const RoadSeg& g : R.segs) {
    if (g.x1 < w.bx || g.x0 > w.bx + 15 || g.z1 < w.bz || g.z0 > w.bz + 15) continue;
    const VStyle& st = vstyle(g.style);
    const float total = g.at.back();
    for (int z = w.bz; z < w.bz + 16; z++) for (int x = w.bx; x < w.bx + 16; x++) {
      if (inRect(g.skipA, x, z) || inRect(g.skipB, x, z)) continue;
      double along = 0, side = 0;
      const double d = nearest(g, x + 0.5, z + 0.5, &along, &side);
      if (d > 2.6) continue;
      const int gy = w.ground(x, z);
      if (gy < 2 || gy > 250) continue;
      int wy = gy + 1; bool bridge = false;
      for (BlockId b = w.get(x, wy, z); wy < 252 && (b == WATER || b == ICE); b = w.get(x, ++wy, z)) bridge = true;
      auto clear = [&](int y0, int y1) { for (int y = y0; y <= y1; y++) { const BlockId b = w.get(x, y, z); if (b != 0 && softBlock(b) && !reg().tLiquid[b]) w.put(x, y, z, "air"); } };
      const bool edge = d > 1.5;
      if (bridge) {
        w.put(x, wy, z, st.planks.c_str());
        if (edge) w.put(x, wy + 1, z, st.fence.c_str()); else clear(wy + 1, wy + 2);
        if (edge && std::fmod(along, 8.0) < 1.2) for (int y = wy - 1; y > gy; y--) w.put(x, y, z, st.log.c_str());   // posts down to the bed
        continue;
      }
      if (!edge) {
        const BlockId top = w.get(x, gy, z);
        if (top != 0 && reg().tSolid[top] && !reg().tLiquid[top]) w.put(x, gy, z, w.pick(st.path, x, gy, z).c_str());
        clear(gy + 1, gy + 2);
      } else if (side > 0 && along > 24 && along < total - 24 && std::fmod(along, 72.0) < 1.0 && d < 2.3) {   // a lamp post by the way
        clear(gy + 1, gy + 3);
        w.put(x, gy + 1, z, st.fence.c_str()); w.put(x, gy + 2, z, st.fence.c_str()); w.put(x, gy + 3, z, LANTERN);
      }
    }
  }
}
} // namespace

bool roadNear(const StructureIndex& idx, int x, int z, int pad, RoadInfo& out) {
  static const int k = structureTypeIndex("road");
  if (k < 0) return false;
  std::vector<const StructurePlan*> plans; idx.near(x, z, pad, plans, k);
  double best = pad;
  for (auto* P : plans) if (P && P->ext) for (const RoadSeg& g : extOf<RoadExt>(*P).segs) {
    if (x < g.x0 - pad || x > g.x1 + pad || z < g.z0 - pad || z > g.z1 + pad) continue;
    const double d = nearest(g, x + 0.5, z + 0.5);
    if (d <= best) { best = d; out.pts = g.pts; out.from = g.from; out.to = g.to; }
  }
  return !out.pts.empty();
}

void registerStructureRoads() {
  StructureType t; t.key = "road"; t.name = "a road"; t.region = RREG; t.clearance = 0; t.dims = 1u << DIM_OVERWORLD;
  t.flags = SF_SURFACE | SF_HIDDEN; t.place = roadPlace; t.build = roadBuild;
  t.treeOff = [](const StructurePlan& S, int x, int z) {
    if (!S.ext) return false;
    for (const RoadSeg& g : extOf<RoadExt>(S).segs) if (x >= g.x0 - 2 && x <= g.x1 + 2 && z >= g.z0 - 2 && z <= g.z1 + 2 && nearest(g, x + 0.5, z + 0.5) < 4.5) return true;
    return false;
  };
  t.treePad = 2;
  registerStructure(std::move(t));
}

} // namespace dq
