#include "client/app.h"
#include <algorithm>
#include <cstring>
#include "render/particles.h"
#include "common/ui_scale.h"
#include "ui/profiles.h"
#include "client/test_script.h"
#include "render/portal_view.h"
#include "render/vk_backend.h"
#include "client/netplay.h"
#include "render/tile_anim.h"
#include "client/session.h"
#include "core/init.h"
#include "render/env_layers.h"
#include "ui/screen.h"
#include "ui/ui.h"
#include "audio/audio.h"
#include "client/client_fx.h"
#include "mod/modloader.h"
#include "assets/icons.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <ctime>

namespace dq { void applyGrassSetting(const Settings& s); }   // render/world_renderer.cpp

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
    else if (a == "--screen") args.screen = next();
    else if (a == "--bridge") { args.bridge = 25590; if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0])) args.bridge = std::atoi(argv[++i]); }
  }
}

bool App::initWindow() {
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");   // physical pixels, so we scale the UI ourselves
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) { DQ_ERR("SDL: %s", SDL_GetError()); return false; }
  SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, settings.rawMouse ? "0" : "1");   // raw look, unless Settings asks for warping
  if (auto db = readFile(assetsDir() / "gamecontrollerdb.txt")) SDL_GameControllerAddMappingsFromRW(SDL_RWFromConstMem(db->data(), (int)db->size()), 1);
  // a 1280×720 window is tiny on a 2K/3K/4K monitor; grow the untouched default so Auto has room
  {
    SDL_Rect bounds{0, 0, 1920, 1080}, usable{0, 0, 1280, 720};
    if (SDL_GetDisplayBounds(0, &bounds) != 0 || bounds.w < 320 || bounds.h < 240) bounds = {0, 0, 1920, 1080};
    if (SDL_GetDisplayUsableBounds(0, &usable) != 0 || usable.w < 320 || usable.h < 240) usable = bounds;
    int edge = std::min(bounds.w, bounds.h);
    float z = uiZoomFromSetting(settings.guiScale, uiTierForEdge(edge).zoom);
    int w = settings.windowW, h = settings.windowH;
    gameWindowForZoom(z, usable.w, usable.h, settings.windowW, settings.windowH, w, h);
    settings.windowW = w; settings.windowH = h;
  }
  RendererKind want = args.rendererSet ? args.renderer : settings.renderer;
  if (want == RendererKind::Vulkan) {
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    window = SDL_CreateWindow(("DokuQuest " DQ_VERSION), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, settings.windowW, settings.windowH, flags);
    if (!window) { DQ_ERR("window: %s", SDL_GetError()); return false; }
    if (!gfxInitVulkan(window)) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DokuQuest", "Vulkan couldn't be started on this graphics driver.\nPick Modern or Legacy OpenGL in the launcher.", window);
      return false;
    }
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
  if (gfx.vulkan) ImGui_ImplSDL2_InitForVulkan(window);
  else ImGui_ImplSDL2_InitForOpenGL(window, glctx);
  ImGui_ImplOpenGL3_Init(gfx.vulkan || gfx.modern ? "#version 450 core" : "#version 330 core");
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
  applyUserTiles(tiles);   // the Texture Studio's pictures, over any pack
  holidayTiles(tiles);     // December's wrapped chests
  // ice you can see through like Minecraft's (about 60 % cover), whichever pack painted it
  for (const char* k : {"ice", "frosted_ice"}) {
    auto it = tiles.index.find(k);
    if (it == tiles.index.end() || it->second < 0 || it->second >= tiles.count) continue;
    u8* px = tiles.rgba.data() + (size_t)it->second * tiles.size * tiles.size * 4;
    for (int i = 0; i < tiles.size * tiles.size; i++) if (px[i * 4 + 3] > 8 && px[i * 4 + 3] != 253) px[i * 4 + 3] = (u8)std::min(155, px[i * 4 + 3] * 3 / 4);
  }
  // Better than Adventure's rubyglass, whichever pack painted it: the crystal is see-through (BTA's is 80 % cover, a
  // little more open here so it reads as glass in the Nether's glow), and the whole family shines from within the way
  // BTA's emissive layers make it: the brightest half of each picture glows fully, the rest a little
  for (const char* k : {"rubyglass_crystal", "rubyglass_column", "rubyglass_node", "rubyglass_growth", "rubyglass_block", "rubyglass_bricks", "red_crystal", "red_crystal_cluster"}) {
    auto it = tiles.index.find(k);
    if (it == tiles.index.end() || it->second < 0 || it->second >= tiles.count) continue;
    const int S = tiles.size; u8* px = tiles.rgba.data() + (size_t)it->second * S * S * 4;
    const bool glass = std::string(k) == "rubyglass_crystal" || std::string(k) == "red_crystal";
    const bool giant = std::string(k) == "rubyglass_column" || std::string(k) == "rubyglass_node";   // the big crystals: see-through too
    std::vector<int> lums;
    for (int i = 0; i < S * S; i++) if (px[i * 4 + 3] > 8) lums.push_back(px[i * 4] * 3 + px[i * 4 + 1] * 6 + px[i * 4 + 2]);
    if (lums.empty()) continue;
    std::nth_element(lums.begin(), lums.begin() + lums.size() / 2, lums.end());
    const int mid = lums[lums.size() / 2];
    if (tiles.emissive.size() < (size_t)tiles.count * S * S) tiles.emissive.resize((size_t)tiles.count * S * S, 0);
    u8* e = tiles.emissive.data() + (size_t)it->second * S * S;
    for (int i = 0; i < S * S; i++) {
      if (px[i * 4 + 3] <= 8) { e[i] = 0; continue; }
      // the crystal: clear red glass (about 45 % cover, its edges a little more) with only a soft glow, so what's behind
      // it shows; the opaque rubyglass (column, node, bricks) glows the way BTA's emissive layers do
      if (glass && px[i * 4 + 3] != 253) px[i * 4 + 3] = (u8)std::min<int>(px[i * 4 + 3], 115);
      if (giant && px[i * 4 + 3] != 253) px[i * 4 + 3] = (u8)std::min<int>(px[i * 4 + 3], 150);
      const bool bright = px[i * 4] * 3 + px[i * 4 + 1] * 6 + px[i * 4 + 2] >= mid;
      e[i] = glass ? (bright ? 120 : 25) : giant ? (bright ? 210 : 45) : (bright ? 225 : 70);
    }
  }
  bakeTileRelief(tiles);   // once per tile set: whole-voxel geometry + smooth normal maps (assets/tile_relief.cpp)
  for (int p = 0; p < 2; p++) {
    for (GLuint* t : {&tileTex.color[p], &tileTex.height[p], &tileTex.normal[p]}) { if (*t) glDeleteTextures(1, t); *t = 0; }
  }
  int page = std::min(2048, gfx.maxArrayLayers);
  if (tiles.count > page * 2) DQ_WARN("tiles: %d tiles but this GPU holds %d; some blocks will look wrong", tiles.count, page * 2);
  tileTex.page = page;
  size_t tsz = (size_t)tiles.size * tiles.size;
  std::vector<u8> h4((size_t)tiles.count * tsz * 4);
  // r = geometry (whole micro-voxel steps, scaled to 0..255), g = glow; the smooth normals/crevices are their own array
  for (size_t i = 0; i < tiles.relief.size(); i++) { h4[i * 4] = (u8)((tiles.relief[i] * 255 + TileSet::RELIEF_STEPS / 2) / TileSet::RELIEF_STEPS); h4[i * 4 + 1] = tiles.emissive.empty() ? 0 : tiles.emissive[i]; h4[i * 4 + 3] = 255; }
  // The clear texels of a cutout (leaves, glass, flowers) carry the colour of their nearest painted neighbour (the alpha stays
  // clear). Mipmaps average a tile down, and clear black texels there gave every far leaf and pane a dark rim.
  std::vector<u8> bled(tiles.rgba);
  {
    const int S = tiles.size; std::vector<int> q; std::vector<u8> seen((size_t)S * S);
    for (int t = 0; t < tiles.count; t++) {
      u8* px = bled.data() + (size_t)t * tsz * 4;
      q.clear(); std::fill(seen.begin(), seen.end(), 0);
      for (int i = 0; i < S * S; i++) if (px[i * 4 + 3]) { seen[i] = 1; q.push_back(i); }
      if (q.empty() || (int)q.size() == S * S) continue;
      for (size_t h = 0; h < q.size(); h++) {   // outward from the painted texels, a ring at a time
        const int i = q[h], x = i % S, y = i / S;
        const int nb[4] = {x > 0 ? i - 1 : -1, x < S - 1 ? i + 1 : -1, y > 0 ? i - S : -1, y < S - 1 ? i + S : -1};
        for (int j : nb) if (j >= 0 && !seen[j]) { seen[j] = 1; std::memcpy(px + j * 4, px + i * 4, 3); q.push_back(j); }
      }
    }
  }
  for (int p = 0; p < 2; p++) {
    int first = p * page, n = std::min(page, tiles.count - first);
    if (n <= 0) { n = 1; first = 0; }   // keep a valid (unused) second page
    tileTex.color[p] = makeTextureArray(tiles.size, tiles.size, n, bled.data() + first * tsz * 4, true);
    tileTex.height[p] = makeTextureArray(tiles.size, tiles.size, n, h4.data() + first * tsz * 4, false);
    tileTex.normal[p] = makeTextureArray(tiles.size, tiles.size, n, tiles.normal.data() + first * tsz * 4, false, true);
  }
  applyTextureFiltering();
  itemIcons().build(tiles);
  DQ_LOG("tiles: %d micro-voxel tiles (%s)", tiles.count, tiles.packName.empty() ? "built-in Doku" : tiles.packName.c_str());
}

