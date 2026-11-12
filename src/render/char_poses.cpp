// render/char_poses.cpp — the newer poses laid on charAnimate's (render/char_anim.cpp), after AnimatedPlayer
// (thehippomaster), Not Enough Animations / Animations n' Stuff and Hold My Items (Punched Stuffs), used with permission:
// drinking with the head tipped back, the flinch from a hit, the knees giving as you die, a sleeper's slack limbs, and
// the poses for a shield, a crossbow, a trident and a spear. Each eases in by its own amount, so the blend between states
// (render/char_blend.h) has smooth ends to work between.
#include "render/char_anim.h"

namespace dq {

namespace {
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline vec3 mixv(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline float ease(float u) { u = clampf(u, 0, 1); return u * u * (3 - 2 * u); }
}

void charMorePoses(CharAngles& A, const CharPose& pose) {
  const float t = pose.time;
  const bool lying = pose.swim || pose.crawl || pose.sleep || pose.glide;
  // ---- drinking: the bottle tipped up to the lips and the head back with it, a swallow now and then ----
  if (pose.eat > 0 && pose.drink && !lying) {
    const float up = std::min(1.f, (1 - pose.eat) * 4);
    A.head.x -= 0.45f * up + std::max(0.f, std::sin(t * 9)) * 0.04f * up;
  } else if (pose.eat > 0 && !lying) A.head.x += std::max(0.f, std::sin(t * 22)) * 0.06f;   // chewing: the head nods with each bite
  // ---- What Are They Up To (render/activity_layer.cpp): looking down at the screen in their hands; dozing, the head dropped ----
  if (pose.gui && !lying) A.head.x += 0.18f;
  if (pose.doze && !lying) A.head.x += 0.55f + std::sin(t * 1.3f) * 0.04f;
  // ---- a hit: a sharp flinch back, the arms jerking up and out, then easing off ----
  if (pose.hurt > 0 && pose.hurt < 0.45f && pose.deathT <= 0 && !pose.sleep) {
    const float u = pose.hurt / 0.45f, f = u < 0.15f ? ease(u / 0.15f) : 1 - ease((u - 0.15f) / 0.85f);
    if (!lying && !pose.sit) { A.body.x += 0.2f * f; A.head.x -= 0.22f * f; A.shinR.x -= 0.18f * f; A.shinL.x -= 0.18f * f; A.legR.x += 0.08f * f; A.legL.x += 0.08f * f; A.lift -= 0.03f * f; }
    if (!pose.held) A.armR += vec3(0.35f, 0, 0.3f) * f;
    if (!pose.off) A.armL += vec3(0.35f, 0, -0.3f) * f;
    A.foreR.x += 0.3f * f; A.foreL.x += 0.3f * f;
  }
  // ---- dying: the knees give and the body folds, then (charRig tips it over) the limbs go slack ----
  if (pose.deathT > 0) {
    const float buckle = ease(pose.deathT / 0.16f), fall = ease((pose.deathT - 0.1f) / 0.38f), slack = ease((pose.deathT - 0.4f) / 0.3f);
    const float knees = buckle * (1 - 0.6f * slack);
    A.legR.x = 0.55f * knees + 0.05f; A.legL.x = 0.4f * knees - 0.05f;
    A.shinR.x = -1.1f * knees - 0.05f; A.shinL.x = -0.9f * knees - 0.1f;
    A.legR.z = 0.06f; A.legL.z = -0.08f - 0.1f * slack;
    A.body.x = -0.35f * buckle * (1 - fall) - 0.1f * fall; A.body.y = 0; A.body.z = 0;
    A.lift = -0.22f * buckle * (1 - fall);
    A.armR = mixv(vec3(0.5f, 0, 0.25f), vec3(0.3f, 0, 0.9f), slack); A.foreR = vec3(0.25f, 0, 0);
    A.armL = mixv(vec3(0.6f, 0, -0.3f), vec3(1.3f, 0, -0.35f), slack); A.foreL = vec3(0.45f, 0, 0);
    A.head = vec3(0.35f * buckle - 0.25f * slack, 0.3f * slack, 0);
  }
  // ---- asleep: slack arms at the sides, the head turned on the pillow, slow breathing ----
  if (pose.sleep && pose.deathT <= 0) {
    const float br = std::sin(t * 1.4f) * 0.025f;
    A.body = vec3(br, 0, 0); A.lift = 0;
    A.armR = vec3(0.08f, 0, 0.14f + br); A.armL = vec3(0.12f, 0, -0.16f - br); A.foreR = vec3(0.25f, 0, 0); A.foreL = vec3(0.35f, 0, 0);
    A.legR = vec3(0.04f, 0, 0.05f); A.legL = vec3(0.1f, 0, -0.04f); A.shinR = vec3(-0.08f, 0, 0); A.shinL = vec3(-0.2f, 0, 0);
    A.head = vec3(-br, ((pose.seed & 1) ? 0.35f : -0.35f), 0);
  }
  if (lying || pose.deathT > 0 || pose.hang || pose.climb > 0 || pose.ladder) return;
  const float pitch = clampf(pose.headPitch, -1.f, 1.f);
  // ---- sat on the ground (by a campfire): the seat down on it, knees up, leant forward a touch over them ----
  if (pose.sitGround) {
    static float G[4] = {0, 0, 0, 0}; static bool gl = false;   // tuning aid: DQ_DEV_SITG="thigh,shin,lift,lean" (added)
    if (!gl) { gl = true; if (const char* v = std::getenv("DQ_DEV_SITG")) std::sscanf(v, "%f,%f,%f,%f", &G[0], &G[1], &G[2], &G[3]); }
    A.legR = vec3(2.25f + G[0], 0, 0.16f); A.legL = vec3(2.25f + G[0], 0, -0.16f);
    A.shinR = A.shinL = vec3(-2.3f + G[1], 0, 0);
    A.lift = -0.57f + G[2]; A.body.x = 0.22f + G[3];
    if (!pose.reach) { A.armR = vec3(0.75f, 0.1f, 0.1f); A.foreR = vec3(0.6f, 0, 0); A.armL = vec3(0.75f, -0.1f, -0.1f); A.foreL = vec3(0.6f, 0, 0); }   // (forearms on the knees)
  }
  // ---- a dodge roll: curled up tight, knees to the chest, arms round them, the chin tucked (in fast, out at the end) ----
  if (pose.roll > 0) {
    const float u = clampf(pose.roll, 0, 1), k = std::sin(std::min(1.f, u * 4) * PI * 0.5f) * (u < 0.8f ? 1.f : (1 - u) / 0.2f);
    A.legR = mixv(A.legR, vec3(2.0f, 0, 0.12f), k); A.legL = mixv(A.legL, vec3(2.0f, 0, -0.12f), k);
    A.shinR = mixv(A.shinR, vec3(-2.6f, 0, 0), k); A.shinL = mixv(A.shinL, vec3(-2.6f, 0, 0), k);   // (heels to the seat)
    A.armR = mixv(A.armR, vec3(1.25f, 0.35f, 0), k); A.armL = mixv(A.armL, vec3(1.25f, -0.35f, 0), k);
    A.foreR = mixv(A.foreR, vec3(1.2f, 0, 0), k); A.foreL = mixv(A.foreL, vec3(1.2f, 0, 0), k);
    A.head = mixv(A.head, vec3(0.55f, 0, 0), k); A.body.x += 0.35f * k;
    return;
  }
  // ---- a shield raised across the body in the off hand (the elbow out, the forearm across the chest) ----
  // ---- on guard behind a shield (Epic Fight's guard): turned shield side on, feet apart (the shield side's ahead, both
  // knees bent), the shield arm out before the chest with the forearm across it. The weapon hand stays as it was (raised
  // back over the shoulder it read as a wind-up, not a guard). Written for the shield in the left hand and mirrored for
  // the right (y and z of an arm flip side to side). Walking keeps its stride: the stance is for standing.
  if (pose.block > 0) {
    const float k = ease(pose.block), sd = pose.blockMain ? -1.f : 1.f;
    static float G[16] = {0}; static bool gl = false;   // tuning aid: DQ_DEV_GUARD="turn,sx,sy,sz,sf,wx,wy,wz,wf,lead,back,knee,crouch,head" (added)
    if (!gl) { gl = true; if (const char* v = std::getenv("DQ_DEV_GUARD")) std::sscanf(v, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &G[0], &G[1], &G[2], &G[3], &G[4], &G[5], &G[6], &G[7], &G[8], &G[9], &G[10], &G[11], &G[12], &G[13]); }
    auto side = [&](vec3 v, bool left) { return left ? v : vec3(v.x, -v.y, -v.z); };   // (an angle for the left arm, for either)
    const bool shLeft = sd > 0;
    const float turn = (0.42f + G[0]) * k;
    A.body.y += sd * turn; A.head.y -= sd * turn * (0.85f + G[13]);   // (the head keeps looking where you aim)
    A.body.x -= 0.05f * k;
    // the shield arm: up before the chest, the forearm bent across it
    const vec3 sh = side(vec3(0.80f + G[1] + pitch * 0.35f, -0.45f + G[2], 0.18f + G[3]), shLeft), sf(1.30f + G[4], 0, 0);
    if (shLeft) { A.armL = mixv(A.armL, sh, k); A.foreL = mixv(A.foreL, sf, k); } else { A.armR = mixv(A.armR, sh, k); A.foreR = mixv(A.foreR, sf, k); }
    // the feet: the shield side's leg ahead, the other back, both knees bent, sunk a little (standing only)
    const float still = k * (1 - clampf(pose.walkAmt * 1.6f, 0, 1));
    vec3& legF = shLeft ? A.legL : A.legR; vec3& legB = shLeft ? A.legR : A.legL;
    vec3& shinF = shLeft ? A.shinL : A.shinR; vec3& shinB = shLeft ? A.shinR : A.shinL;
    legF.x += (0.38f + G[9]) * still; shinF.x -= (0.42f + G[11]) * still;
    legB.x -= (0.30f + G[10]) * still; shinB.x -= (0.30f + G[11]) * still;
    legF.y += sd * turn * 0.6f * still; legB.y += sd * turn * 0.6f * still;   // (the feet turned with the hips)
    A.lift -= (0.07f + G[12]) * still;
  }
  // ---- a crossbow: wound back against the hip (the foot in the stirrup), then levelled in both hands to the eye ----
  if (pose.xbowCharge > 0) {
    const float c = pose.xbowCharge, k = ease(std::min(1.f, c * 5)), pull = std::sin(std::min(1.f, c) * PI * 3) * 0.12f;
    A.armL = mixv(A.armL, vec3(0.75f, 0.35f, 0), k); A.foreL = mixv(A.foreL, vec3(0.6f, 0, 0), k);
    A.armR = mixv(A.armR, vec3(0.25f - 0.35f * c + pull, -0.2f, 0.15f), k); A.foreR = mixv(A.foreR, vec3(0.9f + 0.4f * c, 0, 0), k);
    A.body.x -= 0.22f * k; A.body.y -= 0.15f * k; A.head.x += 0.22f * k;
    A.legR.x += 0.35f * k; A.shinR.x -= 0.5f * k;
  } else if (pose.xbowAim) {
    A.armR = vec3(1.45f + pitch, -0.12f, 0); A.foreR = vec3(0.15f, 0, 0);
    A.armL = vec3(1.5f + pitch, 0.5f, 0); A.foreL = vec3(0.35f, 0, 0);
    A.body.y -= 0.18f; A.head.y += 0.18f;
  }
  // ---- a trident drawn back over the shoulder to throw: the arm up and back, the other reaching out ahead for aim ----
  if (pose.throwCharge > 0) {
    const float k = ease(pose.throwCharge), tr = pose.throwCharge >= 1 ? std::sin(t * 31) * 0.02f : 0;
    A.armR = mixv(A.armR, vec3(3.3f + tr, -0.1f, 0.12f), k); A.foreR = mixv(A.foreR, vec3(0.35f, 0, 0), k);
    if (!pose.off) { A.armL = mixv(A.armL, vec3(1.1f + pitch * 0.5f, 0.2f, -0.1f), k); A.foreL = mixv(A.foreL, vec3(0.15f, 0, 0), k); }
    A.body.y += 0.35f * k; A.body.x += 0.1f * k; A.head.y -= 0.35f * k;
    A.legR.x -= 0.25f * k; A.legL.x += 0.3f * k; A.shinL.x -= 0.2f * k;
  }
  // ---- a spear couched under the arm for a charge (Punchy's spear_charge): leaning into it, the point level ----
  if (pose.spearCharge > 0) {
    const float k = ease(pose.spearCharge);
    A.armR = mixv(A.armR, vec3(1.0f + pitch * 0.4f, -0.12f, 0.12f), k); A.foreR = mixv(A.foreR, vec3(0.65f, 0, 0), k);
    if (!pose.off) { A.armL = mixv(A.armL, vec3(0.95f, 0.45f, 0), k); A.foreL = mixv(A.foreL, vec3(0.55f, 0, 0), k); }
    A.body.x -= 0.3f * k; A.head.x += 0.25f * k;
  }
}

} // namespace dq
