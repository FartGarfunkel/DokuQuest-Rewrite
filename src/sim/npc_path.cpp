// sim/npc_path.cpp — how townsfolk find their way (game/villagers.js npcPath, grown up): an A* over the blocks' real
// collision shapes. A step is a cell where a body can stand: on top of what's below it, or on a low shape in the cell
// itself (a bottom slab, a lift's platform, a carpet, a path block's top is a full block's), with its height clear over
// the footprint it really takes up (a ladder's plate on the wall, a torch, a sign don't block it; a fence post does).
// Ups of a little over a block are jumped; half steps (slabs, stairs) are walked; drops of up to 3 are taken; doors and
// gates are walked through (opened on the way); ladders, vines, ropes and chains are climbed. A path that can't reach the
// goal says so (partial) and ends where it got closest along the way, not at a wall beside the goal.
#include "sim/villagers_internal.h"
#include "sim/physics.h"
#include "world/attach.h"
#include "core/registry.h"
#include <queue>
#include <deque>

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
// and the spans of what's beside the footprint toward each side (0 +x, 1 -x, 2 +z, 3 -z: an open trapdoor's plate, a
// sign, a pane's arm at the cell's edge), which a body crossing that side goes through
struct Cell {
  float lo = 9, hi = -9; u8 f = 0, liquid = 0; bool loaded = true;
  float slo[4] = {9, 9, 9, 9}, shi[4] = {-9, -9, -9, -9};
  float alo = 9, ahi = -9;   // (every box in the cell, footprint or not: a corner cut across on a diagonal)
  bool any() const { return lo <= hi; }
};

