// sim/ragdoll.cpp — see ragdoll.h.
#include "sim/ragdoll.h"
#include "sim/physics.h"
#include "sim/level.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"

namespace dq {

static constexpr float RD_H = 1.f / 60, RD_G = 24, RD_SETTLE = 0.006f;
static constexpr int PAIRS[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};

static mat3 gsFrame(vec3 a, vec3 b) {
  vec3 e0 = glm::length(a) > 1e-8f ? glm::normalize(a) : vec3(1, 0, 0);
  vec3 e1 = b - e0 * glm::dot(b, e0);
  if (glm::length(e1) < 1e-6f) e1 = std::fabs(e0.y) < 0.9f ? glm::cross(e0, vec3(0, 1, 0)) : glm::cross(e0, vec3(1, 0, 0));
  e1 = glm::normalize(e1);
  return mat3(e0, e1, glm::cross(e0, e1));
}
static float lcg(u32& s) { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 16777216.f; }

void Ragdoll::build(vec3 o, const std::vector<RagdollPartIn>& parts, vec3 vel, vec3 kick, u32 seed, vec3 side) {
  origin = o; age = 0; frozen = false; stillSteps = 0; acc = 0; wakeT = 0; awakeT = 0;
  bodies.clear(); x.clear(); px.clear(); rlen.clear(); contact.clear();
  m0.assign(parts.size(), mat4(1)); owner.assign(parts.size(), -1);
  lo0.assign(parts.size(), vec3(0)); hi0.assign(parts.size(), vec3(0));
  for (size_t i = 0; i < parts.size(); i++) { lo0[i] = glm::min(parts[i].lo, parts[i].hi); hi0[i] = glm::max(parts[i].lo, parts[i].hi); }
  for (size_t i = 0; i < parts.size(); i++) {
    const RagdollPartIn& P = parts[i];
    m0[i] = P.m;
    const vec3 c0(P.m[0]), c1(P.m[1]), c2(P.m[2]);
    if (P.m[3][3] == 0 || glm::length(c0) < 1e-6f || glm::length(c1) < 1e-6f || glm::length(c2) < 1e-6f) continue;   // lost / unposed
    const int par = P.parent >= 0 && P.parent < (int)i ? owner[P.parent] : -1;
    if (!P.simulate && par >= 0) { owner[i] = par; continue; }
    Body b; b.part = (int)i; b.parent = par; b.limit = P.limit;
    b.F0 = gsFrame(c0, c1);
    const vec3 sz = glm::abs(P.hi - P.lo);
    b.L = glm::max(vec3(glm::length(c0) * sz.x, glm::length(c1) * sz.y, glm::length(c2) * sz.z) * 0.5f, vec3(0.04f));
    b.c0 = vec3(P.m * vec4((P.lo + P.hi) * 0.5f, 1));
    if (par >= 0) {
      const Body& pb = bodies[par];
      const vec3 j(P.m[3]);
      b.qSelf = glm::transpose(b.F0) * (j - b.c0);
      b.qParent = glm::transpose(pb.F0) * (j - pb.c0);
      const vec3 r = b.c0 - j;
      b.hasRest = glm::length(r) > 0.02f;
      if (b.hasRest) b.rest = glm::transpose(pb.F0) * glm::normalize(r);
    }
    owner[i] = (int)bodies.size();
    bodies.push_back(b);
    x.push_back(b.c0);
    for (int k = 0; k < 3; k++) x.push_back(b.c0 + b.F0[k] * b.L[k]);
    for (auto& pr : PAIRS) rlen.push_back(glm::distance(x[x.size() - 4 + pr[0]], x[x.size() - 4 + pr[1]]));
  }
  // what touches the ground: points all over the surface of every drawn box, in its piece's frame. (Only the piece's own
  // corners and middle were tested before: a head, a fleece, a mane or a foot riding on a piece went straight into the
  // ground, and a face lying across a block edge sank between its corners.)
  for (size_t i = 0; i < parts.size(); i++) {
    const int b = owner[i];
    if (b < 0) continue;
    const vec3 sz = hi0[i] - lo0[i];
    if (sz.x <= 1e-5f || sz.y <= 1e-5f || sz.z <= 1e-5f) continue;   // (a pivot only: nothing drawn)
    Body& B = bodies[b];
    const mat3 Ft = glm::transpose(B.F0);
    int n[3];
    for (int k = 0; k < 3; k++) n[k] = clampv((int)std::ceil(glm::length(vec3(m0[i][k])) * sz[k] / 0.12f) + 1, 2, 12);   // (a block's edge can't poke in between)
    for (int a = 0; a < n[0]; a++) for (int bb = 0; bb < n[1]; bb++) for (int c = 0; c < n[2]; c++) {
      if (a != 0 && a != n[0] - 1 && bb != 0 && bb != n[1] - 1 && c != 0 && c != n[2] - 1) continue;   // (the surface)
      const vec3 u(a / float(n[0] - 1), bb / float(n[1] - 1), c / float(n[2] - 1));
      const vec3 p = vec3(m0[i] * vec4(lo0[i] + sz * u, 1));
      B.pts.push_back(Ft * (p - B.c0));
    }
  }
  for (size_t i = 0; i < parts.size(); i++) if (owner[i] >= 0) {   // its weight: the size of everything it carries
    const vec3 sz = hi0[i] - lo0[i];
    bodies[owner[i]].m += std::max(0.f, glm::length(vec3(m0[i][0])) * sz.x * glm::length(vec3(m0[i][1])) * sz.y * glm::length(vec3(m0[i][2])) * sz.z);
  }
  for (Body& B : bodies) {   // (a thin tail is easy to turn, a torso with its head on is not)
    float s = 0; B.R = glm::length(B.L); for (auto& q : B.pts) { s += glm::dot(q, q); B.R = std::max(B.R, glm::length(q)); }
    B.I = std::max(B.pts.empty() ? glm::dot(B.L, B.L) / 3.f : s / B.pts.size(), 0.004f);
  }
  // the start: the body's own motion and the blow, tipping it over away from the blow (a little random sway either way)
  // (a heavy blow's knockback can be huge: a body is pushed, not thrown across the field)
  vec3 push = vel + kick;
  { vec2 h(push.x, push.z); const float l = glm::length(h); if (l > 3.f) { h *= 3.f / l; push.x = h.x; push.z = h.y; } push.y = clampv(push.y, -8.f, 2.5f); }
  vec3 kh(push.x, 0, push.z);
  float kl = glm::length(kh);
  if (kl < 0.3f) { float a = lcg(seed) * 6.2831853f; kh = vec3(std::cos(a), 0, std::sin(a)) * 0.6f; kl = 0.6f; }
  vec3 dir = kh / kl;
  // it tips over sideways, across its length (a four-legged body rolls onto its flank; it doesn't rear up), towards the
  // side the blow pushed it
  if (glm::length(side) > 1e-3f) { vec3 s = glm::normalize(vec3(side.x, 0, side.z)); dir = glm::dot(s, dir) >= 0 ? s : -s; }
  px.resize(x.size());
  for (size_t i = 0; i < x.size(); i++) {
    vec3 v = push + dir * (std::max(0.f, x[i].y) * (1.0f + std::min(kl, 3.f) * 0.2f));
    v += vec3(lcg(seed) - 0.5f, lcg(seed) * 0.4f, lcg(seed) - 0.5f) * 0.5f;
    px[i] = x[i] - v * RD_H;
  }
}

mat3 Ragdoll::frame(int b) const {
  const vec3 c = x[b * 4];
  return gsFrame(x[b * 4 + 1] - c, x[b * 4 + 2] - c);
}
vec3 Ragdoll::at(int b, vec3 q) const {
  const Body& B = bodies[b]; const vec3 c = x[b * 4];
  return c + (x[b * 4 + 1] - c) * (q.x / B.L.x) + (x[b * 4 + 2] - c) * (q.y / B.L.y) + (x[b * 4 + 3] - c) * (q.z / B.L.z);
}
void Ragdoll::push(int b, vec3 q, vec3 d, float k) {
  const Body& B = bodies[b];
  const float w1 = q.x / B.L.x, w2 = q.y / B.L.y, w3 = q.z / B.L.z, w0 = 1 - w1 - w2 - w3;
  const float den = w0 * w0 + w1 * w1 + w2 * w2 + w3 * w3;
  if (den < 1e-8f) return;
  const vec3 s = d * (k / den);
  x[b * 4] += s * w0; x[b * 4 + 1] += s * w1; x[b * 4 + 2] += s * w2; x[b * 4 + 3] += s * w3;
}
void Ragdoll::rigid(int b) {
  for (int p = 0; p < 6; p++) {
    vec3& a = x[b * 4 + PAIRS[p][0]]; vec3& c = x[b * 4 + PAIRS[p][1]];
    vec3 d = c - a; float l = glm::length(d);
    if (l < 1e-8f) continue;
    vec3 corr = d * ((l - rlen[b * 6 + p]) / l * 0.5f);
    a += corr; c -= corr;
  }
}

void Ragdoll::snap(int b) {
  for (int r = 0; r < 2; r++) rigid(b);
  const Body& B = bodies[b];
  const vec3 C = (x[b * 4] + x[b * 4 + 1] + x[b * 4 + 2] + x[b * 4 + 3]) * 0.25f;
  const mat3 F = frame(b);
  const vec3 o = (F[0] * B.L.x + F[1] * B.L.y + F[2] * B.L.z) * 0.25f;   // (the four points' middle, from the centre)
  x[b * 4] = C - o;
  for (int k = 0; k < 3; k++) x[b * 4 + 1 + k] = x[b * 4] + F[k] * B.L[k];
}

// what's solid: the world itself (the tests' measure), or the boxes round a falling body gathered once a step (the solver:
// a few dozen boxes looked up by their cell, not the world's chunk maps a hundred thousand times a frame)
struct SolidQuery { virtual ~SolidQuery() = default; virtual bool inside(vec3 P) const = 0; };
static bool insideSolid(const World& w, vec3 P);
struct WorldSolid : SolidQuery { const World& w; explicit WorldSolid(const World& ww) : w(ww) {} bool inside(vec3 P) const override { return insideSolid(w, P); } };
struct CachedSolid : SolidQuery {
  std::unordered_map<u64, std::vector<AABB>> cells; std::vector<AABB> all;
  static u64 key(int x, int y, int z) { return ((u64)(u32)(x + (1 << 20)) << 42) ^ ((u64)(u32)y << 21) ^ (u64)(u32)(z + (1 << 20)); }
  void gather(const World& w, vec3 lo, vec3 hi) {
    cells.clear(); all.clear();
    thread_local std::vector<DBox> boxes;
    gatherBoxes(w, lo.x, std::max(0.f, lo.y), lo.z, hi.x, std::min((float)WORLD_H, hi.y), hi.z, boxes);
    for (const DBox& b : boxes) {
      AABB a{vec3((float)b.x0, (float)b.y0, (float)b.z0), vec3((float)b.x1, (float)b.y1, (float)b.z1)};
      const vec3 c = (a.lo + a.hi) * 0.5f;
      cells[key(ifloor(c.x), ifloor(c.y), ifloor(c.z))].push_back(a);
      all.push_back(a);
    }
  }
  bool inside(vec3 P) const override {
    auto it = cells.find(key(ifloor(P.x), ifloor(P.y), ifloor(P.z)));
    if (it == cells.end()) return false;
    for (const AABB& b : it->second) if (P.x > b.lo.x && P.x < b.hi.x && P.y > b.lo.y && P.y < b.hi.y && P.z > b.lo.z && P.z < b.hi.z) return true;
    return false;
  }
  bool overlaps(vec3 lo, vec3 hi) const { for (const AABB& b : all) if (lo.x < b.hi.x && hi.x > b.lo.x && lo.y < b.hi.y && hi.y > b.lo.y && lo.z < b.hi.z && hi.z > b.lo.z) return true; return false; }
};
static thread_local CachedSolid t_solid;

// strictly inside some block's collision box? (an exit point lying on the face of the block under it is free: the old
// test, a tiny box round the point, touched that face and called every way out along a floor blocked, so a point by a
// step was shoved up a whole block, or counted as a whole block deep)
static bool insideSolid(const World& w, vec3 P) {
  thread_local std::vector<AABB> boxes;
  const int bx = ifloor(P.x), by = ifloor(P.y), bz = ifloor(P.z);
  if (by < 0 || by >= WORLD_H || !reg().tSolid[w.getBlockPhys(bx, by, bz)]) return false;
  boxes.clear(); blockBoxes(w, bx, by, bz, boxes);
  for (const AABB& b : boxes) if (P.x > b.lo.x && P.x < b.hi.x && P.y > b.lo.y && P.y < b.hi.y && P.z > b.lo.z && P.z < b.hi.z) return true;
  return false;
}

// a point inside a block's collision box: how to get it out (towards where it came from, else the shortest way up
// or sideways into free space). false = not inside anything.
// The way out of the blocks for a point inside one: the shortest of the 26 ways (straight out, and diagonally across the
// edges and corners: a point under the seam where a wall meets the floor is a hair from the air above-and-beside it, not
// a whole block from the air straight up or down), marched out through whatever solid lies that way. biased: the way it
// came from is cheaper and up a little cheaper, and it is never pushed down unless it came from below (pushed down through
// a floor, a body fell out of the world). false = not inside anything.
static bool wayOut(const SolidQuery& S, vec3 P, vec3 prevP, bool biased, vec3& d, vec3 home = vec3(0)) {
  if (!S.inside(P)) return false;
  static std::vector<vec3> DIRS = [] { std::vector<vec3> v; for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) if (x || y || z) v.push_back(glm::normalize(vec3(x, y, z))); return v; }();
  const vec3 back = prevP - P; const float bl = glm::length(back);
  float cost[26];
  for (int k = 0; k < 26; k++) {
    const vec3& D = DIRS[k];
    cost[k] = 1;
    if (!biased) continue;
    if (D.y < -0.5f && prevP.y >= P.y) { cost[k] = -1; continue; }
    if (bl > 1e-3f && glm::dot(D, back / bl) > 0.5f) cost[k] *= 0.4f;
    else if (bl <= 1e-3f && glm::length(home) > 1e-4f && glm::dot(D, glm::normalize(home)) > 0.5f) cost[k] *= 0.6f;   // (not moving: towards the body)
    if (D.y > 0.5f) cost[k] *= 0.8f;
  }
  // each way's exact distance out (marched, then halved down to a hair), the cheapest wins; a tie goes to the more upward
  // way (the first in the list had won ties: a body at rest crept off sideways, a hair at a time, for ever)
  float best = 1e9f, bestY = -2; vec3 bd(0, 1, 0);
  for (int k = 0; k < 26; k++) {
    if (cost[k] < 0) continue;
    float lo = 0, hi = -1;
    for (float t = 1.f / 32; t <= 1.6f; t += 1.f / 32) {
      if (lo * cost[k] > best) break;   // (can't win)
      if (!S.inside(P + DIRS[k] * t)) { hi = t; break; }
      lo = t;
    }
    if (hi < 0) continue;
    for (int r = 0; r < 6; r++) { const float m = (lo + hi) * 0.5f; if (S.inside(P + DIRS[k] * m)) lo = m; else hi = m; }
    const float sc = hi * cost[k];
    if (sc < best - 1e-5f || (sc < best + 1e-5f && DIRS[k].y > bestY)) { best = sc; bestY = DIRS[k].y; bd = DIRS[k] * (hi + 0.002f); }
  }
  d = best < 1e8f ? bd : vec3(0, 1.f, 0);   // (buried: up)
  return true;
}
static bool pushOut(const World&, vec3 P, vec3 prevP, vec3& d, vec3 home = vec3(0)) { return wayOut(t_solid, P, prevP, true, d, home); }

