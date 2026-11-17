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
  origin = o; age = 0; frozen = false; stillSteps = 0; acc = 0;
  bodies.clear(); x.clear(); px.clear(); rlen.clear();
  m0.assign(parts.size(), mat4(1)); owner.assign(parts.size(), -1);
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

// a point inside a block's collision box: how to get it out (towards where it came from, else the shortest way up
// or sideways into free space). false = not inside anything.
static bool pushOut(const World& w, vec3 P, vec3 prevP, vec3& d) {
  thread_local std::vector<AABB> boxes;
  const int bx = ifloor(P.x), by = ifloor(P.y), bz = ifloor(P.z);
  if (by < 0 || by >= WORLD_H) return false;
  boxes.clear(); blockBoxes(w, bx, by, bz, boxes);
  for (const AABB& b : boxes) {
    if (!(P.x > b.lo.x && P.x < b.hi.x && P.y > b.lo.y && P.y < b.hi.y && P.z > b.lo.z && P.z < b.hi.z)) continue;
    static const ivec3 DIRS[6] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
    float best = 1e9f; vec3 bd(0, b.hi.y - P.y + 0.001f, 0);
    for (const ivec3& D : DIRS) {
      const float pen = D.x > 0 ? b.hi.x - P.x : D.x < 0 ? P.x - b.lo.x : D.y > 0 ? b.hi.y - P.y : D.y < 0 ? P.y - b.lo.y : D.z > 0 ? b.hi.z - P.z : P.z - b.lo.z;
      const vec3 out = P + vec3(D) * (pen + 0.01f);
      if (reg().tSolid[w.getBlockPhys(ifloor(out.x), clampv(ifloor(out.y), 0, WORLD_H - 1), ifloor(out.z))] && collides(w, out.x - 0.001, out.y - 0.001, out.z - 0.001, out.x + 0.001, out.y + 0.001, out.z + 0.001)) continue;
      const bool cameFrom = D.x > 0 ? prevP.x >= b.hi.x : D.x < 0 ? prevP.x <= b.lo.x : D.y > 0 ? prevP.y >= b.hi.y : D.y < 0 ? prevP.y <= b.lo.y : D.z > 0 ? prevP.z >= b.hi.z : prevP.z <= b.lo.z;
      const float cost = pen * (cameFrom ? 0.3f : 1.f) * (D.y > 0 ? 0.6f : D.y < 0 ? 1.5f : 1.f);
      if (cost < best) { best = cost; bd = vec3(D) * (pen + 0.001f); }
    }
    d = bd;
    return true;
  }
  return false;
}

void Ragdoll::sub(const World& w, float h) {
  const size_t n = x.size();
  for (size_t i = 0; i < n; i++) {
    const vec3 v = (x[i] - px[i]) * (age < 2 ? 0.995f : 0.95f);   // (after the fall, the twitching dies down)
    px[i] = x[i];
    x[i] += v + vec3(0, -RD_G * h * h, 0);
  }
  const int nb = (int)bodies.size();
  for (int it = 0; it < 4; it++) {
    for (int b = 0; b < nb; b++) rigid(b);
    for (int b = 0; b < nb; b++) {
      const Body& B = bodies[b];
      if (B.parent < 0) continue;
      const vec3 C = at(b, B.qSelf) - at(B.parent, B.qParent);
      auto den = [&](int k, vec3 q) { const Body& K = bodies[k]; float w1 = q.x / K.L.x, w2 = q.y / K.L.y, w3 = q.z / K.L.z, w0 = 1 - w1 - w2 - w3; return w0 * w0 + w1 * w1 + w2 * w2 + w3 * w3; };
      const float db = den(b, B.qSelf), dp = den(B.parent, B.qParent), s = db + dp;
      if (s < 1e-8f) continue;
      // (the parent is the heavier: a dangling paw doesn't fling the torso about)
      push(b, B.qSelf, -C * (db / s) * 1.0f);
      push(B.parent, B.qParent, C * (dp / s) * 0.6f);
      // the joint's cone: no limb bends further than it could
      if (B.hasRest) {
        const vec3 a = glm::normalize(frame(B.parent) * B.rest);
        const vec3 jp = at(B.parent, B.qParent);
        const vec3 v = x[b * 4] - jp; const float lv = glm::length(v);
        if (lv > 1e-4f) {
          const vec3 vn = v / lv;
          if (glm::dot(vn, a) < std::cos(B.limit)) {
            vec3 ax = glm::cross(a, vn);
            if (glm::length(ax) < 1e-6f) ax = std::fabs(a.y) < 0.9f ? glm::cross(a, vec3(0, 1, 0)) : glm::cross(a, vec3(1, 0, 0));
            ax = glm::normalize(ax);
            const vec3 t = a * std::cos(B.limit) + glm::cross(ax, a) * std::sin(B.limit);
            push(b, vec3(0), (jp + t * lv - x[b * 4]) * 0.5f);
          }
        }
      }
    }
    if (it == 1 || it == 3) for (int b = 0; b < nb; b++) {   // the ground: every corner of the piece's box, and its middle
      const Body& B = bodies[b];
      bool touched = false;
      for (int c = 0; c < 9; c++) {
        const vec3 q = c == 8 ? vec3(0) : vec3(c & 1 ? B.L.x : -B.L.x, c & 2 ? B.L.y : -B.L.y, c & 4 ? B.L.z : -B.L.z) * 0.92f;
        const vec3 P = at(b, q);
        const vec3 pc = px[b * 4];
        const vec3 prevP = pc + (px[b * 4 + 1] - pc) * (q.x / B.L.x) + (px[b * 4 + 2] - pc) * (q.y / B.L.y) + (px[b * 4 + 3] - pc) * (q.z / B.L.z);
        vec3 d;
        if (pushOut(w, origin + P, origin + prevP, d)) { push(b, q, d); touched = true; }
      }
      if (touched) for (int k = 0; k < 4; k++) px[b * 4 + k] += (x[b * 4 + k] - px[b * 4 + k]) * 0.18f;   // friction: it scrapes to a stop
    }
  }
  float most = 0;
  for (size_t i = 0; i < n; i++) most = std::max(most, glm::distance(x[i], px[i]));
  stillSteps = most < (age < 3 ? RD_SETTLE : RD_SETTLE * 2) ? stillSteps + 1 : 0;
  age += h;
  if (stillSteps > 30 || age > 12) frozen = true;
}

void Ragdoll::step(const World& w, float dt) {
  if (frozen || bodies.empty()) return;
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
