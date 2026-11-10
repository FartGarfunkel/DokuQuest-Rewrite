#include "render/world_fx.h"
#include <map>
#include <algorithm>
#include <cstdlib>
#include "render/gl.h"
#include "client/session.h"
#include "client/app.h"
#include "common/holiday.h"
#include "render/sky.h"
#include "ui/screen.h"
#include "audio/audio.h"
#include "client/client_fx.h"
#include "mod/modloader.h"
#include "render/item_layers.h"
#include "render/vehicle_layer.h"
#include "render/char_layer.h"
#include "ui/magic_screens.h"
#include "render/content_layers.h"
#include "render/creature_layer.h"
#include "render/endstory_layer.h"
#include "render/villager_layer.h"
#include "client/play.h"
#include "client/photo_mode.h"
#include "gen/structures_api.h"
#include "sim/rules.h"
#include <imgui.h>

namespace dq {

// ---- DQ_PROFILE=1: CPU time per render layer and per world pass, the slowest listed in the log every 600 frames ----
namespace {
struct Prof {
  bool on = std::getenv("DQ_PROFILE") != nullptr;
  bool finish = on && std::atoi(std::getenv("DQ_PROFILE")) == 2;   // DQ_PROFILE=2: glFinish after each scope (GPU time)
  bool gpu = on && std::atoi(std::getenv("DQ_PROFILE")) == 3;      // DQ_PROFILE=3: GPU timestamps round each scope (pipelined)
  struct GpuQ { std::string k; GLuint a, b; };
  std::vector<GpuQ> pend[4]; int slot = 0; GLuint64 lastGpu = 0;
  std::map<std::string, double> ms; u64 frames = 0; double lastT = 0;
  int every = std::getenv("DQ_PROFILE_FRAMES") ? std::max(10, std::atoi(std::getenv("DQ_PROFILE_FRAMES"))) : 600;
  void add(const std::string& k, double t) { ms[k] += t; }
  void frame() {
    if (gpu) {
      slot = (slot + 1) % 4;
      for (auto& g : pend[slot]) {
        GLuint64 ta = 0, tb = 0; glGetQueryObjectui64v(g.a, GL_QUERY_RESULT, &ta); glGetQueryObjectui64v(g.b, GL_QUERY_RESULT, &tb);
        ms["gpu " + g.k] += (double)(tb - ta) / 1e6; GLuint q[2] = {g.a, g.b}; glDeleteQueries(2, q);
        // a long time on the GPU inside one scope is ours; a long gap between two of ours is the GPU busy elsewhere
        if (tb - ta > 100000000ull) DQ_LOG("profile: gpu hitch %s %.0f ms", g.k.c_str(), (tb - ta) / 1e6);
        if (lastGpu && ta > lastGpu && ta - lastGpu > 100000000ull) DQ_LOG("profile: gpu idle for us %.0f ms before %s", (ta - lastGpu) / 1e6, g.k.c_str());
        lastGpu = tb;
      }
      pend[slot].clear();
    }
    if (!on || ++frames % every) return;
    std::vector<std::pair<double, std::string>> v; for (auto& [k, t] : ms) v.push_back({t / every, k});
    std::sort(v.rbegin(), v.rend());
    const double now = nowSeconds(); const double fps = lastT > 0 ? every / (now - lastT) : 0; lastT = now;
    std::string out = "fps " + std::to_string((int)fps) + " | "; for (size_t i = 0; i < v.size() && i < 24; i++) { char b[96]; snprintf(b, sizeof b, "%s %.2f | ", v[i].second.c_str(), v[i].first); out += b; }
    DQ_LOG("profile (ms/frame): %s", out.c_str());
    ms.clear();
  }
};
Prof& prof() { static Prof p; return p; }
struct ProfScope {
  const char* a; const char* b; double t0;
  GLuint qa = 0;
  ProfScope(const char* a_, const char* b_ = "") : a(a_), b(b_), t0(prof().on ? nowSeconds() : 0) { if (prof().gpu) { glGenQueries(1, &qa); glQueryCounter(qa, GL_TIMESTAMP); } }
  ~ProfScope() {
    if (prof().finish) glFinish();
    if (prof().gpu) { GLuint qb; glGenQueries(1, &qb); glQueryCounter(qb, GL_TIMESTAMP); prof().pend[prof().slot].push_back({std::string(a) + b, qa, qb}); }
    if (!prof().on) return;
    const double ms = (nowSeconds() - t0) * 1000.0;
    prof().add(std::string(a) + b, ms);
    if (ms > 100) DQ_LOG("profile: hitch %s%s %.0f ms (frame %llu)", a, b, ms, (unsigned long long)prof().frames);
  }
};
}

static std::vector<RenderLayerFactory>& layerFactories() { static std::vector<RenderLayerFactory> f; return f; }
void registerRenderLayer(RenderLayerFactory f) { layerFactories().push_back(std::move(f)); }

GameSession::GameSession(App& a, std::unique_ptr<Level> lv) : app(a), level(std::move(lv)) {
  frame.settings = &app.settings;
  setHolidayEventsEnabled(app.settings.extra.value("holidayEvents", true));
  fx = std::make_unique<ClientFx>(*this); level->fx = fx.get();
  worldRenderer.init(app.settings, app.tileTex);
  sky = std::make_unique<SkyRenderer>();
  sky->init();
  spawnPlayer();
  locals.emplace_back();   // the first person: keyboard + mouse (+ any controller until someone else joins)
  locals[0].player = player; locals[0].frame.settings = &app.settings; locals[0].src.kb = true;
  worldRenderer.setWorld(&world());
  registerItemLayers();   // held item + first-person arm, dropped items (render/item_layers)
  registerCharacterLayers();   // players as voxel characters (render/char_layer)
  registerMagicClient();  // spell effects, mana bar, magic keys and screens (render/magic_fx, ui/magic_screens)
  registerContentLayers();   // maps, instruments, fishing, camping, elytra, stable keys (render/content_layers)
  registerCreatureLayers();   // creatures, projectiles, gibs, felled trees, fauna (render/creature_layer)
  registerEndStoryLayers();   // the watcher, pearls, darkness/dread, the story's quest line, egg tags (render/endstory_layer)
  registerVehicleLayers();    // minecarts, boats, the ship, machines, pistons, TNT (render/vehicle_layer)
  registerVillagerClient();   // villagers as voxel people, their tags, your jobs, the talk/board/path screens (render/villager_layer)
  for (auto& f : layerFactories()) if (auto l = f(*this)) layers.push_back(l);
  std::sort(layers.begin(), layers.end(), [](auto& x, auto& y) { return x->order() < y->order(); });
  if (app.mods) app.mods->onWorldOpen(*level, app.events, false);
  app.events.emit("worldOpen", json::array({level->info.name}));
  DQ_LOG("world '%s' open (seed %d)", level->info.name.c_str(), level->info.seed);
}

GameSession::~GameSession() {
  if (app.mods) app.mods->onWorldClose();
  level->fx = &level->nullFx;
  for (auto& l : locals) freeLocalUi(l);
  layers.clear();
  worldRenderer.setWorld(nullptr);
}

void GameSession::spawnPlayer() {
  auto p = std::make_unique<Player>();
  p->name = app.settings.playerName;
  p->skin = app.settings.skin;
  json saved = level->loadPlayer(p->name);
  if (!saved.is_null()) p->load(saved);
  else {
    p->mode = level->info.mode;
    vec3 sp = level->info.spawn;
    float yaw = 0;
    const char* at = std::getenv("DQ_SPAWN_AT");   // tests: "x,y,z" or a structure kind (gen/structures_api.h)
    if (at && level->gen && debugSpawnPoint(*level->gen, at, sp, yaw)) p->yaw = yaw;
    else if (sp.y < 0 && level->gen && !rule(*level, "startInWild") && level->info.worldType != "floating" && villageStartPoint(*level->gen, sp, yaw)) { level->info.spawn = sp; p->yaw = yaw; }   // villages.js: a new world starts at a village's edge (not over the void: Floating Islands looks for an island below)
    if (sp.y < 0 && level->gen && rule(*level, "startInWild") && wildStartPoint(*level->gen, sp)) level->info.spawn = sp;   // World setting "Start in the wild"
    if (sp.y < 0) {
      DimGenerator* g = level->gen->forDim(DIM_OVERWORLD);
      int sx = 0, sz = 0, h = g ? g->spawnHeight(0, 0) : 100;
      // look for dry land near the origin, like the original
      for (int r = 0; r < 400 && g; r += 16) {
        ColumnSample s; g->sample(r, r / 2, s);
        if (s.height > 0 && s.water < 0) { sx = r; sz = r / 2; h = s.height + 1; break; }
      }
      if (level->info.worldType == "floating" && g) {   // the middle of an island: every way round has ground under it
        bool found = false;
        for (int r = 0; r < 1600 && !found; r += 24) for (int a = 0; a < 8 && !found; a++) {
          const int x = (int)(std::cos(a * 0.785f) * r), z = (int)(std::sin(a * 0.785f) * r);
          ColumnSample s; g->sample(x, z, s); if (s.height <= 0 || s.floatBottom < 0) continue;
          bool ok = true; for (int d = 0; d < 4 && ok; d++) { ColumnSample q; g->sample(x + (d == 0 ? 8 : d == 1 ? -8 : 0), z + (d == 2 ? 8 : d == 3 ? -8 : 0), q); ok = q.height > 0 && q.floatBottom >= 0; }
          if (ok) { sx = x; sz = z; h = s.height + 1; found = true; }
        }
      }
      sp = vec3(sx + 0.5f, dimBase(DIM_OVERWORLD) + h + 1, sz + 0.5f);
      level->info.spawn = sp;
    }
    p->pos = sp;
  }
  player = static_cast<Player*>(level->spawn(std::move(p)));
}

bool GameSession::paused() const {
  if (!split()) { Screen* s = app.topScreen(); return s && s->pausesGame(); }
  // split screen: a shared menu (pause) stops the world; otherwise only when everybody is in a menu that does
  auto top = [](const std::vector<std::unique_ptr<Screen>>& v) { return v.empty() ? nullptr : v.back().get(); };
  const auto& shared = entered ? locals[cur].screens : app.screens;
  if (Screen* g = top(shared); g && g->pausesGame()) return true;
  for (int i = 0; i < (int)locals.size(); i++) {
    Screen* t = top(entered && i == cur ? app.screens : locals[i].screens);
    if (!t || !t->pausesGame()) return false;
  }
  return true;
}

void GameSession::save() {
  for (auto& l : locals) { Player* p = (&l == &locals[cur]) ? player : l.player; if (p) level->savePlayer(p->name, p->save()); }
  level->save(true);
}

void GameSession::tickLocalPlayer(double dt) {
  clientTickLocalPlayer(*this, dt);   // client/play.cpp -> sim/controller (the rules live in sim/)
}

// layers that belong to one viewer (menu keys, deaths, the mana bar) run for every person; the rest of the world only once
static bool perViewerLayer(const char* n) { return !std::strcmp(n, "hud") || !std::strcmp(n, "magic-fx"); }

void GameSession::update(double dt) {
  if (!player) return;
  level->autosaveEvery = (double)std::max(0, app.settings.autosaveSec);   // a live change in Settings applies on the next tick
  setHolidayEventsEnabled(app.settings.extra.value("holidayEvents", true));
  selectLocal(0);
  if (level->net) level->net->update(*level, dt);   // multiplayer (net/): a joined world, or one open to LAN
  checkJoin();                                      // Start on a free controller joins split screen
  struct RdGuard {   // more people, shorter view: lend the capped render distance to everything below
    GameSession& s; bool on;
    explicit RdGuard(GameSession& g) : s(g), on(g.split()) { s.lendRenderDist(on); }
    ~RdGuard() { s.lendRenderDist(false); }
  } rdGuard(*this);
  const int n = (int)locals.size();
  if (split()) {
    layoutLocals();
    for (int i = 0; i < n; i++) { enterLocal(i); if (Screen* sc = app.topScreen()) sc->update(app, dt); leaveLocal(); }   // each person's own menu
  }
  bool pz = paused() && !level->allRealms().empty() && !level->net;   // a shared world never pauses
  if (!pz) {
    if (!split()) {
      tickLocalPlayer(dt);
      for (auto& l : layers) l->afterPlayerMove(*this, dt);
    } else for (int i = 0; i < n; i++) {
      enterLocal(i);
      tickLocalPlayer(dt);
      for (auto& l : layers) l->afterPlayerMove(*this, dt);
      leaveLocal();
    }
    tickAcc += dt;
    int ticks = 0;
    { ProfScope ps("sim.tick"); while (tickAcc >= TICK_DT && ticks < 4) { level->tick(TICK_DT); tickAcc -= TICK_DT; ticks++; } }
    if (ticks == 4) tickAcc = 0;
  }
  World& w = world();
  if (worldRenderer.world() != &w) { worldRenderer.setWorld(&w); worldRenderer.remeshAll(); }   // a new realm: mesh what is already loaded there
  std::vector<StreamAnchor> anchors = {{player->pos, app.settings.renderDist, true}};
  for (int i = 1; i < n; i++) if (locals[i].player && locals[i].player->realm == player->realm) anchors.push_back({locals[i].player->pos, app.settings.renderDist, true});
  for (auto& l : layers) l->streamAnchors(*this, anchors);
  if (level->net) level->net->streamAnchors(player->realm, anchors);   // the host keeps remote players' surroundings loaded
  { ProfScope ps("world.stream"); w.stream(anchors, 3.0); }
  // cameras: one per person, each in its own viewport
  int dw, dh; SDL_GL_GetDrawableSize(app.window, &dw, &dh);
  float sx = (float)dw / std::max(1, app.width), sy = (float)dh / std::max(1, app.height);
  for (int i = 0; i < n; i++) {
    selectLocal(i);
    Camera& cam = frame.cam;
    clientPlayerCamera(*this, cam, dt);   // client/play.cpp: eye height, view bob, shoulder/behind/front views
    int vw = dw, vh = dh;
    if (split()) { const LocalView& v = locals[i].view; vw = std::max(1, (int)std::lround(v.w * sx)); vh = std::max(1, (int)std::lround(v.h * sy)); }
    cam.aspect = vh > 0 ? (float)vw / vh : 1.f;
    cam.farZ = std::max(1024.f, (float)app.settings.farTerrain + 512.f);
    cam.update();
    frame.time = app.time; frame.width = vw; frame.height = vh;
    const bool photo = photoMode().active && !split();
    const double realTime = level->info.time;
    if (photo && photoMode().setTime) level->info.time = photoDayTime();   // photo mode's hour: only for the sky's picture
    sky->computeFrame(*level, frame, app.settings.renderDist);
    level->info.time = realTime;
    if (photo) photoFrame(frame);
    cam.update();
    { ProfScope ps("world.meshUpdate"); worldRenderer.update(cam, *app.pool, 4.0 / n); }
  }
  selectLocal(0);
  if (!split()) for (auto& l : layers) { ProfScope ps(l->name(), ".update"); l->update(*this, dt); }
  else for (int i = 0; i < n; i++) {
    enterLocal(i);
    for (auto& l : layers) if (i == 0) l->update(*this, dt); else if (perViewerLayer(l->name())) l->update(*this, 0.0);
    leaveLocal();
  }
  if (app.audio) app.audio->setListener(vec3(frame.cam.pos), frame.cam.forward(), vec3(0, 1, 0));
}

void GameSession::render() {
  if (split()) { splitRender(); return; }
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
  prof().frame();
  { ProfScope ps("world.fx"); worldFxBegin(*this, frame); }   // the built-in shaders' sun shadow map (render/world_fx.cpp)
  { ProfScope ps("sky"); sky->draw(frame); }
  { ProfScope ps("world.opaque"); worldRenderer.drawOpaque(frame); }
  { ProfScope ps("world.cutout"); worldRenderer.drawCutout(frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".opaque"); l->drawOpaque(*this, frame); }
  { ProfScope ps("world.fxCopy"); worldFxAfterOpaque(*this, frame); }   // corner shading, then the scene copied for the water
  { ProfScope ps("world.translucent"); worldRenderer.drawTranslucent(frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".translucent"); l->drawTranslucent(*this, frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".overlay"); l->drawOverlay(*this, frame); }
  worldFxEnd();
  glDisable(GL_DEPTH_TEST);
}

void GameSession::drawHud() {
  // the HUD itself (crosshair, hotbar, hearts, toasts, chat, F3 debug) is the UI's render layer (ui/hud.cpp);
  // layers check hudHidden themselves (F1 hides everything but the chat and toasts, like the original)
  for (auto& l : layers) { ProfScope ps(l->name(), ".hud"); l->drawHud(*this); }
}

} // namespace dq