bool Ragdoll::collide(const World& w, int b, int rounds) {
  const Body& B = bodies[b];
  bool touched = false;
  const vec3 pc = px[b * 4];
  // every point in the blocks at once, a few rounds: each one's push as a rigid slide-and-turn, the deepest counting most
  // and the turns of points on opposite sides cancelling (a box lying flat sinks evenly and comes up evenly; one corner
  // at a time rocked it for ever and it never lay still)
  thread_local std::vector<std::pair<vec3, vec3>> hits;
  for (int round = 0; round < rounds; round++) {
    hits.clear();
    float deepest = 0;
    for (const vec3& q : B.pts) {
      const vec3 P = at(b, q);
      const vec3 prevP = pc + (px[b * 4 + 1] - pc) * (q.x / B.L.x) + (px[b * 4 + 2] - pc) * (q.y / B.L.y) + (px[b * 4 + 3] - pc) * (q.z / B.L.z);
      vec3 d;
      if (pushOut(w, origin + P, origin + prevP, d, x[0] - P)) { hits.push_back({q, d}); deepest = std::max(deepest, glm::length(d)); }
    }
    if (hits.empty()) break;
    touched = true;
    const vec3 c = x[b * 4];
    const float I = B.I;
    vec3 dc(0), th(0); float wsum = 0;
    for (auto& [q, d] : hits) {
      const float dl = glm::length(d); const vec3 n = d / dl, r = at(b, q) - c, rn = glm::cross(r, n);
      const float lam = dl / (1 + glm::dot(rn, rn) / I), wgt = dl / deepest;
      dc += n * lam * wgt; th += rn * (lam / I) * wgt; wsum += wgt;
    }
    dc /= wsum; th /= wsum;
    // (the slide is at least the deepest push along its own way: averaging must not leave the deepest point in)
    const float ang = glm::length(th);
    for (int k = 0; k < 4; k++) {
      vec3 v = x[b * 4 + k] - c;
      if (ang > 1e-7f) { const vec3 a = th / ang; v = v * std::cos(ang) + glm::cross(a, v) * std::sin(ang) + a * glm::dot(a, v) * (1 - std::cos(ang)); }
      x[b * 4 + k] = c + dc + v;
    }
  }
  return touched;
}

