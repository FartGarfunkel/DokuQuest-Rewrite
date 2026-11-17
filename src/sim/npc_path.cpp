// sim/npc_path.cpp — how townsfolk find their way (game/villagers.js npcPath, grown up): an A* over the blocks' real
// collision shapes. A step is a cell where a body can stand: on top of what's below it, or on a low shape in the cell
// itself (a bottom slab, a lift's platform, a carpet, a path block's top is a full block's), with its height clear over
// the footprint it really takes up (a ladder's plate on the wall, a torch, a sign don't block it; a fence post does).
// Ups of a little over a block are jumped; half steps (slabs, stairs) are walked; drops of up to 3 are taken; doors and
// gates are walked through (opened on the way); ladders, vines, ropes and chains are climbed. A path that can't reach the
// goal says so (partial) and ends where it got closest along the way, not at a wall beside the goal.
#include "sim/villagers_internal.h"
#include "sim/physics.h"
#include "core/registry.h"
#include <queue>

namespace dq {

namespace {
std::vector<u8> g_flags;   // per block id: what the way finding needs to know of it
enum : u8 { F_DOOR = 1, F_DANGER = 2, F_CLIMB = 4, F_NOSTAND = 8, F_FARM = 16 };
void tables() {
  const Registry& R = reg();
  if (g_flags.size() == R.blocks.size()) return;
  g_flags.assign(R.blocks.size(), 0);
  for (auto& b : R.blocks) {
    const std::string& k = b.key;
    u8 f = 0;
    if ((R.tShape[b.id] == ShapeKind::Door && k.find("iron") == std::string::npos) || R.tShape[b.id] == ShapeKind::Gate) f |= F_DOOR;
    if (b.has("fire") || k == "lava" || k == "cactus" || k == "magma_block" || k == "sweet_berry_bush" || k == "wither_rose" || k == "campfire" || k == "soul_campfire" || k == "fire") f |= F_DANGER;
    if (R.tShape[b.id] == ShapeKind::Fence || R.tShape[b.id] == ShapeKind::Wall) f |= F_NOSTAND;
    if (b.has("climb")) f |= F_CLIMB;
    if (k == "farmland") f |= F_FARM;
    g_flags[b.id] = f;
  }
}

// one cell as the way finder sees it: the span of its collision boxes over a body's footprint in it (relative to the
// cell's floor; none: lo > hi), and its flags
struct Cell { float lo = 9, hi = -9; u8 f = 0, liquid = 0; bool loaded = true; bool any() const { return lo <= hi; } };

struct Finder {
  const World& w; float hw, H;
  std::unordered_map<i64, Cell> cache;
  std::vector<AABB> boxes;
  Finder(const World& w_, float hw_, float H_) : w(w_), hw(std::clamp(hw_, 0.1f, 0.45f)), H(H_) {}
  static i64 key(int x, int y, int z) { return ((i64)(x & 0xFFFFF) << 40) | ((i64)(z & 0xFFFFF) << 20) | (i64)(y & 0xFFFFF); }
  const Cell& cell(int x, int y, int z) {
    auto [it, fresh] = cache.try_emplace(key(x, y, z));
    Cell& c = it->second;
    if (!fresh) return c;
    if (y < 0 || y >= WORLD_H || !w.isLoaded(x, std::max(0, y), z)) { c.loaded = false; c.lo = 0; c.hi = 1; return c; }
    const BlockId id = w.getBlock(x, y, z);
    c.f = id < g_flags.size() ? g_flags[id] : 0;
    c.liquid = reg().tLiquid[id];
    if (c.f & F_DOOR) return c;   // (walked through: opened on the way)
    boxes.clear(); blockBoxes(w, x, y, z, boxes);
    const float fx0 = x + 0.5f - hw, fx1 = x + 0.5f + hw, fz0 = z + 0.5f - hw, fz1 = z + 0.5f + hw;
    for (const AABB& b : boxes) {
      if (b.hi.x <= fx0 || b.lo.x >= fx1 || b.hi.z <= fz0 || b.lo.z >= fz1) continue;
      c.lo = std::min(c.lo, b.lo.y - y); c.hi = std::max(c.hi, b.hi.y - y);
    }
    return c;
  }
  // nothing in the way of a body over [y0, y1] in this column (doors don't count: they're opened)
  bool clearSpan(int x, int z, float y0, float y1) {
    for (int y = ifloor(y0) - 1; y <= ifloor(y1); y++) {
      const Cell& c = cell(x, y, z);
      if (!c.loaded) return false;
      if (c.f & F_DANGER) return false;
      if (c.any() && y + c.hi > y0 + 0.001f && y + c.lo < y1 - 0.001f) return false;
    }
    return true;
  }
};

constexpr float NONE = -1e9f;
}

// where a body stands at step (x, y, z): its feet's height, or NONE. *climb: on a ladder (or a vine, a rope...), not on
// a floor. *wet: in water.
static float standAt(Finder& F, int x, int y, int z, bool* climb, bool* wet) {
  const Cell& a = F.cell(x, y, z);
  const Cell& b = F.cell(x, y - 1, z);
  if (!a.loaded || !b.loaded) return NONE;
  if ((a.f | b.f) & F_DANGER) return NONE;
  float f = NONE;
  *climb = false; *wet = false;
  if (a.any()) {   // a low shape in the cell itself: a bottom slab, a lift's platform, a carpet, a snow layer
    if (a.hi <= 0.6f && a.lo < 0.3f && !(a.f & F_NOSTAND)) f = y + a.hi;
    else return NONE;
  } else if (b.any() && b.hi > 0.6f && !(b.f & (F_NOSTAND | F_DOOR))) f = (y - 1) + std::min(b.hi, 1.f);
  else if (b.liquid == 1 || a.liquid == 1) { f = (float)y; *wet = true; }
  else if ((a.f & F_CLIMB) || (b.f & F_CLIMB)) { f = (float)y; *climb = true; }
  if (f == NONE) return NONE;
  if (a.liquid == 1) *wet = true;
  if (!F.clearSpan(x, z, f + 0.02f, f + F.H)) {
    return NONE;
  }
  return f;
}

std::vector<NpcStep> npcPath(const World& w, vec3 from, vec3 to, float bodyW, float bodyH, bool* partial, int maxNodes) {
  tables();
  const int RR = 56;
  std::vector<NpcStep> none;
  if (partial) *partial = true;
  Finder F(w, bodyW, std::max(1.f, bodyH));
  const int sx = ifloor(from.x), sz = ifloor(from.z), tx = ifloor(to.x), tz = ifloor(to.z);
  int sy = ifloor(from.y + 0.45f);
  const int ty = ifloor(to.y + 0.45f);
  bool c0, w0;
  float f0 = standAt(F, sx, sy, sz, &c0, &w0);
  if (f0 == NONE) {   // (mid-step, mid-jump, on a ladder: the nearest step that will hold them)
    for (int dy : {1, -1, 2, -2}) if ((f0 = standAt(F, sx, sy + dy, sz, &c0, &w0)) != NONE) { sy += dy; break; }
    if (f0 == NONE) f0 = (float)sy;
  }
  const bool startWet = w0;
  struct Node { int x, y, z; float g, f, floor; int parent; bool closed, climb; };
  std::vector<Node> nodes; nodes.reserve(maxNodes * 3);
  std::unordered_map<i64, int> seen;
  auto key = [&](int x, int y, int z) { return Finder::key(x, y, z); };
  // the goal's distance: across the land, and up or down (a little more: a floor's worth of stairs is a walk)
  auto hfn = [&](int x, int y, int z) { float dx = std::abs(x - tx), dz = std::abs(z - tz); return std::max(dx, dz) + 0.414f * std::min(dx, dz) + std::abs(y - ty) * 1.2f; };
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
  nodes.push_back({sx, sy, sz, 0, hfn(sx, sy, sz), f0, -1, false, c0});
  seen[key(sx, sy, sz)] = 0; open.push({nodes[0].f, 0});
  int best = 0; float bestH = hfn(sx, sy, sz); int n = 0; bool reached = false;
  static const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  auto push = [&](int ci, int nx, int ny, int nz, float nf, float cost, bool climb) {
    const i64 k = key(nx, ny, nz); const float g2 = nodes[ci].g + cost;
    auto it = seen.find(k);
    if (it != seen.end() && (nodes[it->second].closed || nodes[it->second].g <= g2)) return;
    nodes.push_back({nx, ny, nz, g2, g2 + hfn(nx, ny, nz) * 1.1f, nf, ci, false, climb});
    seen[k] = (int)nodes.size() - 1; open.push({nodes.back().f, (int)nodes.size() - 1});
  };
  while (!open.empty() && n++ < maxNodes) {
    const int ci = open.top().second; open.pop();
    if (nodes[ci].closed) continue;
    nodes[ci].closed = true;
    const Node c = nodes[ci];
    const float hc = hfn(c.x, c.y, c.z);
    if (hc < bestH) { bestH = hc; best = ci; }
    if (c.x == tx && c.z == tz && std::abs(c.y - ty) <= 1) { best = ci; reached = true; break; }
    const bool cDoor = F.cell(c.x, c.y, c.z).f & F_DOOR;
    for (auto& dd : DIRS) {
      const int dx = dd[0], dz = dd[1], nx = c.x + dx, nz = c.z + dz;
      if (std::abs(nx - sx) >= RR || std::abs(nz - sz) >= RR) continue;
      const bool diag = dx && dz;
      if (diag && (c.climb || cDoor)) continue;
      // the step there: level, up a step or a jump (to a little over a block), or down a drop of up to 3
      for (int ny = c.y + 1; ny >= c.y - 4; ny--) {
        bool cl, wt;
        const float nf = standAt(F, nx, ny, nz, &cl, &wt);
        if (nf == NONE) continue;
        // (the highest step in that column that holds is the one: they can't walk under a ledge they'd step onto)
        const float rise = nf - c.floor;
        if (rise > 1.15f || rise < -3.2f) break;
        if (cl && rise < -0.6f) break;   // (not dropped onto a ladder from beside it: climbed down it from above)
        if (diag && std::fabs(rise) > 0.55f) break;   // (no jumping or dropping across a corner)
        const bool nDoor = F.cell(nx, ny, nz).f & F_DOOR;
        if (diag && nDoor) break;
        // room on the way: a jump needs headroom where they jump from, a drop room in the column they drop down
        const float top = std::max(nf, c.floor);
        if (rise > 0.55f && !F.clearSpan(c.x, c.z, c.floor + 0.02f, nf + F.H)) break;
        if (rise < -0.05f && !F.clearSpan(nx, nz, nf + 0.02f, c.floor + F.H)) break;
        if (diag && (!F.clearSpan(c.x + dx, c.z, top + 0.02f, top + F.H) || !F.clearSpan(c.x, c.z + dz, top + 0.02f, top + F.H)
                     || (F.cell(c.x + dx, ny, c.z).f & F_DOOR) || (F.cell(c.x, ny, c.z + dz).f & F_DOOR))) break;
        float cost = diag ? 1.414f : 1.f;
        if (rise > 0.55f) cost += 0.8f;          // a jump
        else if (rise > 0.05f) cost += 0.15f;    // a half step
        if (rise < -0.55f) cost += 0.35f * -rise;
        if (F.cell(nx, ny - 1, nz).f & F_FARM) cost += 4;   // keep off the crops
        if (wt) { if (!startWet) break; cost += 14; }     // never into a fountain or a pond (only out of one)
        if (cl) cost += 0.5f;
        push(ci, nx, ny, nz, nf, cost, cl);
        break;
      }
    }
    // up and down a ladder (a vine, a rope, a chain): straight up the climbable column, and off the top
    const Cell& here = F.cell(c.x, c.y, c.z);
    const Cell& below = F.cell(c.x, c.y - 1, c.z);
    if ((here.f & F_CLIMB) && F.clearSpan(c.x, c.z, c.y + 1.02f, c.y + 1 + F.H)) {
      bool cl, wt; const float nf = standAt(F, c.x, c.y + 1, c.z, &cl, &wt);
      if (nf != NONE) push(ci, c.x, c.y + 1, c.z, nf, 1.6f, true);
    }
    if (below.f & F_CLIMB) {
      bool cl, wt; const float nf = standAt(F, c.x, c.y - 1, c.z, &cl, &wt);
      if (nf != NONE && nf < c.floor - 0.5f) push(ci, c.x, c.y - 1, c.z, nf, 1.4f, true);
    }
  }
  if (partial) *partial = !reached;
  if (best == 0) return none;
  std::vector<NpcStep> out;
  for (int c = best; c > 0; c = nodes[c].parent) out.push_back(NpcStep{ivec3(nodes[c].x, nodes[c].y, nodes[c].z), nodes[c].floor, nodes[c].climb});
  std::reverse(out.begin(), out.end());
  return out;
}

float npcStandAt(const World& w, int x, int y, int z, float bodyW, float bodyH) {
  tables();
  Finder F(w, bodyW, bodyH); bool cl, wt;
  const float f = standAt(F, x, y, z, &cl, &wt);
  return f == NONE || cl ? NAN : f;
}

} // namespace dq