struct Finder {
  const World& w; float hw, H;
  bool wide = false;   // a body broader than a block (a golem): it stands on 2 x 2 cells, its middle at their shared corner
  std::unordered_map<i64, Cell> cache;
  std::vector<AABB> boxes;
  Finder(const World& w_, float hw_, float H_) : w(w_), hw(std::clamp(hw_, 0.1f, 0.45f)), H(H_), wide(hw_ > 0.5f) {}
  static i64 key(int x, int y, int z) { return ((i64)(x & 0xFFFFF) << 40) | ((i64)(z & 0xFFFFF) << 20) | (i64)(y & 0xFFFFF); }
  const Cell& cell(int x, int y, int z) {
    auto [it, fresh] = cache.try_emplace(key(x, y, z));
    Cell& c = it->second;
    if (!fresh) return c;
    if (y < 0 || y >= WORLD_H || !w.isLoaded(x, std::max(0, y), z)) { c.loaded = false; c.lo = 0; c.hi = 1; return c; }
    const BlockId id = w.getBlock(x, y, z);
    c.f = id < g_flags.size() ? g_flags[id] : 0;
    c.liquid = reg().tLiquid[id];
    // (the cell's attachments as you find them: a ladder or a vine on the wall beside a torch is climbed, a shut
    // trapdoor over a hole is a floor; their boxes are blockBoxes', world/attach.h)
    const bool parts = w.hasAttachments(x, y, z);
    if (parts && attachClimb(w, x, y, z)) c.f |= F_CLIMB;
    boxes.clear();
    if (c.f & F_DOOR) {   // (walked through: opened on the way — whatever shares its cell isn't)
      if (!parts) return c;
      static thread_local std::vector<PixBox> pb;
      pb.clear(); attachCollideBoxes(w, x, y, z, pb);
      for (auto& p : pb) boxes.push_back({vec3(x, y, z) + vec3(p.x0, p.y0, p.z0) / 16.f, vec3(x, y, z) + vec3(p.x1, p.y1, p.z1) / 16.f});
    } else blockBoxes(w, x, y, z, boxes);
    const float fx0 = x + 0.5f - hw, fx1 = x + 0.5f + hw, fz0 = z + 0.5f - hw, fz1 = z + 0.5f + hw;
    auto side = [&](int k, const AABB& b) { c.slo[k] = std::min(c.slo[k], b.lo.y - y); c.shi[k] = std::max(c.shi[k], b.hi.y - y); };
    for (const AABB& b : boxes) {
      c.alo = std::min(c.alo, b.lo.y - y); c.ahi = std::max(c.ahi, b.hi.y - y);
      const bool midZ = b.hi.z > fz0 && b.lo.z < fz1, midX = b.hi.x > fx0 && b.lo.x < fx1;
      if (midZ && midX) { c.lo = std::min(c.lo, b.lo.y - y); c.hi = std::max(c.hi, b.hi.y - y); continue; }
      if (midZ) { if (b.hi.x > fx1) side(0, b); if (b.lo.x < fx0) side(1, b); }
      if (midX) { if (b.hi.z > fz1) side(2, b); if (b.lo.z < fz0) side(3, b); }
    }
    return c;
  }
  // nothing at all in this column over [y0, y1] (the corner a diagonal step cuts across)
  bool emptySpan(int x, int z, float y0, float y1) {
    for (int y = ifloor(y0) - 1; y <= ifloor(y1); y++) {
      const Cell& c = cell(x, y, z);
      if (!c.loaded || (c.f & (F_DANGER | F_DOOR))) return false;
      if (c.alo <= c.ahi && y + c.ahi > y0 + 0.001f && y + c.alo < y1 - 0.001f) return false;
    }
    return true;
  }
  // nothing at the edge of this column toward side k in the way of a body crossing it over [y0, y1]
  bool crossClear(int x, int z, int k, float y0, float y1) {
    for (int y = ifloor(y0) - 1; y <= ifloor(y1); y++) {
      const Cell& c = cell(x, y, z);
      if (c.slo[k] <= c.shi[k] && y + c.shi[k] > y0 + 0.001f && y + c.slo[k] < y1 - 0.001f) return false;
    }
    return true;
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
// what a walker steps up without a jump: the 0.6 a creature on the ground steps (sim/creature.cpp moveBox). A bed's top
// (9/16) is a step, as you find it, not a jump that wants a jump's headroom under a cottage's low ceiling
constexpr float STEP = 0.61f;
}

// where a body stands at step (x, y, z): its feet's height, or NONE. *climb: on a ladder (or a vine, a rope...), not on
// a floor. *wet: in water.
static float standAt(Finder& F, int x, int y, int z, bool* climb, int* wet) {
  const Cell& a = F.cell(x, y, z);
  const Cell& b = F.cell(x, y - 1, z);
  if (!a.loaded || !b.loaded) return NONE;
  if ((a.f | b.f) & F_DANGER) return NONE;
  float f = NONE;
  *climb = false; *wet = 0;
  if (a.any()) {   // a low shape in the cell itself: a bottom slab, a lift's platform, a carpet, a snow layer
    if (a.hi <= 0.6f && a.lo < 0.3f && !(a.f & F_NOSTAND)) f = y + a.hi;
    else return NONE;
  } else if (b.any() && b.hi > 0.6f && !(b.f & (F_NOSTAND | F_DOOR))) f = (y - 1) + std::min(b.hi, 1.f);
  else if (b.liquid == 1 || a.liquid == 1) {   // (out of their depth: swimming)
    // (on top of water a block deep, with a floor under it: no place to swim, they stand on the bottom, wading. Taken for
    // a swim it hid every ford and flooded street's cell below it: the highest step in a column is the one taken)
    if (!a.liquid) { const Cell& u = F.cell(x, y - 2, z); if (u.loaded && u.any() && u.hi > 0.6f && !u.liquid) return NONE; }
    f = (float)y; *wet = 2;
  }
  else if ((a.f & F_CLIMB) || (b.f & F_CLIMB)) { f = (float)y; *climb = true; }
  if (f == NONE) return NONE;
  if (a.liquid == 1 && !*wet) *wet = F.cell(x, y + 1, z).liquid == 1 ? 2 : 1;   // (on the bottom: wading if it's below the waist)
  if (!F.clearSpan(x, z, f + 0.02f, f + F.H)) {
    return NONE;
  }
  return f;
}

// a step for the body as it is: one cell, or (broad) the 2 x 2 cells x..x+1, z..z+1 all standing level enough and clear
static float stand(Finder& F, int x, int y, int z, bool* climb, int* wet) {
  if (!F.wide) return standAt(F, x, y, z, climb, wet);
  float f = NONE, lo = 1e9f; int wmax = 0;
  for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
    bool cl; int wt;
    const float g = standAt(F, x + i, y, z + j, &cl, &wt);
    if (g == NONE || cl) return NONE;
    f = std::max(f, g); lo = std::min(lo, g); wmax = std::max(wmax, wt);
  }
  if (f - lo > STEP) return NONE;   // (not straddling a step)
  for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) if (!F.clearSpan(x + i, z + j, f + 0.02f, f + F.H)) return NONE;
  *climb = false; *wet = wmax;
  return f;
}
static bool clearCols(Finder& F, int x, int z, float y0, float y1) {
  if (!F.wide) return F.clearSpan(x, z, y0, y1);
  for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) if (!F.clearSpan(x + i, z + j, y0, y1)) return false;
  return true;
}