// One round of every constraint: the pieces rigid, the joints together and within their cones, then the blocks (so the
// blocks always have the last word: nothing a joint pulls is left inside a wall or the ground).
void Ragdoll::solve(const World& w, std::vector<u8>& touched, int rounds) {
  const int nb = (int)bodies.size();
  for (int b = 0; b < nb; b++) snap(b);
  for (int b = 0; b < nb; b++) {
    const Body& B = bodies[b];
    if (B.parent < 0) continue;
    const vec3 C = at(b, B.qSelf) - at(B.parent, B.qParent);
    auto den = [&](int k, vec3 q) { const Body& K = bodies[k]; float w1 = q.x / K.L.x, w2 = q.y / K.L.y, w3 = q.z / K.L.z, w0 = 1 - w1 - w2 - w3; return w0 * w0 + w1 * w1 + w2 * w2 + w3 * w3; };
    // (each moved by its share by weight: eight thin legs don't shake a spider's little thorax about, nor a paw a torso)
    const float wb = 1 / B.m, wp = 1 / bodies[B.parent].m;
    const float db = den(b, B.qSelf) * wb, dp = den(B.parent, B.qParent) * wp, s = db + dp;
    if (s < 1e-8f) continue;
    push(b, B.qSelf, -C * (db / s));
    push(B.parent, B.qParent, C * (dp / s));
    // the joint's cone: no limb bends further than it could. The limb is turned back about its own joint (it was dragged
    // back by its middle, which pulled it off the joint: a neck hung a quarter of a block off the shoulders for ever)
    // (a limb lying on the ground lets the ground have its way: the cone turning it back in and the ground pushing it out
    // again, step after step, was a shiver that never stopped)
    if (B.hasRest && !(b < (int)contact.size() && contact[b] && age > 0.5f)) {
      const vec3 a = glm::normalize(frame(B.parent) * B.rest);
      const vec3 jp = at(b, B.qSelf);
      const vec3 v = x[b * 4] - jp; const float lv = glm::length(v);
      if (lv > 1e-4f) {
        const vec3 vn = v / lv;
        const float ang = std::acos(clampv(glm::dot(vn, a), -1.f, 1.f));
        if (ang > B.limit) {
          vec3 ax = glm::cross(vn, a);
          if (glm::length(ax) < 1e-6f) ax = std::fabs(a.y) < 0.9f ? glm::cross(a, vec3(0, 1, 0)) : glm::cross(a, vec3(1, 0, 0));
          ax = glm::normalize(ax);
          const float th = (ang - B.limit) * 0.5f, cs = std::cos(th), sn = std::sin(th);
          for (int k = 0; k < 4; k++) {   // (turned towards the cone by half the excess, about the joint)
            const vec3 r = x[b * 4 + k] - jp;
            x[b * 4 + k] = jp + r * cs + glm::cross(ax, r) * sn + ax * glm::dot(ax, r) * (1 - cs);
          }
        }
      }
    }
  }
  for (int b = 0; b < nb; b++) snap(b);
  for (int b = 0; b < nb; b++) {
    const vec3 c = origin + x[b * 4], r(bodies[b].R + 0.05f);
    if (!t_solid.overlaps(c - r, c + r)) continue;   // (nowhere near a block)
    if (collide(w, b, rounds)) touched[b] = 1;
  }
}

