// launcher/launcher_main.cpp — the DokuQuest Launcher program: window + GL context (its GL_RENDERER feeds the
// graphics suggestion), fonts like the game, the frame loop with the tabs (News, Server, Mod Studio, Options)
// above launcher.html's bottom bar, and the hand-off to a newer launcher the updater installed.
// Smoke options like the game: --userdir DIR --smoke-frames N --smoke-shot FILE --tab news|server|studio[:page]|options
// Test options: --update-api URL (a saved GitHub response, e.g. file:///…/latest.json) --check-updates --start-server --play --no-handoff
#include "launcher/launcher.h"
#include "core/init.h"
#include "mod/modloader.h"
#include "render/gl.h"
#include "assets/tileset.h"
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL_main.h>
#include <thread>
#include <cstring>

namespace dq {
namespace {

struct Args {
  std::string userDir, smokeShot, tab, updateApi;
  int smokeFrames = 0;
  bool checkUpdates = false, startServer = false, noHandoff = false, play = false;
};

Args parseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (s == "--userdir") a.userDir = next();
    else if (s == "--smoke-frames") a.smokeFrames = std::atoi(next().c_str());
    else if (s == "--smoke-shot") a.smokeShot = next();
    else if (s == "--tab") a.tab = next();
    else if (s == "--update-api") a.updateApi = next();
    else if (s == "--check-updates") a.checkUpdates = true;
    else if (s == "--start-server") a.startServer = true;
    else if (s == "--play") a.play = true;
    else if (s == "--no-handoff") a.noHandoff = true;
    else if (s == "--help") { std::printf("dokuquest-launcher [--userdir DIR] [--tab news|server|studio[:page]|options] [--smoke-frames N --smoke-shot FILE]\n"); std::exit(0); }
  }
  return a;
}

// A newer launcher installed by the updater takes over; if it dies at once, this one carries on.
bool handOff(Launcher& L, const Args& a, int argc, char** argv, int& exitCode) {
  std::string ver;
  fs::path exe = L.updater.executable("dokuquest-launcher", &ver);
  if (ver == DQ_VERSION || !fs::exists(exe)) return false;
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(argv[i]);
  args.push_back("--no-handoff");
  ChildProcess p; ProcOptions o; o.cwd = exe.parent_path();
  DQ_LOG("launcher: handing off to %s (%s)", exe.string().c_str(), ver.c_str());
  if (!p.start(exe, args, o)) { L.updater.markBad(ver, "launcher did not start"); return false; }
  for (int i = 0; i < 40; i++) {
    if (!p.running()) {
      if (p.exitCode() == 0) { exitCode = 0; return true; }
      L.updater.markBad(ver, strFormat("launcher exited with code %d", p.exitCode()));
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  exitCode = 0;
  return true;   // it is up and running; this process steps aside (the child keeps running)
}

bool initWindow(Launcher& L) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) { DQ_ERR("SDL: %s", SDL_GetError()); return false; }
  if (auto db = readFile(assetsDir() / "gamecontrollerdb.txt")) SDL_GameControllerAddMappingsFromRW(SDL_RWFromConstMem(db->data(), (int)db->size()), 1);
  SDL_Rect r{0, 0, 1280, 800};
  SDL_GetDisplayUsableBounds(0, &r);
  L.width = std::min(1200, r.w - 40); L.height = std::min(760, r.h - 40);
  // the launcher only draws its UI, but it asks for the newest GL the driver has, so its GL_RENDERER/version
  // tell the options what this computer can run (same order of tries as the game)
  struct Try { int maj, min; } tries[] = {{4, 6}, {4, 5}, {4, 3}, {3, 3}};
  for (auto& t : tries) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, t.maj);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, t.min);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (!L.window) L.window = SDL_CreateWindow("DokuQuest Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, L.width, L.height,
                                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!L.window) { DQ_ERR("window: %s", SDL_GetError()); return false; }
    L.gl = SDL_GL_CreateContext(L.window);
    if (L.gl) { if (gfxInit(RendererKind::Legacy)) break; SDL_GL_DeleteContext(L.gl); L.gl = nullptr; }
  }
  if (!L.gl) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DokuQuest", "Your graphics driver doesn't support OpenGL 3.3, which DokuQuest needs.\nUpdating the graphics driver usually fixes this.", L.window);
    return false;
  }
  SDL_SetWindowMinimumSize(L.window, 640, 400);
  SDL_GL_SetSwapInterval(1);
  SDL_GetWindowSize(L.window, &L.width, &L.height);
  if (auto icon = readBinary(assetsDir() / "icon.png")) {
    int w, h; std::vector<u8> px;
    if (loadPngMem(icon->data(), icon->size(), w, h, px)) {
      SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
      SDL_SetWindowIcon(L.window, s); SDL_FreeSurface(s);
    }
  }
  L.gpu = suggestForGpu(gfx.vendor, gfx.renderer, gfx.major, gfx.minor);
  DQ_LOG("launcher: graphics %s / %s (GL %d.%d) → suggest %s, %s renderer", gfx.vendor.c_str(), gfx.renderer.c_str(), gfx.major, gfx.minor,
         L.gpu.preset.c_str(), rendererName(L.gpu.kind));
  return true;
}

