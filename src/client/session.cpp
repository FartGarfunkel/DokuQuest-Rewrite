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
#include "gen/structures_api.h"
#include <imgui.h>

namespace dq {

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
    else if (sp.y < 0 && level->gen && villageStartPoint(*level->gen, sp, yaw)) { level->info.spawn = sp; p->yaw = yaw; }   // villages.js: a new world starts at a village's edge
    if (sp.y < 0) {
      DimGenerator* g = level->gen->forDim(DIM_OVERWORLD);
      int sx = 0, sz = 0, h = g ? g->spawnHeight(0, 0) : 100;
      // look for dry land near the origin, like the original
      for (int r = 0; r < 400 && g; r += 16) {
        ColumnSample s; g->sample(r, r / 2, s);
        if (s.height > 0 && s.water < 0) { sx = r; sz = r / 2; h = s.height + 1; break; }
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
    while (tickAcc >= TICK_DT && ticks < 4) { level->tick(TICK_DT); tickAcc -= TICK_DT; ticks++; }
    if (ticks == 4) tickAcc = 0;
  }
  World& w = world();
  if (worldRenderer.world() != &w) { worldRenderer.setWorld(&w); worldRenderer.remeshAll(); }   // a new realm: mesh what is already loaded there
  std::vector<StreamAnchor> anchors = {{player->pos, app.settings.renderDist, true}};
  for (int i = 1; i < n; i++) if (locals[i].player && locals[i].player->realm == player->realm) anchors.push_back({locals[i].player->pos, app.settings.renderDist, true});
  for (auto& l : layers) l->streamAnchors(*this, anchors);
  if (level->net) level->net->streamAnchors(player->realm, anchors);   // the host keeps remote players' surroundings loaded
  w.stream(anchors, 3.0);
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
    sky->computeFrame(*level, frame, app.settings.renderDist);
    cam.update();
    worldRenderer.update(cam, *app.pool, 4.0 / n);
  }
  selectLocal(0);
  if (!split()) for (auto& l : layers) l->update(*this, dt);
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
  sky->draw(frame);
  worldRenderer.drawOpaque(frame);
  worldRenderer.drawCutout(frame);
  for (auto& l : layers) l->drawOpaque(*this, frame);
  worldRenderer.drawTranslucent(frame);
  for (auto& l : layers) l->drawTranslucent(*this, frame);
  for (auto& l : layers) l->drawOverlay(*this, frame);
  glDisable(GL_DEPTH_TEST);
}

void GameSession::drawHud() {
  // the HUD itself (crosshair, hotbar, hearts, toasts, chat, F3 debug) is the UI's render layer (ui/hud.cpp);
  // layers check hudHidden themselves (F1 hides everything but the chat and toasts, like the original)
  for (auto& l : layers) l->drawHud(*this);
}

} // namespace dq
