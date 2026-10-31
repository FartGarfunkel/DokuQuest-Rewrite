// sim/physics.cpp — port of game/physics.js (gatherBoxes, clipAxis, moveEntity, sampleContacts, raycastBlocks).
#include "sim/physics.h"
#include "world/shapes.h"
#include "sim/blockinfo.h"
#include <algorithm>

namespace dq {

static thread_local std::vector<PixBox> t_pix;

void gatherBoxes(const World& w, double minX, double minY, double minZ, double maxX, double maxY, double maxZ,
                 std::vector<DBox>& out, double feet, bool sneak) {
  out.clear();
  const Registry& R = reg();
  int x0 = (int)std::floor(minX), x1 = (int)std::floor(maxX - 1e-7);
  int yb = (int)std::floor(minY), y0 = yb - 1, y1 = (int)std::floor(maxY - 1e-7);
  int z0 = (int)std::floor(minZ), z1 = (int)std::floor(maxZ - 1e-7);
  for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    BlockId id = w.getBlockPhys(x, y, z);
    if (!R.tSolid[id]) continue;
    bool carved = fullSolid(id) && w.getCarve(x, y, z);   // carve flag lives in its own byte now
    if (!hasShapeBoxes(id) && !carved) { if (y >= yb) out.push_back({(double)x, (double)y, (double)z, x + 1.0, y + 1.0, z + 1.0}); continue; }
    if (bhas(id, BF::Scaffold) && !std::isnan(feet) && (sneak || feet < y + 1 - 0.05)) continue;
    if (!worldBoxes(w, x, y, z, id, SHAPE_COLLIDE, t_pix)) { if (y >= yb) out.push_back({(double)x, (double)y, (double)z, x + 1.0, y + 1.0, z + 1.0}); continue; }
    for (auto& b : t_pix) {
      DBox d{x + b.x0 / 16.0, y + b.y0 / 16.0, z + b.z0 / 16.0, x + b.x1 / 16.0, y + b.y1 / 16.0, z + b.z1 / 16.0};
      if (d.x1 <= minX || d.x0 >= maxX || d.y1 <= minY || d.y0 >= maxY || d.z1 <= minZ || d.z0 >= maxZ) continue;
      out.push_back(d);
    }
  }
}

bool collides(const World& w, double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
  static thread_local std::vector<DBox> bs;
  gatherBoxes(w, minX, minY, minZ, maxX, maxY, maxZ, bs);
  for (auto& b : bs) if (b.x1 > minX && b.x0 < maxX && b.y1 > minY && b.y0 < maxY && b.z1 > minZ && b.z0 < maxZ) return true;
  return false;
}

void blockBoxes(const World& w, int x, int y, int z, std::vector<AABB>& out) {
  BlockId id = w.getBlockPhys(x, y, z);
  if (!reg().tSolid[id]) return;
  vec3 b(x, y, z);
  bool carved = fullSolid(id) && w.getCarve(x, y, z);
  if ((!hasShapeBoxes(id) && !carved) || !worldBoxes(w, x, y, z, id, SHAPE_COLLIDE, t_pix)) { out.push_back({b, b + vec3(1)}); return; }
  for (auto& p : t_pix) out.push_back({b + vec3(p.x0, p.y0, p.z0) / 16.f, b + vec3(p.x1, p.y1, p.z1) / 16.f});
}

// Sweep one axis against the boxes: how far can we move before touching one?
static double clipAxis(const std::vector<DBox>& boxes, int ax, double d, const double mn[3], const double mx[3]) {
  int o1 = (ax + 1) % 3, o2 = (ax + 2) % 3;
  for (auto& bb : boxes) {
    const double b[6] = {bb.x0, bb.y0, bb.z0, bb.x1, bb.y1, bb.z1};
    if (b[o1 + 3] <= mn[o1] + 1e-7 || b[o1] >= mx[o1] - 1e-7 || b[o2 + 3] <= mn[o2] + 1e-7 || b[o2] >= mx[o2] - 1e-7) continue;
    if (d > 0 && b[ax] >= mx[ax] - 1e-7) d = std::min(d, b[ax] - mx[ax]);
    else if (d < 0 && b[ax + 3] <= mn[ax] + 1e-7) d = std::max(d, b[ax + 3] - mn[ax]);
  }
  return d;
}

