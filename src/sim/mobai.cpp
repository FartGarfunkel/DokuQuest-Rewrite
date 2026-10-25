// sim/mobai.cpp — finding a way on foot (game/mobai.js).
// A* over the block grid: creatures walk, step up one block (with a hop), drop down up to three, cut
// corners only where both sides are clear, wade through water (slowly) and keep out of lava, fire and
// cactus. Searches are capped, and if the goal can't be reached they head for the nearest spot they could
// get to, so they still come as close as they can.
#include "sim/mobai.h"
#include "sim/creature.h"
#include "sim/physics.h"
#include "core/registry.h"
#include <queue>

namespace dq {

static std::vector<u8> navDanger, navNoStand;
static void navTables() {
  const Registry& R = reg();
  if (navDanger.size() == R.blocks.size()) return;
  navDanger.assign(R.blocks.size(), 0); navNoStand.assign(R.blocks.size(), 0);
  for (auto& b : R.blocks) {
    const std::string& k = b.key;
    if (b.has("fire") || k == "lava" || k == "cactus" || k == "magma_block" || k == "sweet_berry_bush" || k == "wither_rose" || k == "campfire" || k == "soul_campfire" || k == "fire") navDanger[b.id] = 1;
    if (R.tShape[b.id] == ShapeKind::Fence || R.tShape[b.id] == ShapeKind::Wall) navNoStand[b.id] = 1;
  }
}

std::vector<ivec3> findPath(const World& w, ivec3 s, ivec3 t, const PathOpts& o) {
  navTables();
  const Registry& R = reg();
  const int H = std::max(1, o.h), RAD = 40;
  std::vector<ivec3> none;
  if (std::abs(t.x - s.x) > RAD - 2 || std::abs(t.z - s.z) > RAD - 2) return none;
  const bool small = o.w <= 0.45f;
  auto doorish = [&](BlockId id) { return small && (R.tShape[id] == ShapeKind::Door || R.tShape[id] == ShapeKind::Gate); };
  std::vector<AABB> tmp;
  auto doorOK = [&](int x, int y, int z, int ax) {
    if (ax < 0) return false;
    tmp.clear(); blockBoxes(w, x, y, z, tmp);
    if (tmp.empty()) return true;
    for (auto& b : tmp) {
      float lo = (ax == 0 ? b.lo.z - z : b.lo.x - x) * 16, hi = (ax == 0 ? b.hi.z - z : b.hi.x - x) * 16;
      if (hi - lo > 4 || (lo > 0.01f && hi < 15.99f)) return false;
    }
    return true;
  };
  auto pass = [&](int x, int y, int z, int ax) { BlockId id = w.getBlock(x, y, z); return R.tSolid[id] ? doorish(id) && doorOK(x, y, z, ax) : !navDanger[id]; };
  auto clear = [&](int x, int y, int z, int ax) { for (int k = 0; k < H; k++) if (!pass(x, y + k, z, ax)) return false; return true; };
  auto inDoor = [&](int x, int y, int z) { for (int k = 0; k < H; k++) if (doorish(w.getBlock(x, y + k, z))) return true; return false; };
  auto water = [&](int x, int y, int z) { return R.tLiquid[w.getBlock(x, y, z)] == 1; };
  auto ground = [&](int x, int y, int z) { BlockId id = w.getBlock(x, y - 1, z); return (R.tSolid[id] && !navNoStand[id] && !navDanger[id]) || (o.swim && water(x, y - 1, z)) || (o.swim && water(x, y, z)); };
  auto stand = [&](int x, int y, int z, int ax) { return y > 0 && y < WORLD_H - H && clear(x, y, z, ax) && ground(x, y, z); };
  auto hfn = [&](int x, int y, int z) { float dx = std::abs(x - t.x), dz = std::abs(z - t.z); return std::max(dx, dz) + 0.414f * std::min(dx, dz) + std::abs(y - t.y) * 0.6f; };
  struct Node { int x, y, z; float g, f; int parent; bool closed; };
  std::vector<Node> nodes; nodes.reserve(o.maxNodes * 4);
  std::unordered_map<i64, int> seen;
  auto key = [&](int x, int y, int z) { return (i64)(x - s.x + RAD) | ((i64)(z - s.z + RAD) << 8) | ((i64)y << 16); };
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
  nodes.push_back({s.x, s.y, s.z, 0, hfn(s.x, s.y, s.z), -1, false});
  seen[key(s.x, s.y, s.z)] = 0; open.push({nodes[0].f, 0});
  int best = 0; float bestH = nodes[0].f; int n = 0;
  static const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  while (!open.empty() && n++ < o.maxNodes) {
    int ci = open.top().second; open.pop();
    if (nodes[ci].closed) continue;
    nodes[ci].closed = true;
    Node c = nodes[ci];
    float hc = hfn(c.x, c.y, c.z);
    if (hc < bestH) { bestH = hc; best = ci; }
    if (c.x == t.x && c.z == t.z && std::abs(c.y - t.y) <= 1) { best = ci; break; }
    for (auto& d : DIRS) {
      int dx = d[0], dz = d[1], nx = c.x + dx, nz = c.z + dz;
      if (std::abs(nx - s.x) >= RAD || std::abs(nz - s.z) >= RAD) continue;
      if (dx && dz && (!clear(c.x + dx, c.y, c.z, -1) || !clear(c.x, c.y, c.z + dz, -1))) continue;   // no squeezing past corners
      int ax = dx && dz ? -1 : dx ? 0 : 1;
      if (small && inDoor(c.x, c.y, c.z) && !clear(c.x, c.y, c.z, ax)) continue;               // out of a doorway only the way it opens
      int ny = -1; float cost = dx && dz ? 1.414f : 1;
      if (stand(nx, c.y, nz, ax)) ny = c.y;
      else if (!dx || !dz) {
        if (pass(c.x, c.y + H, c.z, -1) && stand(nx, c.y + 1, nz, ax)) { ny = c.y + 1; cost += 0.6f; }   // hop up one
        else if (clear(nx, c.y, nz, ax)) {
          for (int dd = 1; dd <= o.maxDrop; dd++) { if (stand(nx, c.y - dd, nz, ax)) { ny = c.y - dd; cost += 0.3f * dd; break; } else if (!pass(nx, c.y - dd, nz, ax)) break; }
        }
      }
      if (ny < 0) continue;
      if (water(nx, ny, nz)) cost += 1.5f;
      i64 k = key(nx, ny, nz); float g2 = c.g + cost;
      auto it = seen.find(k);
      if (it != seen.end() && (nodes[it->second].closed || nodes[it->second].g <= g2)) continue;
      nodes.push_back({nx, ny, nz, g2, g2 + hfn(nx, ny, nz) * 1.1f, ci, false});
      int ni = (int)nodes.size() - 1;
      seen[k] = ni; open.push({nodes[ni].f, ni});
    }
  }
  if (best == 0) return none;
  std::vector<ivec3> out;
  for (int c = best; c > 0; c = nodes[c].parent) out.push_back({nodes[c].x, nodes[c].y, nodes[c].z});
  std::reverse(out.begin(), out.end());
  return out;
}

// Which way to head to reach `to` on foot: follows a path, refreshed now and then. nullopt to go straight.
std::optional<float> Creature::navDir(Level& lv, vec3 to, float dt) {
  navT -= dt;
  bool moved = !hasNavGoal || std::hypot(to.x - navGoal.x, to.z - navGoal.z) > 1.5f || std::fabs(to.y - navGoal.y) > 1.5f;
  if (navT <= 0 || moved || !hasPath) {
    navT = 0.6f + frand() * 0.4f;
    PathOpts o; o.h = std::max(1, (int)std::ceil(h - 0.05f)); o.maxDrop = 3; o.swim = true; o.w = w;
    path = findPath(lv.world(realm), ivec3(ifloor(pos.x), ifloor(pos.y + 0.45f), ifloor(pos.z)), ivec3(ifloor(to.x), ifloor(to.y + 0.45f), ifloor(to.z)), o);
    hasPath = !path.empty(); pathI = 0; navGoal = to; hasNavGoal = true;
  }
  if (!hasPath) return std::nullopt;
  while (pathI < path.size()) {
    ivec3 nd = path[pathI];
    if (std::hypot(nd.x + 0.5f - pos.x, nd.z + 0.5f - pos.z) < 0.35f + w * 0.4f && std::fabs(nd.y - pos.y) < 1.2f) pathI++;
    else break;
  }
  if (pathI >= path.size()) return std::nullopt;
  ivec3 nd = path[pathI];
  if (nd.y - pos.y > 0.55f && onGround) wantJump = true;      // the next spot is a real step up
  // stuck against something: look again soon
  float prog = hasNavLast ? std::hypot(pos.x - navLast.x, pos.z - navLast.y) : 0;
  navLast = vec2(pos.x, pos.z); hasNavLast = true;
  stuckT = prog < dt * 0.3f ? stuckT + dt : 0;
  if (stuckT > 1.2f) { stuckT = 0; navT = 0; if (stepAhead(lv)) wantJump = true; }
  return std::atan2(-(nd.x + 0.5f - pos.x), -(nd.z + 0.5f - pos.z));
}

// Is there a block just ahead that a jump would get it up onto? (one high, room above, not a fence or a wall)
bool Creature::stepAhead(Level& lv) const {
  const World& wd = lv.world(realm);
  const Registry& R = reg();
  float fx = -std::sin(yaw), fz = -std::cos(yaw), r = w + 0.35f;
  int x = ifloor(pos.x + fx * r), z = ifloor(pos.z + fz * r), y = ifloor(pos.y + 0.3f);
  BlockId id = wd.getBlock(x, y, z);
  if (!R.tSolid[id]) return false;
  u8 k = R.tShape[id];
  if (k == ShapeKind::Fence || k == ShapeKind::Wall || k == ShapeKind::Gate) return false;
  for (int dy = 1; dy <= (int)std::ceil(h); dy++) if (R.tSolid[wd.getBlock(x, y + dy, z)]) return false;
  return true;
}

} // namespace dq
