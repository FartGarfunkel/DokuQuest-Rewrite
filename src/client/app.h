// client/app.h — the game program: window + GL context (renderer chosen in the launcher),
// UI screen stack, the open world session, audio, input. One App per process (app()).
#pragma once
#include "common/settings.h"
#include "common/threadpool.h"
#include "input/input.h"
#include "sim/events.h"
#include "sim/level.h"
#include "assets/tileset.h"
#include "render/frame.h"
#include <SDL.h>

struct ImFont;

namespace dq {

class Screen;
class GameSession;
class AudioEngine;
class ModLoader;

struct LaunchArgs {
  RendererKind renderer = RendererKind::Modern;
  bool rendererSet = false;
  std::string world;         // open this world folder directly
  std::string createWorld;   // create a world with this name
  std::string seed;
  std::string userDir;
  std::string server;        // host:port to join
  std::string smokeShot;     // smoke test: screenshot path
  int smokeFrames = 0;       // smoke test: frames to run before exiting
  json smokeScript;          // smoke test: scripted actions
  bool fromLauncher = false;
};

class App {
public:
  App();
  ~App();
  int run(int argc, char** argv);

  Settings settings;
  LaunchArgs args;
  Input input;
  EventBus events;
  std::unique_ptr<ThreadPool> pool;
  std::unique_ptr<AudioEngine> audio;
  std::unique_ptr<ModLoader> mods;
  SDL_Window* window = nullptr;
  SDL_GLContext glctx = nullptr;
  int width = 1280, height = 720;
  double time = 0, dt = 0.016;
  u64 frame = 0;
  bool quit = false;

  // tiles (micro-voxel colours) shared by every renderer
  TileSet tiles;
  TileTextures tileTex;
  void reloadTiles();       // after a resource pack change

  // fonts (Pixelify Sans for UI text, Jersey 15 for titles) at a few sizes
  ImFont* fontUi = nullptr; ImFont* fontUiBig = nullptr; ImFont* fontTitle = nullptr; ImFont* fontSmall = nullptr;
  float uiScale = 1;

  // screens
  std::vector<std::unique_ptr<Screen>> screens;
  void pushScreen(std::unique_ptr<Screen> s);
  void popScreen();
  void setScreen(std::unique_ptr<Screen> s);   // replace the whole stack
  Screen* topScreen() const { return screens.empty() ? nullptr : screens.back().get(); }
  template <class T> T* findScreen() { for (auto& s : screens) if (auto p = dynamic_cast<T*>(s.get())) return p; return nullptr; }

  // world session
  std::unique_ptr<GameSession> session;
  bool openWorld(const fs::path& folder);
  bool createWorld(LevelInfo info);
  bool joinServer(const std::string& address);
  void leaveWorld(bool toTitle = true);
  fs::path savesDir() const { return userSub("saves"); }

  void toast(const std::string& text, const std::string& kind = "");
  void takeScreenshot(const fs::path& path = {});
  void applySettings();      // after the options screen changes things
private:
  bool initWindow();
  void initImGui();
  void frameLoop();
  void parseArgs(int argc, char** argv);
  bool screenshotPending = false; fs::path screenshotPath;
};

App& app();

} // namespace dq
