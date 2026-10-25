#include "client/session.h"
#include "client/app.h"
#include "render/sky.h"
#include "ui/screen.h"
#include "audio/audio.h"
#include "mod/modloader.h"
#include "render/item_layers.h"
#include "client/play.h"
#include <imgui.h>

namespace dq {

static std::vector<RenderLayerFactory>& layerFactories() { static std::vector<RenderLayerFactory> f; return f; }
void registerRenderLayer(RenderLayerFactory f) { layerFactories().push_back(std::move(f)); }

GameSession::GameSession(App& a, std::unique_ptr<Level> lv) : app(a), level(std::move(lv)) {
  frame.settings = &app.settings;
  worldRenderer.init(app.settings, app.tileTex);
  sky = std::make_unique<SkyRenderer>();
  sky->init();
  spawnPlayer();
  worldRenderer.setWorld(&world());
  registerItemLayers();   // held item + first-person arm, dropped items (render/item_layers)
  for (auto& f : layerFactories()) if (auto l = f(*this)) layers.push_back(l);
  std::sort(layers.begin(), layers.end(), [](auto& x, auto& y) { return x->order() < y->order(); });
  if (app.mods) app.mods->onWorldOpen(*level, app.events, false);
  app.events.emit("worldOpen", json::array({level->info.name}));
  DQ_LOG("world '%s' open (seed %d)", level->info.name.c_str(), level->info.seed);
}

GameSession::~GameSession() {
  if (app.mods) app.mods->onWorldClose();
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
  if (hudHidden) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(app.width * 0.5f, app.height * 0.5f);
  if (!app.topScreen()) {
    dl->AddLine(ImVec2(c.x - 9, c.y), ImVec2(c.x + 10, c.y), IM_COL32(255, 255, 255, 200), 2);
    dl->AddLine(ImVec2(c.x, c.y - 9), ImVec2(c.x, c.y + 10), IM_COL32(255, 255, 255, 200), 2);
  }
  for (auto& l : layers) l->drawHud(*this);
  if (debugOverlay || app.settings.showFps) {
    std::string s = strFormat("DokuQuest %s  %.0f fps  (%s GL %d.%d)\n", DQ_VERSION, 1.0 / std::max(1e-3, app.dt), gfx.modern ? "Modern" : "Legacy", gfx.major, gfx.minor);
    if (debugOverlay) {
      vec3 p = player->pos; int d = dimOfY((int)p.y);
      s += strFormat("XYZ %.2f / %.2f / %.2f  (%s, y %d in band)\n", p.x, p.y, p.z, dimName(d), (int)p.y - dimBase(d));
      s += strFormat("chunks %zu  gen %d  sections %d/%d  mesh jobs %d\n", world().chunks().size(), world().pendingGeneration(), worldRenderer.drawnSections, worldRenderer.totalSections, worldRenderer.pendingJobs);
      s += strFormat("day %lld  time %.0f  %s\n", (long long)level->info.day, level->info.time, target.hit ? reg().block(target.id).key.c_str() : "");
      s += gfx.renderer;
    }
    dl->AddText(ImVec2(8, 8), IM_COL32(255, 255, 255, 230), s.c_str());
  }
}

} // namespace dq
