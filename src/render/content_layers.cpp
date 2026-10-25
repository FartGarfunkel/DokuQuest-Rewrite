// render/content_layers.cpp — the content features in the running game: the float and its line, the grappling
// hook and its rope, campfire smoke and sparks (Level::fx particles), the live instruments, the elytra glide for
// the local player, the M (map) and H (stable) keys, and opening the content screens.
#include "render/content_layers.h"
#include "render/content_draw.h"
#include "render/map_render.h"
#include "render/instruments.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/content_screens.h"
#include "assets/voxel.h"
#include "sim/maps.h"
#include "sim/fishing.h"
#include "sim/camping.h"
#include "sim/elytra.h"
#include "sim/stable.h"
#include <SDL.h>

namespace dq {

namespace {
// ---- small voxel models: the float, the hook
struct SmallModel { VoxelMeshGL gl; ivec3 origin{0}; float voxel = 0.01f; bool built = false; };
void boxInto(VoxelGrid& g, ivec3 off, vec3 lo, vec3 hi, u32 col) {   // lo/hi in voxels (centimetres)
  for (int z = (int)std::round(lo.z); z < (int)std::round(hi.z); z++) for (int y = (int)std::round(lo.y); y < (int)std::round(hi.y); y++)
    for (int x = (int)std::round(lo.x); x < (int)std::round(hi.x); x++) g.set(x + off.x, y + off.y, z + off.z, col);
}
SmallModel& bobberModel() {
  static SmallModel& m = *new SmallModel();
  if (m.built) return m;
  m.built = true;
  VoxelGrid g(ivec3(14, 29, 14)); m.origin = ivec3(7, 4, 7);
  boxInto(g, m.origin, vec3(-7, 5, -7), vec3(7, 17, 7), voxRGBA(200, 40, 40));
  boxInto(g, m.origin, vec3(-7, -4, -7), vec3(7, 5, 7), voxRGBA(235, 235, 235));
  boxInto(g, m.origin, vec3(-1.5f, 17, -1.5f), vec3(1.5f, 25, 1.5f), voxRGBA(235, 235, 235));
  VoxelMeshData md; buildVoxelMesh(g, md); m.gl.upload(md);
  return m;
}
SmallModel& hookModel() {
  static SmallModel& m = *new SmallModel();
  if (m.built) return m;
  m.built = true;
  VoxelGrid g(ivec3(28, 6, 26)); m.origin = ivec3(14, 3, 14);
  u32 c = voxRGBA(154, 164, 174), d = voxRGBA(120, 128, 138);
  boxInto(g, m.origin, vec3(-3, -3, -12), vec3(3, 3, 12), c);
  for (float s : {1.f, -1.f}) {
    boxInto(g, m.origin, vec3(s > 0 ? 0 : -14, -2.5f, -14), vec3(s > 0 ? 14 : 0, 2.5f, -10), d);
    boxInto(g, m.origin, vec3(s > 0 ? 10 : -14, -2.5f, -14), vec3(s > 0 ? 14 : -10, 2.5f, -4), d);
  }
  VoxelMeshData md; buildVoxelMesh(g, md); m.gl.upload(md);
  return m;
}
vec2 lightAt(World& w, vec3 p) { u8 l = w.getLight(ifloor(p.x), ifloor(p.y), ifloor(p.z)); return vec2((l >> 4) / 15.f, (l & 15) / 15.f); }
// where the line leaves the player (the rod's tip, or the hand that threw the hook)
vec3 tipOf(const GameSession& s, const Player& p, float fwd, float side, float up) {
  vec3 l = lookDir(p), r(std::cos(p.yaw), 0, -std::sin(p.yaw));
  return p.pos + vec3(0, EYE, 0) + l * fwd + r * side + vec3(0, up, 0);
}
std::vector<vec3> sagLine(vec3 a, vec3 b, float sag, int N, const Camera& cam) {
  std::vector<vec3> out;
  for (int i = 0; i <= N; i++) {
    float t = (float)i / N;
    vec3 q = a + (b - a) * t; q.y -= sag * 4 * t * (1 - t);
    out.push_back(cam.rel(glm::dvec3(q)));
  }
  return out;
}

// ---- a client's stable answers come as messages: catch them before the chat does
class ContentFxTap : public LevelFx {
public:
  LevelFx* inner = nullptr;
  void sound(const std::string& n, vec3 p, float r, float v, float pi) override { inner->sound(n, p, r, v, pi); }
  void soundUi(u32 id, const std::string& n) override { inner->soundUi(id, n); }
  void material(const std::string& m, const std::string& k, vec3 p) override { inner->material(m, k, p); }
  void voice(const std::string& c, const std::string& k, vec3 p, float pi) override { inner->voice(c, k, p, pi); }
  void particles(const std::string& k, vec3 p, int n, const json& e) override { inner->particles(k, p, n, e); }
  void message(u32 id, const std::string& t, const std::string& k) override {
    if (k == "stable-list") { try { stableListArrived(json::parse(t)); } catch (...) {} return; }
    inner->message(id, t, k);
  }
  void shake(u32 id, float s) override { inner->shake(id, s); }
};

class ContentLayer : public RenderLayer {
public:
  explicit ContentLayer(GameSession& s) : app(s.app), lvp(s.level.get()) {
    hScreen = app.events.on("contentScreen", [this](const json& a) {
      GameSession* s2 = app.session.get();
      if (!s2 || !s2->player || !a.is_array() || a.size() < 3 || a[0] != s2->player->name) return true;
      std::string id = a[1].get<std::string>();
      const json& args = a[2];
      if (id == "map") app.pushScreen(makeMapScreen(args.value("slot", -1)));
      else if (id == "waystones") app.pushScreen(makeWaystoneScreen(args.value("here", "")));
      else if (id == "stable") app.pushScreen(makeStableScreen());
      else if (id == "places") app.pushScreen(makePlacesScreen());
      return true;
    });
  }
  ~ContentLayer() override { app.events.off(hScreen); if (tapped && lvp && lvp->fx == &tap) lvp->fx = tap.inner; }
  const char* name() const override { return "content"; }
  int order() const override { return 120; }