std::vector<NpcStep> npcPath(const World& w, vec3 from, vec3 to, float bodyW, float bodyH, bool* partial, int maxNodes, bool* snapped) {
  tables();
  const int RR = maxNodes > 4000 ? 110 : 56;   // (a bigger search looks further: across a city)
  std::vector<NpcStep> none;
  if (partial) *partial = true;
  if (snapped) *snapped = false;
  Finder F(w, bodyW, std::max(1.f, bodyH));
  // (a broad body's step is the corner its middle stands on: the cell to its north-west names it)
  const int off = F.wide ? 1 : 0;
  auto cellOf = [&](float v) { return F.wide ? ifloor(v + 0.5f) - 1 : ifloor(v); };
  int sx = cellOf(from.x), sz = cellOf(from.z);
  int tx = cellOf(to.x), tz = cellOf(to.z);
  (void)off;
  int sy = ifloor(from.y + 0.45f);
  int ty = ifloor(to.y + 0.45f);
  {   // a goal nobody can stand at (out over the water, inside a block, up in the air): the nearest place by it they can
    bool cl; int wt; bool ok = false;
    for (int dy : {0, 1, -1}) if (stand(F, tx, ty + dy, tz, &cl, &wt) != NONE && wt != 2) { ok = true; break; }   // (out of their depth isn't a place to stand: they never swim out to it)
    if (!ok) {
      float bd = 1e9f; int bx = tx, by = ty, bz = tz;
      for (int r = 1; r <= 3 && bd > 1e8f; r++)
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
          if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
          for (int dy : {0, 1, -1, 2, -2}) {
            const float f = stand(F, tx + dx, ty + dy, tz + dz, &cl, &wt);
            if (f == NONE || cl || wt == 2) continue;
            const float d = (float)(dx * dx + dz * dz) + 0.5f * dy * dy;
            if (d < bd) { bd = d; bx = tx + dx; by = ty + dy; bz = tz + dz; }
            break;
          }
        }
      if (bd < 1e8f) { tx = bx; ty = by; tz = bz; if (snapped) *snapped = true; }
    }
  }
  bool c0; int w0;
  float f0 = stand(F, sx, sy, sz, &c0, &w0);
  if (f0 == NONE && F.wide) {   // (a broad body off the grid of corners: the nearest corner by it that holds it)
    for (int r = 1; r <= 2 && f0 == NONE; r++)
      for (int dz = -r; dz <= r && f0 == NONE; dz++) for (int dx = -r; dx <= r && f0 == NONE; dx++) {
        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
        for (int dy : {0, 1, -1}) if ((f0 = stand(F, sx + dx, sy + dy, sz + dz, &c0, &w0)) != NONE) { sx += dx; sy += dy; sz += dz; break; }
      }
    // (wedged where no 2 x 2 holds it: no way at its width. It doesn't squeeze on as a narrower body would — that took
    // golems into gardens and alleys they couldn't turn round in; it waits to be set on open ground (sim/villager_ai.cpp))
    if (f0 == NONE) return none;
  }
  if (f0 == NONE) {   // (mid-step, mid-jump, on a ladder: the nearest step that will hold them)
    for (int dy : {1, -1, 2, -2}) if ((f0 = stand(F, sx, sy + dy, sz, &c0, &w0)) != NONE) { sy += dy; break; }
    if (f0 == NONE) f0 = (float)sy;
  }
  const bool startWet = w0 != 0;
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
        bool cl; int wt;
        const float nf = stand(F, nx, ny, nz, &cl, &wt);
        if (nf == NONE) continue;
        // (the highest step in that column that holds is the one: they can't walk under a ledge they'd step onto)
        const float rise = nf - c.floor;
        if (rise > 1.15f || rise < -3.2f) break;
        if (cl && rise < -0.6f) break;   // (not dropped onto a ladder from beside it: climbed down it from above)
        if (diag && std::fabs(rise) > STEP) break;   // (no jumping or dropping across a corner)
        const bool nDoor = F.cell(nx, ny, nz).f & F_DOOR;
        if (diag && nDoor) break;
        // room on the way: a jump needs headroom where they jump from, a drop room in the column they drop down
        const float top = std::max(nf, c.floor);
        if (rise > STEP && !clearCols(F, c.x, c.z, c.floor + 0.02f, nf + F.H)) break;
        if (rise < -0.05f && !clearCols(F, nx, nz, nf + 0.02f, c.floor + F.H)) break;
        if (diag && !F.wide && (!F.emptySpan(c.x + dx, c.z, top + 0.02f, top + F.H) || !F.emptySpan(c.x, c.z + dz, top + 0.02f, top + F.H))) break;
        // (nor across a corner over a sheer drop: the walker's own guard at an edge stops them there, facing the void, for
        // good — guards stood on a cliff's lip by the town's wall that way)
        if (diag && !F.wide && (sheerDrop(F.w, c.x + dx, ifloor(top), c.z) || sheerDrop(F.w, c.x, ifloor(top), c.z + dz))) break;
        if (diag && F.wide) {   // (a broad body across a corner: both the steps beside it hold it too)
          bool c1, c2; int w1, w2;
          if (stand(F, c.x + dx, c.y, c.z, &c1, &w1) == NONE || stand(F, c.x, c.y, c.z + dz, &c2, &w2) == NONE) break;
        }
        // (and nothing at the edges they cross: a plate, a shutter, a sign)
        const int kx = dx > 0 ? 0 : 1, kz = dz > 0 ? 2 : 3;
        if (!F.wide && dx && (!F.crossClear(c.x, c.z, kx, top + 0.02f, top + F.H) || !F.crossClear(nx, nz, kx ^ 1, top + 0.02f, top + F.H))) break;
        if (!F.wide && dz && (!F.crossClear(c.x, c.z, kz, top + 0.02f, top + F.H) || !F.crossClear(nx, nz, kz ^ 1, top + 0.02f, top + F.H))) break;
        float cost = diag ? 1.414f : 1.f;
        if (rise > STEP) cost += 0.8f;          // a jump
        else if (rise > 0.05f) cost += 0.15f;    // a half step
        if (rise < -0.55f) cost += 0.35f * -rise;
        if (F.cell(nx, ny - 1, nz).f & F_FARM) cost += 4;   // keep off the crops
        // never into a fountain or a pond out of their depth (only out of one); a ford waded, if there's no way round
        if (wt == 2) { if (!startWet) break; cost += 14; }
        else if (wt == 1) cost += 6;
        if (cl) cost += 0.5f;
        push(ci, nx, ny, nz, nf, cost, cl);
        break;
      }
    }
    // up and down a ladder (a vine, a rope, a chain): straight up the climbable column, and off the top
    const Cell& here = F.cell(c.x, c.y, c.z);
    const Cell& below = F.cell(c.x, c.y - 1, c.z);
    if (!F.wide && (here.f & F_CLIMB) && F.clearSpan(c.x, c.z, c.y + 1.02f, c.y + 1 + F.H)) {
      bool cl; int wt; const float nf = standAt(F, c.x, c.y + 1, c.z, &cl, &wt);
      if (nf != NONE) push(ci, c.x, c.y + 1, c.z, nf, 1.6f, true);
    }
    if (!F.wide && (below.f & F_CLIMB)) {
      bool cl; int wt; const float nf = standAt(F, c.x, c.y - 1, c.z, &cl, &wt);
      if (nf != NONE && nf < c.floor - 0.5f) push(ci, c.x, c.y - 1, c.z, nf, 1.4f, true);
    }
  }
  if (partial) *partial = !reached;
  if (best == 0) return none;
  std::vector<NpcStep> out;
  for (int c = best; c > 0; c = nodes[c].parent) out.push_back(NpcStep{ivec3(nodes[c].x, nodes[c].y, nodes[c].z), nodes[c].floor, nodes[c].climb, F.wide ? vec2(nodes[c].x + 1.f, nodes[c].z + 1.f) : vec2(nodes[c].x + 0.5f, nodes[c].z + 0.5f)});
  std::reverse(out.begin(), out.end());
  return out;
}

