#include "ui/cinematic.h"
#include "render/world_fx.h"
#include <map>
#include <algorithm>
#include <cstdlib>
#include "render/gl.h"
#include "client/session.h"
#include "net/protocol.h"
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
#include "sim/world_spawn.h"
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
  level->frameGlide = true;   // (replicas moved every frame: update)
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
    else if (sp.y < 0) sp = chooseWorldSpawn(*level, &p->yaw);   // a village's edge, the wilds or dry land (sim/world_spawn.h; a server uses it too)
    p->pos = sp;
    freshSpawnT = 0;   // (its ground checked once it's loaded: update)
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
  // a new spawn inside the ground (the start point is chosen from the terrain's plan, before the cliffs, trees and buildings are
  // in): once its chunk is here, up to the first place with two free blocks over something solid
  if (freshSpawnT >= 0 && player) {
    freshSpawnT += dt;
    World& w = world();
    const ivec3 b = ivec3(glm::floor(player->pos));
    if (w.chunk(dimOfY(std::clamp(b.y, 0, WORLD_H - 1)), b.x >> 4, b.z >> 4)) {
      const Registry& R = reg();
      auto freeAt = [&](int y) { const BlockId a = w.getBlock(b.x, y, b.z); return !R.tSolid[a] && !R.tLiquid[a]; };
      if (!freeAt(b.y) || !freeAt(b.y + 1)) {
        const int top = dimBase(dimOfY(b.y)) + 255;
        for (int y = b.y + 1; y < top - 1; y++) if (freeAt(y) && freeAt(y + 1) && R.tSolid[w.getBlock(b.x, y - 1, b.z)]) {
          player->pos.y = (float)y; player->vel = vec3(0); level->info.spawn = player->pos;
          DQ_LOG("spawn: lifted out of the ground to y %d", y);
          break;
        }
      }
      freshSpawnT = -1;
    } else if (freshSpawnT > 30) freshSpawnT = -1;
  }
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
  {   // the others' replicas move every frame, not in the sim's 30-a-second steps (which judder at any other frame rate):
      // in someone else's world they glide to their place at this moment (sim/net_interp.cpp); hosting, a guest's body
      // keeps its newest place for the rules and is only drawn at its view (showReplicas)
    const double now = nowSeconds();
    for (auto& e : level->entities()) {
      if (e->removed || !e->ghost) continue;
      if (level->isRemote()) { e->glide(dt); e->prevPos = e->pos; e->prevYaw = e->yaw; }
      else if (!e->netHist.empty()) {
        e->netUpdateView(now);
        // hosting: a guest's doings put on their drawing copy when their smoothed body gets to that moment
        if (auto* p = e->isPlayer() ? dynamic_cast<Player*>(e.get()) : nullptr; p && p->netView) {
          net::hostViewStep(*p, now, (float)dt);
        }
      }
    }
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
    cinematicCamera(*this, cam, dt);      // ui/cinematic.h: a station scene's camera (the lean over a crafting table)
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
  showReplicas(true);   // (the layers animate the guests as they are drawn)
  if (!split()) for (auto& l : layers) { ProfScope ps(l->name(), ".update"); l->update(*this, dt); }
  else for (int i = 0; i < n; i++) {
    enterLocal(i);
    for (auto& l : layers) if (i == 0) l->update(*this, dt); else if (perViewerLayer(l->name())) l->update(*this, 0.0);
    leaveLocal();
  }
  showReplicas(false);
  if (app.audio) app.audio->setListener(vec3(frame.cam.pos), frame.cam.forward(), vec3(0, 1, 0));
}

