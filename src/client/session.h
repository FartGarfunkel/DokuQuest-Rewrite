// client/session.h — an open world on this device: the level (single player) or a replica
// (multiplayer), the local player, renderers and the frame/tick loop.
#pragma once
#include "sim/level.h"
#include "sim/player.h"
#include "sim/physics.h"
#include "render/frame.h"
#include "render/world_renderer.h"

namespace dq {

class App;
class SkyRenderer;

// Systems that draw things in the world (entities, particles, LOD, portals, held item, clouds...)
// register with the session; each is called in the matching pass.
class RenderLayer {
public:
  virtual ~RenderLayer() = default;
  virtual const char* name() const = 0;
  virtual int order() const { return 0; }
  virtual void update(class GameSession& s, double dt) {}
  virtual void drawOpaque(class GameSession& s, const FrameCtx& f) {}
  virtual void drawTranslucent(class GameSession& s, const FrameCtx& f) {}
  virtual void drawOverlay(class GameSession& s, const FrameCtx& f) {}   // after the world (hand, screen effects)
  virtual void drawHud(class GameSession& s) {}                          // ImGui HUD
};
using RenderLayerFactory = std::function<std::shared_ptr<RenderLayer>(class GameSession&)>;
void registerRenderLayer(RenderLayerFactory f);

class GameSession {
public:
  GameSession(App& app, std::unique_ptr<Level> lv);
  ~GameSession();
  App& app;
  std::unique_ptr<Level> level;
  Player* player = nullptr;           // the local player entity (owned by the level)
  WorldRenderer worldRenderer;
  std::unique_ptr<SkyRenderer> sky;
  FrameCtx frame;
  std::vector<std::shared_ptr<RenderLayer>> layers;
  std::unique_ptr<LevelFx> fx;        // the level's sounds/particles/messages on this device (client/client_fx.h)
  template <class T> T* layer() { for (auto& l : layers) if (auto p = dynamic_cast<T*>(l.get())) return p; return nullptr; }

  World& world() { return level->world(player ? player->realm : REALM_MAIN); }
  void update(double dt);
  void render();
  void drawHud();
  void save();

  // camera / view
  int viewMode = 0;                   // 0 first person, 1 behind, 2 front (F5)
  bool hudHidden = false, debugOverlay = false;
  RayHit target;                      // block under the crosshair
  double tickAcc = 0;
  float breakProgress = 0; ivec3 breakBlock{0}; bool breaking = false;
  bool paused() const;

private:
  void tickLocalPlayer(double dt);
  void spawnPlayer();
};

} // namespace dq
