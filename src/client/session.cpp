#include "client/session.h"
#include "client/app.h"
#include "render/sky.h"
#include "ui/screen.h"
#include "audio/audio.h"
#include "client/client_fx.h"
#include "mod/modloader.h"
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "ui/magic_screens.h"
#include "render/content_layers.h"
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
  Player& p = *player;
  Input& in = app.input;
  World& w = world();
  bool menu = app.topScreen() != nullptr;
  vec2 look = menu ? vec2(0) : in.lookDelta();
  p.yaw -= look.x; p.pitch = clampv(p.pitch - look.y, -1.55f, 1.55f);
  vec2 mv = menu ? vec2(0) : in.moveAxis();
  bool creative = p.creative();
  if (creative && in.pressed(Act::Jump)) {
    static double lastJump = -1;
    if (app.time - lastJump < 0.3) p.flying = !p.flying;
    lastJump = app.time;
  }
  p.sneaking = !menu && in.down(Act::Sneak);
  if (!menu && in.down(Act::Sprint) && mv.y > 0) p.sprinting = true;
  if (mv.y <= 0) p.sprinting = false;
  float speed = p.flying ? (p.sprinting ? 21.6f : 10.8f) : p.sneaking ? 1.3f : p.sprinting ? 5.6f : 4.3f;
  vec3 fwd(-std::sin(p.yaw), 0, -std::cos(p.yaw)), right(std::cos(p.yaw), 0, -std::sin(p.yaw));
  vec3 wish = (fwd * mv.y + right * mv.x) * speed;
  BlockId feet = w.getBlock(ifloor(p.pos.x), ifloor(p.pos.y + 0.1f), ifloor(p.pos.z));
  p.inWater = reg().tLiquid[feet] == 1; p.inLava = reg().tLiquid[feet] == 2;
  if (p.flying) {
    p.vel = wish;
    if (!menu && in.down(Act::Jump)) p.vel.y = speed; else if (!menu && in.down(Act::Sneak)) p.vel.y = -speed; else p.vel.y = 0;
  } else {
    float accel = p.onGround ? 14.f : 3.f;
    p.vel.x += (wish.x - p.vel.x) * std::min(1.f, accel * (float)dt);
    p.vel.z += (wish.z - p.vel.z) * std::min(1.f, accel * (float)dt);
    if (p.inWater || p.inLava) {
      p.vel *= std::pow(0.2f, (float)dt);
      p.vel.y -= 4.f * (float)dt;
      if (!menu && in.down(Act::Jump)) p.vel.y = std::min(p.vel.y + 18.f * (float)dt, 3.5f);
    } else {
      p.vel.y -= 32.f * (float)dt;
      p.vel.y = std::max(p.vel.y, -78.4f);
      if (!menu && in.down(Act::Jump) && p.onGround) p.vel.y = 9.0f;
    }
  }
  vec3 before = p.pos;
  MoveResult r = moveBox(w, p.pos, p.w, p.h, p.vel * (float)dt, p.onGround && !p.flying ? 0.6f : 0.f);
  if (r.hitX) p.vel.x = 0;
  if (r.hitZ) p.vel.z = 0;
  if (r.hitY) { if (p.vel.y < 0 && !p.flying && p.fallDist > 3 && !creative) p.hp -= p.fallDist - 3; p.vel.y = 0; p.fallDist = 0; }
  else if (p.vel.y < 0) p.fallDist += before.y - p.pos.y;
  p.onGround = r.onGround;
  if (p.onGround && p.flying && creative) p.flying = false;
  // never fall out of the world below the End
  if (p.pos.y < -64) { p.pos = level->info.spawn; p.vel = vec3(0); }

  // hotbar
  if (!menu) {
    for (int i = 0; i < 9; i++) if (in.pressed((Act)((int)Act::Hot1 + i))) p.selected = i;
    if (in.wheel() != 0) p.selected = floormod(p.selected - (int)in.wheel(), 9);
    if (in.pressed(Act::HotNext)) p.selected = (p.selected + 1) % 9;
    if (in.pressed(Act::HotPrev)) p.selected = (p.selected + 8) % 9;
    if (in.pressed(Act::Debug)) debugOverlay = !debugOverlay;
    if (in.pressed(Act::HideHud)) hudHidden = !hudHidden;
    if (in.pressed(Act::View)) viewMode = (viewMode + 1) % 3;
    if (in.pressed(Act::Screenshot)) app.takeScreenshot();
  }

  // crosshair target, mining and placing (the full rules live in sim/ gameplay systems)
  glm::dvec3 eye = glm::dvec3(p.pos) + glm::dvec3(0, 1.62, 0);
  vec3 dir(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  target = raycastBlocks(w, eye, dir, creative ? 6.f : 5.f);
  if (menu) { breaking = false; return; }
  if (target.hit && in.down(Act::Attack)) {
    if (!breaking || target.block != breakBlock) { breaking = true; breakBlock = target.block; breakProgress = 0; }
    const BlockDef& b = reg().block(target.id);
    float hard = b.hardness < 0 ? 1e9f : b.hardness;
    float t = creative ? 0 : std::max(0.05f, hard * 1.5f);
    breakProgress += (float)dt;
    if (breakProgress >= t && (creative ? in.pressed(Act::Attack) || breakProgress > 0.25f : true)) {
      ivec3 bp = target.block;
      json args = json::array({p.name, bp.x, bp.y, bp.z, b.key});
      if (app.events.emit("blockBreak", args)) {
        w.setBlock(bp.x, bp.y, bp.z, AIR, 0, SetFlags::Urgent);
        if (!creative && !b.drop.empty()) p.give(ItemStack(b.drop, 1));
        if (app.audio) app.audio->materialAt(b.sound, "dig", vec3(bp) + vec3(0.5f));
      }
      breaking = false; breakProgress = 0;
    }
  } else breaking = false;
  if (target.hit && in.pressed(Act::Use)) {
    ItemStack& h = p.held();
    const ItemDef* it = h.def();
    if (it && !it->block.empty()) {
      ivec3 np = target.block + target.normal;
      BlockId id = reg().blockId(it->block);
      vec3 lo = vec3(np), hi = lo + vec3(1);
      AABB pb{p.pos - vec3(p.w, 0, p.w), p.pos + vec3(p.w, p.h, p.w)};
      BlockId there = w.getBlock(np.x, np.y, np.z);
      if (id && (!reg().tSolid[id] || !pb.overlaps({lo, hi})) && (there == AIR || reg().tReplaceable[there])) {
        if (app.events.emit("blockPlace", json::array({p.name, np.x, np.y, np.z, it->block}))) {
          w.setBlock(np.x, np.y, np.z, id, 0, SetFlags::Urgent);
          if (!creative && --h.n <= 0) h.clear();
          if (app.audio) app.audio->materialAt(reg().block(id).sound, "place", vec3(np) + vec3(0.5f));
        }
      }
    }
  }
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
  cam.pos = glm::dvec3(player->pos) + glm::dvec3(0, player->sneaking ? 1.5 : 1.62, 0);
  cam.yaw = player->yaw; cam.pitch = player->pitch;
  if (viewMode) {
    vec3 f = vec3(-std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), -std::cos(cam.yaw) * std::cos(cam.pitch));
    float back = viewMode == 1 ? -4.f : 4.f;
    RayHit h = raycastBlocks(w, cam.pos, glm::normalize(f * back), 4.f);
    float d = h.hit ? std::max(0.3f, h.dist - 0.3f) : 4.f;
    cam.pos += glm::dvec3(glm::normalize(f * back) * d);
    if (viewMode == 2) { cam.yaw += PI; cam.pitch = -cam.pitch; }
  }
  cam.fov = app.settings.fov;
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
