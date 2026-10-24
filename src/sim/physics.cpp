#include "sim/physics.h"

namespace dq {

void blockBoxes(const World& w, int x, int y, int z, std::vector<AABB>& out) {
  BlockId id = w.getBlockPhys(x, y, z);
  const Registry& R = reg();
  if (!id || !R.tSolid[id]) return;
  vec3 b(x, y, z);
  u8 meta = w.getMeta(x, y, z);
  switch (R.tShape[id]) {
    case ShapeKind::Slab: if (meta & 1) out.push_back({b, b + vec3(1)}); else if (meta & 2) out.push_back({b + vec3(0, 0.5f, 0), b + vec3(1)}); else out.push_back({b, b + vec3(1, 0.5f, 1)}); return;
    case ShapeKind::Carpet: out.push_back({b, b + vec3(1, 1 / 16.f, 1)}); return;
    case ShapeKind::Layer: out.push_back({b, b + vec3(1, (meta & 7) / 8.f, 1)}); return;
    case ShapeKind::Fence: case ShapeKind::Wall: out.push_back({b + vec3(0.375f, 0, 0.375f), b + vec3(0.625f, 1.5f, 0.625f)}); return;
    case ShapeKind::Path: out.push_back({b, b + vec3(1, 15 / 16.f, 1)}); return;
    default: out.push_back({b, b + vec3(1)});
  }
}

static bool collides(const World& w, const AABB& box, std::vector<AABB>& tmp) {
  int x0 = ifloor(box.lo.x), x1 = ifloor(box.hi.x - 1e-4f), y0 = ifloor(box.lo.y) - 1, y1 = ifloor(box.hi.y - 1e-4f), z0 = ifloor(box.lo.z), z1 = ifloor(box.hi.z - 1e-4f);
  for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    tmp.clear(); blockBoxes(w, x, y, z, tmp);
    for (auto& b : tmp) if (b.overlaps(box)) return true;
  }
  return false;
}

// sweep one axis: move as far as possible in small resolved steps
static float sweepAxis(const World& w, vec3& pos, float hw, float h, int axis, float d, std::vector<AABB>& tmp) {
  if (d == 0) return 0;
  vec3 p = pos; p[axis] += d;
  AABB box{p - vec3(hw, 0, hw), p + vec3(hw, h, hw)};
  if (!collides(w, box, tmp)) { pos = p; return d; }
  // binary search the free distance
  float lo = 0, hi = d;
  for (int i = 0; i < 10; i++) {
    float mid = (lo + hi) * 0.5f;
    vec3 q = pos; q[axis] += mid;
    AABB b2{q - vec3(hw, 0, hw), q + vec3(hw, h, hw)};
    if (collides(w, b2, tmp)) hi = mid; else lo = mid;
  }
  pos[axis] += lo;
  return lo;
}

MoveResult moveBox(const World& w, vec3& pos, float hw, float h, vec3 delta, float stepUp) {
  MoveResult r;
  std::vector<AABB> tmp;
  // split large moves so fast falls don't tunnel
  int steps = std::max(1, (int)std::ceil(glm::length(delta) / 0.45f));
  vec3 sd = delta / (float)steps;
  for (int s = 0; s < steps; s++) {
    float my = sweepAxis(w, pos, hw, h, 1, sd.y, tmp);
    if (my != sd.y) { r.hitY = true; if (sd.y < 0) r.onGround = true; }
    for (int axis : {0, 2}) {
      float want = sd[axis];
      float got = sweepAxis(w, pos, hw, h, axis, want, tmp);
      if (got != want) {
        // step up onto low obstacles when on the ground
        bool stepped = false;
        if (stepUp > 0 && (r.onGround || sd.y <= 0)) {
          vec3 up = pos; up.y += stepUp;
          AABB ub{up - vec3(hw, 0, hw), up + vec3(hw, h, hw)};
          if (!collides(w, ub, tmp)) {
            vec3 q = up;
            float g2 = sweepAxis(w, q, hw, h, axis, want - got, tmp);
            if (std::fabs(g2) > 1e-3f) { sweepAxis(w, q, hw, h, 1, -stepUp, tmp); pos = q; stepped = true; }
          }
        }
        if (!stepped) { if (axis == 0) r.hitX = true; else r.hitZ = true; }
      }
    }
  }
  if (!r.onGround) {   // touching the ground without moving down this frame
    vec3 q = pos; q.y -= 0.01f;
    AABB b{q - vec3(hw, 0, hw), q + vec3(hw, h, hw)};
    if (collides(w, b, tmp)) r.onGround = true;
  }
  return r;
}

RayHit raycastBlocks(const World& w, glm::dvec3 origin, vec3 dir, float maxDist, bool hitLiquids) {
  RayHit h;
  const Registry& R = reg();
  glm::dvec3 p = origin;
  ivec3 b((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z));
  ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
  glm::dvec3 tDelta(std::fabs(1.0 / (dir.x ? dir.x : 1e-9)), std::fabs(1.0 / (dir.y ? dir.y : 1e-9)), std::fabs(1.0 / (dir.z ? dir.z : 1e-9)));
  glm::dvec3 tMax;
  for (int i = 0; i < 3; i++) {
    double frac = p[i] - std::floor(p[i]);
    tMax[i] = (step[i] > 0 ? (1 - frac) : frac) * tDelta[i];
  }
  ivec3 n(0);
  double t = 0;
  while (t <= maxDist) {
    BlockId id = w.getBlock(b.x, b.y, b.z);
    if (id) {
      bool liquid = R.tLiquid[id];
      if ((!liquid || hitLiquids) && R.tRender[id] != (u8)Render::None) {
        h.hit = true; h.block = b; h.normal = n; h.dist = (float)t; h.id = id; h.point = vec3(origin + glm::dvec3(dir) * t);
        return h;
      }
    }
    int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
    b[a] += step[a]; t = tMax[a]; tMax[a] += tDelta[a];
    n = ivec3(0); n[a] = -step[a];
  }
  return h;
}

} // namespace dq