  void afterPlayerMove(GameSession& s, double dt) override {
    Player* p = s.player;
    if (!p || g_elytraByController) return;
    Level& lv = *s.level;
    bool menu = s.app.topScreen() != nullptr;
    if (p->gliding && haveSnap) { p->pos = snapPos; p->vel = snapVel; elytraMove(lv, *p, dt); }
    else if (!menu && s.app.input.pressed(Act::Jump)) elytraJump(lv, *p);
  }

  void update(GameSession& s, double dt) override {
    Player* p = s.player;
    if (!p) return;
    Level& lv = *s.level;
    if (!tapped && lv.isRemote()) { tapped = true; tap.inner = lv.fx; lv.fx = &tap; }
    updateInstruments(s, dt);
    snapPos = p->pos; snapVel = p->vel; haveSnap = true;
    // the M and H keys
    bool menu = s.app.topScreen() != nullptr;
    const u8* ks = SDL_GetKeyboardState(nullptr);
    bool hNow = ks && ks[SDL_SCANCODE_H];
    if (!menu && !p->dead) {
      if (s.app.input.pressed(Act::Map)) {
        int slot = mapBestSlot(lv, *p);
        if (slot == -1) toast(lv, *p, "No map with you \xE2\x80\x94 make one from 8 paper round a compass");
        else { sfxUi(lv, *p, "map_paper"); s.app.pushScreen(makeMapScreen(slot)); }
      }
      if (hNow && !hDown) {
        if (p->sneaking) {
          if (lv.isRemote()) contentAction(lv, *p, "stable", {{"op", "quick"}});
          else { std::string m = stableQuick(lv, *p); if (!m.empty()) toast(lv, *p, m); }
        } else { sfxUi(lv, *p, "whistle"); s.app.pushScreen(makeStableScreen()); }
      }
    }
    hDown = hNow;
    campfireFx(s, dt);
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (f.portalDepth || !s.player) return;
    Level& lv = *s.level;
    World& w = s.world();
    std::vector<std::pair<std::vector<vec3>, std::pair<vec4, vec2>>> lines;
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    for (auto& e : lv.entities()) {
      if (e->removed || e->realm != s.player->realm) continue;
      if (auto* b = dynamic_cast<Bobber*>(e.get())) {
        Player* p = dynamic_cast<Player*>(lv.entity(b->owner));
        if (!p) continue;
        SmallModel& M = bobberModel();
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        vec2 L = lightAt(w, b->pos + vec3(0, 0.5f, 0));
        mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(b->pos)));
        if (b->biteT > 0) m = glm::rotate(m, std::sin(b->age * 25) * 0.2f, vec3(0, 0, 1));
        m = glm::scale(m, vec3(M.voxel)); m = glm::translate(m, -vec3(M.origin));
        VR.draw(M.gl, m, VoxelLight{L.x, L.y});
        // the line, from the rod's tip, sagging unless something's pulling
        bool fp = p == s.player && s.viewMode == 0;
        vec3 tip = fp ? tipOf(s, *p, 0.95f, 0.42f, 0.18f) : p->pos + vec3(0, 1.9f, 0) + lookDir(*p) * vec3(1.1f, 0.6f, 1.1f) + vec3(std::cos(p->yaw), 0, -std::sin(p->yaw)) * 0.5f;
        vec3 end = b->pos + vec3(0, 0.24f, 0);
        float d = std::hypot(end.x - tip.x, end.z - tip.z);
        float sag = b->biteT > 0 ? 0.02f : b->state == Bobber::Float ? 0.12f + d * 0.025f : 0.06f;
        lines.push_back({sagLine(tip, end, sag, 12, f.cam), {vec4(0.92f, 0.92f, 0.92f, 1), L}});
      } else if (auto* h = dynamic_cast<GrappleHook*>(e.get())) {
        Player* p = dynamic_cast<Player*>(lv.entity(h->owner));
        if (!p) continue;
        SmallModel& M = hookModel();
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        vec2 L = lightAt(w, h->pos);
        mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(h->pos)));
        m = glm::rotate(m, std::atan2(-(h->pos.x - p->pos.x), -(h->pos.z - p->pos.z)) + PI, vec3(0, 1, 0));
        m = glm::scale(m, vec3(M.voxel)); m = glm::translate(m, -vec3(M.origin));
        VR.draw(M.gl, m, VoxelLight{L.x, L.y});
        bool fp = p == s.player && s.viewMode == 0;
        vec3 tip = fp ? tipOf(s, *p, 0.7f, 0.35f, -0.25f) : p->pos + vec3(std::cos(p->yaw) * 0.4f, 1.2f, -std::sin(p->yaw) * 0.4f);
        float D = glm::distance(tip, h->pos);
        lines.push_back({sagLine(tip, h->pos, 0.05f + D * 0.04f, 10, f.cam), {vec4(0.72f, 0.58f, 0.35f, 1), L}});
      }
    }
    if (begun) VR.end();
    if (!lines.empty() && contentDraw().begin(f, f.cam.viewProj)) {
      for (auto& [pts, cl] : lines) contentDraw().lines(pts, cl.first, cl.second);
      contentDraw().end();
    }
  }

