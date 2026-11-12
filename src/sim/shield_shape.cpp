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
  {{-0.193f, 1.083f, -0.271f}, {-0.885f, 0.115f, 0.451f}, {0.097f, 0.993f, -0.063f}, {-0.456f, -0.012f, -0.890f}},
  {{-0.190f, 1.202f, -0.292f}, {-0.882f, -0.085f, 0.464f}, {0.010f, 0.980f, 0.200f}, {-0.471f, 0.181f, -0.863f}},
  {{-0.198f, 1.323f, -0.282f}, {-0.861f, -0.281f, 0.423f}, {-0.069f, 0.890f, 0.450f}, {-0.503f, 0.359f, -0.786f}}};
const Sample GUARD_SNEAK[3] = {
  {{-0.259f, 0.777f, -0.420f}, {-0.907f, 0.126f, 0.401f}, {-0.078f, 0.886f, -0.456f}, {-0.413f, -0.445f, -0.794f}},
  {{-0.277f, 0.876f, -0.487f}, {-0.869f, -0.049f, 0.492f}, {-0.171f, 0.964f, -0.206f}, {-0.464f, -0.263f, -0.846f}},
  {{-0.306f, 0.987f, -0.525f}, {-0.813f, -0.238f, 0.532f}, {-0.243f, 0.968f, 0.062f}, {-0.529f, -0.078f, -0.845f}}};

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
  const bool mainSh = isShield(p.inv[p.selected]);
  const bool offSh = !p.offhand.empty() && isShield(p.offhand[0]);
  if (!mainSh && !offSh) return false;
  out = shieldBoxAt(p.pos, p.yaw, p.pitch, p.blockT > 0, p.sneaking, mainSh);
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