void Ragdoll::sub(const World& w, float h) {
  const size_t n = x.size();
  const int nb = (int)bodies.size();
  {   // the blocks round the whole body, once a step (room to move this step and to be pushed out the far side)
    vec3 lo(1e9f), hi(-1e9f);
    for (int b = 0; b < nb; b++) { const vec3 c = origin + x[b * 4]; lo = glm::min(lo, c - vec3(bodies[b].R)); hi = glm::max(hi, c + vec3(bodies[b].R)); }
    t_solid.gather(w, lo - vec3(1.8f), hi + vec3(1.8f));
  }
  thread_local std::vector<u8> touched; touched.assign(nb, 0);
  if (age == 0) {   // died half in a step or a wall: out first, joints and all (and no kick from it)
    std::vector<vec3> before = x;
    for (int r = 0; r < 12; r++) solve(w, touched, 6);
    for (size_t i = 0; i < n; i++) px[i] += x[i] - before[i];
    touched.assign(nb, 0);
  }
  for (size_t i = 0; i < n; i++) {
    vec3 v = (x[i] - px[i]) * (age < 2 ? 0.995f : 0.95f);   // (after the fall, the twitching dies down)
    if (const float l = glm::length(v); l > 0.2f) v *= 0.2f / l;   // (12 blocks/s at most: nothing passes through a slab in one step)
    px[i] = x[i];
    x[i] += v + vec3(0, -RD_G * h * h, 0);
  }
  for (int it = 0; it < 8; it++) solve(w, touched, it == 7 ? 8 : 3);
  // last of all: whatever is still a hair in (the turns of a few points shared out), the piece simply lifted clear
  for (int b = 0; b < nb; b++) for (int r = 0; r < 3; r++) {
    const Body& B = bodies[b];
    float deepest = 0; vec3 dd(0);
    for (const vec3& q : B.pts) { vec3 d; if (pushOut(w, origin + at(b, q), origin + at(b, q), d, x[0] - at(b, q)) && glm::length(d) > deepest) { deepest = glm::length(d); dd = d; } }
    if (deepest <= 0) break;
    for (int k = 0; k < 4; k++) x[b * 4 + k] += dd;
    touched[b] = 1;
  }
  for (int b = 0; b < nb; b++) if (touched[b]) for (int k = 0; k < 4; k++) px[b * 4 + k] += (x[b * 4 + k] - px[b * 4 + k]) * 0.45f;   // friction: it scrapes to a stop
  contact.assign(touched.begin(), touched.end());
  float most = 0;
  for (size_t i = 0; i < n; i++) { const float m = glm::distance(x[i], px[i]); if (m > most) { most = m; motionPart = bodies[i / 4].part; } }
  motion = most;
  // settled: still; or, after the fall, only a small twitch left where a limb lies wedged between the ground and its joint
  stillSteps = most < (age < 3 ? RD_SETTLE : age < 4 ? RD_SETTLE * 2 : age < 6 ? RD_SETTLE * 4 : RD_SETTLE * 10) ? stillSteps + 1 : 0;   // (wedged in a gap a wing may shiver: let it lie)
  age += h;
  awakeT += h;
  if (stillSteps > 30 || awakeT > 12) frozen = true;
}

