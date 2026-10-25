#include "client/app.h"
#include "render/portal_view.h"
#include "client/netplay.h"
#include "render/tile_anim.h"
#include "client/session.h"
#include "core/init.h"
#include "render/env_layers.h"
#include "ui/screen.h"
#include "audio/audio.h"
#include "client/client_fx.h"
#include "mod/modloader.h"
#include "assets/icons.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <ctime>

namespace dq {

static App* g_app = nullptr;
App& app() { return *g_app; }
App::App() = default;
App::~App() = default;

void App::parseArgs(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--renderer") { std::string r = next(); args.rendererSet = true; args.renderer = r == "legacy" ? RendererKind::Legacy : r == "vulkan" ? RendererKind::Vulkan : RendererKind::Modern; }
    else if (a == "--world") args.world = next();
    else if (a == "--create") args.createWorld = next();
    else if (a == "--seed") args.seed = next();
    else if (a == "--world-type") args.worldType = next();
    else if (a == "--userdir") args.userDir = next();
    else if (a == "--connect") args.server = next();
    else if (a == "--smoke-frames") args.smokeFrames = std::stoi(next());
    else if (a == "--smoke-shot") args.smokeShot = next();
    else if (a == "--smoke-script") { auto j = readJson(next()); if (j) args.smokeScript = *j; }
    else if (a == "--launcher") args.fromLauncher = true;
  }
}

bool App::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) { DQ_ERR("SDL: %s", SDL_GetError()); return false; }
  if (auto db = readFile(assetsDir() / "gamecontrollerdb.txt")) SDL_GameControllerAddMappingsFromRW(SDL_RWFromConstMem(db->data(), (int)db->size()), 1);
  RendererKind want = args.rendererSet ? args.renderer : settings.renderer;
  if (want == RendererKind::Vulkan) { DQ_WARN("Vulkan renderer is not available yet; using Modern OpenGL"); want = RendererKind::Modern; }
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  struct Try { int maj, min; RendererKind k; };
  std::vector<Try> tries;
  if (want == RendererKind::Modern) tries = {{4, 6, RendererKind::Modern}, {4, 5, RendererKind::Modern}, {4, 3, RendererKind::Modern}, {3, 3, RendererKind::Legacy}};
  else tries = {{3, 3, RendererKind::Legacy}};
  for (auto& t : tries) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, t.maj);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, t.min);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    if (!window) window = SDL_CreateWindow(("DokuQuest " DQ_VERSION), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, settings.windowW, settings.windowH, flags);
    if (!window) { DQ_ERR("window: %s", SDL_GetError()); return false; }
    glctx = SDL_GL_CreateContext(window);
    if (glctx) { if (gfxInit(t.k)) break; SDL_GL_DeleteContext(glctx); glctx = nullptr; }
  }
  if (!glctx) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DokuQuest", "Your graphics driver doesn't support OpenGL 3.3, which DokuQuest needs.\nUpdating the graphics driver usually fixes this.", window);
    return false;
  }
  SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
  SDL_GetWindowSize(window, &width, &height);
  if (auto icon = readBinary(assetsDir() / "icon.png")) {
    int w, h; std::vector<u8> px;
    if (loadPngMem(icon->data(), icon->size(), w, h, px)) {
      SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
      SDL_SetWindowIcon(window, s); SDL_FreeSurface(s);
    }
  }
  return true;
}

void App::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ImGui_ImplSDL2_InitForOpenGL(window, glctx);
  ImGui_ImplOpenGL3_Init(gfx.modern ? "#version 450 core" : "#version 330 core");
  float dpi = 1; int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh); if (width > 0) dpi = (float)dw / width;
  uiScale = clampv(height / 720.f, 0.75f, 2.5f);
  ImFontConfig cfg; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
  std::string ui = (assetsDir() / "fonts" / "PixelifySans.ttf").string(), title = (assetsDir() / "fonts" / "Jersey15.ttf").string();
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
  auto load = [&](const std::string& f, float px) { return fs::exists(f) ? io.Fonts->AddFontFromFileTTF(f.c_str(), px * uiScale * dpi, &cfg, ranges) : io.Fonts->AddFontDefault(); };
  fontUi = load(ui, 18); fontSmall = load(ui, 14); fontUiBig = load(ui, 24); fontTitle = load(title, 56);
  io.FontGlobalScale = 1.f / dpi;
}