// store a double coordinate in a float without crossing into whatever it touches (side: +1 = contact on the + side)
static float storeToward(double v, int side) {
  float f = (float)v;
  if (side > 0 && (double)f > v) f = std::nextafter(f, -INFINITY);
  else if (side < 0 && (double)f < v) f = std::nextafter(f, INFINITY);
  return f;
}

// A body that starts the tick inside something solid (shoved by a crowd or a blow, a block set where it stands, a
// ladder's lip, a float rounded the wrong way) is put back out first, the shortest way that leaves it free (Minecraft's
// pushOutOfBlocks). The sweep below only stops boxes ahead of a body, so without this it could walk on inside a wall.
static void pushOut(const World& w, double& ex, double& ey, double& ez, double ew, double eh, bool sneak) {
  static thread_local std::vector<DBox> bs;
  const double E = 2e-3;
  for (int it = 0; it < 3; it++) {
    double mn[3] = {ex - ew + E, ey + E, ez - ew + E}, mx[3] = {ex + ew - E, ey + eh - E, ez + ew - E};
    gatherBoxes(w, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], bs, ey, sneak);
    double need[6] = {0, 0, 0, 0, 0, 0};   // +x -x +y -y +z -z
    bool any = false;
    for (auto& b : bs) {
      if (!(b.x1 > mn[0] && b.x0 < mx[0] && b.y1 > mn[1] && b.y0 < mx[1] && b.z1 > mn[2] && b.z0 < mx[2])) continue;
      any = true;
      double c[6] = {b.x1 - (ex - ew), (ex + ew) - b.x0, b.y1 - ey, (ey + eh) - b.y0, b.z1 - (ez - ew), (ez + ew) - b.z0};
      for (int k = 0; k < 6; k++) need[k] = std::max(need[k], c[k]);
    }
    if (!any) return;
    int order[6] = {0, 1, 2, 3, 4, 5};
    auto cost = [&](int k) { return need[k] * (k == 3 ? 2.0 : k == 2 ? 0.9 : 1.0); };   // rather up than down
    std::sort(order, order + 6, [&](int a, int b) { return cost(a) < cost(b); });
    static const int AX[6] = {0, 0, 1, 1, 2, 2}; static const double SG[6] = {1, -1, 1, -1, 1, -1};
    bool done = false;
    for (int k : order) {
      if (need[k] > 1.25) break;
      double p[3] = {ex, ey, ez}; p[AX[k]] += SG[k] * (need[k] + 1e-3);
      if (!collides(w, p[0] - ew + E, p[1] + E, p[2] - ew + E, p[0] + ew - E, p[1] + eh - E, p[2] + ew - E)) { ex = p[0]; ey = p[1]; ez = p[2]; done = true; break; }
    }
    if (done) return;
    int k = order[0];   // nowhere free in one step: take the shortest and look again from there
    if (need[k] > 1.25) return;
    (AX[k] == 0 ? ex : AX[k] == 1 ? ey : ez) += SG[k] * (need[k] + 1e-3);
  }
}