float npcStandAt(const World& w, int x, int y, int z, float bodyW, float bodyH) {
  tables();
  Finder F(w, bodyW, bodyH); bool cl; int wt;
  const float f = stand(F, x, y, z, &cl, &wt);
  return f == NONE || cl || wt == 2 ? NAN : f;   // (out of their depth: nowhere to stand)
}

bool npcOpenGround(const World& w, vec3 near, vec3 square, float bodyW, float bodyH, vec3& out, int maxR) {
  tables();
  const bool wide = bodyW > 0.5f;
  const int cx = wide ? ifloor(near.x + 0.5f) - 1 : ifloor(near.x), cz = wide ? ifloor(near.z + 0.5f) - 1 : ifloor(near.z), cy = ifloor(near.y + 0.45f);
  int tried = 0;
  for (int r = 0; r <= maxR; r++)
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
      const int x = cx + dx, z = cz + dz;
      if (!w.isLoaded(x, cy, z) || !w.isLoaded(x + 1, cy, z + 1)) continue;
      for (int dy : {0, 1, -1, 2, -2, 3, -3}) {
        const float f = npcStandAt(w, x, cy + dy, z, bodyW, bodyH);
        if (std::isnan(f)) continue;
        if (Finder F(w, bodyW, bodyH); [&] { bool cl; int wt; stand(F, x, cy + dy, z, &cl, &wt); return wt != 0; }()) break;   // (not in the water)
        const vec3 at = wide ? vec3(x + 1.f, f, z + 1.f) : vec3(x + 0.5f, f, z + 0.5f);
        // (and a way from there to the square at their width: open ground, not a pocket between the stalls)
        if (++tried > 40) return false;
        bool partial = true;
        const auto way = npcPath(w, at, square, bodyW, bodyH, &partial, 4000);
        if (!partial) { out = at; return true; }
        break;
      }
    }
  return false;
}