void GameSession::showReplicas(bool on) {
  if (on) {
    shownAt.clear(); shownGear.clear(); shownViews.clear();
    if (level->isRemote()) return;   // (in someone else's world the replicas are already where they're drawn)
    for (auto& e : level->entities()) {
      if (e->removed || !e->ghost) continue;
      auto* p = e->isPlayer() ? dynamic_cast<Player*>(e.get()) : nullptr;
      // a guest with a drawing copy (their doings at the moment their smoothed body is at): the copy drawn in their place,
      // where the smooth view has them (riding: where the mount, which moves here, has them), then put back
      if (p && p->netView) {
        std::shared_ptr<Player> sp = p->netView;
        Player& v = *sp;
        const bool smooth = e->hasView && !e->riding;
        v.pos = v.prevPos = smooth ? e->viewPos : e->pos; v.vel = e->vel;
        v.yaw = v.prevYaw = smooth ? e->viewYaw : e->yaw; v.pitch = smooth ? e->viewPitch : e->pitch; v.bodyYaw = smooth ? e->viewBody : e->bodyYaw;
        v.hasView = false;
        std::swap(*p, v);
        shownViews.push_back({p, sp});
        continue;
      }
      // (riding: on the mount, which moves here, so where the mount has them)
      if (e->hasView && !e->riding) {
        shownAt.push_back({e.get(), {e->pos.x, e->pos.y, e->pos.z, e->yaw, e->pitch, e->bodyYaw}});
        e->pos = e->viewPos; e->yaw = e->viewYaw; e->pitch = e->viewPitch; e->bodyYaw = e->viewBody;
      }
      // a guest's hands and armour as their game has them now (their bags here change only when their game saves them)
      if (!p || !p->netHands || p->inv.empty()) continue;
      Gear g; g.p = p; g.sel = clampv(p->selected, 0, (int)p->inv.size() - 1);
      g.held = p->inv[g.sel]; g.off = p->offhand.empty() ? ItemStack() : p->offhand[0]; g.armor = p->armor;
      if (g.held.id != p->netHeld) p->inv[g.sel] = p->netHeld.empty() ? ItemStack() : ItemStack(p->netHeld, 1);
      if (!p->offhand.empty() && p->offhand[0].id != p->netOffItem) p->offhand[0] = p->netOffItem.empty() ? ItemStack() : ItemStack(p->netOffItem, 1);
      if (p->netArmor.is_array())
        for (size_t i = 0; i < p->armor.size() && i < p->netArmor.size(); i++) {
          const std::string id = p->netArmor[i].is_string() ? p->netArmor[i].get<std::string>() : std::string();
          if (p->armor[i].id != id) p->armor[i] = id.empty() ? ItemStack() : ItemStack(id, 1);
        }
      shownGear.push_back(std::move(g));
    }
  } else {
    for (auto& [p, v] : shownViews) std::swap(*p, *v);   // (the body back; the copy keeps what drawing did to it: its swing played on)
    for (auto& [e, a] : shownAt) { e->pos = vec3(a[0], a[1], a[2]); e->yaw = a[3]; e->pitch = a[4]; e->bodyYaw = a[5]; }
    for (auto& g : shownGear) { g.p->inv[g.sel] = g.held; if (!g.p->offhand.empty()) g.p->offhand[0] = g.off; g.p->armor = g.armor; }
    shownAt.clear(); shownGear.clear(); shownViews.clear();
  }
}

void GameSession::render() {
  if (split()) { prof().frame(); splitRender(); return; }
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
  prof().frame();
  { ProfScope ps("world.fx"); worldFxBegin(*this, frame); }   // the built-in shaders' sun shadow map (render/world_fx.cpp)
  // the sky after the land (only where it left the picture empty), unless a shader pack's passes expect it first
  const bool skyLast = app.settings.shaderPack.empty();
  if (!skyLast) { ProfScope ps("sky"); sky->draw(frame); }
  { ProfScope ps("world.opaque"); worldRenderer.drawOpaque(frame); }
  { ProfScope ps("world.cutout"); worldRenderer.drawCutout(frame); }
  if (skyLast) { ProfScope ps("sky"); sky->draw(frame, true); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".opaque"); l->drawOpaque(*this, frame); }
  { ProfScope ps("world.fxCopy"); worldFxAfterOpaque(*this, frame); }   // corner shading, then the scene copied for the water
  { ProfScope ps("world.translucent"); worldRenderer.drawTranslucent(frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".translucent"); l->drawTranslucent(*this, frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".overlay"); l->drawOverlay(*this, frame); }
  worldFxEnd();
  glDisable(GL_DEPTH_TEST);
}

// one person's view in split screen (client/split.cpp splitRender: its viewport and scissor are set): no world fx there
void GameSession::drawSplitView() {
  // the sky after the land (only where it left the picture empty), unless a shader pack's passes expect it first
  const bool skyLast = app.settings.shaderPack.empty();
  if (!skyLast) { ProfScope ps("sky"); sky->draw(frame); }
  { ProfScope ps("world.opaque"); worldRenderer.drawOpaque(frame); }
  { ProfScope ps("world.cutout"); worldRenderer.drawCutout(frame); }
  if (skyLast) { ProfScope ps("sky"); sky->draw(frame, true); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".opaque"); l->drawOpaque(*this, frame); }
  { ProfScope ps("world.translucent"); worldRenderer.drawTranslucent(frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".translucent"); l->drawTranslucent(*this, frame); }
  for (auto& l : layers) { ProfScope ps(l->name(), ".overlay"); l->drawOverlay(*this, frame); }
}

void GameSession::drawHud() {
  // the HUD itself (crosshair, hotbar, hearts, toasts, chat, F3 debug) is the UI's render layer (ui/hud.cpp);
  // layers check hudHidden themselves (F1 hides everything but the chat and toasts, like the original)
  for (auto& l : layers) { ProfScope ps(l->name(), ".hud"); l->drawHud(*this); }
}

} // namespace dq