MoveOut moveEntity(const World& w, Entity& e, float dt, const MoveParams& mp) {
  MoveOut res;
  double ex = e.pos.x, ey = e.pos.y, ez = e.pos.z, ew = e.w, eh = e.h;
  pushOut(w, ex, ey, ez, ew, eh, mp.sneaking);
  double dx = (double)e.vel.x * dt, dy = (double)e.vel.y * dt, dz = (double)e.vel.z * dt;
  double step = mp.stepHeight;
  bool wasGround = e.onGround;
  e.onGround = false;
  int side[3] = {0, 0, 0};
  std::vector<DBox> boxes;
  auto sweep = [&](double mxd, double myd, double mzd, double out[3]) {
    double mn[3] = {ex - ew, ey, ez - ew}, mxv[3] = {ex + ew, ey + eh, ez + ew};
    gatherBoxes(w, std::min(mn[0], mn[0] + mxd) - 0.01, std::min(mn[1], mn[1] + myd) - 0.01, std::min(mn[2], mn[2] + mzd) - 0.01,
                std::max(mxv[0], mxv[0] + mxd) + 0.01, std::max(mxv[1], mxv[1] + myd) + 0.01, std::max(mxv[2], mxv[2] + mzd) + 0.01,
                boxes, ey, mp.sneaking);
    double ry = clipAxis(boxes, 1, myd, mn, mxv); mn[1] += ry; mxv[1] += ry;
    double rx = clipAxis(boxes, 0, mxd, mn, mxv); mn[0] += rx; mxv[0] += rx;
    double rz = clipAxis(boxes, 2, mzd, mn, mxv);
    out[0] = rx; out[1] = ry; out[2] = rz;
  };
  int n = std::max(1, (int)std::ceil(std::max({std::fabs(dx), std::fabs(dy), std::fabs(dz)}) / 0.45));
  dx /= n; dy /= n; dz /= n;
  bool blockedX = false, blockedZ = false, landed = false, ceil = false;
  double r[3];
  for (int i = 0; i < n; i++) {
    sweep(dx, dy, dz, r);
    double rx = r[0], ry = r[1], rz = r[2];
    bool hitX = std::fabs(rx - dx) > 1e-6, hitZ = std::fabs(rz - dz) > 1e-6;
    // step up: bumped into something low while on the ground; a body just off the ground may still step a small lip
    bool grounded = wasGround || landed || (dy < 0 && std::fabs(ry - dy) > 1e-6);
    double stepMax = grounded ? step : std::min(step, (double)mp.airStep);
    if ((hitX || hitZ) && stepMax > 0) {
      // lift no higher than needed: a doorway's lintel can stop a full-height lift from fitting through
      double sy = ey, up[3]; sweep(0, stepMax, 0, up); double maxUp = up[1];
      bool stepped = false;
      for (double want : {0.07, 0.13, 0.26, 0.51, step}) {
        if (want > stepMax + 1e-6) break;
        double lift = std::min(want, maxUp);
        ey = sy + lift;
        double s2[3]; sweep(dx, 0, dz, s2);
        ex += s2[0]; ez += s2[2];
        double dn[3]; sweep(0, -lift - 0.01, 0, dn);
        ey += dn[1];
        if (std::hypot(s2[0], s2[2]) > std::hypot(rx, rz) + 1e-4 && dn[1] < 0) {
          landed = true; dy = 0; e.vel.y = std::min(e.vel.y, 0.f); side[1] = -1;
          if (std::fabs(s2[0] - dx) > 1e-6) { blockedX = true; side[0] = dx > 0 ? 1 : -1; }
          if (std::fabs(s2[2] - dz) > 1e-6) { blockedZ = true; side[2] = dz > 0 ? 1 : -1; }
          stepped = true; break;
        }
        ex -= s2[0]; ez -= s2[2]; ey = sy;
        if (lift >= maxUp - 1e-6) break;
      }
      if (stepped) continue;
    }
    ex += rx; ey += ry; ez += rz;
    if (std::fabs(ry - dy) > 1e-6) { if (dy < 0) { landed = true; side[1] = -1; } else { ceil = true; side[1] = 1; } dy = 0; }
    if (hitX) { blockedX = true; side[0] = dx > 0 ? 1 : -1; dx = 0; }
    if (hitZ) { blockedZ = true; side[2] = dz > 0 ? 1 : -1; dz = 0; }
  }
  if (landed) { e.onGround = true; e.vel.y = 0; res.landed = true; }
  if (ceil) { res.hitCeiling = true; e.vel.y = std::min(0.f, e.vel.y); }
  if (blockedX) { e.vel.x = 0; res.hitWall = res.hitX = true; }
  if (blockedZ) { e.vel.z = 0; res.hitWall = res.hitZ = true; }
  // probe just below the feet so standing still still counts as grounded
  if (!e.onGround && e.vel.y <= 0) {
    std::vector<DBox> bs;
    gatherBoxes(w, ex - ew + 0.01, ey - 0.02, ez - ew + 0.01, ex + ew - 0.01, ey, ez + ew - 0.01, bs, ey, mp.sneaking);
    for (auto& b : bs) if (b.x1 > ex - ew + 0.01 && b.x0 < ex + ew - 0.01 && b.y1 > ey - 0.02 && b.y0 < ey && b.z1 > ez - ew + 0.01 && b.z0 < ez + ew - 0.01) { e.onGround = true; break; }
  }
  e.pos = {storeToward(ex, side[0]), storeToward(ey, side[1]), storeToward(ez, side[2])};
  return res;
}