std::shared_ptr<const NpcReach> npcBroadReach(const World& w, vec3 square, float bodyW, float bodyH, int radius) {
  tables();
  auto R = std::make_shared<NpcReach>();
  Finder F(w, bodyW, std::max(1.f, bodyH));
  auto cellOf = [&](float v) { return F.wide ? ifloor(v + 0.5f) - 1 : ifloor(v); };
  int sx = cellOf(square.x), sz = cellOf(square.z), sy = ifloor(square.y + 0.45f);
  bool cl; int wt; float f0 = NONE;
  // (the square's middle may be a fountain or a well: the nearest step by it that holds the body)
  for (int r = 0; r <= 4 && f0 == NONE; r++)
    for (int dz = -r; dz <= r && f0 == NONE; dz++) for (int dx = -r; dx <= r && f0 == NONE; dx++) {
      if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
      for (int dy : {0, 1, -1, 2, -2}) { const float f = stand(F, sx + dx, sy + dy, sz + dz, &cl, &wt); if (f != NONE && wt != 2) { f0 = f; sx += dx; sy += dy; sz += dz; break; } }
    }
  if (f0 == NONE) return R;
  const int ox = sx, oz = sz;
  struct Q { int x, y, z; float f; };
  std::deque<Q> q; q.push_back({sx, sy, sz, f0}); R->at[NpcReach::key(sx, sz)] = sy;
  static const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  while (!q.empty() && R->at.size() < 60000) {
    const Q c = q.front(); q.pop_front();
    for (auto& d : DIRS) {
      const int nx = c.x + d[0], nz = c.z + d[1];
      if (std::abs(nx - ox) > radius || std::abs(nz - oz) > radius || R->at.count(NpcReach::key(nx, nz))) continue;
      const bool diag = d[0] && d[1];
      for (int ny = c.y + 1; ny >= c.y - 3; ny--) {
        const float nf = stand(F, nx, ny, nz, &cl, &wt);
        if (nf == NONE) continue;
        const float rise = nf - c.f;
        if (rise > 1.15f || rise < -3.2f || wt == 2) break;
        if (diag && std::fabs(rise) > STEP) break;
        if (rise > STEP && !clearCols(F, c.x, c.z, c.f + 0.02f, nf + F.H)) break;
        if (rise < -0.05f && !clearCols(F, nx, nz, nf + 0.02f, c.f + F.H)) break;
        if (diag) { bool c1, c2; int w1, w2; if (stand(F, c.x + d[0], c.y, c.z, &c1, &w1) == NONE || stand(F, c.x, c.y, c.z + d[1], &c2, &w2) == NONE) break; }
        R->at[NpcReach::key(nx, nz)] = ny; q.push_back({nx, ny, nz, nf});
        break;
      }
    }
  }
  R->wide = F.wide;
  for (auto& [k, y] : R->at) R->cells.push_back(k);
  return R;
}

} // namespace dq
