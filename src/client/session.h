// client/session.h — an open world on this device: the level (single player) or a replica
// (multiplayer), the local player, renderers and the frame/tick loop.
#pragma once
#include "sim/level.h"
#include "sim/player.h"
#include "sim/physics.h"
#include "render/frame.h"
#include "render/world_renderer.h"
#include "input/input.h"
#include "ui/screen.h"

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
  virtual void afterPlayerMove(class GameSession& s, double dt) {}       // right after the local player moved, before the camera
  virtual void streamAnchors(class GameSession& s, std::vector<StreamAnchor>& a) {}   // more places to keep loaded in the player's realm
};
using RenderLayerFactory = std::function<std::shared_ptr<RenderLayer>(class GameSession&)>;
void registerRenderLayer(RenderLayerFactory f);

// ---- split screen (client/split.cpp): up to four people on one device, each with their own player, view, devices and menus.
// The session mirrors the SELECTED local's per-person fields (player, frame, target, viewMode...) so the code written for one
// player keeps working: selectLocal(i) swaps them, enterLocal(i)/leaveLocal() also route input and menus to that person.
struct LocalView { int x = 0, y = 0, w = 1, h = 1; };   // window units (App::width / height), top-left origin
struct LocalPlayer {
  int index = 0;
  Player* player = nullptr;
  FrameCtx frame;
  RayHit target; int viewMode = 0; bool hudHidden = false, debugOverlay = false;
  bool breaking = false; ivec3 breakBlock{0}; float breakProgress = 0;
  LocalView view;
  InputSource src;                                  // keyboard + mouse (the first player) and/or controllers
  std::vector<std::unique_ptr<Screen>> screens;     // this person's own menus (inventory, death...) while the screen is split
  void* ui = nullptr;                               // this viewport's ImGui context
};

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

  // split screen
  std::vector<LocalPlayer> locals;    // locals[0] is the first player (keyboard + mouse); `player` etc. mirror locals[cur]
  int cur = 0;
  bool split() const { return locals.size() > 1; }
  void selectLocal(int i);
  void enterLocal(int i);             // select + route input and menus to that person
  void leaveLocal();                  // back to person 0, everything merged
  int addLocal(SDL_JoystickID pad);   // Start on a free controller: -1 when full or not possible
  void removeLocal(int i);
  void layoutLocals();                // viewport rectangles for 1..4 people
  void routeInput();                  // tell Input who owns which controller
  int renderDist() const;             // the user's render distance, capped with more people (game.js renderDist)
  void checkJoin();                   // a free controller pressed Start
  void splitUi();                     // each person's HUD and menus in their own viewport (own ImGui context)
  int userRenderDist = -1;            // settings.renderDist while the capped value is lent to the layers
  bool entered = false;               // enterLocal() swapped that person's menus into App::screens (the shared ones wait in locals[cur].screens)
  bool uiWantsText = false;           // a split-screen viewport has a text box focused (keys must not act in the world)
  u64 joinedFrame = ~0ull;            // the frame a controller joined on (its Start press must not also pause)
  void lendRenderDist(bool on);       // with 2+ people: settings.renderDist is the capped value while the world updates / draws
  void freeLocalUi(LocalPlayer& lp);
  void drawNameTags();                // the other people's names over their heads, in the current ImGui context

private:
  void updateLocals(double dt);
  void splitRender();
  void tickLocalPlayer(double dt);
  void spawnPlayer();
};

} // namespace dq