MoveResult moveBox(const World& w, vec3& pos, float hw, float h, vec3 delta, float stepUp) {
  Entity e; e.pos = pos; e.w = hw; e.h = h; e.vel = delta; e.onGround = stepUp > 0;
  MoveOut o = moveEntity(w, e, 1.0f, {stepUp, 0, false});
  pos = e.pos;
  MoveResult r; r.hitX = o.hitX; r.hitZ = o.hitZ; r.hitY = o.landed || o.hitCeiling; r.onGround = e.onGround;
  return r;
}

Contacts sampleContacts(const World& w, const Entity& e, float eye) {
  Contacts r;
  const Registry& R = reg();
  int x0 = ifloor(e.pos.x - e.w), x1 = ifloor(e.pos.x + e.w), z0 = ifloor(e.pos.z - e.w), z1 = ifloor(e.pos.z + e.w);
  int y0 = ifloor(e.pos.y), y1 = ifloor(e.pos.y + e.h - 0.01f);
  for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    BlockId id = w.getBlock(x, y, z);
    if (!id) continue;
    int lq = R.tLiquid[id];
    if (lq == 1) r.water = true; else if (lq == 2) r.lava = true;
    else if (R.tWlog[id] && (w.getMeta(x, y, z) & WLOG)) r.water = true;
    if (R.tPortal[id]) r.portal = R.tPortal[id];
    const BlockInfo& bi = binfo(id);
    if (bi.f & BF::Climb) { r.ladder = true; if (bi.f & BF::Scaffold) r.scaffold = true; }
    if (bi.f & BF::Sticky) r.web = true;
    if (bi.f & BF::Fire) r.fire = true;
  }
  BlockId under = w.getBlock(ifloor(e.pos.x), ifloor(e.pos.y - 0.05f), ifloor(e.pos.z));
  if (bhas(under, BF::Hot)) r.hot = true;
  // cactus is touched from the sides
  BlockId cactus = KB().cactus;
  if (cactus) for (int y = y0; y <= y1; y++) {
    const float off[4][2] = {{e.w + 0.05f, 0}, {-e.w - 0.05f, 0}, {0, e.w + 0.05f}, {0, -e.w - 0.05f}};
    for (auto& o : off) if (w.getBlock(ifloor(e.pos.x + o[0]), y, ifloor(e.pos.z + o[1])) == cactus) r.cactus = true;
  }
  if (eye >= 0) {
    int ex = ifloor(e.pos.x), ey = ifloor(e.pos.y + eye), ez = ifloor(e.pos.z);
    BlockId id = w.getBlock(ex, ey, ez);
    if (R.tLiquid[id] == 1) {
      u8 m = w.getMeta(ex, ey, ez);
      BlockId above = w.getBlock(ex, ey + 1, ez);
      double top = ey + liquidHeight(id, m, above);
      r.eyeWater = e.pos.y + eye < top;
    } else if (R.tWlog[id] && (w.getMeta(ex, ey, ez) & WLOG)) r.eyeWater = true;
    if (R.tLiquid[id] == 2) r.eyeLava = true;
  }
  return r;
}