void initImGui(Launcher& L) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ImGui_ImplSDL2_InitForOpenGL(L.window, L.gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");
  float dpi = 1; int dw, dh; SDL_GL_GetDrawableSize(L.window, &dw, &dh); if (L.width > 0) dpi = (float)dw / L.width;
  ImFontConfig cfg; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
  std::string ui = (assetsDir() / "fonts" / "PixelifySans.ttf").string(), title = (assetsDir() / "fonts" / "Jersey15.ttf").string();
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
  auto load = [&](const std::string& f, float px) { return fs::exists(f) ? io.Fonts->AddFontFromFileTTF(f.c_str(), px * dpi, &cfg, ranges) : io.Fonts->AddFontDefault(); };
  L.fUi = load(ui, 16); L.fSmall = load(ui, 13); L.fMid = load(ui, 20); L.fBig = load(ui, 26); L.fTitle = load(title, 34);
  ImFontConfig mono; mono.SizePixels = 13 * dpi;
  L.fMono = io.Fonts->AddFontDefault(&mono);
  io.FontGlobalScale = 1.f / dpi;
  applyLauncherStyle();
}

void screenshot(Launcher& L, const fs::path& path) {
  int dw, dh; SDL_GL_GetDrawableSize(L.window, &dw, &dh);
  std::vector<u8> px((size_t)dw * dh * 4), flip(px.size());
  glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (int y = 0; y < dh; y++) std::memcpy(&flip[(size_t)y * dw * 4], &px[(size_t)(dh - 1 - y) * dw * 4], (size_t)dw * 4);
  for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
  if (savePng(path, dw, dh, flip.data())) DQ_LOG("launcher: screenshot %s", path.string().c_str());
}

void drawFrame(Launcher& L) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos); ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(2);
  float W = (float)L.width, H = (float)L.height, barH = 112;
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  drawDirt(bg, ImVec2(0, 0), ImVec2(W, H - barH), L);
  bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H - barH), IM_COL32(0, 0, 0, 89), IM_COL32(0, 0, 0, 89), IM_COL32(0, 0, 0, 140), IM_COL32(0, 0, 0, 140));
  // tab row
  const float pad = 18;
  ImGui::SetCursorPos(ImVec2(pad, 14));
  const char* names[] = {"News", "Server", "Mod Studio", "Options"};
  for (int i = 0; i < (int)LTab::Count; i++) {
    if (i) ImGui::SameLine(0, 6);
    if (tabButton(L, names[i], (int)L.tab == i)) L.tab = (LTab)i;
  }
  // right of the tabs: what's going on
  std::string status;
  if (L.updater.busy()) status = L.updater.message + (L.updater.state == Updater::State::Downloading ? strFormat(" %d%%", (int)(L.updater.progress * 100)) : "");
  else if (serverRunning()) status = "Server running";
  if (!status.empty()) {
    ImVec2 ts = L.fSmall->CalcTextSizeA(L.fSmall->FontSize, FLT_MAX, 0, status.c_str());
    ImGui::GetWindowDrawList()->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(W - pad - ts.x, 14 + (34 - ts.y) * 0.5f), C_DIM, status.c_str());
  }
  float top = 14 + 34 + 10, contentH = H - barH - top - 14;
  ImGui::SetCursorPos(ImVec2(pad, top));
  ImGui::BeginChild("##content", ImVec2(W - pad * 2, contentH), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoScrollbar);
  switch (L.tab) {
    case LTab::News: {
      bool side = W >= 760;
      float sideW = 260, newsW = side ? W - pad * 2 - sideW - 16 : W - pad * 2;
      drawNews(L, ImVec2(newsW, contentH));
      if (side) { ImGui::SameLine(0, 16); drawHomeSide(L, ImVec2(sideW, contentH)); }
      break;
    }
    case LTab::Server: drawServer(L); break;
    case LTab::Studio: drawStudio(L); break;
    case LTab::Options: drawOptions(L); break;
    default: break;
  }
  ImGui::EndChild();
  drawBottomBar(L, barH);
  // controllers: LB/RB switch tabs, B goes back to the news
  bool typing = ImGui::GetIO().WantTextInput;
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) L.tab = (LTab)(((int)L.tab + (int)LTab::Count - 1) % (int)LTab::Count);
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) L.tab = (LTab)(((int)L.tab + 1) % (int)LTab::Count);
  if (!typing && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) && L.tab != LTab::News) L.tab = LTab::News;
  ImGui::End();
}

} // namespace
} // namespace dq

