// render/attack_poses.cpp — the attack moves drawn (sim/attack_moves.h): each move's keys are our own rig's angles fitted
// to measured swings (tools/port/gen_attack_poses.py -> attack_poses_data.inc), timed so the blade passes through the
// air in front of you exactly while the move is live (hitFrom..hitTo). In from the stance over the blend before the
// keys start, the keys, the follow-through held to the move's recovery, then back out to the stance by its end.
#include "render/char_anim.h"
#include "sim/attack_moves.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>

namespace dq {
namespace {
struct AtkKey { float t, bx, by, lift, legR, legL, shinR, shinL, ax, ay, az, fx, gx, gz, ox, oy, oz, ofx; };
struct AtkClip { const AtkKey* k; int n; bool left; float pre, blend; };   // blend: in from the stance (the clip's own, at most 0.1 s)
#include "render/attack_poses_data.inc"
float sm(float u) { u = std::clamp(u, 0.f, 1.f); return u * u * (3 - 2 * u); }
// new families borrow a drawn cut until they have their own. The sword samples stay where they are.
int clipOf(u8 id) {
  switch (id) {
    case MV_GS1: return MV_SWORD1 - 1;
    case MV_GS2: return MV_SWORD2 - 1;
    case MV_SPEAR1: return MV_SWORD_DASH - 1;
    case MV_SPEAR2: return MV_SWORD2 - 1;
    case MV_MACE1: return MV_AXE1 - 1;
    case MV_MACE2: return MV_AXE2 - 1;
    default: return (int)id - 1;
  }
}
}  // namespace

float attackWeight(const CharPose& pose) {
  const u8 id = pose.atkMove;
  if (id == MV_NONE || id >= MV_COUNT) return 0;
  const AttackMove& M = attackMove(id);
  const float t = pose.atkT, blend = CLIPS[clipOf(id)].blend;
  const float out = M.len > M.recovery ? 1 - sm((t - M.recovery) / (M.len - M.recovery)) : 1.f;
  return std::max(0.f, std::min(sm(t / blend), out));
}

bool devAttackPose(u8& move, float& t) {
  static int id = -1; static float at = 0; static bool read = false;
  if (!read) { read = true; if (const char* v = std::getenv("DQ_DEV_ATK")) std::sscanf(v, "%d,%f", &id, &at); }
  if (id <= 0 || id >= MV_COUNT) return false;
  move = (u8)id; t = at; return true;
}

mat4 gripTurn(const mat4& fore, vec2 turn) {
  if (turn.x == 0 && turn.y == 0) return fore;
  mat4 m = glm::translate(fore, vec3(0, -6, -1));
  m = glm::rotate(m, turn.x, vec3(1, 0, 0)); m = glm::rotate(m, turn.y, vec3(0, 0, 1));
  return glm::translate(m, vec3(0, 6, 1));
}

// the body mirrored left for right (turns about x kept, about y and z the other way): an off-hand blow is the main hand's
// mirrored, so the stance is mirrored, the move laid over it, and the whole mirrored back
static void mirrorAngles(CharAngles& A) {
  auto m = [](vec3& v) { v.y = -v.y; v.z = -v.z; };
  std::swap(A.armR, A.armL); std::swap(A.foreR, A.foreL); std::swap(A.legR, A.legL); std::swap(A.shinR, A.shinL); std::swap(A.toolR, A.toolL);
  for (vec3* v : {&A.armR, &A.armL, &A.foreR, &A.foreL, &A.legR, &A.legL, &A.shinR, &A.shinL, &A.body, &A.head}) m(*v);
  A.toolR.y = -A.toolR.y; A.toolL.y = -A.toolL.y;
}
static void attackPoseRaw(CharAngles& A, const CharPose& pose);
void attackPose(CharAngles& A, const CharPose& pose) {
  if (!pose.atkOff) { attackPoseRaw(A, pose); return; }
  mirrorAngles(A); attackPoseRaw(A, pose); mirrorAngles(A);
}
// lay one fitted clip on the body. kt is the clip's own time (after its blend).
static void applyClip(CharAngles& A, const CharPose& pose, const AtkClip& C, float kt, float k) {
  if (k <= 0) return;
  const AtkKey* K = C.k;
  int i = 0; while (i < C.n - 2 && kt > K[i + 1].t) i++;
  const AtkKey &a = K[i], &b = K[std::min(i + 1, C.n - 1)];
  const float w = b.t > a.t ? std::clamp((kt - a.t) / (b.t - a.t), 0.f, 1.f) : 1.f;
  auto L = [&](float AtkKey::*f) { return a.*f + (b.*f - a.*f) * w; };
  auto mixv = [](vec3 x, vec3 y, float u) { return x + (y - x) * u; };
  const float walk = std::clamp(pose.walkAmt * 1.5f, 0.f, 1.f), st = k * (1 - walk);   // (walking keeps its stride)
  A.body.x += L(&AtkKey::bx) * k; A.body.y += L(&AtkKey::by) * k;
  A.lift += L(&AtkKey::lift) * st;
  A.legR.x = mixv(vec3(A.legR.x), vec3(L(&AtkKey::legR)), st).x; A.legL.x = mixv(vec3(A.legL.x), vec3(L(&AtkKey::legL)), st).x;
  A.shinR.x = mixv(vec3(A.shinR.x), vec3(L(&AtkKey::shinR)), st).x; A.shinL.x = mixv(vec3(A.shinL.x), vec3(L(&AtkKey::shinL)), st).x;
  A.legR.y += L(&AtkKey::by) * 0.35f * st; A.legL.y += L(&AtkKey::by) * 0.35f * st;   // (the feet turn a little with the hips)
  const vec3 arm(L(&AtkKey::ax), L(&AtkKey::ay), L(&AtkKey::az)), other(L(&AtkKey::ox), L(&AtkKey::oy), L(&AtkKey::oz));
  const vec2 grip(L(&AtkKey::gx), L(&AtkKey::gz));
  if (!C.left) {
    A.armR = mixv(A.armR, arm, k); A.foreR.x += (L(&AtkKey::fx) - A.foreR.x) * k; A.toolR = grip * k;
    if (!(pose.atkOff ? pose.held : pose.off)) { A.armL = mixv(A.armL, other, k * 0.85f); A.foreL.x += (L(&AtkKey::ofx) - A.foreL.x) * k * 0.85f; }
  } else {
    A.armL = mixv(A.armL, arm, k); A.foreL.x += (L(&AtkKey::fx) - A.foreL.x) * k; A.toolL = grip * k;
    A.armR = mixv(A.armR, other, k * 0.85f); A.foreR.x += (L(&AtkKey::ofx) - A.foreR.x) * k * 0.85f;
  }
}

static void attackPoseRaw(CharAngles& A, const CharPose& pose) {
  const u8 id = pose.atkMove;
  if (id == MV_NONE || id >= MV_COUNT) return;
  const float k = attackWeight(pose);
  const AtkClip& C = CLIPS[clipOf(id)];
  // the keys' own time starts after the blend in; the move's live window is blend + the clip's contact (sim/attack_moves.cpp)
  float kt = pose.atkT - C.blend;
  if (id == MV_GS1 || id == MV_GS2 || id == MV_SPEAR1 || id == MV_SPEAR2) {
    // the part of the borrowed clip where the blade is in front. The wind-up plays before the hit,
    // the cut plays through the hit, and we do not run on into the pose where the blade just lies out flat.
    const AttackMove& M = attackMove(id);
    const float c0 = id == MV_GS1 ? 0.04f : id == MV_SPEAR1 ? 0.08f : 0.10f;
    const float c1 = id == MV_GS1 ? 0.105f : id == MV_SPEAR1 ? 0.22f : 0.16f;
    if (M.hitFrom <= 1e-4f || pose.atkT <= M.hitFrom) kt = c0 * (M.hitFrom > 1e-4f ? std::clamp(pose.atkT / M.hitFrom, 0.f, 1.f) : 1.f);
    else if (pose.atkT >= M.hitTo) kt = c1;
    else kt = c0 + (c1 - c0) * ((pose.atkT - M.hitFrom) / (M.hitTo - M.hitFrom));
  }
  applyClip(A, pose, C, kt, k);
  // look pitch is applied as a shoulder rotation in charRig (same axis as attackFrameToWorld), not as extra arm angles
}

} // namespace dq
