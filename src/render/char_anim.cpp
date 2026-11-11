// render/char_anim.cpp — avatarAnim (render/skins.js) with game/anim.js's livelier bodies, and the part matrices
// of avatarDraws / gearDraws.
#include "render/char_anim.h"
#include <cstdio>
#include <cstdlib>

namespace dq {

namespace {
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline vec3 mixv(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline mat4 rot(mat4 m, vec3 r) {
  if (r.y) m = glm::rotate(m, r.y, vec3(0, 1, 0));
  if (r.x) m = glm::rotate(m, r.x, vec3(1, 0, 0));
  if (r.z) m = glm::rotate(m, r.z, vec3(0, 0, 1));
  return m;
}
inline glm::mat3 rotM(vec3 r) { return glm::mat3(rot(mat4(1), r)); }
// the angles (x, then y; no z) that point a limb's -y along the unit vector d, as rot() turns it
vec3 aimAngles(vec3 d) {
  const float x = std::acos(clampf(-d.y, -1, 1));
  return vec3(x, std::sin(x) > 1e-4f ? std::atan2(-d.x, -d.z) : 0.f, 0);
}
// two bones from the shoulder S (l1 to the elbow, l2 on to the fist) reaching for T, the elbow bent toward pole: the
// upper arm's angles and the forearm's (in the upper arm's frame). Out of reach, the arm points straight at it.
void reachArm(vec3 S, vec3 T, float l1, float l2, vec3 pole, vec3& up, vec3& fore) {
  vec3 v = T - S; float d = glm::length(v);
  const vec3 vn = d > 1e-4f ? v / d : vec3(0, -1, 0);
  d = clampf(d, std::fabs(l1 - l2) + 0.01f, l1 + l2 - 0.01f);
  const float ca = clampf((l1 * l1 + d * d - l2 * l2) / (2 * l1 * d), -1, 1), sa = std::sqrt(1 - ca * ca);
  vec3 pp = pole - vn * glm::dot(pole, vn); const float pl = glm::length(pp);
  pp = pl > 1e-4f ? pp / pl : vec3(0, 0, 0);
  const vec3 u = vn * ca + pp * sa;
  up = aimAngles(u);
  const vec3 w = glm::normalize(S + vn * d - (S + u * l1));
  fore = aimAngles(glm::transpose(rotM(up)) * w);
}
}

// a commando crawl (charRig lays the body flat on its chest: from here "up" the body, +y, is forward along the ground and
// -z is the ground, 2 px below the middle). Each forearm in turn reaches out beside the head and pulls the body up to it,
// the elbows out to the sides; the knee across from the reaching arm draws up to the side and pushes. Still, both
// forearms rest in front and the legs lie straight. The hands and feet are put where they go (reachArm, as the bow's).
static void crawlLimbs(CharAngles& A, const CharPose& pose, const CharModel& M, float b, float kl, float hip) {
  const float mv = clampf(pose.crawlMv, 0, 1), ph = pose.crawlPh, s = std::sin(ph), c = std::cos(ph);
  const float rR = 0.5f + 0.5f * s * mv, rL = 0.5f - 0.5f * s * mv;                         // reach: 0 pulled back .. 1 out front
  const float upR = std::max(0.f, c) * 1.5f * mv, upL = std::max(0.f, -c) * 1.5f * mv;      // the arm going forward lifts clear
  A.body = vec3(0, s * 0.08f * mv, s * 0.05f * mv);   // the hips and shoulders wriggle with it
  A.lift = 0;
  const glm::mat3 Rb = rotM(A.body);
  const vec3 hipP(0, hip, 0);
  auto toBody = [&](vec3 r) { return glm::transpose(Rb) * (r - hipP); };
  const vec3 pR = M.pivot[CP_ARMR_U], pL = M.pivot[CP_ARML_U];
  const vec3 sR(pR.x * b, pR.y - 12, pR.z), sL(pL.x * b, pL.y - 12, pL.z);
  const float l1 = glm::length(M.pivot[CP_ARMR_D] - pR), l2 = BOW_FIST;
  // the hands beside the head (it is 8 wide), from the chin (pulled) to the brow (reached), resting on the ground
  auto hand = [&](float side, float r, float up) { return vec3(side * (5.6f * b), hip + 13.f + 6.f * r, 0.3f + up); };
  reachArm(sR, toBody(hand(1, rR, upR)), l1, l2, vec3(1, -0.5f, 0), A.armR, A.foreR);
  reachArm(sL, toBody(hand(-1, rL, upL)), l1, l2, vec3(-1, -0.5f, 0), A.armL, A.foreL);
  // the legs (the root's children, not the body's): the knee across from the reaching hand draws up and out
  auto leg = [&](int up, float side, float k, vec3& legA, vec3& shinA) {
    const vec3 pv = M.pivot[up], hipJ(pv.x * b, hip, pv.z);
    const float m1 = glm::length(M.pivot[up + 1] - pv) * kl, m2 = 5.6f * kl;
    const vec3 foot(side * (2.2f + 1.0f * k) * b, hip - (m1 + m2) + 0.4f + 5.0f * k * kl, 0.f);
    reachArm(hipJ, foot, m1, m2, vec3(side, 0.15f, 0), legA, shinA);
  };
  leg(CP_LEGR_U, 1, rL * mv, A.legR, A.shinR);
  leg(CP_LEGL_U, -1, rR * mv, A.legL, A.shinL);
}

// the bow drawn (third person): the stave's grip in the bow hand (the left), its string pulled back to the right hand
// just in front of the chin, the arrow along the aim. The arms reach for the places (render/char_layer.cpp then hangs
// the bow on the hands where they are, bent as far as they're apart). Body space is the arms' parent frame.
static void crawlLimbs(CharAngles& A, const CharPose& pose, const CharModel& M, float b, float kl, float hip);
static void bowDrawArms(CharAngles& A, const CharPose& pose, const CharModel& M, float b, float hip) {
  const float k = std::min(1.f, pose.bow), ease = k * k * (3 - 2 * k);
  const glm::mat3 Rb = rotM(A.body);
  const vec3 hipP(0, hip, 0);
  auto toBody = [&](vec3 r) { return glm::transpose(Rb) * (r - hipP); };
  const vec3 pR = M.pivot[CP_ARMR_U], pL = M.pivot[CP_ARML_U];
  const vec3 sR(pR.x * b, pR.y - 12, pR.z), sL(pL.x * b, pL.y - 12, pL.z);   // the shoulders (body space)
  const float l1 = glm::length(M.pivot[CP_ARMR_D] - pR), l2 = BOW_FIST;
  // the aim: straight ahead, up or down as they look
  const float pitch = clampf(pose.headPitch, -1.2f, 1.2f), cp = std::cos(pitch), sp = std::sin(pitch);
  auto pitched = [&](vec3 v) { return vec3(v.x, v.y * cp - v.z * sp, v.y * sp + v.z * cp); };
  const vec3 aim = pitched(vec3(0, 0, -1));
  const vec3 neck = hipP + Rb * vec3(0, 12, 0);
  const vec3 D = neck + pitched(vec3(0.6f, -0.6f, -4.6f));   // the draw hand, at the chin
  // the bow hand out along the aim, as far as the string is drawn (and as the arm reaches)
  const vec3 SL = hipP + Rb * sL, w = D - SL;
  const float L = l1 + l2 - 0.05f, aw = glm::dot(aim, w), disc = aw * aw - glm::dot(w, w) + L * L;
  const float tMax = disc > 0 ? -aw + std::sqrt(disc) : -aw;
  const float want = 1.41421f * (0.235f + 0.3405f * ease) * BOW_SCALE / CHAR_PX;
  const float tr = k >= 0.95f ? std::sin(pose.time * 47) * 0.12f : 0;
  const vec3 F = D + aim * std::min(want, tMax) + vec3(tr, tr * 0.5f, 0);
  reachArm(sL, toBody(F), l1, l2, vec3(-0.6f, -1, 0), A.armL, A.foreL);
  reachArm(sR, toBody(D), l1, l2, vec3(1, -0.25f, 0.6f), A.armR, A.foreR);
}

CharAngles charAnimate(const CharPose& pose) {
  CharAngles A;
  if (pose.still) return A;
  float a = pose.walkAmt, ph = pose.walk, t = pose.time, run = pose.sprint ? 1.f : 0.f, idle = 1 - a, swimBreath = 0;
  float sL = std::sin(ph), cL = std::cos(ph), amp = (0.75f + 0.3f * run) * a * (pose.backward ? 0.8f : 1.f);
  if (pose.backward) cL = -cL;   // backing off: each knee bends as its foot reaches back
  // ---- skins.js avatarAnim: Mo' Bends-style movement, knees, elbows, a twist in the body ----
  A.legR.x = sL * amp; A.legL.x = -sL * amp;
  A.shinR.x = -a * (0.08f + (0.8f + 0.4f * run) * std::max(0.f, cL));
  A.shinL.x = -a * (0.08f + (0.8f + 0.4f * run) * std::max(0.f, -cL));
  A.armR = vec3(-sL * amp * 0.8f, 0, (0.05f + std::sin(t * 1.3f) * 0.03f) * idle);
  A.armL = vec3(sL * amp * 0.8f, 0, -(0.05f + std::sin(t * 1.3f) * 0.03f) * idle);
  A.foreR.x = A.foreL.x = 0.1f + 0.45f * run * a + 0.06f * idle;
  A.body = vec3(-0.2f * run * a, sL * 0.1f * a, 0);
  A.lift = (1 - std::fabs(sL)) * (0.045f + 0.035f * run) * a + std::sin(t * 1.9f) * 0.004f * idle;   // (running: a bigger bounce, more weight)
  if (pose.backward) A.body.x += 0.08f * a;
  A.head.x += std::fabs(sL) * 0.06f * run * a;   // the head nods into each running footfall
  // standing about: a slow shift of weight from foot to foot, the shoulders rising with each breath
  A.body.z += std::sin(t * 0.55f) * 0.025f * idle; A.body.x += std::sin(t * 1.9f) * 0.012f * idle;
  A.legR.z += std::sin(t * 0.55f) * 0.02f * idle; A.legL.z += std::sin(t * 0.55f) * 0.02f * idle;
  if (pose.held && !pose.carry) A.armR.x = A.armR.x * 0.5f + 0.31f;
  if (pose.off && !pose.carry) A.armL.x = A.armL.x * 0.5f + 0.31f;
  // off the ground: knees up; falling hard: arms up
  if (pose.air && !pose.swim && !pose.crawl && !pose.sit) {
    // Story Mode weight: pushing off (legs straight under you, arms swung up), tucked at the top, then reaching down for
    // the ground as you fall (and arms going up with a long fall)
    const float fall = clampf(-pose.vy / 12, 0, 1), rise = clampf(pose.vy / 6, 0, 1), apex = 1 - clampf(std::fabs(pose.vy) / 4, 0, 1);
    const float reach = clampf((-pose.vy - 2) / 6, 0, 1);
    A.legR = vec3(0.55f * (1 - rise) * (1 - reach * 0.6f) - 0.15f * rise, 0, 0.05f); A.shinR = vec3(-1.0f * (1 - rise * 0.8f) * (1 - reach * 0.7f) - 0.3f * apex, 0, 0);
    A.legL = vec3(-0.15f + 0.35f * apex - 0.05f * rise, 0, -0.05f); A.shinL = vec3(-0.4f - 0.5f * apex, 0, 0);
    if (!pose.held) A.armR = vec3(0.3f + fall * 2.2f + rise * 0.5f, 0, 0.3f + fall * 0.5f);
    if (!pose.off) A.armL = vec3(0.3f + fall * 2.2f + rise * 0.5f, 0, -0.3f - fall * 0.5f);
    A.body.x += 0.1f * rise - 0.08f * reach;
    if (pose.airLeft) { std::swap(A.legR, A.legL); std::swap(A.shinR, A.shinL); A.legR.z = -A.legR.z; A.legL.z = -A.legL.z; }   // the other leg leads this jump
  }
  if (pose.land > 0) {   // the landing: knees give, hips drop, the body folds forward — as deep as the drop was hard
    const float dur = 0.2f * std::max(1.f, pose.landAmt), u = clampf(pose.land / dur, 0, 1), k = (u * u * (3 - 2 * u)) * pose.landAmt;
    A.lift -= 0.1f * k; A.legR.x += 0.5f * k; A.legL.x += 0.5f * k; A.shinR.x -= k; A.shinL.x -= k; A.body.x -= 0.25f * k;
    if (!pose.held) A.armR.x += 0.35f * k; if (!pose.off) A.armL.x += 0.35f * k;   // the arms come forward to balance
  }
  // swinging a tool: Minecraft's attack swing, a quick chop that eases out, the body twisting with it
  float at = pose.swing;
  if (at > 0) {
    float tw = std::sin(std::sqrt(at) * TAU) * 0.2f;
    float f = 1 - at; f *= f; f *= f; f = 1 - f;
    float f2 = std::sin(f * PI), f3 = std::sin(at * PI) * (pose.headPitch + 0.7f) * 0.75f;
    if (at < 0.16f) { const float w = std::sin(at / 0.16f * PI); f2 -= 0.45f * w; A.body.y += 0.08f * w; }   // anticipation: a wind-up before the blow
    A.body.y -= tw;
    A.armR.x += f2 * 1.2f + f3; A.armR.y -= tw * 2; A.armR.z -= std::sin(at * PI) * 0.4f;
    A.armL.y -= tw; A.armL.x -= tw;
    A.foreR.x += f2 * 0.25f;
  }
  if (pose.bow > 0) {   // the bow arm straight out at the target; the drawing hand pulls back to the chin (third person:
                        // charRig's bowDrawArms puts the hands on the bow); the body turns its bow shoulder to the target
    float k = std::min(1.f, pose.bow), tr = k >= 0.95f ? std::sin(t * 47) * 0.012f : 0;
    A.armL = vec3(1.52f + tr, 0.1f + 0.08f * k, 0); A.foreL = vec3(0.04f, 0, 0);
    A.armR = vec3(1.45f, -0.45f - 0.25f * k, 0); A.foreR = vec3(0.1f + 0.15f * k, 0, -1.1f * k);
    const float turn = 0.5f + 0.5f * k;   // side-on, like an archer (the feet turned half as far): the bow arm reaches further
    A.body = vec3(A.body.x, A.body.y - turn, 0); A.legR.y -= turn * 0.5f; A.legL.y -= turn * 0.5f;
  }
  if (pose.eat > 0 && pose.drink) { const float up = std::min(1.f, (1 - pose.eat) * 4); A.armR = vec3(1.55f + 0.5f * up, -0.4f, 0); A.foreR = vec3(1.0f - 0.2f * up, 0, 0); }   // a bottle tipped up
  else if (pose.eat > 0) { A.armR = vec3(1.15f + std::sin(t * 22) * 0.12f, -0.35f, 0); A.foreR = vec3(0.9f, 0, 0); }
  if (pose.scope) {   // like the bow's drawing hand to the chin, a little higher: the forearm folded in to the eye (DQ_DEV_SCOPE="ax,ay,az,fx,fy,fz")
    static float S[6] = {2.3f, -0.25f, 0.f, 0.25f, 0.f, -1.6f}; static bool sl = false;
    if (!sl) { sl = true; if (const char* v = std::getenv("DQ_DEV_SCOPE")) std::sscanf(v, "%f,%f,%f,%f,%f,%f", &S[0], &S[1], &S[2], &S[3], &S[4], &S[5]); }
    A.armR = vec3(S[0] + pose.headPitch, S[1], S[2]); A.foreR = vec3(S[3], S[4], S[5]);
  }
  if (pose.carry) { A.armR = vec3(1.25f, 0, 0.05f); A.armL = vec3(1.25f, 0, -0.05f); A.foreR = A.foreL = vec3(0.3f, 0, 0); }
  if (pose.castWind > 0) { float k = std::sin(pose.castWind * PI / 2); A.armR = vec3(0.6f + 2.3f * k, -0.15f, 0.12f); A.foreR = vec3(0.35f * k, 0, 0); A.body.y -= 0.2f * k; }
  if (pose.sneak) { A.body.x = -0.45f; A.legR.x += 0.2f; A.legL.x += 0.2f; A.shinR.x -= 0.4f; A.shinL.x -= 0.4f; A.lift -= 0.12f; }
  if (pose.hang || pose.climb > 0) {   // up on a ledge: arms reaching up, legs hanging (swinging up as you pull over)
    float k = pose.climb;
    A.armR = vec3(2.9f - k * 1.6f, 0, 0.18f); A.armL = vec3(2.9f - k * 1.6f, 0, -0.18f); A.foreR = A.foreL = vec3(0.15f, 0, 0);
    A.legR = vec3(0.15f + k * 1.2f, 0, 0.04f); A.legL = vec3(-0.05f + k * 0.4f, 0, -0.04f); A.shinR = vec3(-0.2f - k * 1.2f, 0, 0); A.shinL = vec3(-0.1f, 0, 0);
    A.body = vec3(-0.1f - k * 0.4f, 0, 0);
  }
  if (pose.hang && pose.shimMv > 0) {   // movement.js: shimmying along the ledge, hand over hand
    float s = std::sin(pose.shimPh), c = std::cos(pose.shimPh);
    A.armR = vec3(2.9f + s * 0.22f, 0, 0.18f + std::max(0.f, c) * 0.25f); A.armL = vec3(2.9f - s * 0.22f, 0, -0.18f - std::max(0.f, -c) * 0.25f);
    A.legR = vec3(0.15f + s * 0.15f, 0, 0.04f + std::max(0.f, c) * 0.12f); A.legL = vec3(-0.05f - s * 0.15f, 0, -0.04f - std::max(0.f, -c) * 0.12f);
    A.body = vec3(-0.1f, 0, s * 0.06f);
  }
  if (pose.ladder && !pose.hang && pose.climb <= 0) {
    // on a ladder or vines: the hands reach up the rungs in turn and the knees lift, faster the faster you climb
    float s = std::sin(pose.ladderPh), k = 0.35f + 0.65f * clampf(pose.ladderMv, 0, 1);
    A.armR = vec3(2.35f + s * 0.45f * k, 0, 0.1f); A.armL = vec3(2.35f - s * 0.45f * k, 0, -0.1f);
    A.foreR = vec3(0.45f - s * 0.3f * k, 0, 0); A.foreL = vec3(0.45f + s * 0.3f * k, 0, 0);
    A.legR = vec3(0.35f + std::max(0.f, s) * 0.75f * k, 0, 0.04f); A.legL = vec3(0.35f + std::max(0.f, -s) * 0.75f * k, 0, -0.04f);
    A.shinR = vec3(-0.45f - std::max(0.f, s) * 0.9f * k, 0, 0); A.shinL = vec3(-0.45f - std::max(0.f, -s) * 0.9f * k, 0, 0);
    A.body = vec3(-0.06f, 0, 0); A.lift = 0;
  }
  if (pose.sit) { A.legR = vec3(1.4f, 0, 0.1f); A.legL = vec3(1.4f, 0, -0.1f); A.shinR = A.shinL = vec3(-1.4f, 0, 0); }
  if (pose.swim) {
    // the fast swim, lying flat: a breaststroke (not a windmill). The arms glide out ahead together, sweep out wide and
    // pull in under the chest with the elbows bent, then shoot forward again; the legs frog-kick as the arms go forward,
    // and the head comes up for a breath on the pull. Keyframes through one stroke (u 0..1), eased between.
    float sp = pose.swimPh != 0 ? pose.swimPh : ph * 0.9f;
    const float u = std::fmod(std::fmod(sp, TAU) + TAU, TAU) / TAU;
    struct Key { float u, ax, az, fx, leg, legZ, shin, head; };
    static const Key K[] = {
      {0.00f, 3.10f, 0.10f, 0.05f, 0.00f, 0.04f, -0.05f, 0.0f},   // the glide: arms out ahead, legs together
      {0.18f, 3.05f, 0.18f, 0.10f, 0.00f, 0.04f, -0.08f, 0.0f},
      {0.42f, 2.55f, 1.10f, 0.40f, 0.05f, 0.06f, -0.15f, 0.35f},  // the out-sweep and pull: wide, the head up to breathe
      {0.60f, 1.75f, 0.35f, 1.55f, 0.55f, 0.35f, -1.45f, 0.25f},  // hands in under the chest, knees drawn up and out
      {0.80f, 2.80f, 0.12f, 0.45f, 0.10f, 0.30f, -0.50f, 0.05f},  // shooting forward as the legs kick round
      {1.00f, 3.10f, 0.10f, 0.05f, 0.00f, 0.04f, -0.05f, 0.0f},
    };
    int i = 0; while (i < 4 && u > K[i + 1].u) i++;
    const Key &a0 = K[i], &a1 = K[i + 1];
    float w = clampf((u - a0.u) / (a1.u - a0.u), 0, 1); w = w * w * (3 - 2 * w);
    auto L = [&](float Key::*f) { return a0.*f + (a1.*f - a0.*f) * w; };
    A.armR = vec3(L(&Key::ax), 0, L(&Key::az)); A.armL = vec3(A.armR.x, 0, -A.armR.z);
    A.foreR = A.foreL = vec3(L(&Key::fx), 0, 0);
    A.legR = vec3(L(&Key::leg), 0, L(&Key::legZ)); A.legL = vec3(A.legR.x, 0, -A.legR.z);
    A.shinR = A.shinL = vec3(L(&Key::shin), 0, 0);
    A.body = vec3(L(&Key::head) * 0.25f, 0, 0);   // the chest lifts a little with the breath
    A.lift = 0;
    swimBreath = L(&Key::head);
  } else if (pose.paddle && !pose.sit) {
    // out of your depth: treading water when still, a breaststroke when you swim along
    float sp = pose.swimPh, mv = clampf(pose.swimMv, 0, 1), u = std::fmod(std::fmod(sp, TAU) + TAU, TAU);
    float sw = std::sin(u), reach = std::max(0.f, std::cos(u)), out = std::max(0.f, sw), rec = std::max(0.f, -sw);
    vec3 tArmR(0.45f + std::sin(sp * 1.3f) * 0.12f, 0, 0.75f + std::sin(sp) * 0.18f), tFore(0.55f + std::sin(sp * 1.3f + 1) * 0.2f, 0, 0);
    vec3 bArmR(1.25f + reach * 0.85f - rec * 0.3f, 0, 0.12f + out * 0.75f), bFore(0.2f + rec * 1.1f, 0, 0);
    A.armR = mixv(tArmR, bArmR, mv); A.foreR = mixv(tFore, bFore, mv);
    A.armL = vec3(A.armR.x, -A.armR.y, -A.armR.z); A.foreL = A.foreR;
    float kick = (1 + std::sin(u + 0.6f)) / 2;
    vec3 tLegR(0.3f + std::sin(sp) * 0.3f, 0, 0.12f), tShinR(-0.6f - std::cos(sp) * 0.3f, 0, 0), tLegL(0.3f - std::sin(sp) * 0.3f, 0, -0.12f), tShinL(-0.6f + std::cos(sp) * 0.3f, 0, 0);
    vec3 bLegR(0.15f + kick * 0.55f, 0, 0.08f + kick * 0.3f), bShin(-0.15f - kick * 1.1f, 0, 0), bLegL(0.15f + kick * 0.55f, 0, -0.08f - kick * 0.3f);
    A.legR = mixv(tLegR, bLegR, mv); A.legL = mixv(tLegL, bLegL, mv); A.shinR = mixv(tShinR, bShin, mv); A.shinL = mixv(tShinL, bShin, mv);
    A.body = vec3(-0.15f - mv * 0.85f, 0, 0);
    A.lift = std::sin(sp * (1 + mv)) * 0.035f;
  }
  // the head keeps looking where the player looks, whatever the body does
  A.head = vec3((pose.swim ? 0.9f + (pose.glide ? 0.f : swimBreath) : pose.crawl ? 1.2f + clampf(pose.headPitch, -0.6f, 0.6f) * 0.4f : pose.headPitch) - A.body.x, -A.body.y, 0);

  // ---- game/anim.js: livelier bodies ----
  float b0 = A.body.x, b1 = A.body.y;
  bool special = pose.swim || pose.crawl || pose.sleep || pose.hang || pose.climb > 0 || pose.ladder || pose.sit || pose.deathT > 0 || pose.carry || pose.bow > 0 || pose.paddle || pose.fly;
  if (!special) {
    bool busyR = pose.held || pose.swing > 0 || pose.eat > 0 || pose.castWind > 0 || pose.scope;
    if (!pose.air) {
      // a looser walk: hips swing, shoulders counter-twist, arms swing wider and elbows bend, the head nods
      A.body.y += sL * 0.07f * a;
      A.body.z += sL * 0.035f * a;
      if (!busyR) A.armR.x *= 1.25f;
      if (!pose.off) A.armL.x *= 1.25f;
      if (!busyR) A.armR.z += 0.06f * a;
      A.armL.z -= 0.06f * a;
      if (!busyR) A.foreR.x += (0.18f + 0.3f * run) * a * (0.6f + 0.4f * std::max(0.f, -sL));
      A.foreL.x += (0.18f + 0.3f * run) * a * (0.6f + 0.4f * std::max(0.f, sL));
      A.body.x -= 0.1f * run * a;
      A.body.x -= pose.accLean;
      if (!busyR) A.foreR.x += 0.08f * idle;
      A.foreL.x += 0.08f * idle;
    }
    A.body.z += pose.turnLean;
    float lt = pose.fp ? clampf(pose.legTurn - pose.headTurn, -0.8f, 0.8f) : pose.legTurn;
    A.legR.y += lt; A.legL.y += lt;
  }
  A.head.x -= A.body.x - b0; A.head.y -= A.body.y - b1;
  if (!special && !pose.air) { A.head.x += std::fabs(std::cos(ph)) * 0.04f * a + std::sin(t * 0.53f) * 0.02f * idle; A.head.y += std::sin(t * 0.37f) * 0.04f * idle; }
  if (pose.turned && pose.headTurn) A.head.y += clampf(pose.headTurn, -1.25f, 1.25f);
  // ---- Story Mode-style life ----
  if (!special && !pose.air && !pose.fp && pose.deathT <= 0) {
    // feet planted on uneven ground: the hips drop to the lower foot and the other knee bends up to its step
    const float k = clampf(1 - a * 3, 0, 1);
    if (k > 0 && (pose.footL != 0 || pose.footR != 0)) {
      const float lo = std::min(0.f, std::min(pose.footL, pose.footR));
      A.lift += lo * k;
      auto bend = [&](float up, vec3& leg, vec3& shin) {
        const float r = clampf(up, 0, 0.6f), ang = std::acos(clampf(1 - r / 0.75f, -1, 1));
        leg.x += ang * k; shin.x -= 2 * ang * k;
      };
      bend(pose.footR - lo, A.legR, A.shinR); bend(pose.footL - lo, A.legL, A.shinL);
    }
    // idles: after a few seconds standing about, now and then something to do, each person their own way
    const bool armsFree = !pose.held && pose.swing <= 0 && pose.eat <= 0 && !pose.mapMain && !pose.talk && !pose.read && pose.wave <= 0;
    if (pose.idleT > 5 && idle > 0.95f && !pose.sneak) {
      const float span = 9.f, u = (pose.idleT - 5) / span;
      const int n = (int)u; const float ph = u - n;
      const float e = std::sin(clampf((ph - 0.1f) / 0.55f, 0, 1) * PI), env = e * e * (3 - 2 * e);
      u32 h = pose.seed * 2654435761u + (u32)n * 40503u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
      switch (h % 6) {
        case 0: A.head.y += std::sin(ph * TAU * 1.5f) * 0.75f * env; A.head.x -= 0.08f * env; break;   // a look round
        case 1: if (armsFree) {   // a stretch: arms up, a lean back
          A.armR = mixv(A.armR, vec3(2.75f, 0, 0.35f), env); A.armL = mixv(A.armL, vec3(2.75f, 0, -0.35f), env);
          A.foreR = mixv(A.foreR, vec3(0.1f, 0, 0), env); A.foreL = mixv(A.foreL, vec3(0.1f, 0, 0), env);
          A.body.x += 0.12f * env; A.head.x -= 0.35f * env; A.lift += 0.02f * env;
        } break;
        case 2: if (armsFree) {   // a scratch of the head
          A.armR = mixv(A.armR, vec3(2.3f, 0.2f, -0.55f), env); A.foreR = mixv(A.foreR, vec3(1.35f + std::sin(t * 15) * 0.1f, 0, 0), env);
          A.head.x += 0.1f * env; A.head.z = 0.12f * env;
        } break;
        case 3: A.head.x += 0.5f * env; A.armR.x += 0.55f * env * (armsFree ? 1.f : 0.6f); A.armL.x += 0.45f * env; A.foreR.x += 0.5f * env; A.foreL.x += 0.4f * env; break;   // a look at the hands
        case 4: A.legR.x += std::max(0.f, std::sin(t * 9)) * 0.18f * env; A.shinR.x -= std::max(0.f, std::sin(t * 9)) * 0.1f * env; A.body.z += 0.04f * env; break;   // a tap of the foot
        default: A.body.z += std::sin(ph * PI * 2) * 0.06f * env; A.legR.z += 0.05f * env; A.legL.z += 0.05f * env; A.head.y += 0.25f * env; break;   // the weight shifted, a glance aside
      }
    }
  }
  // maps.js: a map held up in front of the chest, in both hands (or one, when the other holds something)
  if (!(pose.fp || pose.carry || pose.sleep || pose.swim || pose.crawl || pose.hang || pose.climb > 0 || pose.deathT > 0 || pose.swing > 0) && (pose.mapMain || pose.mapOff)) {
    if (pose.mapMain && !pose.off) { A.armR = vec3(0.95f, 0, -0.3f); A.armL = vec3(0.95f, 0, 0.3f); A.foreR = A.foreL = vec3(0.2f, 0, 0); }
    else { if (pose.mapMain) A.armR = vec3(0.9f, 0, -0.1f); if (pose.mapOff) A.armL = vec3(0.9f, 0, 0.1f); }
  }
  if (pose.fly) {
    float s = pose.flySpeed, vy = pose.flyVy, hover = 1 - s, k = t * 1.6f;
    float up = clampf((vy - 1) / 4, 0, 1), down = clampf((-vy - 1) / 4, 0, 1);
    A.legR = vec3(0.08f + std::sin(k) * 0.12f * hover + 0.6f * up, 0, 0.03f); A.legL = vec3(-0.04f - std::sin(k + 1.3f) * 0.12f * hover + 0.5f * up, 0, -0.03f);
    A.shinR = vec3(-0.25f * hover - 0.1f - 0.7f * up, 0, 0); A.shinL = vec3(-0.15f * hover - 0.05f - 0.7f * up, 0, 0);
    float ax = -0.25f * s + 0.15f * hover + 0.7f * down, az = 0.12f + 0.18f * hover + 0.25f * down;
    if (!pose.held && !(pose.swing > 0)) { A.armR = vec3(ax, 0, az + std::sin(k) * 0.05f); A.foreR = vec3(0.15f, 0, 0); }
    if (!pose.off) { A.armL = vec3(ax, 0, -az - std::sin(k) * 0.05f); A.foreL = vec3(0.15f, 0, 0); }
    A.body = vec3(0, 0, std::sin(k * 0.7f) * 0.03f * hover + pose.turnLean * 1.5f);
    A.head = vec3(pose.headPitch + pose.lean * 0.9f, pose.turned ? clampf(pose.headTurn, -1.25f, 1.25f) : 0, 0);
    A.lift = std::sin(k) * 0.04f * hover;
  }
  // game/elytra.js: arms swept back along the sides, legs together
  if (pose.glide) {
    A.armR = vec3(0.15f, 0, 0.32f); A.armL = vec3(0.15f, 0, -0.32f); A.foreR = A.foreL = vec3(0.05f, 0, 0);
    A.legR = vec3(0, 0, 0.04f); A.legL = vec3(0, 0, -0.04f); A.shinR = A.shinL = vec3(0); A.body = vec3(0); A.lift = 0;
  }
  // game/villagers.js avatarAnim: new moves for the shared body
  const vec3 bowR = A.armR, bowF = A.foreR;   // (the bow arm as it walks or stands, before the gestures)
  if (pose.headYaw != 0) A.head.y += pose.headYaw;
  if (pose.wave > 0) { float k = std::min(1.f, pose.wave * 3); A.armR = vec3(2.7f * k, 0, 0.35f * k + std::sin(t * 14) * 0.35f * k); A.foreR = vec3(0.2f, 0, 0); }
  else if (pose.talk) { A.armR.x += 0.5f + std::sin(t * 3.1f) * 0.25f; A.foreR.x += 0.6f + std::sin(t * 2.3f) * 0.3f; A.armL.x += 0.35f + std::sin(t * 2.7f + 1) * 0.2f; A.foreL.x += 0.5f; A.head.x += std::sin(t * 4.3f) * 0.06f; }
  else if (pose.fold && pose.walkAmt < 1e-3f) { A.armR = vec3(0.95f, 0.35f, -0.55f); A.foreR = vec3(1.25f, 0, 0); A.armL = vec3(0.95f, -0.35f, 0.55f); A.foreL = vec3(1.25f, 0, 0); }
  if (pose.read) { A.armR = vec3(0.85f, -0.25f, 0); A.foreR = vec3(0.7f, 0, 0); A.armL = vec3(0.85f, 0.25f, 0); A.foreL = vec3(0.7f, 0, 0); A.head.x += 0.35f; }
  if (pose.hoe > 0) { float k = std::sin(pose.hoe * PI); A.armR = vec3(0.6f + k * 1.2f, 0, 0); A.armL = vec3(0.6f + k * 1.1f, 0, 0); A.body.x -= k * 0.25f; }
  if (pose.bowArm && pose.bow <= 0 && pose.swing <= 0) { A.armR = vec3(std::min(bowR.x, 0.1f), 0, bowR.z); A.foreR = vec3(std::min(bowF.x, 0.06f), 0, 0); }   // the arm hangs nearly straight: the bow it carries lies level at the side
  charMorePoses(A, pose);
  return A;
}

void charRig(const CharModel& M, CharPose pose, const mat4& base0, CharRig& R, bool cloak, bool underPack) {
  // anim.js: the body turned its own way (third person), the head where you look
  if (!pose.fp && pose.hasBodyYaw && !pose.swim && !pose.crawl && !pose.sleep) { pose.yaw = pose.bodyYaw; pose.turned = true; }
  // the peoples stand at their own heights (gearScaleDraws): scaled about the feet (the hips when seated)
  float sy = M.stature.x, sx = M.stature.y;
  const float EYE_CAP = 1.72f;
  if (pose.fp && sy > EYE_CAP / 1.62f) { float k = (EYE_CAP / 1.62f) / sy; sy *= k; sx *= k; }
  mat4 base = base0;
  if (sy != 1 || sx != 1) {
    float py = pose.sit ? 0.75f : 0;
    base = glm::translate(base, vec3(0, py * (1 - sy), 0));
    base = glm::scale(base, vec3(sx, sy, sx));
  }
  base = glm::rotate(base, pose.yaw, vec3(0, 1, 0));
  if (pose.lean > 0 && !pose.fp) { base = glm::translate(base, vec3(0, 0.9f, 0)); base = glm::rotate(base, -pose.lean, vec3(1, 0, 0)); base = glm::translate(base, vec3(0, -0.9f, 0)); }
  if (pose.deathT > 0) { const float u = clampf((pose.deathT - 0.1f) / 0.38f, 0, 1); base = glm::rotate(base, u * u * PI / 2, vec3(0, 0, 1)); }
  if (pose.swim) { base = glm::translate(base, vec3(0, 0.32f, 0.55f)); base = glm::rotate(base, -PI / 2 + pose.swimPitch * 0.8f, vec3(1, 0, 0)); base = glm::translate(base, vec3(0, -0.9f, 0)); }
  else if (pose.crawl && !pose.fp) {   // flat on the ground, the chest on it (its plane 2 px in front of the middle)
    base = glm::translate(base, vec3(0, 2.3f * CHAR_PX, 0.5f)); base = glm::rotate(base, -PI / 2, vec3(1, 0, 0)); base = glm::translate(base, vec3(0, -0.9f, 0));
  }
  if (pose.sleep) { base = glm::rotate(base, PI, vec3(0, 1, 0)); base = glm::translate(base, vec3(0, 0.3f, 0)); base = glm::rotate(base, PI / 2, vec3(1, 0, 0)); base = glm::translate(base, vec3(0, -0.9f, 0)); }
  if (pose.fp) base = glm::translate(base, vec3(0, 0, 0.25f));   // FP_BODY: just behind the eyes
  CharAngles A = charAnimate(pose);
  if (pose.fp) { A.body.x *= 0.25f; A.body.y *= 0.5f; A.armL = vec3(A.armL.x * 0.45f, A.armL.y, A.armL.z - 0.06f); A.foreL.x *= 0.5f; }
  R.fpTorso = pose.fp && !pose.swim && !pose.crawl && !pose.sleep && pose.deathT <= 0;
  // the body shape: leg length lifts everything above the hips, build widens, the head its own size
  const BodyShape& S = M.body;
  float kl = S.legs, b = S.build, hip = 12 * kl;
  if (pose.crawl && !pose.fp && !pose.swim && pose.deathT <= 0) crawlLimbs(A, pose, M, b, kl, hip);
  else if (pose.bow > 0 && !pose.fp && !pose.swim && !pose.sleep && pose.deathT <= 0) bowDrawArms(A, pose, M, b, hip);
  vec3 hs = vec3(S.head);
  if (S.headShape == "long") hs.y *= 1.12f; else if (S.headShape == "wide") { hs.x *= 1.12f; hs.z *= 1.05f; }
  mat4 root = glm::translate(base, vec3(0, A.lift, 0));
  root = glm::scale(root, vec3(CHAR_PX));   // model pixels from here on
  auto child = [](const mat4& parent, vec3 off, vec3 r) { return rot(glm::translate(parent, off), r); };
  vec3 bodyPv(0, hip, 0);
  mat4 bodyM = child(root, bodyPv, A.body);
  R.bodyRaw = bodyM;
  R.part[CP_BODY] = bodyM; R.scale[CP_BODY] = glm::scale(mat4(1), vec3(b, 1, b));
  R.body = bodyM * R.scale[CP_BODY];
  R.part[CP_HEAD] = child(bodyM, vec3(0, 12, 0), A.head); R.scale[CP_HEAD] = glm::scale(mat4(1), hs);
  for (int side = 0; side < 2; side++) {
    int up = side ? CP_ARML_U : CP_ARMR_U, dn = up + 1, lu = side ? CP_LEGL_U : CP_LEGR_U, ld = lu + 1;
    vec3 armA = side ? A.armL : A.armR, foreA = side ? A.foreL : A.foreR, legA = side ? A.legL : A.legR, shinA = side ? A.shinL : A.shinR;
    vec3 pu = M.pivot[up], pl = M.pivot[lu];
    vec3 armOff(pu.x * b, pu.y - 12, pu.z);
    if (pose.fp && side == 1) armOff.z -= 0.2f / CHAR_PX, armA.x += 0.32f;   // an empty left hand, a little forward so it shows
    R.part[up] = child(bodyM, armOff, armA);
    R.part[dn] = child(R.part[up], M.pivot[dn] - pu, foreA);
    R.part[lu] = child(root, vec3(pl.x * b, hip, pl.z), legA);
    R.part[ld] = child(R.part[lu], (M.pivot[ld] - pl) * vec3(1, kl, 1), shinA);
    R.scale[up] = R.scale[dn] = glm::scale(mat4(1), vec3(b, 1, b));
    R.scale[lu] = R.scale[ld] = glm::scale(mat4(1), vec3(b, kl, b));
  }
  // ---- gear3d.js gearDraws: frames for the look's shapes ----
  R.gear.clear();
  const mat4& B = R.body;
  R.gear["body"] = B;
  R.gear["head"] = R.part[CP_HEAD] * R.scale[CP_HEAD];
  R.gear["armR"] = R.part[CP_ARMR_U] * R.scale[CP_ARMR_U]; R.gear["foreR"] = R.part[CP_ARMR_D] * R.scale[CP_ARMR_D];
  R.gear["armL"] = R.part[CP_ARML_U] * R.scale[CP_ARML_U]; R.gear["foreL"] = R.part[CP_ARML_D] * R.scale[CP_ARML_D];
  R.gear["legR"] = R.part[CP_LEGR_U] * R.scale[CP_LEGR_U]; R.gear["shinR"] = R.part[CP_LEGR_D] * R.scale[CP_LEGR_D];
  R.gear["legL"] = R.part[CP_LEGL_U] * R.scale[CP_LEGL_U]; R.gear["shinL"] = R.part[CP_LEGL_D] * R.scale[CP_LEGL_D];
  // the skirt's front and back follow whichever leg is ahead or behind
  auto legAng = [&](int p) { vec3 y = glm::normalize(vec3(R.part[p][1])), by = glm::normalize(vec3(bodyM[1])), bz = glm::normalize(vec3(bodyM[2])); return std::atan2(glm::dot(y, bz), glm::dot(y, by)); };
  float tR = legAng(CP_LEGR_U), tL = legAng(CP_LEGL_U);
  float front = std::min(1.45f, std::max({tR, tL, 0.f})), back = tR > 0.5f && tL > 0.5f ? std::min(tR, tL) : std::min({tR, tL, 0.f});
  R.gear["skirtF"] = front ? glm::rotate(B, front, vec3(1, 0, 0)) : B;
  R.gear["skirtB"] = back ? glm::rotate(B, back, vec3(1, 0, 0)) : B;
  {   // what hangs from the chin and the nape follows the head only part way
    vec3 hz = glm::normalize(vec3(R.part[CP_HEAD][2])), bx = glm::normalize(vec3(bodyM[0])), by = glm::normalize(vec3(bodyM[1])), bz = glm::normalize(vec3(bodyM[2]));
    float zx = glm::dot(hz, bx), zy = glm::dot(hz, by), zz = glm::dot(hz, bz);
    float yaw = std::atan2(zx, zz), up = std::asin(clampf(-zy, -1, 1));
    mat4 n = glm::translate(B, vec3(0, 12, 0)); n = glm::rotate(n, yaw * 0.6f, vec3(0, 1, 0)); if (up > 0) n = glm::rotate(n, up * 0.5f, vec3(1, 0, 0));
    R.gear["neck"] = n;
  }
  if (cloak && !pose.fp && !pose.sleep) {
    float t = pose.time, wa = pose.walkAmt, capeA = pose.capeA;
    float sway = pose.capeSway ? pose.capeSway : std::sin(t * 2.3f) * 0.035f * (1 - wa);
    mat4 c = glm::translate(B, vec3(0, 12, 2.4f)), lo;
    if (underPack) {
      c = glm::rotate(c, -0.02f, vec3(1, 0, 0));
      lo = glm::translate(c, vec3(0, -11, 0)); lo = glm::rotate(lo, -capeA * 0.95f - std::sin(t * 3.1f) * 0.03f * wa, vec3(1, 0, 0)); lo = glm::rotate(lo, sway * 0.8f, vec3(0, 0, 1));
    } else {
      c = glm::rotate(c, -capeA * 0.55f, vec3(1, 0, 0)); c = glm::rotate(c, sway, vec3(0, 0, 1));
      lo = glm::translate(c, vec3(0, -11, 0)); lo = glm::rotate(lo, -capeA * 0.6f - std::sin(t * 3.1f) * 0.03f * wa, vec3(1, 0, 0)); lo = glm::rotate(lo, sway * 0.6f, vec3(0, 0, 1));
    }
    R.gear["cape"] = c; R.gear["capeLo"] = lo;
  }
  if (!pose.fp) {
    float t = pose.time;
    mat4 tl = glm::translate(B, vec3(0, 0, 2)); tl = glm::rotate(tl, std::sin(t * 1.6f) * 0.3f, vec3(0, 1, 0)); tl = glm::rotate(tl, 0.08f + std::sin(t * 1.1f) * 0.06f, vec3(1, 0, 0));
    R.gear["tail"] = tl;
  }
}

} // namespace dq
