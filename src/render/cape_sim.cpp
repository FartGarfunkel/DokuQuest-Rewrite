// render/cape_sim.cpp — see cape_sim.h
//
// Worked in angles, not loose points: from the air rushing past the back (running, falling, a turn, the wind) each strip
// gets the angle the cloth would settle at (the drag against its weight, more at the hem than the shoulders), and swings
// to it on a damped spring (slower toward the hem, so it lags and follows through). A chain of free points (the first
// try) went chaotic at a run (its top moved most of a strip's length each step) and twitched at a high frame rate: this
// can't, and moves the same at any frame rate. The strips are held to the body as before (never in through the back,
// never bent more than cloth bends from one strip to the next).
#include "render/cape_sim.h"
#include <cmath>
#include <algorithm>

namespace dq {

static constexpr float STEP = 1.f / 120;    // fixed steps, so it moves the same at any frame rate
static constexpr float GRAVITY = 14.f;      // the cloth's weight against the drag (blocks / s^2)
static constexpr float DRAG = 2.2f;         // the air's pull, per second (more at the hem: see below)
static constexpr float BEND = 0.35f;        // the most one strip turns from the one above (about 20 degrees)
static constexpr float SWAY_MAX = 0.42f;    // the most it swings to the side

static vec3 xform(const mat4& m, vec3 p) { return vec3(m * vec4(p, 1)); }

// clamps the strips' angles to the body and fills the outputs (bend / side: each strip's turn from the one above)
static void holdToBody(CapeSim& s, float maxLift) {
  float liftUp = 0, swayUp = 0;
  for (int i = 0; i < CapeSim::N; i++) {
    float& lift = s.lift[i]; float& sway = s.sway[i];
    if (!std::isfinite(lift)) lift = 0;
    if (!std::isfinite(sway)) sway = 0;
    const float lo = i == 0 ? 0.f : std::max(0.f, liftUp - BEND), hi = i == 0 ? maxLift : std::min(maxLift, liftUp + BEND);
    lift = std::clamp(lift, lo, std::max(lo, hi));   // never in through the back or the legs
    sway = std::clamp(sway, i == 0 ? -SWAY_MAX : std::max(-SWAY_MAX, swayUp - BEND), i == 0 ? SWAY_MAX : std::min(SWAY_MAX, swayUp + BEND));
    s.bend[i] = lift - liftUp; s.side[i] = sway - swayUp;
    liftUp = lift; swayUp = sway;
  }
}

void capeStep(CapeSim& s, const mat4& body, vec3 anchor, vec3 wind, double time, float maxLift) {
  const vec3 A = xform(body, anchor);
  // the body's frame (no scale): its axes in the world
  const vec3 ax = glm::normalize(vec3(body[0])), ay = glm::normalize(vec3(body[1])), az = glm::normalize(vec3(body[2]));
  if (s.t < 0 || glm::distance(A, s.lastAnchor) > 2.f || !std::isfinite(A.x + A.y + A.z) || time < s.t) {   // first, or a teleport: hang it afresh
    for (int i = 0; i < CapeSim::N; i++) { s.lift[i] = s.sway[i] = 0; s.pt[i] = vec3(0); }
    s.t = time; s.acc = 0; s.lastAnchor = A; s.prev[0] = az; s.prev[1] = vec3(0);
    holdToBody(s, maxLift);
    return;
  }
  s.acc = std::min(s.acc + (float)(time - s.t), 0.25f);
  s.t = time;
  const int steps = (int)(s.acc / STEP);
  if (steps == 0) { holdToBody(s, maxLift); return; }   // (between steps it holds still: no twitching at a high frame rate)
  s.acc -= steps * STEP;
  const float dt = steps * STEP;
  // how fast the shoulders moved and the body turned since the last step, eased (frame times jitter)
  const vec3 vel = (A - s.lastAnchor) / dt;
  const vec3 back0 = s.prev[0];   // the back's facing at the last step
  float yawRate = 0;
  { const vec3 a(back0.x, 0, back0.z), b(az.x, 0, az.z); if (glm::length(a) > 1e-4f && glm::length(b) > 1e-4f) yawRate = std::atan2(a.x * b.z - a.z * b.x, glm::dot(a, b)) / dt; }
  s.prev[0] = az; s.lastAnchor = A;
  vec3& sv = s.prev[1];   // (the eased speed)
  sv += (vel - sv) * std::min(1.f, dt * 12.f);
  const vec3 air = wind - sv;   // the air past the back, in the world
  // into the body's frame: + z out behind, + x to the side, + y up
  const float flowBack = glm::dot(air, az), flowSide = glm::dot(air, ax) - yawRate * 0.35f, flowUp = glm::dot(air, ay);
  const float flowLen = std::hypot(flowBack, flowSide);
  for (int k = 0; k < steps; k++) {
    const float tk = (float)(time - (steps - 1 - k) * STEP);
    for (int i = 0; i < CapeSim::N; i++) {
      const float f = (i + 1.f) / CapeSim::N;
      const float drag = DRAG * (0.45f + 0.9f * f);
      // where the air holds it: blown out behind, pushed over by a side wind, lifted by a fall
      float lt = std::atan2(drag * std::max(0.f, flowBack) + 0.5f * drag * std::max(0.f, flowUp), GRAVITY);
      float st = std::atan2(drag * flowSide, GRAVITY + drag * std::max(0.f, flowBack));
      // a ripple running down it to the hem, more in a rush of air
      lt += std::sin(tk * 7.3f - i * 0.9f) * std::min(0.12f, 0.012f * flowLen) * f;
      st += std::sin(tk * 5.1f - i * 0.8f) * std::min(0.06f, 0.006f * flowLen) * f;
      // the spring: quick at the shoulders, slower at the hem (it lags and follows through)
      const float w = 11.f - 5.f * f, z = 0.6f;
      vec3& v = s.pt[i];   // (x: the lift's speed, y: the sway's)
      v.x += (w * w * (lt - s.lift[i]) - 2 * z * w * v.x) * STEP;
      v.y += (w * w * (st - s.sway[i]) - 2 * z * w * v.y) * STEP;
      s.lift[i] += v.x * STEP; s.sway[i] += v.y * STEP;
    }
    holdToBody(s, maxLift);
    for (int i = 0; i < CapeSim::N; i++)   // (held at a limit: it stops there, it doesn't push on through it)
      if ((s.lift[i] <= 0.f && s.pt[i].x < 0) || (s.lift[i] >= maxLift && s.pt[i].x > 0)) s.pt[i].x = 0;
  }
}

} // namespace dq