void Ragdoll::step(const World& w, float dt) {
  if (bodies.empty()) return;
  if (frozen) {
    // still lying where it was? Once a second it is tried with one step: if the ground went from under it, or a block
    // was set into it, it wakes and falls again (a body frozen in mid-air or half in a new wall, for ever, before)
    if ((wakeT += dt) < 1) return;
    wakeT = 0;
    if (!w.isLoaded(ifloor(origin.x), clampv(ifloor(origin.y), 0, WORLD_H - 1), ifloor(origin.z))) return;
    const std::vector<vec3> sx = x, spx = px;
    px = x;   // (at rest: no speed)
    frozen = false;
    const float a0 = age; age = std::max(age, 6.f);
    sub(w, RD_H);
    bool touching = false; for (u8 c : contact) touching |= c != 0;
    if (touching && motion < RD_SETTLE * 3) { x = sx; px = spx; frozen = true; age = a0; return; }   // (as it was: no creeping)
    stillSteps = 0; age = std::max(a0, 3.f); awakeT = 0;
    return;
  }
  if (!w.isLoaded(ifloor(origin.x), clampv(ifloor(origin.y), 0, WORLD_H - 1), ifloor(origin.z))) return;   // (no ground to land on yet)
  acc += std::min(dt, 0.1f);
  while (acc >= RD_H && !frozen) { acc -= RD_H; sub(w, RD_H); }
}
void Ragdoll::settle(const World& w, float maxSecs) {
  if (!w.isLoaded(ifloor(origin.x), clampv(ifloor(origin.y), 0, WORLD_H - 1), ifloor(origin.z))) return;
  for (float t = 0; t < maxSecs && !frozen && !bodies.empty(); t += RD_H) sub(w, RD_H);
}

