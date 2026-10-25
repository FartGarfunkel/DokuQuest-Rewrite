// launcher/launcher.h — the DokuQuest launcher (port of launcher/desktop/launcher.html + main.js):
// news (patch notes), your worlds, player name, Play (starts the game as a child process), game folder,
// options (renderer, graphics presets with GPU auto-detect, packs, updates), the dedicated server and the
// Mod Studio. Drawn with Dear ImGui in the original's dark "Doku" look (dirt, grey Minecraft-style buttons).
#pragma once
#include "common/settings.h"
#include "launcher/process.h"
#include "launcher/updater.h"
#include <SDL.h>
#include <imgui.h>

namespace dq {
class ModLoader;

enum class LTab { News, Server, Studio, Options, Count };
const char* tabId(LTab t);       // news | server | studio | options (--tab)

struct GpuInfo {
  std::string vendor, renderer, version;
  int major = 3, minor = 3;
  std::string preset = "medium";                 // suggested graphics preset
  RendererKind kind = RendererKind::Modern;      // suggested renderer
  std::string why;                               // shown under the suggestion
};
GpuInfo suggestForGpu(const std::string& vendor, const std::string& renderer, int major, int minor);

struct WorldEntry { std::string folder, name, mode; i64 lastPlayed = 0; };
std::vector<WorldEntry> listWorlds(const fs::path& dir);   // saves/ or servers/ (newest first)

struct Launcher {
  Settings settings;
  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  int width = 1200, height = 740;
  ImFont *fUi = nullptr, *fSmall = nullptr, *fMid = nullptr, *fBig = nullptr, *fTitle = nullptr, *fMono = nullptr;
  unsigned texDirt = 0, texBtn = 0, texBtnHi = 0, texGo = 0, texGoHi = 0, texIcon = 0;
  LTab tab = LTab::News;
  GpuInfo gpu;
  std::unique_ptr<ModLoader> mods;
  Updater updater;
  double time = 0;
  bool quit = false;
  std::string userDirArg;       // --userdir, passed on to the game

  // the game
  ChildProcess game;
  bool gameRunning = false;
  std::string gameVersion;      // version of the game that is running
  std::string worldPlaying;
  std::vector<WorldEntry> worlds;
  double worldsT = -100;
  void play(const std::string& world = {});
  void pollGame();
  void refreshWorlds(bool force = false);

  // the green bar in the bottom bar (updater.js "notice")
  std::string notice; LTab noticeTab = LTab::Count;
  void setNotice(const std::string& t, LTab open = LTab::Count) { notice = t; noticeTab = open; }

  bool keepOpen() const { return settings.extra.value("launcherKeepOpen", true); }
  void saveSettings() { settings.save(); }
};

// ---- ui.cpp: the Doku look
void applyLauncherStyle();
void makeLauncherTextures(Launcher& L);
void drawDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, const Launcher& L, float alphaDark = 0);
enum class BtnKind { Grey, Go };
bool mcButton(Launcher& L, const char* label, ImVec2 size, BtnKind kind = BtnKind::Grey, bool enabled = true, ImFont* font = nullptr);
void shadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, ImU32 shadow, float off, const char* text, const char* end = nullptr);
void beginDokuPanel(const char* id, ImVec2 size, bool scroll = true, ImGuiWindowFlags extra = 0);
void endDokuPanel();
void sideHeading(Launcher& L, const char* text);          // .side h3 (dim, uppercase)
void dimText(const char* fmt, ...);                       // wrapped dim text
bool tabButton(Launcher& L, const char* label, bool active, ImVec2 size = ImVec2(0, 0));   // Mod Studio nav style
void openPath(const fs::path& p);                        // file manager
void openUrl(const std::string& url);
// colours from launcher.html
constexpr ImU32 C_INK = IM_COL32(242, 237, 228, 255), C_DIM = IM_COL32(185, 176, 162, 255), C_GOLD = IM_COL32(255, 215, 90, 255),
                C_EMBER = IM_COL32(255, 138, 61, 255), C_WHITE = IM_COL32(255, 255, 255, 255), C_OK = IM_COL32(155, 211, 74, 255),
                C_BAD = IM_COL32(255, 107, 94, 255), C_FROST = IM_COL32(143, 227, 255, 255);

// ---- news.cpp
void loadNews(const fs::path& assetsRoot);   // assets/news/*.dqn of the version Play runs
void drawNews(Launcher& L, ImVec2 size);

// ---- home.cpp: the side panel (worlds, tips) and the bottom bar (brand, name, Play)
void drawHomeSide(Launcher& L, ImVec2 size);
void drawBottomBar(Launcher& L, float height);

// ---- options.cpp
void drawOptions(Launcher& L);

// ---- server_tab.cpp: the dedicated server, in-process
void drawServer(Launcher& L);
bool serverRunning();
bool serverStartFromConfig(Launcher& L);   // --start-server (tests)
void serverShutdown();
void serverInitConsole();

// ---- modstudio.cpp (+ modstudio_docs.cpp)
void drawStudio(Launcher& L);
void studioOpenPage(const std::string& name);   // overview | api | editor | mods | data | engine (--tab studio:editor)
bool jsSyntaxCheck(const std::string& code, std::string& err);   // QuickJS compile-only, like the game runs mods

} // namespace dq