// anisotropic filtering on the block pictures: ground and walls seen at a slant stay sharp into the distance (the
// mipmaps alone blur them; 4x costs next to nothing, even on a laptop's own graphics)
void App::applyTextureFiltering() {
  if (!gfx.anisotropic) return;
  const int want = std::clamp(settings.extra.value("anisotropic", 4), 1, 16);
  const int a = std::max(1, std::min(want, (int)gfx.maxAniso));
  for (int p = 0; p < 2; p++) if (tileTex.color[p]) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, tileTex.color[p]);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, a);
  }
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
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
  // the open menus close first, while the world is still here (an open inventory hands its crafting grid back to the
  // player as it closes; closed after the world went, it wrote into a player that no longer existed and crashed)
  if (session) { setScreen(nullptr); session->save(); session.reset(); }
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
  SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, settings.rawMouse ? "0" : "1");
  if (input.captured()) { input.setCapture(window, false); input.setCapture(window, true); }   // the hint is read when capture turns on
  if (gfx.vulkan) gfxVulkanSetVsync(settings.vsync != 0);
  else SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
  if (audio) audio->setVolumes(settings);
  if (session) { session->worldRenderer.mopt.fancyLeaves = settings.fancyLeaves; session->worldRenderer.mopt.wavingPlants = settings.wavingPlants;
    if (session->worldRenderer.microVoxels != settings.microVoxels) { session->worldRenderer.microVoxels = settings.microVoxels; session->worldRenderer.reloadShaders(); }
    applyGrassSetting(settings);   // render/world_renderer.cpp: 3D grass on or off
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
  profileLook(settings, 0);   // ui/profiles.cpp: undo a profile edit the last run left half done
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
  registerUi(*this);
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
  screens.clear();   // screens own GL objects (shaders, textures): free them while the GL context still exists
  particles().releaseGl();   // the particle engine is a static, destroyed after main returns
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
  if (gfx.vulkan) gfxShutdownVulkan();
  else SDL_GL_DeleteContext(glctx);
  SDL_DestroyWindow(window); SDL_Quit();
  return 0;
}

