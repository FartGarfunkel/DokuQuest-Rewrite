// sim/moving_piece.cpp — see moving_piece.h.
#include "sim/moving_piece.h"
#include "sim/glue.h"
#include "sim/blockinfo.h"
#include "sim/chest_work.h"
#include "world/dirs.h"
#include "world/shapes.h"
#include "core/registry.h"
#include <cmath>
#include <unordered_set>

namespace dq {

namespace {
struct CellHash { size_t operator()(const ivec3& p) const { return (size_t)((u32)p.x * 73856093u ^ (u32)p.y * 19349663u ^ (u32)p.z * 83492791u); } };

// the other half of a two-block thing (a door, a bed, a tall plant, a large chest), or false
bool partnerOf(const World& w, ivec3 c, ivec3& out) {
  const BlockId id = w.getBlock(c.x, c.y, c.z); const u8 m = w.getMeta(c.x, c.y, c.z);
  const int k = reg().tShape[id];
  if (k == ShapeKind::Door) { out = c + ivec3(0, (m & 16) ? -1 : 1, 0); return true; }
  if (k == ShapeKind::Bed) { const int f = m & 3, s = (m & 4) ? -1 : 1; out = c + ivec3(DIRX[f] * s, 0, DIRZ[f] * s); return true; }
  if (bhas(id, BF::Double)) { out = c + ivec3(0, (m & 1) ? -1 : 1, 0); return true; }
  if (reg().block(id).has("container")) {
    ivec3 hs[2];
    if (chestHalves(const_cast<World&>(w), c, hs) == 2) { out = hs[0] == c ? hs[1] : hs[0]; return true; }
  }
  return false;
}
}

int motionAxis(const Motion& m) { return m.face == 2 || m.face == 3 ? 0 : m.face <= 1 ? 1 : 2; }

bool gatherPiece(const World& w, const Motion& m, Piece& out, std::string* why) {
  out.cells.clear();
  auto fail = [&](const char* s) { if (why) *why = s; out.cells.clear(); return false; };
  const ivec3 start = m.machine + faceDir(m.face) * std::max(1, m.reach);
  if (m.reach <= 1 ? !glued(w, m.machine, m.face) : !glueable(w.getBlock(start.x, start.y, start.z))) return fail("nothing is glued to its moving face");
  std::unordered_set<ivec3, CellHash> seen{start, m.machine};
  std::vector<ivec3> q{start};
  for (size_t i = 0; i < q.size(); i++) {
    if (q.size() > (size_t)PIECE_MAX) return fail("more than 512 blocks are glued together");
    const ivec3 c = q[i];
    for (int f = 0; f < 6; f++) {
      const ivec3 n = c + faceDir(f);
      if (seen.count(n) || !glued(w, c, f)) continue;
      seen.insert(n); q.push_back(n);
    }
  }
  if (q.size() > (size_t)PIECE_MAX) return fail("more than 512 blocks are glued together");
  std::unordered_set<ivec3, CellHash> in(q.begin(), q.end());
  for (const ivec3& c : q) {
    ivec3 o;
    if (partnerOf(w, c, o) && !in.count(o) && w.getBlock(o.x, o.y, o.z) == w.getBlock(c.x, c.y, c.z)) return fail("glue both halves of a two-block thing");
  }
  // turned about a sideways axis (a drawbridge, a hatch), a two-block thing would come apart: refused
  if (m.kind == 0 && motionAxis(m) != 0)
    for (const ivec3& c : q) { ivec3 o; if (partnerOf(w, c, o)) return fail("doors, beds and tall plants can't turn on their side"); }
  out.cells = std::move(q);
  return true;
}

namespace {
// the offset from the machine turned `ang` radians about the motion's axis (positive: the quarter-turn sense above)
vec3 turned(const Motion& m, vec3 o, float ang) {
  const float c = std::cos(ang), s = std::sin(ang);
  switch (motionAxis(m)) {
    case 0: return vec3(o.x * c - o.z * s, o.y, o.x * s + o.z * c);    // east -> south
    case 1: return vec3(o.x, o.y * c - o.z * s, o.y * s + o.z * c);    // up -> south
    default: return vec3(o.x * c + o.y * s, o.y * c - o.x * s, o.z);   // up -> east
  }
}
}

ivec3 movedCell(const Motion& m, ivec3 cell, float fraction) {
  if (m.kind == 1) return cell + faceDir(m.face) * (int)std::lround(m.distance * fraction);
  const vec3 o = vec3(cell - m.machine);
  const vec3 r = turned(m, o, m.quarters * (float)M_PI * 0.5f * fraction);
  return m.machine + ivec3((int)std::lround(r.x), (int)std::lround(r.y), (int)std::lround(r.z));
}

bool pathClear(const World& w, const Motion& m, const Piece& p, std::vector<ivec3>* breakable, ivec3* blockedAt) {
  std::unordered_set<ivec3, CellHash> in(p.cells.begin(), p.cells.end()), checked;
  const int steps = m.kind == 1 ? std::max(1, std::abs(m.distance)) : std::max(1, std::abs(m.quarters) * 6);   // (a hinge: every 15 degrees)
  for (const ivec3& c : p.cells)
    for (int i = 1; i <= steps; i++) {
      const ivec3 t = movedCell(m, c, (float)i / steps);
      if (in.count(t) || t == m.machine || !checked.insert(t).second) continue;
      const BlockId id = w.getBlock(t.x, t.y, t.z);
      if (!id) continue;
      if (reg().tLiquid[id] == 1 || reg().tLiquid[id] == 3) continue;   // (water: it goes through, as a gate by a river must; lava stops it)
      if (reg().tReplaceable[id] && !reg().tLiquid[id]) { if (breakable) breakable->push_back(t); continue; }   // (grass, flowers, snow: broken as it goes)
      if (blockedAt) *blockedAt = t;
      return false;
    }
  return true;
}

} // namespace dq