void App::reloadTiles() {
  tiles = TileSet();
  loadBuiltinTiles(tiles);
  addBuiltinTileAnims(tiles);   // render/tile_anim.h (before a pack replaces tiles)
  tiles.packName = applyResourcePack(tiles, settings.resourcePack);
  for (int p = 0; p < 2; p++) { if (tileTex.color[p]) glDeleteTextures(1, &tileTex.color[p]); if (tileTex.height[p]) glDeleteTextures(1, &tileTex.height[p]); tileTex.color[p] = tileTex.height[p] = 0; }
  int page = std::min(2048, gfx.maxArrayLayers);
  if (tiles.count > page * 2) DQ_WARN("tiles: %d tiles but this GPU holds %d; some blocks will look wrong", tiles.count, page * 2);
  tileTex.page = page;
  size_t tsz = (size_t)tiles.size * tiles.size;
  std::vector<u8> h4((size_t)tiles.count * tsz * 4);
  for (size_t i = 0; i < tiles.height.size(); i++) { h4[i * 4] = tiles.height[i]; h4[i * 4 + 1] = tiles.emissive.empty() ? 0 : tiles.emissive[i]; h4[i * 4 + 3] = 255; }
  for (int p = 0; p < 2; p++) {
    int first = p * page, n = std::min(page, tiles.count - first);
    if (n <= 0) { n = 1; first = 0; }   // keep a valid (unused) second page
    tileTex.color[p] = makeTextureArray(tiles.size, tiles.size, n, tiles.rgba.data() + first * tsz * 4, true);
    tileTex.height[p] = makeTextureArray(tiles.size, tiles.size, n, h4.data() + first * tsz * 4, false);
  }
  itemIcons().build(tiles);
  DQ_LOG("tiles: %d micro-voxel tiles (%s)", tiles.count, tiles.packName.empty() ? "built-in Doku" : tiles.packName.c_str());
}

void App::pushScreen(std::unique_ptr<Screen> s) { Screen* raw = s.get(); screens.push_back(std::move(s)); raw->onOpen(*this); }
void App::popScreen() { if (screens.empty()) return; screens.back()->onClose(*this); screens.pop_back(); }
void App::setScreen(std::unique_ptr<Screen> s) { while (!screens.empty()) popScreen(); if (s) pushScreen(std::move(s)); }

bool App::openWorld(const fs::path& folder) {
  leaveWorld(false);
  auto lv = Level::open(folder, pool.get(), &events);
  if (!lv) { toast("That world couldn't be opened", "bad"); return false; }
  session = std::make_unique<GameSession>(*this, std::move(lv));
  setScreen(nullptr);
  return true;
}
bool App::createWorld(LevelInfo info) {
  leaveWorld(false);
  auto lv = Level::create(savesDir(), info, pool.get(), &events);
  if (!lv) return false;
  session = std::make_unique<GameSession>(*this, std::move(lv));
  setScreen(nullptr);
  return true;
}
bool App::joinServer(const std::string& address) { return netJoin(*this, address); }   // client/netplay.cpp
void App::leaveWorld(bool toTitle) {
  netBeforeLeave(*this);   // goodbye to the host / stop serving (client/netplay.cpp)
  if (session) { session->save(); session.reset(); }
  if (toTitle) setScreen(makeTitleScreen());
}

void App::toast(const std::string& text, const std::string& kind) {
  events.emit("toast", json::array({text, kind}));
  DQ_LOG("toast: %s", text.c_str());
}

void App::takeScreenshot(const fs::path& path) {
  screenshotPending = true;
  if (!path.empty()) screenshotPath = path;
  else {
    char buf[64]; std::time_t t = std::time(nullptr); std::strftime(buf, sizeof buf, "%Y-%m-%d_%H.%M.%S", std::localtime(&t));
    screenshotPath = userSub("screenshots") / (std::string(buf) + ".png");
  }
}

void App::applySettings() {
  settings.save();
  SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
  if (audio) audio->setVolumes(settings);
  if (session) { session->worldRenderer.mopt.fancyLeaves = settings.fancyLeaves; session->worldRenderer.mopt.wavingPlants = settings.wavingPlants;
    if (session->worldRenderer.microVoxels != settings.microVoxels) { session->worldRenderer.microVoxels = settings.microVoxels; session->worldRenderer.reloadShaders(); }
    session->worldRenderer.remeshAll(); }
}