mat4 Ragdoll::partMatrix(int i) const {
  if (!posed(i)) return mat4(0);
  const int b = owner[i];
  const Body& B = bodies[b];
  const mat3 R = frame(b) * glm::transpose(B.F0);
  return glm::translate(mat4(1), x[b * 4]) * mat4(R) * glm::translate(mat4(1), -B.c0) * m0[i];
}
vec3 Ragdoll::centre() const {
  if (bodies.empty()) return vec3(0);
  vec3 s(0); for (size_t b = 0; b < bodies.size(); b++) s += x[b * 4];
  return s / (float)bodies.size();
}
// how deep a point is in the solid: the shortest way out that doesn't end in another solid box (0 = not inside)
static float depthAt(const World& w, vec3 P) { vec3 d; return wayOut(WorldSolid(w), P, P, false, d) ? glm::length(d) - 0.002f : 0.f; }
float Ragdoll::penetration(const World& w, int* part) const {
  float worst = 0;
  for (int i = 0; i < parts(); i++) {
    if (!posed(i)) continue;
    const vec3 sz = hi0[i] - lo0[i];
    if (sz.x <= 0 || sz.y <= 0 || sz.z <= 0) continue;   // (a pivot only: nothing drawn)
    const mat4 M = partMatrix(i);
    const int N = 5;
    for (int a = 0; a < N; a++) for (int b = 0; b < N; b++) for (int c = 0; c < N; c++) {
      const vec3 u(a / float(N - 1), b / float(N - 1), c / float(N - 1));
      const vec3 P = origin + vec3(M * vec4(lo0[i] + sz * u, 1));
      const float d = depthAt(w, P);
      if (d > worst) { worst = d; if (part) *part = i; }
    }
  }
  return worst;
}

