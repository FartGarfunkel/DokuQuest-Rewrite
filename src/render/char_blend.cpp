// render/char_blend.cpp — see char_blend.h
#include "render/char_blend.h"
#include <glm/gtc/quaternion.hpp>

namespace dq {

namespace {
enum : u32 {
  K_SNEAK = 1u << 0, K_SIT = 1u << 1, K_SLEEP = 1u << 2, K_SWIM = 1u << 3, K_CRAWL = 1u << 4, K_HANG = 1u << 5, K_LADDER = 1u << 6,
  K_FLY = 1u << 7, K_GLIDE = 1u << 8, K_PADDLE = 1u << 9, K_CARRY = 1u << 10, K_EAT = 1u << 11, K_BOW = 1u << 12, K_SCOPE = 1u << 13,
  K_AIR = 1u << 14, K_DEAD = 1u << 15, K_SPRINT = 1u << 16, K_MAP = 1u << 17, K_CAST = 1u << 18, K_BLOCK = 1u << 19, K_XBOW = 1u << 20,
  K_THROW = 1u << 21, K_SPEAR = 1u << 22, K_HELD = 1u << 23, K_FP = 1u << 24, K_DRINK = 1u << 25,
};
// a rotation's polar part (the nearest rotation): the frames carry a people's stature as a squash, so not pure rotations
glm::mat3 polar(glm::mat3 m) {
  const float d = glm::determinant(m);
  if (std::fabs(d) < 1e-12f) return glm::mat3(1);
  m /= std::cbrt(d);
  for (int i = 0; i < 6; i++) m = 0.5f * (m + glm::transpose(glm::inverse(m)));
  return m;
}
}

u32 charStateKey(const CharPose& p) {
  u32 k = 0;
  if (p.sneak) k |= K_SNEAK; if (p.sit) k |= K_SIT; if (p.sleep) k |= K_SLEEP; if (p.swim) k |= K_SWIM; if (p.crawl) k |= K_CRAWL;
  if (p.hang || p.climb > 0) k |= K_HANG; if (p.ladder) k |= K_LADDER; if (p.fly) k |= K_FLY; if (p.glide) k |= K_GLIDE;
  if (p.paddle) k |= K_PADDLE; if (p.carry) k |= K_CARRY; if (p.eat > 0) k |= K_EAT; if (p.eat > 0 && p.drink) k |= K_DRINK;
  if (p.bow > 0) k |= K_BOW; if (p.scope) k |= K_SCOPE; if (p.air) k |= K_AIR; if (p.deathT > 0) k |= K_DEAD; if (p.sprint) k |= K_SPRINT;
  if (p.mapMain || p.mapOff) k |= K_MAP; if (p.castWind > 0) k |= K_CAST; if (p.block > 0) k |= K_BLOCK;
  if (p.xbowCharge > 0 || p.xbowAim) k |= K_XBOW; if (p.throwCharge > 0) k |= K_THROW; if (p.spearCharge > 0) k |= K_SPEAR;
  if (p.held) k |= K_HELD; if (p.fp) k |= K_FP;
  return k;
}

float charBlendDuration(u32 from, u32 to) {
  const u32 ch = from ^ to;
  if (ch & K_FP) return 0;
  if (ch & (K_SWIM | K_SLEEP | K_CRAWL | K_GLIDE)) return 0.32f;    // the whole body turning over
  if (ch & (K_SIT | K_HANG | K_LADDER | K_FLY | K_PADDLE | K_DEAD)) return 0.24f;
  if (ch & ~(K_AIR | K_SPRINT | K_HELD)) return 0.18f;               // the arms: eating, a bow, a shield...
  if (ch & K_SPRINT) return 0.2f;
  return 0.12f;                                                       // a hop, a step off a block
}

mat4 blendMat(const mat4& a, const mat4& b, float w) {
  if (w <= 0) return a;
  if (w >= 1) return b;
  const glm::mat3 A(a), B(b), Qa = polar(A), Qb = polar(B);
  const glm::mat3 Ka = glm::transpose(Qa) * A, Kb = glm::transpose(Qb) * B;   // what's left: size and squash
  glm::quat qa = glm::quat_cast(Qa), qb = glm::quat_cast(Qb);
  const glm::mat3 M = glm::mat3_cast(glm::slerp(qa, qb, w)) * (Ka + (Kb - Ka) * w);
  mat4 r(M);
  r[3] = a[3] + (b[3] - a[3]) * w;
  return r;
}

void charBlendStep(CharBlend& b, CharRig& R, const mat4& base, u32 key, double now) {
  const mat4 inv = glm::inverse(base);
  CharRig L = R;   // about the feet
  for (int i = 0; i < CP_COUNT; i++) L.part[i] = inv * R.part[i];
  L.body = inv * R.body; L.bodyRaw = inv * R.bodyRaw;
  for (auto& [n, m] : L.gear) m = inv * m;
  if (!b.has) { b.has = true; b.key = key; b.t = b.dur = 0; b.last = now; b.shown = std::move(L); return; }
  const float dt = (float)std::clamp(now - b.last, 0.0, 0.1);
  b.last = now;
  if (key != b.key) {
    b.dur = charBlendDuration(b.key, key);
    b.key = key; b.t = 0;
    b.from = b.shown;
  } else b.t += dt;
  if (b.t >= b.dur) { b.shown = std::move(L); return; }
  const float u = b.t / b.dur, w = u * u * (3 - 2 * u);
  for (int i = 0; i < CP_COUNT; i++) L.part[i] = blendMat(b.from.part[i], L.part[i], w);
  L.body = blendMat(b.from.body, L.body, w); L.bodyRaw = blendMat(b.from.bodyRaw, L.bodyRaw, w);
  for (auto& [n, m] : L.gear) { auto it = b.from.gear.find(n); if (it != b.from.gear.end()) m = blendMat(it->second, m, w); }
  for (int i = 0; i < CP_COUNT; i++) R.part[i] = base * L.part[i];
  R.body = base * L.body; R.bodyRaw = base * L.bodyRaw;
  for (auto& [n, m] : R.gear) m = base * L.gear[n];
  b.shown = std::move(L);
}

} // namespace dq