int main(int argc, char** argv) {
  using namespace dq;
  installCrashHandler();
  Args a = parseArgs(argc, argv);
  CoreOptions co; co.userDirOverride = a.userDir;
  if (!initCore(co)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DokuQuest", "The game's data folder is missing or damaged. Reinstall DokuQuest.", nullptr);
    return 1;
  }
  logSetFile((userDir() / "launcher.log").string());
  static Launcher L;
  L.userDirArg = a.userDir;
  L.settings = Settings::load();
  if (L.settings.extra.contains("updateSource") && L.settings.extra["updateSource"].is_string() && validRepoSource(L.settings.extra["updateSource"])) L.updater.source = L.settings.extra["updateSource"];
  L.updater.apiOverride = a.updateApi;
  if (!a.noHandoff && a.smokeFrames == 0) { int code = 0; if (handOff(L, a, argc, argv, code)) return code; }
  L.mods = std::make_unique<ModLoader>();
  L.mods->scan();
  L.mods->loadData();   // data packs, so server worlds have the mods' blocks and items
  if (!initWindow(L)) return 2;
  if (L.settings.preset == "auto") {   // first start: take the suggestion for this computer
    L.settings.applyPreset(L.gpu.preset); L.settings.renderer = L.gpu.kind; L.saveSettings();
  }
  initImGui(L);
  makeLauncherTextures(L);
  serverInitConsole();
  loadNews(L.updater.executable("dokuquest", nullptr).parent_path() / "assets");
  if (!a.tab.empty()) {
    std::string t = a.tab, page; size_t c = t.find(':'); if (c != std::string::npos) { page = t.substr(c + 1); t = t.substr(0, c); }
    for (int i = 0; i < (int)LTab::Count; i++) if (t == tabId((LTab)i)) L.tab = (LTab)i;
    if (!page.empty()) studioOpenPage(page);
  }
  if (a.checkUpdates || (L.settings.autoUpdate && a.smokeFrames == 0)) L.updater.check(!a.checkUpdates);
  bool serverAutostarted = false;
  int frame = 0; double last = nowSeconds();
  std::string lastInstalled;
  while (!L.quit) {
    // while the game runs in front, the launcher idles (it still watches the game, the server and downloads)
    bool idle = L.gameRunning && !(SDL_GetWindowFlags(L.window) & SDL_WINDOW_INPUT_FOCUS) && a.smokeFrames == 0;
    if (idle) SDL_WaitEventTimeout(nullptr, 100);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT) L.quit = true;
      if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) SDL_GetWindowSize(L.window, &L.width, &L.height);
    }
    double now = nowSeconds(); L.time += now - last; last = now;
    L.pollGame();
    L.updater.poll();
    if (L.updater.installed != lastInstalled) {
      lastInstalled = L.updater.installed;
      if (!lastInstalled.empty()) { L.setNotice("Updated to " + niceVersion(lastInstalled) + " · press Play"); loadNews(L.updater.executable("dokuquest", nullptr).parent_path() / "assets"); }
    }
    if (a.startServer && !serverAutostarted) { serverAutostarted = true; serverStartFromConfig(L); }
    if (a.play && frame == 1) L.play();
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
    ImGui::PushFont(L.fUi);
    drawFrame(L);
    ImGui::PopFont();
    ImGui::Render();
    int dw, dh; SDL_GL_GetDrawableSize(L.window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.165f, 0.118f, 0.082f, 1); glClear(GL_COLOR_BUFFER_BIT);   // body background #2a1e15
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    frame++;
    if (a.smokeFrames > 0 && frame == a.smokeFrames && !a.smokeShot.empty()) screenshot(L, a.smokeShot);
    SDL_GL_SwapWindow(L.window);
    if (a.smokeFrames > 0 && frame >= a.smokeFrames) L.quit = true;
  }
  serverShutdown();
  if (!L.gameRunning) {   // options are saved as they change; keep a typed name (a running game owns settings.json)
    Settings disk = Settings::load();
    if (disk.playerName != L.settings.playerName) { disk.playerName = L.settings.playerName; disk.save(); }
  }
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
  SDL_GL_DeleteContext(L.gl); SDL_DestroyWindow(L.window); SDL_Quit();
  return 0;
}