private:
  App& app;
  Level* lvp;
  int hScreen = 0;
  bool hDown = false, tapped = false, haveSnap = false;
  vec3 snapPos{0}, snapVel{0};
  ContentFxTap tap;
  double farAcc = 0;

  // campfire smoke, flames and sparks (camping.js campfireFx), and far columns of smoke beyond them
  void campfireFx(GameSession& s, double dtd) {
    Level& lv = *s.level;
    Player* pl = s.player;
    World& w = s.world();
    float step = (float)std::min(dtd, 0.1);
    float wind = s.frame.wind, wx = 0.35f * wind, wz = 0.15f * wind;
    vec3 c = vec3(s.frame.cam.pos);
    farAcc += dtd;
    bool farPass = farAcc >= 0.25;
    float farStep = (float)farAcc;
    if (farPass) farAcc = 0;
    float far = std::min((float)s.app.settings.renderDist * 16 + 8, 200.f);
    std::vector<std::pair<float, ivec3>> farFires;
    auto R = [] { return frand(); };
    for (const ivec3& q : campfiresIn(lv, pl->realm)) {
      u8 meta = w.getMeta(q.x, q.y, q.z);
      if (meta & CAMPFIRE_OUT) continue;
      float mx = (q.x & ~15) + 8.f, mz = (q.z & ~15) + 8.f;
      float dx = std::fabs(mx - c.x), dz = std::fabs(mz - c.z), d = std::hypot(dx, dz);
      if (std::fabs(q.y - c.y) > 160) continue;
      if (!(dx < 28 && dz < 28)) { if (farPass && d <= far) farFires.push_back({d, q}); continue; }
      bool soul = w.getBlock(q.x, q.y, q.z) == reg().blockId("soul_campfire");
      bool signal = w.getBlock(q.x, q.y - 1, q.z) == reg().blockId("hay_bale");
      auto flue = campFlue(w, q.x, q.y, q.z);
      vec3 sp = flue ? *flue : vec3((float)q.x, q.y + 0.55f, (float)q.z);
      // a column of smoke, rising and spreading
      for (int n = 0; n < 3; n++) if (R() < step * (signal ? 6 : 4.5f) / 3) {
        float life = signal ? 11 + R() * 4 : 6 + R() * 3, sz = (flue ? 0.3f : 0.26f) + R() * 0.12f;
        lv.fx->particles("campfire_smoke", vec3(sp.x + 0.5f + frand2() * 0.4f, sp.y, sp.z + 0.5f + frand2() * 0.4f), 1,
          {{"vel", {frand2() * 0.12f, (signal ? 1.9f : 1.25f) + R() * 0.4f, frand2() * 0.12f}}, {"life", life}, {"size", sz}, {"grow", 2.8}, {"grav", -0.03}, {"rise", true},
           {"wind", {wx * (signal ? 1.2f : 2.f), wz * (signal ? 1.2f : 2.f)}}, {"light", flue ? 3 : 8}});
      }
      // in a hearth only thin wisps curl up into the flue
      if (flue && R() < step * 2) lv.fx->particles("smoke_puff", vec3(q.x + 0.3f + R() * 0.4f, q.y + 0.5f, q.z + 0.3f + R() * 0.4f), 1, {{"vel", {0, 0.45, 0}}, {"life", 0.8f + R() * 0.5f}, {"size", 0.1}, {"grow", 0.8}, {"grav", -0.02}, {"rise", true}});
      // flames licking up from the logs
      if (R() < step * 7) lv.fx->particles(soul ? "soul_flame" : "flame", vec3(q.x + 0.3f + R() * 0.4f, q.y + 0.3f + R() * 0.2f, q.z + 0.3f + R() * 0.4f), 1, {{"vel", {0, 0.9f + R() * 0.6f, 0}}, {"life", 0.3f + R() * 0.25f}, {"size", 0.07}, {"shrink", true}, {"glow", true}, {"grav", 0}});
      // sparks and glowing embers drifting up
      if (R() < step * 3.5f) lv.fx->particles(soul ? "soul_flame" : "ember_spark", vec3(q.x + 0.35f + R() * 0.3f, q.y + 0.5f, q.z + 0.35f + R() * 0.3f), 1,
        {{"vel", {frand2() * 0.7f, flue ? 0.5f + R() * 0.6f : 1.5f + R() * 1.8f, frand2() * 0.7f}}, {"life", 0.9f + R() * 1.2f}, {"size", 0.022}, {"shrink", true}, {"glow", true}, {"grav", 0.35}, {"flutter", 0.4}});
      // a pop: a little burst of sparks
      if (R() < step * 0.25f) {
        lv.fx->particles("ember_spark", vec3(q.x + 0.5f, q.y + 0.45f, q.z + 0.5f), 5 + (int)(R() * 5), {{"ring", 1.2}, {"vel", {0, flue ? 1.3 : 3.2, 0}}, {"life", 0.9}, {"size", 0.02}, {"glow", true}, {"grav", 4}});
        lv.fx->sound("campfire_pop", vec3(q.x + 0.5f, q.y + 0.5f, q.z + 0.5f), 14);
      }
      if (R() < step * 0.7f) lv.fx->sound("fire_crackle", vec3(q.x + 0.5f, q.y + 0.5f, q.z + 0.5f), 12);
      // food on it sizzles
      json* e = w.blockEntity(q.x, q.y, q.z);
      if (e && e->is_object() && (*e)["items"].is_array() && R() < step * 3)
        for (int k = 0; k < 4 && k < (int)(*e)["items"].size(); k++) if (!(*e)["items"][k].is_null()) {
          lv.fx->particles("smoke_puff", vec3(q.x + CAMP_SPOTS[k].x, q.y + 0.5f, q.z + CAMP_SPOTS[k].y), 1, {{"vel", {0, 0.5, 0}}, {"life", 1.4}, {"size", 0.06}, {"grow", 1.5}, {"grav", -0.05}, {"rise", true}});
          break;
        }
    }
    // (0.9.0.11) smoke you can see from far off: slow columns of big, long-lived puffs (no flames, sparks or sound)
    std::sort(farFires.begin(), farFires.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < farFires.size() && i < 16; i++) {
      ivec3 q = farFires[i].second;
      bool signal = w.getBlock(q.x, q.y - 1, q.z) == reg().blockId("hay_bale");
      if (R() > farStep * (signal ? 1.6f : 1.1f)) continue;
      auto flue = campFlue(w, q.x, q.y, q.z);
      vec3 sp = flue ? *flue : vec3((float)q.x, q.y + 0.6f, (float)q.z);
      lv.fx->particles("campfire_smoke", vec3(sp.x + 0.5f + frand2() * 0.3f, sp.y, sp.z + 0.5f + frand2() * 0.3f), 1,
        {{"vel", {frand2() * 0.1f, (signal ? 2.0f : 1.4f) + R() * 0.4f, frand2() * 0.1f}}, {"life", signal ? 16 + R() * 5 : 11 + R() * 4}, {"size", 0.42f + R() * 0.18f}, {"grow", 3.4},
         {"grav", -0.02}, {"rise", true}, {"wind", {wx * 1.6f, wz * 1.6f}}, {"light", 6}});
    }
  }
};
} // namespace

void registerContentLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerHeldMap();
  registerInstruments();
  registerRenderLayer([](GameSession& s) { return std::make_shared<ContentLayer>(s); });
}

} // namespace dq
