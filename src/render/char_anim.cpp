// render/char_anim.cpp — avatarAnim (render/skins.js) with game/anim.js's livelier bodies, and the part matrices
// of avatarDraws / gearDraws.
#include "render/char_anim.h"

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
}

CharAngles charAnimate(const CharPose& pose) {
  CharAngles A;
  if (pose.still) return A;
  float a = pose.walkAmt, ph = pose.walk, t = pose.time, run = pose.sprint ? 1.f : 0.f, idle = 1 - a;
  float sL = std::sin(ph), cL = std::cos(ph), amp = (0.75f + 0.3f * run) * a;
  // ---- skins.js avatarAnim: Mo' Bends-style movement, knees, elbows, a twist in the body ----
  A.legR.x = sL * amp; A.legL.x = -sL * amp;
  A.shinR.x = -a * (0.08f + (0.8f + 0.4f * run) * std::max(0.f, cL));
  A.shinL.x = -a * (0.08f + (0.8f + 0.4f * run) * std::max(0.f, -cL));
  A.armR = vec3(-sL * amp * 0.8f, 0, (0.05f + std::sin(t * 1.3f) * 0.03f) * idle);
  A.armL = vec3(sL * amp * 0.8f, 0, -(0.05f + std::sin(t * 1.3f) * 0.03f) * idle);
  A.foreR.x = A.foreL.x = 0.1f + 0.45f * run * a + 0.06f * idle;
  A.body = vec3(-0.2f * run * a, sL * 0.1f * a, 0);
  A.lift = (1 - std::fabs(sL)) * 0.045f * a + std::sin(t * 1.9f) * 0.004f * idle;
  // standing about: a slow shift of weight from foot to foot, the shoulders rising with each breath
  A.body.z += std::sin(t * 0.55f) * 0.025f * idle; A.body.x += std::sin(t * 1.9f) * 0.012f * idle;
  A.legR.z += std::sin(t * 0.55f) * 0.02f * idle; A.legL.z += std::sin(t * 0.55f) * 0.02f * idle;
  if (pose.held && !pose.carry) A.armR.x = A.armR.x * 0.5f + 0.31f;
  if (pose.off && !pose.carry) A.armL.x = A.armL.x * 0.5f + 0.31f;
  // off the ground: knees up; falling hard: arms up
  if (pose.air && !pose.swim && !pose.sit) {
    float fall = clampf(-pose.vy / 12, 0, 1);
    A.legR = vec3(0.55f, 0, 0.05f); A.shinR = vec3(-1.0f, 0, 0); A.legL = vec3(-0.15f, 0, -0.05f); A.shinL = vec3(-0.4f, 0, 0);
    if (!pose.held) A.armR = vec3(0.3f + fall * 2.2f, 0, 0.3f + fall * 0.5f);
    if (!pose.off) A.armL = vec3(0.3f + fall * 2.2f, 0, -0.3f - fall * 0.5f);
  }
  if (pose.land > 0) { float k = pose.land / 0.2f; A.lift -= 0.1f * k; A.legR.x += 0.5f * k; A.legL.x += 0.5f * k; A.shinR.x -= k; A.shinL.x -= k; A.body.x -= 0.25f * k; }
  // swinging a tool: Minecraft's attack swing, a quick chop that eases out, the body twisting with it
  float at = pose.swing;
  if (at > 0) {
    float tw = std::sin(std::sqrt(at) * TAU) * 0.2f;
    float f = 1 - at; f *= f; f *= f; f = 1 - f;
    float f2 = std::sin(f * PI), f3 = std::sin(at * PI) * (pose.headPitch + 0.7f) * 0.75f;
    A.body.y -= tw;
    A.armR.x += f2 * 1.2f + f3; A.armR.y -= tw * 2; A.armR.z -= std::sin(at * PI) * 0.4f;
    A.armL.y -= tw; A.armL.x -= tw;
    A.foreR.x += f2 * 0.25f;
  }
  if (pose.bow > 0) {   // the bow arm straight out at the target; the drawing hand pulls back to the cheek
    float k = std::min(1.f, pose.bow), tr = k >= 0.95f ? std::sin(t * 47) * 0.012f : 0;
    A.armL = vec3(1.52f + tr, 0.1f + 0.08f * k, 0); A.foreL = vec3(0.04f, 0, 0);
    A.armR = vec3(1.45f, -0.45f - 0.25f * k, 0); A.foreR = vec3(0.1f + 0.15f * k, 0, -1.1f * k);
    A.body = vec3(A.body.x, A.body.y + 0.18f * k, 0);
  }
  if (pose.eat > 0) { A.armR = vec3(1.15f + std::sin(t * 22) * 0.12f, -0.35f, 0); A.foreR = vec3(0.9f, 0, 0); }
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
    // the fast swim, lying flat: a front crawl, each arm going round in turn, the body rolling, legs fluttering
    float sp = pose.swimPh != 0 ? pose.swimPh : ph * 0.9f;
    auto arm = [](float th, float side, vec3& ar, vec3& fo) {
      float u = std::fmod(std::fmod(th, TAU) + TAU, TAU), rec = u > PI ? std::sin(u - PI) : 0;
      ar = vec3(PI - u, 0, side * (0.18f + 0.32f * rec)); fo = vec3(0.12f + 1.0f * rec, 0, 0);
    };
    arm(sp, 1, A.armR, A.foreR); arm(sp + PI, -1, A.armL, A.foreL);
    float k = std::sin(sp * 3) * 0.32f;
    A.legR = vec3(k, 0, 0.04f); A.legL = vec3(-k, 0, -0.04f);
    A.shinR = vec3(-0.18f - std::max(0.f, std::cos(sp * 3)) * 0.2f, 0, 0); A.shinL = vec3(-0.18f - std::max(0.f, -std::cos(sp * 3)) * 0.2f, 0, 0);
    A.body = vec3(0, std::sin(sp) * 0.28f, 0);
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
  A.head = vec3((pose.swim ? 0.9f : pose.headPitch) - A.body.x, -A.body.y, 0);

  // ---- game/anim.js: livelier bodies ----
  float b0 = A.body.x, b1 = A.body.y;
  bool special = pose.swim || pose.sleep || pose.hang || pose.climb > 0 || pose.ladder || pose.sit || pose.deathT > 0 || pose.carry || pose.bow > 0 || pose.paddle || pose.fly;
  if (!special) {
    bool busyR = pose.held || pose.swing > 0 || pose.eat > 0 || pose.castWind > 0;
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
  // maps.js: a map held up in front of the chest, in both hands (or one, when the other holds something)
  if (!(pose.fp || pose.carry || pose.sleep || pose.swim || pose.hang || pose.climb > 0 || pose.deathT > 0 || pose.swing > 0) && (pose.mapMain || pose.mapOff)) {
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
  // game/villagers.js avatarAnim: new moves for the shared body
  if (pose.headYaw != 0) A.head.y += pose.headYaw;
  if (pose.wave > 0) { float k = std::min(1.f, pose.wave * 3); A.armR = vec3(2.7f * k, 0, 0.35f * k + std::sin(t * 14) * 0.35f * k); A.foreR = vec3(0.2f, 0, 0); }
  else if (pose.talk) { A.armR.x += 0.5f + std::sin(t * 3.1f) * 0.25f; A.foreR.x += 0.6f + std::sin(t * 2.3f) * 0.3f; A.armL.x += 0.35f + std::sin(t * 2.7f + 1) * 0.2f; A.foreL.x += 0.5f; A.head.x += std::sin(t * 4.3f) * 0.06f; }
  else if (pose.fold && pose.walkAmt < 1e-3f) { A.armR = vec3(0.95f, 0.35f, -0.55f); A.foreR = vec3(1.25f, 0, 0); A.armL = vec3(0.95f, -0.35f, 0.55f); A.foreL = vec3(1.25f, 0, 0); }
  if (pose.read) { A.armR = vec3(0.85f, -0.25f, 0); A.foreR = vec3(0.7f, 0, 0); A.armL = vec3(0.85f, 0.25f, 0); A.foreL = vec3(0.7f, 0, 0); A.head.x += 0.35f; }
  if (pose.hoe > 0) { float k = std::sin(pose.hoe * PI); A.armR = vec3(0.6f + k * 1.2f, 0, 0); A.armL = vec3(0.6f + k * 1.1f, 0, 0); A.body.x -= k * 0.25f; }
  return A;
}

void charRig(const CharModel& M, CharPose pose, const mat4& base0, CharRig& R, bool cloak, bool underPack) {
  // anim.js: the body turned its own way (third person), the head where you look
  if (!pose.fp && pose.hasBodyYaw && !pose.swim && !pose.sleep) { pose.yaw = pose.bodyYaw; pose.turned = true; }
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
  if (pose.deathT > 0) base = glm::rotate(base, std::min(1.f, pose.deathT * 3) * PI / 2, vec3(0, 0, 1));
  if (pose.swim) { base = glm::translate(base, vec3(0, 0.32f, 0.55f)); base = glm::rotate(base, -PI / 2 + pose.swimPitch * 0.8f, vec3(1, 0, 0)); base = glm::translate(base, vec3(0, -0.9f, 0)); }
  if (pose.sleep) { base = glm::rotate(base, PI, vec3(0, 1, 0)); base = glm::translate(base, vec3(0, 0.3f, 0)); base = glm::rotate(base, PI / 2, vec3(1, 0, 0)); base = glm::translate(base, vec3(0, -0.9f, 0)); }
  if (pose.fp) base = glm::translate(base, vec3(0, 0, 0.25f));   // FP_BODY: just behind the eyes
  CharAngles A = charAnimate(pose);
  if (pose.fp) { A.body.x *= 0.25f; A.body.y *= 0.5f; A.armL = vec3(A.armL.x * 0.45f, A.armL.y, A.armL.z - 0.06f); A.foreL.x *= 0.5f; }
  R.fpTorso = pose.fp && !pose.swim && !pose.sleep && pose.deathT <= 0;
  // the body shape: leg length lifts everything above the hips, build widens, the head its own size
  const BodyShape& S = M.body;
  float kl = S.legs, b = S.build, hip = 12 * kl;
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
