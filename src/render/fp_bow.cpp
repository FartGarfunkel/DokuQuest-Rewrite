// render/fp_bow.cpp — the bow drawn in first person (game/bows.js fpBow): held out in the left hand, nearer and
// toward the middle as you draw, the string pulled back to the nock with the right hand, trembling at full draw;
// letting go snaps the string and kicks the bow. The stave bends in eighths (item models bow_pq0..8: the
// stave, the string through the nock and the arrow on it). The FOV pull is in client/play.cpp.
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "client/app.h"
#include "assets/item_models.h"
#include "sim/player.h"
#include "core/item_glint.h"

namespace dq {
namespace {

struct BowKick { float prev = 0, kick = 0; double t = 0; };

mat4 aimArm(vec3 sh, vec3 fist) {
  vec3 d = fist - sh; float len = glm::length(d); if (len < 1e-4f) len = 1;
  d /= len;
  vec3 yv = -d, xv(yv.z, 0, -yv.x);
  float xl = glm::length(xv); xv /= xl > 1e-4f ? xl : 1.f;
  vec3 zv = glm::cross(xv, yv);
  const float sl = 0.72f;   // a slimmer arm, so it doesn't fill the view
  mat4 m(1);
  m[0] = vec4(xv * sl, 0); m[1] = vec4(yv, 0); m[2] = vec4(zv * sl, 0);
  m[3] = vec4(fist - (yv * -0.3f + zv * -0.02f), 1);
  return m;
}

int drawFpBow(HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
  static BowKick K;
  Player* p = s.player;
  if (!p) return 0;
  double dt = clampv((float)(f.time - K.t), 0.f, 0.1f); K.t = f.time;
  const ItemStack& held = p->held();
  bool bow = !held.empty() && held.id == "bow" && !p->carrying.is_object() && !p->dead;
  if (!bow) { K.prev = 0; K.kick = 0; return 0; }
  for (int i = 0; i <= 8; i++) itemModel("bow_pq" + std::to_string(i));   // built in the background the first time
  if (K.prev > 0.12f && p->bowPull <= 0) K.kick = 0.28f;
  K.prev = p->bowPull;
  if (K.kick > 0) K.kick -= (float)dt;
  float k = p->bowPull > 0 ? std::min(1.f, p->bowPull) : 0.f, kick = K.kick > 0 ? K.kick / 0.28f : 0.f;
  if (k <= 0 && kick <= 0) return 0;
  float ease = k * k * (3 - 2 * k), shake = k >= 0.95f ? std::sin((float)f.time * 47) * 0.004f : 0.f;
  int q = (int)std::lround(ease * 8);
  const VoxelModel* im = nullptr;
  for (int d = 0; d <= 8 && !im; d++) for (int sgn : {0, 1, -1}) {   // the nearest stave that has been built
    int qq = q + sgn * d; if (qq < 0 || qq > 8) continue;
    const VoxelModel& c = itemModel("bow_pq" + std::to_string(qq));
    if (c.ready()) { im = &c; q = qq; break; }
  }
  if (!im) return 0;
  // the step bob, as the hands have it
  const Settings& st = s.app.settings;
  float bobK = st.viewBob ? L.bobAmt : 0, hsn = std::sin(L.bob * 1.1f), bx = hsn * 0.025f * bobK, by = -hsn * hsn * 0.03f * bobK;
  // the bow's own frame: tips upright (leaning a touch), the belly away from you, the string toward you
  const float cant = 0.22f, sc = 0.85f;
  vec3 T(std::sin(cant), std::cos(cant), 0), U(0, 0, -1), N(-std::cos(cant), std::sin(cant), 0);
  vec3 X = 0.7071f * (T - U), Y = 0.7071f * (T + U);
  mat4 M(1);
  M[0] = vec4(X * sc, 0); M[1] = vec4(Y * sc, 0); M[2] = vec4(N * sc, 0);
  M[3] = vec4(-0.12f + bx * 0.4f - 0.03f * ease, -0.16f + by + shake + kick * 0.03f, -1.0f - kick * 0.05f, 1);
  auto pt = [&](float x, float y) { return vec3(M * vec4(x, y, 0, 1)); };
  // light at your eyes
  World& w = s.world();
  u8 l = w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.62f), ifloor(p->pos.z));
  VoxelLight Lt{(l >> 4) / 15.f, (l & 15) / 15.f};
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_BLEND);
  VoxelRenderer& VR = voxelRenderer();
  VR.begin(f, proj, false);
  VR.draw(im->gl, M * im->matrix(), Lt, vec4(0), itemGlints(held) ? 1.f : 0.f);
  // an arm for each, reaching from the shoulder to the hand: the left round the grip, the right at the nock
  const VoxelModel& arm = firstPersonArm();
  auto drawArm = [&](const mat4& m) { VR.draw(arm.gl, m * arm.matrix(), Lt); drawFirstPersonSleeve(m, Lt); };
  float pull = q / 8.f, hum = kick * std::sin((float)f.time * 90) * 0.03f;
  vec3 grip = pt(-0.235f * (1 + pull * 0.3f) + 0.02f, 0.235f * (1 + pull * 0.3f) - 0.02f);
  drawArm(aimArm(vec3(-0.7f, -0.75f, 0.f), grip));
  vec3 nock = pt(pull * 0.27f + hum, -pull * 0.27f - hum);
  drawArm(aimArm(vec3(0.85f, -0.62f + kick * 0.05f, 0.15f + kick * 0.1f), vec3(nock.x + 0.04f + kick * 0.08f, nock.y - 0.03f, nock.z + 0.03f + kick * 0.06f)));
  VR.end();
  return 3;   // both hands are busy
}

} // namespace

void registerFpBow() {
  static bool done = false;
  if (done) return;
  done = true;
  auto prev = HeldItemLayer::handsHook;
  HeldItemLayer::handsHook = [prev](HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
    int t = drawFpBow(L, s, f, proj);
    if (!(t & 1) && prev) t |= prev(L, s, f, proj);
    return t;
  };
}

} // namespace dq