float Ragdoll::jointGap(int* part) const {
  float g = 0;
  for (int b = 0; b < (int)bodies.size(); b++) if (bodies[b].parent >= 0) {
    const float d = glm::distance(at(b, bodies[b].qSelf), at(bodies[b].parent, bodies[b].qParent));
    if (d > g) { g = d; if (part) *part = bodies[b].part; }
  }
  return g;
}
float Ragdoll::lowest() const { float y = 1e9f; for (auto& p : x) y = std::min(y, p.y); return y; }

// ------------------------------------------------------------------ creatures
bool creatureRagdolls(const Creature& m) {
  if (m.T->npc || m.T->boss || m.T->explicitSkin || m.state == "dying") return false;
  if (m.type == "ender_dragon" || m.type == "wither" || m.type == "wither_storm" || m.type == "end_crystal") return false;
  const RigModel* M = rigModel(m.modelKey());
  return M && !M->parts.empty();
}
bool ragdollFromCreature(Creature& m, Ragdoll& out) {
  const RigModel* M = rigModel(m.modelKey());
  if (!M || M->parts.empty()) return false;
  rigPose(m, *M, creatureBase(m, false));   // the pose it died in (the topple is ours now)
  if (m.mats.size() != M->parts.size()) return false;
  std::vector<RagdollPartIn> in(M->parts.size());
  for (size_t i = 0; i < M->parts.size(); i++) {
    const RigPart& p = M->parts[i];
    RagdollPartIn& r = in[i];
    r.parent = p.parent; r.m = m.mats[i]; r.lo = p.min; r.hi = p.max;
    r.simulate = p.parent < 0 || !p.limb.empty();
    r.limit = p.limb == "head" ? 0.75f : p.limb.rfind("wing", 0) == 0 ? 1.5f : p.limb == "tail" ? 1.0f : 1.25f;
  }
  vec3 kick(0);
  if (auto k = m.data.find("rk"); k != m.data.end() && k->is_array() && k->size() == 3) kick = vec3((*k)[0].get<float>(), (*k)[1].get<float>(), (*k)[2].get<float>());
  const vec3 vel = m.ghost ? vec3(0) : vec3(m.vel.x, std::max(m.vel.y, -6.f), m.vel.z);
  out.build(m.pos, in, vel, kick, m.id * 2654435761u, vec3(std::cos(m.yaw), 0, -std::sin(m.yaw)));   // (its right, yaw 0 facing -z)
  return true;
}

