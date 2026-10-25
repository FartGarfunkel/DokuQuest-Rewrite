// client/play.cpp — local player glue (input -> PlayerController, camera). The rules are in sim/.
#include "client/play.h"
#include "assets/cape_tex.h"
#include "client/app.h"
#include "ui/screen.h"
#include <random>

namespace dq {

struct LocalState {
  std::unique_ptr<PlayerController> ctl;
  Player* player = nullptr;
  Level* level = nullptr;
  double sneakTapT = -10, deadT = 0;
  bool sneakWas = false;
  // camera smoothing
  bool hasEy = false, hasCamY = false, hasFov = false, hasShoulder = false, hasTp = false;
  float ey = 0, camY = 0, fov = 70, tpDist = 4;
  glm::dvec3 shoulder{0};
  int shoulderSide = 1;
  u64 menuFrame = 0;   // the last frame a menu was open (a key that just closed / switched one must not act in the world)
};
static std::unordered_map<GameSession*, LocalState>& states() { static std::unordered_map<GameSession*, LocalState> m; return m; }
static LocalState& stateOf(GameSession& s) {
  LocalState& st = states()[&s];
  if (s.player && (!st.ctl || st.player != s.player || st.level != s.level.get())) {
    st = LocalState{};
    st.ctl = std::make_unique<PlayerController>(*s.level, *s.player); st.player = s.player; st.level = s.level.get();
  }
  return st;
}
PlayerController* localController(GameSession& s) { return s.player ? stateOf(s).ctl.get() : nullptr; }

void clientTickLocalPlayer(GameSession& s, double dt) {
  if (!s.player) return;
  App& app = s.app;
  Input& in = app.input;
  LocalState& st = stateOf(s);
  PlayerController& c = *st.ctl;
  Player& p = *s.player;
  bool menu = app.topScreen() != nullptr;
  if (menu) st.menuFrame = app.frame;
  bool justMenu = app.frame <= st.menuFrame + 1;
  {   // the cape painted in the cape editor (Settings::cape) is worn by the local player; the character layer draws it
    std::string cur = p.data.is_object() && p.data.contains("customCape") && p.data["customCape"].is_string() ? p.data["customCape"].get<std::string>() : "";
    std::string want = wornCapeName(app.settings);
    if (cur != want) { if (want.empty()) p.data.erase("customCape"); else p.data["customCape"] = want; }
  }
  PlayerInput pi;
  if (!menu) {
    vec2 mv = in.moveAxis(), look = in.lookDelta();
    pi.mx = mv.x; pi.mz = -mv.y; pi.lookX = look.x; pi.lookY = look.y;
    pi.jump = in.down(Act::Jump); pi.sneak = in.down(Act::Sneak); pi.sprint = in.down(Act::Sprint);
    pi.attack = in.down(Act::Attack); pi.attackPressed = in.pressed(Act::Attack);
    pi.use = in.down(Act::Use); pi.usePressed = in.pressed(Act::Use);
    pi.drop = in.pressed(Act::Drop) && !justMenu && !in.down(Act::Sprint); pi.dropAll = in.pressed(Act::Drop) && !justMenu && in.down(Act::Sprint);
    pi.pick = in.pressed(Act::PickBlock); pi.swap = in.pressed(Act::SwapHands); pi.place = in.pressed(Act::PlaceMode);
    // crawl: sneak tapped twice (keyboard and pads alike)
    if (in.pressed(Act::Sneak)) { if (app.time - st.sneakTapT < 0.3) { pi.crawl = true; st.sneakTapT = -10; } else st.sneakTapT = app.time; }
    for (int i = 0; i < 9; i++) if (in.pressed((Act)((int)Act::Hot1 + i))) pi.slot = i;
    pi.wheel = -(int)in.wheel();
    if (in.pressed(Act::HotNext)) pi.wheel += 1;
    if (in.pressed(Act::HotPrev)) pi.wheel -= 1;
    if (in.pressed(Act::Debug)) s.debugOverlay = !s.debugOverlay;
    if (in.pressed(Act::HideHud)) s.hudHidden = !s.hudHidden;
    if (in.pressed(Act::View)) s.viewMode = (s.viewMode + 1) % 4;   // first person, over the shoulder, behind, in front
    if (in.pressed(Act::Screenshot)) app.takeScreenshot();
  }
  // death: the UI's death screen calls localController(s)->respawn(); until one is open, jump/use respawns
  if (p.dead) {
    st.deadT += dt;
    if (!menu && st.deadT > 1.0 && (in.pressed(Act::Jump) || in.pressed(Act::Use) || in.pressed(Act::MenuOk))) { c.respawn(); st.deadT = 0; }
  } else st.deadT = 0;
  c.aimOverride = s.viewMode == 1 && st.hasShoulder;
  if (c.aimOverride) { c.aimOrigin = st.shoulder; c.aimDir = lookVector(p.yaw, p.pitch); }
  c.update((float)dt, pi);
  // what the renderer and HUD read
  s.target = c.target;
  s.breaking = c.mining.active;
  s.breakBlock = ivec3(c.mining.x, c.mining.y, c.mining.z);
  s.breakProgress = c.mining.active ? std::min(1.f, c.mining.progress) : 0.f;
}

void clientPlayerCamera(GameSession& s, Camera& cam, double dtd) {
  Player& p = *s.player;
  LocalState& st = stateOf(s);
  PlayerController& c = *st.ctl;
  World& w = s.world();
  const Settings& set = s.app.settings;
  float dt = (float)dtd;
  static thread_local std::mt19937 rng{1234};
  std::uniform_real_distribution<float> U(-0.5f, 0.5f);
  // view bobbing: one gentle dip with each step, a sway from side to side, a touch of roll and a little nod
  float bx = 0, by = 0, roll = 0, nod = 0;
  if (set.viewBob && p.bobAmt > 0.01f) {
    float ph = p.bob * 1.1f, sn = std::sin(ph), a = p.bobAmt;
    bx = sn * 0.03f * a; by = -sn * sn * 0.05f * a; roll = sn * 0.0055f * a; nod = sn * sn * 0.006f * a;
  }
  if (p.hurtTilt != 0) { roll += p.hurtTilt; p.hurtTilt *= std::max(0.f, 1 - dt * 8); if (std::fabs(p.hurtTilt) < 0.002f) p.hurtTilt = 0; }
  float sx = 0, sy = 0;
  if (c.shakeAmt > 0) { sx = U(rng) * c.shakeAmt * 0.4f; sy = U(rng) * c.shakeAmt * 0.4f; c.shakeAmt = std::max(0.f, c.shakeAmt - dt * 1.5f); }
  float rx = std::cos(p.yaw), rz = -std::sin(p.yaw);
  float fov = set.fov + (p.sprinting ? 6.f : 0.f);
  if (c.bowPull > 0) fov -= std::min(1.f, c.bowPull) * 10;
  if (!st.hasFov) { st.fov = fov; st.hasFov = true; }
  st.fov += (fov - st.fov) * std::min(1.f, dt * 8);
  bool seated = p.riding != 0;
  float ey = p.sleeping ? 0.45f : seated ? 1.0f : p.eye;
  // crouching, standing and swimming move your eyes smoothly; stepping up rises smoothly too
  if (!st.hasEy || p.sleeping || seated || p.dead || std::fabs(ey - st.ey) > 1.5f) { st.ey = ey; st.hasEy = true; }
  else st.ey += (ey - st.ey) * std::min(1.f, dt * (ey < st.ey ? 18.f : 11.f));
  float baseY = p.pos.y + st.ey;
  if (st.hasCamY && p.onGround && !p.flying && baseY > st.camY && baseY - st.camY < 1.05f && !seated) baseY = st.camY + (baseY - st.camY) * std::min(1.f, dt * 14);
  st.camY = baseY; st.hasCamY = true;
  cam.pos = glm::dvec3(p.pos.x + rx * bx + sx, baseY + by + sy, p.pos.z + rz * bx);
  cam.yaw = p.yaw; cam.pitch = p.pitch - nod; cam.roll = p.dead ? 0.6f : roll;
  cam.fov = st.fov;
  vec3 L = lookVector(p.yaw, p.pitch);
  if (s.viewMode == 1 && !p.dead) {
    // over the shoulder: behind and off to one side, smoothly following
    int side = st.shoulderSide;
    glm::dvec3 h(cam.pos.x, p.pos.y + (seated ? 1.2f : p.eye) + 0.12f, cam.pos.z);
    RayHit sh = raycastBlocks(w, h, vec3(rx * side, 0, rz * side), 0.85f);
    double so = std::max(0.0, (sh.hit ? sh.dist : 0.75) - 0.2);
    glm::dvec3 o(h.x + rx * side * so, h.y, h.z + rz * side * so);
    RayHit bh = raycastBlocks(w, o, -L, 2.9f);
    double dist = std::max(0.4, (bh.hit ? bh.dist : 2.6) - 0.25);
    glm::dvec3 tgt = o - glm::dvec3(L) * dist;
    double k = st.hasShoulder ? std::min(1.f, dt * 14) : 1;
    if (!st.hasShoulder) st.shoulder = tgt;
    st.shoulder += (tgt - st.shoulder) * k;
    if (bh.hit && glm::length(st.shoulder - o) > dist + 0.05) st.shoulder = tgt;
    st.hasShoulder = true;
    cam.pos = st.shoulder; cam.roll = 0;
    return;
  }
  st.hasShoulder = false;
  if (s.viewMode >= 2 && !p.dead) {
    bool front = s.viewMode == 3;
    float d = front ? 1.f : -1.f;
    glm::dvec3 h(cam.pos.x, p.pos.y + (seated ? 1.2f : p.eye), cam.pos.z);
    RayHit hit = raycastBlocks(w, h, L * d, 4.2f);
    float want = std::max(0.6f, (hit.hit ? hit.dist : 4.f) - 0.25f);
    if (!st.hasTp) { st.tpDist = want; st.hasTp = true; }
    st.tpDist = std::min(want, st.tpDist + (want - st.tpDist) * std::min(1.f, dt * 5));
    cam.pos = h + glm::dvec3(L * d * st.tpDist);
    if (front) { cam.yaw = p.yaw + PI; cam.pitch = -p.pitch; }
    cam.roll = 0;
  } else st.hasTp = false;
}

} // namespace dq