// Ray vs box with the normal of the face hit: t (or -1) and the normal.
static double rayBoxN(const glm::dvec3& o, const glm::dvec3& d, const double mn[3], const double mx[3], ivec3& nOut) {
  double tmin = -INFINITY, tmax = INFINITY; ivec3 n(0);
  for (int a = 0; a < 3; a++) {
    if (std::fabs(d[a]) < 1e-9) { if (o[a] < mn[a] || o[a] > mx[a]) return -1; continue; }
    double t1 = (mn[a] - o[a]) / d[a], t2 = (mx[a] - o[a]) / d[a];
    int s = -1;
    if (t1 > t2) { std::swap(t1, t2); s = 1; }
    if (t1 > tmin) { tmin = t1; n = ivec3(0); n[a] = s; }
    tmax = std::min(tmax, t2);
    if (tmin > tmax) return -1;
  }
  if (tmax < 0) return -1;
  nOut = n;
  return std::max(0.0, tmin);
}

double rayAABB(glm::dvec3 o, glm::dvec3 d, glm::dvec3 lo, glm::dvec3 hi) {
  double tmin = 0, tmax = INFINITY;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(d[a]) < 1e-9) { if (o[a] < lo[a] || o[a] > hi[a]) return -1; continue; }
    double t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
    if (tmin > tmax) return -1;
  }
  return tmin;
}

