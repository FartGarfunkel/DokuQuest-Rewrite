#include "client/session.h"
#include "client/app.h"
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
#include "client/play.h"
#include "gen/structures_api.h"
#include <imgui.h>

namespace dq {

static std::vector<RenderLayerFactory>& layerFactories() { static std::vector<RenderLayerFactory> f; return f; }
void registerRenderLayer(RenderLayerFactory f) { layerFactories().push_back(std::move(f)); }

GameSession::GameSession(App& a, std::unique_ptr<Level> lv) : app(a), level(std::move(lv)) {
  frame.settings = &app.settings;
  fx = std::make_unique<ClientFx>(*this); level->fx = fx.get();
  worldRenderer.init(app.settings, app.tileTex);
  sky = std::make_unique<SkyRenderer>();
  sky->init();
  spawnPlayer();
  worldRenderer.setWorld(&world());
  registerItemLayers();   // held item + first-person arm, dropped items (render/item_layers)
  registerCharacterLayers();   // players as voxel characters (render/char_layer)
  registerMagicClient();  // spell effects, mana bar, magic keys and screens (render/magic_fx, ui/magic_screens)
  registerContentLayers();   // maps, instruments, fishing, camping, elytra, stable keys (render/content_layers)
  registerCreatureLayers();   // creatures, projectiles, gibs, felled trees, fauna (render/creature_layer)
  registerEndStoryLayers();   // the watcher, pearls, darkness/dread, the story's quest line, egg tags (render/endstory_layer)
  registerVehicleLayers();    // minecarts, boats, the ship, machines, pistons, TNT (render/vehicle_layer)
  for (auto& f : layerFactories()) if (auto l = f(*this)) layers.push_back(l);
  std::sort(layers.begin(), layers.end(), [](auto& x, auto& y) { return x->order() < y->order(); });
  if (app.mods) app.mods->onWorldOpen(*level, app.events, false);
  app.events.emit("worldOpen", json::array({level->info.name}));
  DQ_LOG("world '%s' open (seed %d)", level->info.name.c_str(), level->info.seed);
}

GameSession::~GameSession() {
  if (app.mods) app.mods->onWorldClose();
  level->fx = &level->nullFx;
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
  Screen* s = app.topScreen();
  return s && s->pausesGame();
}

void GameSession::save() {
  if (player) level->savePlayer(player->name, player->save());
  level->save(true);
}

void GameSession::tickLocalPlayer(double dt) {
  clientTickLocalPlayer(*this, dt);   // client/play.cpp -> sim/controller (the rules live in sim/)
}

void GameSession::update(double dt) {
  if (!player) return;
  if (level->net) level->net->update(*level, dt);   // multiplayer (net/): a joined world, or one open to LAN
  bool pz = paused() && !level->allRealms().empty() && !level->net;   // a shared world never pauses
  if (!pz) {
    tickLocalPlayer(dt);
    for (auto& l : layers) l->afterPlayerMove(*this, dt);
    tickAcc += dt;
    int n = 0;
    while (tickAcc >= TICK_DT && n < 4) { level->tick(TICK_DT); tickAcc -= TICK_DT; n++; }
    if (n == 4) tickAcc = 0;
  }
  World& w = world();
  if (worldRenderer.world() != &w) worldRenderer.setWorld(&w);
  std::vector<StreamAnchor> anchors = {{player->pos, app.settings.renderDist, true}};
  for (auto& l : layers) l->streamAnchors(*this, anchors);
  if (level->net) level->net->streamAnchors(player->realm, anchors);   // the host keeps remote players' surroundings loaded
  w.stream(anchors, 3.0);
  // camera
  Camera& cam = frame.cam;
  clientPlayerCamera(*this, cam, dt);   // client/play.cpp: eye height, view bob, shoulder/behind/front views
  int dw, dh; SDL_GL_GetDrawableSize(app.window, &dw, &dh);
  cam.aspect = dh > 0 ? (float)dw / dh : 1.f;
  cam.farZ = std::max(1024.f, (float)app.settings.farTerrain + 512.f);
  cam.update();
  frame.time = app.time; frame.width = dw; frame.height = dh;
  sky->computeFrame(*level, frame, app.settings.renderDist);
  cam.update();
  worldRenderer.update(cam, *app.pool, 4.0);
  for (auto& l : layers) l->update(*this, dt);
  if (app.audio) app.audio->setListener(vec3(cam.pos), cam.forward(), vec3(0, 1, 0));
}

void GameSession::render() {
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
