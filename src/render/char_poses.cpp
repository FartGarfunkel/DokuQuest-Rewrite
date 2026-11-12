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
// the shield guard's shield arm (left; mirrored for the right): the upper arm, the forearm. Fitted so the shield strapped
// on the forearm (render/char_layer.cpp shieldPlacement) stands square before the chest; sim/shield_shape.cpp measures it
constexpr float SH_X = 0.88f, SH_Y = 0.72f, SH_Z = 0.01f, SF_X = 0.74f, SF_Y = 0.13f, SF_Z = 1.84f;
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
  // (after Epic Fight's hit: snapped into in 0.06 s, held a moment, then let go; away from where the blow came from)
  if (pose.hurt > 0 && pose.hurt < 0.45f && pose.deathT <= 0 && !pose.sleep) {
    const float h = pose.hurt, f = h < 0.06f ? ease(h / 0.06f) : h < 0.16f ? 1.f : 1 - ease((h - 0.16f) / 0.29f);
    const float fr = clampf(pose.hurtDir.y, -1, 1), sd = clampf(pose.hurtDir.x, -1, 1);   // (from the front: rocked back; from behind: thrown forward)
    if (!lying && !pose.sit) {
      A.body.x += (0.22f * fr - 0.05f) * f; A.body.z -= 0.16f * sd * f; A.body.y += 0.12f * sd * f;
      A.head.x -= (0.24f * fr - 0.08f) * f; A.head.z += 0.1f * sd * f;
      A.shinR.x -= 0.2f * f; A.shinL.x -= 0.2f * f; A.legR.x += 0.09f * f; A.legL.x += 0.09f * f; A.lift -= 0.035f * f;
      if (fr < -0.3f) { A.legR.x -= 0.25f * -fr * f; A.legL.x += 0.2f * -fr * f; }   // (a stumbling step forward)
    }
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
    // (like sitting by a fire: the knees up a little apart, the feet flat out before them, the back upright and leant in
    // a touch, the hands resting on the knees, fingers over them)
    static float G[10] = {0}; static bool gl = false;   // tuning aid: DQ_DEV_SITG="thigh,shin,lift,lean,armX,armY,armZ,fore,spread,head" (added)
    if (!gl) { gl = true; if (const char* v = std::getenv("DQ_DEV_SITG")) std::sscanf(v, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &G[0], &G[1], &G[2], &G[3], &G[4], &G[5], &G[6], &G[7], &G[8], &G[9]); }
    A.legR = vec3(2.18f + G[0], 0, 0.36f + G[8]); A.legL = vec3(2.18f + G[0], 0, -0.36f - G[8]);   // (the knees close, the feet a little in)   // (35 degrees up from the hip)
    A.shinR = A.shinL = vec3(-1.61f + G[1], 0, 0);   // (down and forward to the feet flat on the ground)
    A.lift = -0.60f + G[2]; A.body = vec3(-0.05f + G[3], 0, 0);   // (- leans in)
    A.head.x += 0.05f + G[9];   // (looking down at the fire)
    if (!pose.reach) {
      A.armR = vec3(-0.35f + G[4], G[5], 0.08f + G[6]); A.foreR = vec3(1.90f + G[7], 0, 0);   // (the elbows back at the sides, the forearms forward over the knees)
      A.armL = vec3(-0.35f + G[4], -G[5], -0.08f - G[6]); A.foreL = vec3(1.90f + G[7], 0, 0);
    }
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
  // ---- a shield strapped on a forearm, carried: the elbow at the side, the forearm level ahead, so the shield stands
  // upright on its outside, face out (render/char_layer.cpp shieldPlacement: it goes wherever the forearm goes) ----
  static float CG[6] = {0}; static bool cgl = false;   // tuning aid: DQ_DEV_SHCARRY="ax,ay,az,fx,fy,fz" (added)
  if (!cgl) { cgl = true; if (const char* v = std::getenv("DQ_DEV_SHCARRY")) std::sscanf(v, "%f,%f,%f,%f,%f,%f", &CG[0], &CG[1], &CG[2], &CG[3], &CG[4], &CG[5]); }
  if (pose.shieldArm != 0 && !pose.sit && !pose.swim && !pose.crawl && !pose.paddle && pose.roll <= 0) {
    const bool L = pose.shieldArm > 0; const float o = L ? -1.f : 1.f;   // (z: out from the body on that side)
    vec3& arm = L ? A.armL : A.armR; vec3& fore = L ? A.foreL : A.foreR;
    arm = vec3(arm.x * 0.25f + 0.05f + CG[0], o * CG[1], o * (0.16f + CG[2]));
    fore = vec3(1.5f + CG[3], o * CG[4], o * CG[5]);
  }
  if (pose.block > 0) {
    // (after Epic Fight's shield guard, which is a pose for the upper body laid over whatever the legs do: the body turned
    // shield shoulder forward about half a right angle, the head turned back to keep looking ahead, the shield arm's
    // elbow forward and low and the forearm level across the front of the chest, so the shield strapped on it stands
    // square before the body; the other arm as it was)
    const float k = ease(pose.block), sd = pose.blockMain ? -1.f : 1.f;
    static float G[16] = {0}; static bool gl = false;   // tuning aid: DQ_DEV_GUARD="turn,sx,sy,sz,sf,wx,wy,wz,wf,lead,back,knee,crouch,head,twist,froll" (added)
    if (!gl) { gl = true; if (const char* v = std::getenv("DQ_DEV_GUARD")) std::sscanf(v, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &G[0], &G[1], &G[2], &G[3], &G[4], &G[5], &G[6], &G[7], &G[8], &G[9], &G[10], &G[11], &G[12], &G[13], &G[14], &G[15]); }
    auto side = [&](vec3 v, bool left) { return left ? v : vec3(v.x, -v.y, -v.z); };   // (an angle for the left arm, for either)
    const bool shLeft = sd > 0;
    // a blocked blow (pose.guardHit 1 -> 0): the shield is knocked in, the body twisted further round, the back foot braced
    const float gh = pose.guardHit > 0 ? (pose.guardHit > 0.8f ? ease((1 - pose.guardHit) / 0.2f) : ease(pose.guardHit / 0.8f)) : 0.f;
    const float turn = (0.50f + G[0] + 0.30f * gh) * k;
    A.body.y -= sd * turn; A.head.y += sd * turn * (1.f + G[13]);   // (the head keeps looking where you aim)
    A.body.x -= (0.05f + 0.06f * gh) * k;
    const vec3 sh = side(vec3(SH_X + G[1] + pitch * 0.35f + 0.12f * gh, SH_Y + G[2], SH_Z + G[3] - 0.25f * gh), shLeft);
    const vec3 sf = side(vec3(SF_X + G[4], SF_Y + G[14], SF_Z + G[15]), shLeft);
    if (shLeft) { A.armL = mixv(A.armL, sh, k); A.foreL = mixv(A.foreL, sf, k); } else { A.armR = mixv(A.armR, sh, k); A.foreR = mixv(A.foreR, sf, k); }
    // the legs, standing: both knees give a little, the shield side's foot ahead and pointing on, the back foot turned out
    // (walking keeps its stride: the guard is the upper body's)
    const float still = k * (1 - clampf(pose.walkAmt * 1.6f, 0, 1));
    vec3& legF = shLeft ? A.legL : A.legR; vec3& legB = shLeft ? A.legR : A.legL;
    vec3& shinF = shLeft ? A.shinL : A.shinR; vec3& shinB = shLeft ? A.shinR : A.shinL;
    legF.x += (0.24f + G[9]) * still; shinF.x -= (0.22f + G[11]) * still;
    legB.x += (-0.06f + G[10] - 0.2f * gh) * still; shinB.x -= (0.24f + G[11]) * still;
    legB.y -= sd * (0.48f + 0.1f * gh) * still;
    A.lift -= (0.02f + G[12] + 0.02f * gh) * still;
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