RayHit raycastBlocks(const World& w, glm::dvec3 o, vec3 dirf, float maxDist, bool pickLiquid) {
  RayHit h;
  const Registry& R = reg();
  glm::dvec3 d(dirf);
  int x = (int)std::floor(o.x), y = (int)std::floor(o.y), z = (int)std::floor(o.z);
  int sx = d.x > 0 ? 1 : d.x < 0 ? -1 : 0, sy = d.y > 0 ? 1 : d.y < 0 ? -1 : 0, sz = d.z > 0 ? 1 : d.z < 0 ? -1 : 0;
  double tdx = d.x != 0 ? std::fabs(1 / d.x) : INFINITY, tdy = d.y != 0 ? std::fabs(1 / d.y) : INFINITY, tdz = d.z != 0 ? std::fabs(1 / d.z) : INFINITY;
  double tmx = d.x > 0 ? (x + 1 - o.x) * tdx : d.x < 0 ? (o.x - x) * tdx : INFINITY;
  double tmy = d.y > 0 ? (y + 1 - o.y) * tdy : d.y < 0 ? (o.y - y) * tdy : INFINITY;
  double tmz = d.z > 0 ? (z + 1 - o.z) * tdz : d.z < 0 ? (o.z - z) * tdz : INFINITY;
  ivec3 n(0); double t = 0;
  std::vector<PixBox> bx, ub;
  auto finish = [&](int bx_, int by_, int bz_, BlockId id, ivec3 nn, double dist) {
    h.hit = true; h.block = {bx_, by_, bz_}; h.normal = nn; h.dist = (float)dist; h.id = id; h.point = vec3(o + d * dist);
  };
  for (int i = 0; i < 256 && t <= maxDist; i++) {
    BlockId id = w.getBlock(x, y, z);
    if (id) {
      Render r = (Render)R.tRender[id];
      bool liquid = r == Render::Liquid;
      if (!liquid || (pickLiquid && R.tLiquid[id] && (w.getMeta(x, y, z) & 15) == 0)) {
        bool full = r == Render::Cube || r == Render::Cutout || liquid || !worldBoxes(w, x, y, z, id, SHAPE_SELECT, bx);
        // Remember this cell for the aimed-sub-box pick when it is a chisel-carved block.
        bool carved = id && !R.tLiquid[id] && w.getCarve(x, y, z);
        if (full && !carved) { finish(x, y, z, id, n, t); return h; }
        if (carved) {
          std::vector<PixBox> cb;
          if (!worldBoxes(w, x, y, z, id, SHAPE_SELECT, cb)) { /* fully carved: no octant boxes — fall through to the cell fallback below so a chisel can restore */ }
          else {
            double bt = INFINITY; ivec3 bn2(0); bool anyc = false;
            for (auto& b : cb) {
              double mn[3] = {x + b.x0 / 16.0, y + b.y0 / 16.0, z + b.z0 / 16.0}, mx[3] = {x + b.x1 / 16.0, y + b.y1 / 16.0, z + b.z1 / 16.0};
              ivec3 nn; double tt = rayBoxN(o, d, mn, mx, nn);
              if (tt < 0) continue;
              glm::dvec3 P = o + d * tt;
              P += glm::dvec3(d.x, d.y, d.z) * 0.01;   // step just inside the block from the entry face
              bool contains = P.x > mn[0] && P.x < mx[0] && P.y > mn[1] && P.y < mx[1] && P.z > mn[2] && P.z < mx[2];
              double rank = tt + (contains ? 0.0 : 1e9);
              if (rank < bt) { bt = rank; bn2 = nn; anyc = true; }
            }
            double tCell = std::max({tmx - tdx, tmy - tdy, tmz - tdz});   // param at which the ray entered this cell
            double btTrue = bt > 1e8 ? bt - 1e9 : bt;   // strip the containment penalty for the checks below
            if (anyc && btTrue <= maxDist && (btTrue < tCell || t >= tCell)) { finish(x, y, z, id, bn2, btTrue); return h; }
            // No octant hit in this cell's span: the crosshair passed THROUGH a carved-away notch,
            // straight into the void where that corner used to be. The block still stands there, so
            // finish on its cell — chisel then toggles the aimed (carved) quadrant back on.
            // Only when the ray is INSIDE the cell: while it merely grazes a face plane exactly
            // along an octant boundary line (e.g. aimed straight down a top corner's diagonal),
            // every octant test returns the same grazing t, so this fallback would fire on the
            // entry step and carve the whole block in one click.
            bool inside = o.x > x && o.x < x + 1 && o.y > y && o.y < y + 1 && o.z > z && o.z < z + 1;
            if (inside && t >= tCell && t <= maxDist) { finish(x, y, z, id, n, t); return h; }
          }
        }
        if (full) { finish(x, y, z, id, n, t); return h; }
        double best = INFINITY; ivec3 bn(0); bool any = false;
        for (auto& b : bx) {
          double mn[3] = {x + b.x0 / 16.0, y + b.y0 / 16.0, z + b.z0 / 16.0}, mx[3] = {x + b.x1 / 16.0, y + b.y1 / 16.0, z + b.z1 / 16.0};
          ivec3 nn; double tt = rayBoxN(o, d, mn, mx, nn);
          if (tt >= 0 && tt < best) { best = tt; bn = nn; any = true; }
        }
        // something sunk onto a bottom half slab pokes down into the slab's cell
        if (R.tShape[id] == ShapeKind::Slab && (w.getMeta(x, y, z) & 7) == 0) {
          BlockId up = w.getBlock(x, y + 1, z);
          if (up && R.tSink[up] && worldBoxes(w, x, y + 1, z, up, SHAPE_SELECT, ub)) for (auto& b : ub) {
            double mn[3] = {x + b.x0 / 16.0, y + 1 + b.y0 / 16.0, z + b.z0 / 16.0}, mx[3] = {x + b.x1 / 16.0, y + 1 + b.y1 / 16.0, z + b.z1 / 16.0};
            ivec3 nn; double tt = rayBoxN(o, d, mn, mx, nn);
            if (tt >= 0 && tt < best && tt <= maxDist) { finish(x, y + 1, z, up, nn, tt); return h; }
          }
        }
        if (any && best <= maxDist) { finish(x, y, z, id, bn, best); return h; }
      }
    }
    if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; n = {-sx, 0, 0}; }
    else if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; n = {0, -sy, 0}; }
    else { z += sz; t = tmz; tmz += tdz; n = {0, 0, -sz}; }
  }
  return h;
}

} // namespace dq