void RagdollSet::update(Level& lv, int realm, vec3 focus, float dt) {
  World* w = lv.worldIfOpen(realm);
  if (!w) return;
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != realm) continue;
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c || c->deathTime <= 0 || seen.count(c->id)) continue;
    if (c->deathTime > 1.0f) { seen[c->id] = 0; continue; }   // came into view long dead: leave it
    if (glm::distance(c->pos, focus) > range) continue;
    seen[c->id] = 0;
    if (!creatureRagdolls(*c)) continue;
    CreatureRagdoll r; r.id = c->id; r.type = c->type; r.model = c->modelKey(); r.realm = realm;
    if (!ragdollFromCreature(*c, r.rd)) continue;
    if (const RigModel* M = rigModel(r.model)) { r.shown.resize(M->parts.size()); for (size_t i = 0; i < M->parts.size(); i++) r.shown[i] = rigPartShown(*c, M->parts[i]) ? 1 : 0; }
    if (onNew) onNew(r, *c);
    while ((int)list.size() >= cap && !list.empty()) list.erase(list.begin());
    list.push_back(std::move(r));
  }
  for (auto& r : list) { r.t += dt; if (r.realm == realm) r.rd.step(*w, dt); }
  list.erase(std::remove_if(list.begin(), list.end(), [&](const CreatureRagdoll& r) { return r.t > life || r.realm != realm; }), list.end());
  for (auto it = seen.begin(); it != seen.end();) {
    if (!lv.entity(it->first) && !find(it->first)) it = seen.erase(it); else ++it;
  }
}

} // namespace dq
