// gen/village_mill_lift.cpp — a lift up to the millstone in a style's own windmill (the imported village pieces), as the
// classic windmill has one (gen/structures_townbld.cpp bWindmill): worked out once per piece from its blocks. The shaft is
// a column of the ground floor's open space that runs clear (or through floors, which open for it) up to the millstone's
// floor, beside a standing place there; the rail goes up the column next to it (a wall's, or open room's: nothing in it
// that's furniture), the two stops beyond the rail. Pieces whose millstone stands on the ground floor get none.
#include "gen/village_mill_lift.h"
#include "core/registry.h"
#include <map>
#include <mutex>

namespace dq {

namespace {
struct Grid {
  const VPPiece& p;
  std::vector<BlockId> id;
  explicit Grid(const VPPiece& pc) : p(pc), id((size_t)pc.sx * pc.sy * pc.sz, AIR) {
    for (const VPBlock& k : p.blocks) id[idx(k.x, k.y, k.z)] = p.states[k.s].id;
  }
  size_t idx(int x, int y, int z) const { return ((size_t)y * p.sz + z) * p.sx + x; }
  bool in(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < p.sx && y < p.sy && z < p.sz; }
  BlockId at(int x, int y, int z) const { return in(x, y, z) ? id[idx(x, y, z)] : AIR; }
  bool solid(int x, int y, int z) const { return reg().tSolid[at(x, y, z)]; }
  bool open(int x, int y, int z) const { const BlockId b = at(x, y, z); return !reg().tSolid[b] && !reg().tLiquid[b]; }
  // plain building stuff (a wall, a floor): nothing with a use, a picture or a record of its own
  bool plain(int x, int y, int z) const {
    const BlockId b = at(x, y, z);
    if (b == AIR) return true;
    const BlockDef& d = reg().block(b);
    return reg().tSolid[b] && !reg().tShape[b] && !d.has("climb") && !d.has("machine") && d.str("mech").empty() && d.key != "millstone" && d.key.find("chest") == std::string::npos;
  }
  // a floor across the column at y (solid, open over and under it, and floor on most sides): it opens for the shaft
  bool floorAt(int x, int y, int z) const {
    if (!solid(x, y, z) || !plain(x, y, z)) return false;
    int n = 0; for (int f = 0; f < 4; f++) { static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; if (solid(x + D[f][0], y, z + D[f][1])) n++; }
    return n >= 2;
  }
};
}

std::optional<MillLift> millLiftOf(const VPPiece& p) {
  static std::mutex mu; static std::map<const VPPiece*, std::optional<MillLift>> cache;
  { std::lock_guard<std::mutex> lk(mu); auto it = cache.find(&p); if (it != cache.end()) return it->second; }
  std::optional<MillLift> out;
  [&] {
    const BlockId MS = reg().blockId("millstone");
    if (!MS || !reg().blockId("lift_rail") || !reg().blockId("lift_stop") || !reg().blockId("slider")) return;
    Grid g(p);
    int mx = -1, my = -1, mz = -1;
    for (const VPBlock& k : p.blocks) if (p.states[k.s].id == MS) { mx = k.x; my = k.y; mz = k.z; }
    if (my < 0) return;
    // the ground floor: the lowest level a body stands on inside (a solid under, room over)
    int gy = -1;
    for (int y = 1; y < my && gy < 0; y++) for (int z = 1; z + 1 < p.sz && gy < 0; z++) for (int x = 1; x + 1 < p.sx && gy < 0; x++)
      if (g.solid(x, y - 1, z) && g.open(x, y, z) && g.open(x, y + 1, z)) gy = y;
    // the floor the miller stands on to work the stones (they may sit up on a block of their own): the highest level by
    // them with a floor under and room over
    int sy = -1;
    for (int y = my; y >= 1 && sy < 0; y--)
      for (int dz = -2; dz <= 2 && sy < 0; dz++) for (int dx = -2; dx <= 2 && sy < 0; dx++)
        if ((dx || dz) && g.solid(mx + dx, y - 1, mz + dz) && g.open(mx + dx, y, mz + dz) && g.open(mx + dx, y + 1, mz + dz)) sy = y;
    if (gy < 0 || sy < 0 || sy - gy < 3) return;   // (the stones on the ground floor: no upper floor to ride to)
    static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int bestD = 1 << 30;
    for (int sz = 1; sz + 1 < p.sz; sz++) for (int sx = 1; sx + 1 < p.sx; sx++) {
      // the shaft: open on the ground floor, a plain floor under it for the slider, clear or through floors up to the top
      if (!g.open(sx, gy, sz) || !g.open(sx, gy + 1, sz) || !g.plain(sx, gy - 1, sz) || !g.solid(sx, gy - 1, sz)) continue;
      const int topY = sy - 1;   // (where the platform stops: in the stones' floor, its top half a block under it)
      bool clear = true;
      for (int y = gy + 2; y <= sy + 1 && clear; y++) if (!g.open(sx, y, sz) && !(y <= topY && g.floorAt(sx, y, sz))) clear = false;
      if (!clear || !g.open(sx, sy, sz) || !g.open(sx, sy + 1, sz)) continue;
      // something to step off onto up there, near the stones
      bool step = false;
      for (auto& d : D) { const int x = sx + d[0], z = sz + d[1]; if (g.solid(x, topY, z) && g.open(x, sy, z) && g.open(x, sy + 1, z)) step = true; }
      if (!step) continue;
      for (auto& d : D) {
        const int rx = sx + d[0], rz = sz + d[1], tx = rx + d[0], tz = rz + d[1];
        if (!g.in(rx, 0, rz) || !g.in(tx, 0, tz)) continue;
        bool ok = true;
        for (int y = gy - 1; y <= topY && ok; y++) if (!g.plain(rx, y, rz)) ok = false;   // (the rail: wall or empty room, nothing else)
        if (!ok || !g.plain(tx, gy, tz) || !g.plain(tx, topY, tz)) continue;
        if (rx == mx && rz == mz) continue;
        // (not across the way in at the ground floor: a rail is no door)
        int openSides = 0; for (auto& e : D) if (g.open(rx + e[0], gy, rz + e[1]) && g.open(rx + e[0], gy + 1, rz + e[1])) openSides++;
        if (g.open(rx, gy, rz) && openSides >= 3) continue;
        // (best: the stops set in a wall, the rail by it, the shaft near the stones)
        const int dist = std::abs(sx - mx) + std::abs(sz - mz) + (g.solid(tx, gy, tz) && g.solid(tx, topY, tz) ? 0 : 6) + (g.solid(rx, gy, rz) ? 0 : 1);
        if (dist < bestD) { bestD = dist; MillLift L; L.shaft = {sx, gy - 1, sz}; L.rail = {rx, rz}; L.stop = {tx, tz}; L.top = topY; out = L; }
      }
    }
  }();
  std::lock_guard<std::mutex> lk(mu); cache[&p] = out;
  return out;
}

} // namespace dq