void engineBridgeFrame(App& app, int port);   // client/engine_bridge.cpp

void App::frameLoop() {
  double last = nowSeconds();
  double focusAt = last;   // losing focus waits 250 ms, and the first moments of a run do not count
  while (!quit) {
    double now = nowSeconds();
    dt = std::min(0.1, now - last); last = now; time += dt;
    input.dt = dt;
    input.beginFrame();
    testScriptFrame(*this);   // --smoke-script: scripted keys/pad/commands (client/test_script.cpp)
    if (args.bridge || settings.extra.value("engineBridge", false)) engineBridgeFrame(*this, args.bridge ? args.bridge : 25590);   // the launcher's Engine page, Strata
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      input.handle(e);
      if (e.type == SDL_QUIT) quit = true;
      if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        SDL_GetWindowSize(window, &width, &height);
        Uint32 fl = SDL_GetWindowFlags(window);
        if (!settings.fullscreen && (fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) == 0) { settings.windowW = width; settings.windowH = height; }
      }
    }
    input.endFrame();
    // mouse capture: in-world with no menu open
    bool wantCapture = session && (!topScreen() || !topScreen()->wantsMouse());
    if (session && session->split()) {   // the mouse belongs to the first person: their own menu frees it, or a shared one
      auto& mine = session->locals[0].screens;
      wantCapture = !(topScreen() && topScreen()->wantsMouse()) && !(!mine.empty() && mine.back()->wantsMouse());
    }
    if (wantCapture != input.captured()) input.setCapture(window, wantCapture);

    if (gfx.vulkan) gfxVulkanNewFrame();
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); uiBeginFrame(*this); ImGui::NewFrame();
    ImGui::PushFont(fontUi);
    if (Screen* s = topScreen()) { s->update(*this, dt); }
    if (session) session->update(dt);
    netAfterFrame(*this);   // a joined world whose host went away is left (client/netplay.cpp)
    int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.05f, 0.04f, 0.035f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (session && (!topScreen() || topScreen()->showsWorld())) { session->render(); if (session->split()) session->splitUi(); else session->drawHud(); }
    // screens draw back to front (an overlay may sit over another menu)
    for (size_t i = 0; i < screens.size(); i++) if (i + 1 == screens.size() || screens[i + 1]->showsWorld()) screens[i]->draw(*this);
    uiAfterScreens(*this);   // the menu tab strip (ui/ui_main.cpp)
    if (session && session->split()) {
      // split screen: a shared menu (pause...) answers to everybody; otherwise each person's Back / Pause closes their own menu, or pauses
      if (session->joinedFrame + 1 < frame) {   // (the joiner's own Start press is still an edge in their new input frame the frame after they joined)
        if (Screen* s = topScreen()) {
          if (input.pressed(Act::Pause) || input.pressed(Act::MenuBack)) { if (!s->onBack(*this) && s->id() != std::string("title")) popScreen(); }
        } else {
          bool pause = false;
          for (int i = 0; i < (int)session->locals.size() && !pause; i++) {
            session->enterLocal(i);
            if (Screen* s = topScreen()) { if (input.pressed(Act::Pause) || input.pressed(Act::MenuBack)) { if (!s->onBack(*this)) popScreen(); } }
            else if (input.pressed(Act::Pause)) pause = true;
            session->leaveLocal();
          }
          if (pause) pushScreen(makePauseScreen());
        }
      }
    } else if (input.pressed(Act::Pause) || input.pressed(Act::MenuBack)) {
      if (Screen* s = topScreen()) { if (!s->onBack(*this) && s->id() != std::string("title")) popScreen(); }
      else if (session && input.pressed(Act::Pause)) pushScreen(makePauseScreen());
    }
    // BTA 8.0: pause and mute when the window is not in front, written for this engine.
    // After the Escape handler, so the menu this opens is not closed on the same press.
    // A shared world still runs (GameSession only pauses a world that is not on the network).
    bool focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (focused) focusAt = now;
    else if (args.smokeFrames == 0 && settings.pauseOnLostFocus && session && !topScreen() && now > focusAt + 0.25)
      pushScreen(makePauseScreen());
    if (audio) audio->setMuted(settings.muteOnLostFocus && !focused);
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
    if (gfx.vulkan) gfxVulkanPresent(window);
    else {
      SDL_GL_SwapWindow(window);
      // at most three frames queued on the GPU: wait for the oldest. Where the driver throttles at the swap this
      // is already done; where it doesn't (an offscreen surface, some drivers with vsync off) the CPU ran ahead until
      // the driver's queue filled and then a frame stood still for ~450 ms (HD 520).
      static GLsync fence[3] = {nullptr, nullptr, nullptr}; static int fi = 0;
      if (fence[fi]) { glClientWaitSync(fence[fi], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull); glDeleteSync(fence[fi]); }
      fence[fi] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); fi = (fi + 1) % 3;
    }
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
