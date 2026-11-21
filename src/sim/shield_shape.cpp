// sim/shield_shape.cpp — see shield_shape.h
#include "sim/shield_shape.h"
#include "sim/player.h"
#include "core/registry.h"
#include <algorithm>
#include <cmath>

namespace dq {

namespace {
// the left-forearm shield in the body's frame (x right, y up, z back; the feet at 0; blocks), from the rig:
// middle, across, up, out of the face
struct Sample { vec3 c, ax, ay, az; };
const Sample CARRY = {{-0.532f, 1.075f, -0.174f}, {0.011f, -0.058f, 0.998f}, {0.159f, 0.986f, 0.055f}, {-0.987f, 0.158f, 0.020f}};
const Sample CARRY_SNEAK = {{-0.532f, 0.842f, -0.318f}, {0.011f, 0.382f, 0.924f}, {0.159f, 0.912f, -0.379f}, {-0.987f, 0.151f, -0.050f}};
// on guard it follows the look up and down: sampled with the head pitched -0.8, 0, +0.8 (the head is 0.8 of the look)
const Sample GUARD[3] = {
  {{-0.185f, 1.048f, -0.460f}, {-0.992f, -0.114f, 0.063f}, {-0.123f, 0.979f, -0.161f}, {-0.043f, -0.167f, -0.985f}},
  {{-0.193f, 1.122f, -0.510f}, {-0.997f, -0.066f, 0.029f}, {-0.062f, 0.992f, 0.110f}, {-0.036f, 0.107f, -0.994f}},
  {{-0.197f, 1.206f, -0.538f}, {-1.000f, -0.010f, 0.009f}, {-0.005f, 0.928f, 0.373f}, {-0.012f, 0.373f, -0.928f}}};
const Sample GUARD_SNEAK[3] = {
  {{-0.124f, 0.754f, -0.572f}, {-0.990f, 0.128f, 0.060f}, {0.078f, 0.846f, -0.527f}, {-0.118f, -0.517f, -0.848f}},
  {{-0.119f, 0.804f, -0.646f}, {-0.987f, 0.160f, 0.010f}, {0.151f, 0.948f, -0.280f}, {-0.054f, -0.275f, -0.960f}},
  {{-0.106f, 0.870f, -0.704f}, {-0.979f, 0.203f, -0.030f}, {0.204f, 0.979f, -0.010f}, {0.027f, -0.016f, -1.000f}}};

Sample lerp(const Sample& a, const Sample& b, float t) {
  return {glm::mix(a.c, b.c, t), glm::mix(a.ax, b.ax, t), glm::mix(a.ay, b.ay, t), glm::mix(a.az, b.az, t)};
}
bool isShield(const ItemStack& s) {
  if (s.empty()) return false;
  const ItemDef* d = reg().item(s.id);
  return s.id == "shield" || (d && d->has("shield"));
}
}  // namespace

ShieldBox shieldBoxAt(const vec3& pos, float yaw, float pitch, bool guard, bool sneak, bool rightHand) {
  Sample s;
  if (!guard) s = sneak ? CARRY_SNEAK : CARRY;
  else {
    const Sample* G = sneak ? GUARD_SNEAK : GUARD;
    const float hp = std::clamp(pitch * 0.8f, -0.8f, 0.8f) / 0.8f;   // -1 .. 1
    s = hp < 0 ? lerp(G[1], G[0], -hp) : lerp(G[1], G[2], hp);
  }
  if (rightHand) {   // the mirror image
    const vec3 m(-1, 1, 1);
    s.c *= m; s.ax *= m; s.ay *= m; s.az *= m;
  }
  // body frame -> world: x right, z back
  const float sn = std::sin(yaw), cs = std::cos(yaw);
  const vec3 right(cs, 0, -sn), back(sn, 0, cs), up(0, 1, 0);
  auto dir = [&](const vec3& v) { return right * v.x + up * v.y + back * v.z; };
  ShieldBox b;
  b.c = pos + dir(s.c);
  b.az = glm::normalize(dir(s.az));
  b.ax = glm::normalize(dir(s.ax) - b.az * glm::dot(dir(s.ax), b.az));
  b.ay = glm::cross(b.az, b.ax);
  if (glm::dot(b.ay, dir(s.ay)) < 0) b.ay = -b.ay;
  return b;
}

bool playerShieldBox(const Player& p, ShieldBox& out) {
  if (p.dead || p.sleeping || p.swimming || p.crawling || p.riding || p.dodgeT > 0) return false;
  const int side = shieldSide(p);   // (sim/combat_rpg.h: a shield in a hand, or the shield slot's on the free forearm)
  if (!side) return false;
  // a two-handed block does not raise a shield that is also equipped, so arrows do not meet the raised box
  bool twoHand = false;
  if (p.blockT > 0 && p.selected >= 0 && p.selected < (int)p.inv.size()) {
    const ItemDef* d = p.inv[p.selected].def();
    twoHand = d && d->num("hands") >= 2.0;
  }
  out = shieldBoxAt(p.pos, p.yaw, p.pitch, p.blockT > 0 && !twoHand, p.sneaking, side > 0);
  return true;
}

bool shieldRayHit(const ShieldBox& b, const vec3& o, const vec3& d, float maxT, float* tHit) {
  const vec3 ro = o - b.c;
  const vec3 axes[3] = {b.ax, b.ay, b.az};
  float t0 = 0, t1 = maxT;
  for (int i = 0; i < 3; i++) {
    const float e = glm::dot(axes[i], ro), f = glm::dot(axes[i], d);
    if (std::fabs(f) < 1e-7f) {
      if (std::fabs(e) > b.half[i]) return false;
      continue;
    }
    float ta = (-b.half[i] - e) / f, tb = (b.half[i] - e) / f;
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  if (tHit) *tHit = t0;
  return true;
}

bool shieldContains(const ShieldBox& b, const vec3& p, float margin) {
  const vec3 r = p - b.c;
  return std::fabs(glm::dot(r, b.ax)) <= b.half.x + margin && std::fabs(glm::dot(r, b.ay)) <= b.half.y + margin &&
         std::fabs(glm::dot(r, b.az)) <= b.half.z + margin;
}

}  // namespace dq