int App::run(int argc, char** argv) {
  g_app = this;
  parseArgs(argc, argv);
  CoreOptions co; co.userDirOverride = args.userDir;
  if (!initCore(co)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DokuQuest", "The game's data folder is missing or damaged. Reinstall DokuQuest from the launcher.", nullptr);
    return 1;
  }
  registerPortalView();   // render/portal_view.cpp
  settings = Settings::load();
  if (!settings.shaderPack.empty()) setShaderPack(settings.shaderPack);
  mods = std::make_unique<ModLoader>();
  mods->scan();
  mods->loadData();
  pool = std::make_unique<ThreadPool>(settings.workerThreads);
  registerTerrainLayers();     // render/terrain_fx.cpp: outline, cracks, tile animation, underwater fog
  if (!initWindow()) return 2;
  initImGui();
  input.init(&settings);
  audio = std::make_unique<AudioEngine>();
  audio->init(settings);
  registerAudioLayers();
  reloadTiles();
  registerEnvironmentLayers();   // distant terrain, clouds, weather, particles (render/env_layers.cpp)
  // start: straight into a world (launcher "Play world", smoke tests) or the title screen
  if (!args.world.empty()) { if (!openWorld(savesDir() / args.world)) setScreen(makeTitleScreen()); }
  else if (!args.server.empty()) { if (!joinServer(args.server)) setScreen(makeTitleScreen()); }
  else if (!args.createWorld.empty()) { LevelInfo li; li.name = args.createWorld; li.seedText = args.seed; if (!args.worldType.empty()) li.worldType = args.worldType; createWorld(li); }
  else setScreen(makeTitleScreen());
  frameLoop();
  leaveWorld(false);
  settings.save();
  pool.reset();
  audio.reset();
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
  SDL_GL_DeleteContext(glctx); SDL_DestroyWindow(window); SDL_Quit();
  return 0;
}

void App::frameLoop() {
  double last = nowSeconds();
  while (!quit) {
    double now = nowSeconds();
    dt = std::min(0.1, now - last); last = now; time += dt;
    input.dt = dt;
    input.beginFrame();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      input.handle(e);
      if (e.type == SDL_QUIT) quit = true;
      if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) SDL_GetWindowSize(window, &width, &height);
    }
    input.endFrame();
    // mouse capture: in-world with no menu open
    bool wantCapture = session && (!topScreen() || !topScreen()->wantsMouse());
    if (wantCapture != input.captured()) input.setCapture(window, wantCapture);

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
    ImGui::PushFont(fontUi);
    if (Screen* s = topScreen()) { s->update(*this, dt); }
    if (session) session->update(dt);
    netAfterFrame(*this);   // a joined world whose host went away is left (client/netplay.cpp)
    int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.05f, 0.04f, 0.035f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (session && (!topScreen() || topScreen()->showsWorld())) { session->render(); session->drawHud(); }
    // screens draw back to front (an overlay may sit over another menu)
    for (size_t i = 0; i < screens.size(); i++) if (i + 1 == screens.size() || screens[i + 1]->showsWorld()) screens[i]->draw(*this);
    if (input.pressed(Act::Pause) || input.pressed(Act::MenuBack)) {
      if (Screen* s = topScreen()) { if (!s->onBack(*this) && s->id() != std::string("title")) popScreen(); }
      else if (session && input.pressed(Act::Pause)) pushScreen(makePauseScreen());
    }
    ImGui::PopFont();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (screenshotPending) {
      std::vector<u8> px((size_t)dw * dh * 4);
      glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      std::vector<u8> flip(px.size());
      for (int y = 0; y < dh; y++) std::memcpy(&flip[(size_t)y * dw * 4], &px[(size_t)(dh - 1 - y) * dw * 4], (size_t)dw * 4);
      for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
      if (savePng(screenshotPath, dw, dh, flip.data())) DQ_LOG("screenshot saved: %s", screenshotPath.string().c_str());
      screenshotPending = false;
    }
    SDL_GL_SwapWindow(window);
    frame++;
    if (audio) audio->update(dt);
    // smoke test: run N frames, screenshot, quit
    if (args.smokeFrames > 0) {
      if ((int)frame == args.smokeFrames - 1 && !args.smokeShot.empty()) takeScreenshot(args.smokeShot);
      if ((int)frame >= args.smokeFrames) quit = true;
    }
    if (settings.fpsLimit > 0 && !settings.vsync) {
      double target = 1.0 / settings.fpsLimit, spent = nowSeconds() - now;
      if (spent < target) SDL_Delay((Uint32)((target - spent) * 1000));
    }
  }
}

} // namespace dq
