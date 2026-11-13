// render/loco_poses.cpp — standing, walking and running as Soulrend's Epic Fight has them (its living/idle, walk and run
// loops, sampled into our rig's angles by tools/port/gen_loco_poses.py): idle on its own clock, walk and run on the walk
// phase (one loop = one stride of both legs, the right leg forward at a quarter), mixed by how fast you go.
#include "render/char_anim.h"
#include <cmath>

namespace dq {
namespace {
#include "render/loco_poses_data.inc"
constexpr int N = 32;
void sample(const float (*F)[31], float u, float out[31]) {
  u -= std::floor(u);
  const float f = u * N; const int i = (int)f % N, j = (i + 1) % N; const float w = f - std::floor(f);
  for (int k = 0; k < 31; k++) {
    float a = F[i][k], b = F[j][k];
    if (k < 30) { float d = b - a; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; b = a + d; }
    out[k] = a + (b - a) * w;
  }
}
}  // namespace

void locoPose(CharAngles& A, const CharPose& pose) {
  const float a = std::clamp(pose.walkAmt, 0.f, 1.f), run = std::clamp(pose.runAmt >= 0 ? pose.runAmt : pose.sprint ? 1.f : 0.f, 0.f, 1.f);
  float I[31], W[31], R[31], P[31];
  sample(LOCO_IDLE, pose.time / LOCO_IDLE_LEN + (pose.seed % 97) / 97.f, I);   // (each their own moment in it)
  float u = pose.walk / 6.28318f;
  if (pose.backward) u = -u;   // backing off: the stride played backwards
  sample(LOCO_WALK, u, W); sample(LOCO_RUN, u, R);
  for (int k = 0; k < 31; k++) { const float mv = W[k] + (R[k] - W[k]) * run; P[k] = I[k] + (mv - I[k]) * a; }
  vec3* v = &A.body;   // body head armR foreR armL foreL legR shinR legL shinL, in CharAngles' order
  for (int p = 0; p < 10; p++) v[p] = vec3(P[p * 3], P[p * 3 + 1], P[p * 3 + 2]);
  A.lift = P[30];
}

}  // namespace dq
