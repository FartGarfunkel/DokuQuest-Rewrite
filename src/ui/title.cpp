// ui/title.cpp — the title screen (ui/title.js + the panorama from game/game.js): the name built from stone
// blocks, a yellow splash line that bounces, grey buttons laid out like the old game, over a slowly turning
// demo world; the version line opens What's new.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "render/world_renderer.h"
#include "render/sky.h"
#include "render/gl.h"
#include "common/noise.h"
#include <imgui.h>
#include <set>

namespace dq {

static const char* const SPLASHES[] = {
  "Removed Herobrine!", "Now called DokuQuest!", "Now with weather!", "Listen to the caves...", "Wear a cape!", "Pet the wolf!", "Mind the puddles!", "Now with redstone!", "Minecarts!", "Swim like a fish!", "Lanterns sway in the wind!",
  "Hearts, not hunger!", "Graves keep your stuff!", "Pistons push!", "Saplings plant themselves!", "Grass creeps!",
  "Also try Mineclonia!", "Also try VoxeLibre!", "Also try Vintage Story!", "Also try Veloren!", "Also try Cube World!",
  "Made with WebGL!", "100% voxels!", "Alpha!", "The Ember Depths await!", "//wand!", "Now as an AppImage!",
  "Dragons are coming!", "Better than adventure?", "Punch a tree!", "Notch-style grass!", "Water flows by itself!",
  "Works offline!", "Couch co-op!", "Sail the seas!", "Windmills!", "Mind the Cinder Golem!", "Splash!",
};

// Five-row letters, '#' is a block.
static const char* glyph(char c, int row) {
  static const std::map<char, std::array<const char*, 5>> F = {
    {'E', {"#####", "#....", "####.", "#....", "#####"}}, {'D', {"####.", "#...#", "#...#", "#...#", "####."}},
    {'O', {".###.", "#...#", "#...#", "#...#", ".###."}}, {'K', {"#...#", "#..#.", "###..", "#..#.", "#...#"}},
    {'U', {"#...#", "#...#", "#...#", "#...#", ".###."}}, {'Q', {".###.", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'S', {".####", "#....", ".###.", "....#", "####."}}, {'T', {"#####", "..#..", "..#..", "..#..", "..#.."}}};
  return F.at(c)[row];
}

// drawTitleLogo: "Doku" in mossy cobblestone, "Quest" in stone, cut from the game's own textures, with a
// dark 3D edge down and to the right, light on the top/left edges and a thin black outline
static unsigned buildLogo(const TileSet& ts, int& W, int& H) {
  const char* word = "DOKUQUEST"; const int S = 16, DEPTH = 9;
  struct Cell { int x, y; bool warm; };
  std::vector<Cell> cells; int cx = 0;
  for (int i = 0; word[i]; i++) {
    int w = (int)strlen(glyph(word[i], 0));
    for (int y = 0; y < 5; y++) for (int x = 0; x < w; x++) if (glyph(word[i], y)[x] == '#') cells.push_back({cx + x, y, i < 4});
    cx += w + 1;
  }
  int cols = cx - 1;
  W = cols * S + DEPTH + 4; H = 5 * S + DEPTH + 4;
  std::set<std::pair<int, int>> filled; for (auto& c : cells) filled.insert({c.x, c.y});
  auto has = [&](int x, int y) { return filled.count({x, y}) > 0; };
  auto tileOf = [&](const char* name) -> std::vector<u8> {
    auto it = ts.index.find(name); if (it == ts.index.end()) return {};
    std::vector<u8> out(16 * 16 * 3); const u8* t = ts.tile(it->second); int st = ts.size / 16;
    for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) { const u8* p = t + ((size_t)(j * st) * ts.size + i * st) * 4; for (int k = 0; k < 3; k++) out[(j * 16 + i) * 3 + k] = p[k]; }
    return out;
  };
  auto stone = tileOf("stone"); if (stone.empty()) stone = tileOf("cobblestone");
  auto moss = tileOf("mossy_cobblestone"); if (moss.empty()) moss = stone;
  std::vector<u8> img((size_t)W * H * 4, 0);
  auto put = [&](int x, int y, float r, float g, float b) { if (x < 0 || y < 0 || x >= W || y >= H) return; u8* o = &img[((size_t)y * W + x) * 4]; o[0] = (u8)std::min(255.f, r); o[1] = (u8)std::min(255.f, g); o[2] = (u8)std::min(255.f, b); o[3] = 255; };
  auto px = [&](const Cell& c, int i, int j) { const auto& t = c.warm ? moss : stone; if (t.empty()) return vec3(128); const u8* v = &t[((j % 16) * 16 + (i % 16)) * 3]; return vec3(v[0], v[1], v[2]); };
  for (int k = DEPTH; k >= 1; k--) for (auto& c : cells) for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
    vec3 v = px(c, i, j); float f = 0.22f + 0.16f * (1 - (float)k / DEPTH);
    put(2 + c.x * S + i + (int)std::lround(k * 0.35), 2 + c.y * S + j + k, v.r * f, v.g * f, v.b * f);
  }
  for (auto& c : cells) for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
    vec3 v = px(c, i, j); float f = 1;
    if (j < 2 && !has(c.x, c.y - 1)) f = 1.35f; else if (i < 2 && !has(c.x - 1, c.y)) f = 1.22f;
    else if (j >= S - 2 && !has(c.x, c.y + 1)) f = 0.7f; else if (i >= S - 2 && !has(c.x + 1, c.y)) f = 0.78f;
    put(2 + c.x * S + i, 2 + c.y * S + j, v.r * f, v.g * f, v.b * f);
  }
  std::vector<u8> solid((size_t)W * H); for (size_t i = 0; i < solid.size(); i++) solid[i] = img[i * 4 + 3] ? 1 : 0;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    if (solid[(size_t)y * W + x]) continue;
    if ((x > 0 && solid[(size_t)y * W + x - 1]) || (x < W - 1 && solid[(size_t)y * W + x + 1]) || (y > 0 && solid[(size_t)(y - 1) * W + x]) || (y < H - 1 && solid[(size_t)(y + 1) * W + x])) put(x, y, 12, 10, 8);
  }
  return makeTexture2D(W, H, img.data(), true, false, false);
}

// Slowly orbiting view of a demo world behind the title menu (game.js startPanorama).
struct Panorama {
  std::unique_ptr<Level> lv;
  WorldRenderer wr;
  std::unique_ptr<SkyRenderer> sky;
  FrameCtx frame;
  fs::path folder;
  float yaw = 0.6f; double y = 0;
  bool ok = false;
  bool start(App& a) {
    folder = userSub("cache") / "title-panorama";
    std::error_code ec; fs::remove_all(folder, ec); fs::create_directories(folder, ec);
    LevelInfo info; info.name = "title"; info.seedText = "emberwild-title"; info.seed = seedFromString("emberwild-title");
    info.time = 0.18 * DAY_LEN;
    lv = std::make_unique<Level>(info, folder, a.pool.get(), &a.events);
    frame.settings = &a.settings;
    if (!wr.init(a.settings, a.tileTex)) return false;
    sky = std::make_unique<SkyRenderer>(); sky->init();
    wr.setWorld(&lv->world());
    int top = 0;
    if (DimGenerator* g = lv->gen->forDim(DIM_OVERWORLD))
      for (int dz = -40; dz <= 40; dz += 8) for (int dx = -40; dx <= 40; dx += 8) { ColumnSample s; g->sample(8 + dx, 8 + dz, s); top = std::max(top, std::max((int)s.height, 64)); }
    y = dimBase(DIM_OVERWORLD) + top + 10;
    ok = true;
    return true;
  }
  void stop() {
    if (!lv) return;
    wr.setWorld(nullptr);
    lv.reset();
    std::error_code ec; fs::remove_all(folder, ec);
    ok = false;
  }
  void render(App& a) {
    if (!ok) return;
    yaw += (float)a.dt * 0.04f;
    int R = std::min(a.settings.renderDist, 4);
    World& w = lv->world();
    Camera& cam = frame.cam;
    cam.pos = glm::dvec3(8 + std::sin(yaw) * 6, y, 8 + std::cos(yaw) * 6);
    cam.yaw = yaw + PI; cam.pitch = -0.28f; cam.fov = 70;
    std::vector<StreamAnchor> anchors = {{vec3(cam.pos), R, false}};
    w.stream(anchors, 3.0);
    int dw, dh; SDL_GL_GetDrawableSize(a.window, &dw, &dh);
    cam.aspect = dh > 0 ? (float)dw / dh : 1.f; cam.farZ = 1024;
    cam.update();
    frame.time = a.time; frame.width = dw; frame.height = dh;
    sky->computeFrame(*lv, frame, R);
    cam.update();
    wr.update(cam, *a.pool, 3.0);
    glViewport(0, 0, dw, dh);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    sky->draw(frame);
    wr.drawOpaque(frame); wr.drawCutout(frame); wr.drawTranslucent(frame);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  }
};

// rotate + scale the vertices added to a draw list since `start` around `c` (the bouncing splash)
static void transformVerts(ImDrawList* dl, int start, ImVec2 c, float ang, float k) {
  float cs = std::cos(ang), sn = std::sin(ang);
  for (int i = start; i < dl->VtxBuffer.Size; i++) {
    ImVec2& p = dl->VtxBuffer[i].pos; float x = (p.x - c.x) * k, y = (p.y - c.y) * k;
    p = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
  }
}

class TitleScreen : public Screen {
public:
  const char* id() const override { return "title"; }
  bool showsWorld() const override { return false; }
  bool pausesGame() const override { return false; }
  unsigned logo = 0; int lw = 0, lh = 0;
  std::string splash;
  Panorama pano;
  bool focusPending = true, newsChecked = false; double newsAt = -1;
  void onOpen(App& a) override {
    splash = SPLASHES[(size_t)(unixMillis() / 7) % (sizeof SPLASHES / sizeof SPLASHES[0])];
    logo = buildLogo(a.tiles, lw, lh);
    if (!a.session) pano.start(a);
    newsCheck(a);
  }
  void onClose(App& a) override { pano.stop(); if (logo) { glDeleteTextures(1, &logo); logo = 0; } }
  std::string versionText() const {
    std::string v = DQ_VERSION; size_t p = v.find("-alpha"); if (p != std::string::npos) v.erase(p, 6);
    return "DokuQuest Alpha v" + v + " \xC2\xB7 What's new";
  }
  // The first time a new version runs, the title screen opens its notes once (players new to the game just get the "New" tag)
  void newsCheck(App& a) {
    std::string seen = a.settings.extra.value("seenVersion", "");
    if (seen == DQ_VERSION || a.args.smokeFrames > 0 || !a.args.screen.empty()) return;
    bool played = !seen.empty() || a.settings.extra.value("played", false);
    if (played) newsAt = a.time + 0.4;
  }
  void markSeen(App& a) { a.settings.extra["seenVersion"] = DQ_VERSION; a.settings.save(); }
  void update(App& a, double) override {
    if (newsAt > 0 && a.time >= newsAt && a.topScreen() == this) { newsAt = -1; markSeen(a); a.pushScreen(makeNewsScreen()); }
    if (a.topScreen() == this && a.input.pressed(Act::MenuStart)) uiDefer([](App& app) { app.pushScreen(makeWorldsScreen()); });
  }
  void draw(App& a) override {
    using namespace ui;
    if (a.topScreen() == this || !a.session) pano.render(a);
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // radial-gradient(ellipse at 50% 45%, transparent 35%, rgba(0,0,0,.45)) + linear-gradient(rgba(10,7,5,.05), rgba(10,7,5,.35))
    if (!pano.ok) bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), col::rgb(0x1a1510));
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(10, 7, 5, 13), IM_COL32(10, 7, 5, 13), IM_COL32(10, 7, 5, 89), IM_COL32(10, 7, 5, 89));
    // radial-gradient(ellipse at 50% 45%, transparent 35%, rgba(0,0,0,.45))
    radialVignette(bg, ImVec2(W * 0.5f, H * 0.45f), ImVec2(W * 0.75f, H * 0.75f), 0.35f, IM_COL32(0, 0, 0, 115));
    if (a.topScreen() != this) return;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##title", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float uvw = W / z / 100.f, uvh = H / z / 100.f;
    float logoW = std::min(720.f, 94 * uvw) * z, logoH = lh ? logoW * lh / lw : 0;
    float gap = clampv(5 * uvh, 18.f, 46.f) * z;
    float btnW = std::min(400.f, 90 * uvw) * z, btnH = (H / z < 520 ? 34 : 40) * z, bgap = (H / z < 520 ? 6 : 8) * z;
    float total = logoH + gap + btnH * 6 + bgap * 5;
    float y0 = std::max(16 * z, (H - total) * 0.5f), x0 = (W - logoW) * 0.5f;
    if (logo) {
      // drop-shadow(0 6px 10px rgba(0,0,0,.55))
      for (int i = 3; i >= 1; i--) dl->AddImage((ImTextureID)(intptr_t)logo, ImVec2(x0, y0 + (4 + i * 2) * z), ImVec2(x0 + logoW, y0 + logoH + (4 + i * 2) * z), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 40));
      dl->AddImage((ImTextureID)(intptr_t)logo, ImVec2(x0, y0), ImVec2(x0 + logoW, y0 + logoH));
    }
    { // .mc-splash: right -1%, bottom -8%, rotate(-20deg), scale 1 → 1.09 every 0.55 s
      float fs = clampv(2.5f * uvw, 13.f, 22.f) / fontPx(F_BIG);
      ImVec2 ts = textSize(F_BIG, splash.c_str(), 0, fs);
      ImVec2 br(x0 + logoW * 1.01f, y0 + logoH * 1.08f), p(br.x - ts.x, br.y - ts.y);
      int start = dl->VtxBuffer.Size;
      textShadow(dl, F_BIG, p, col::rgb(0xffff3c), splash.c_str(), ImVec2(2, 2) * z, col::rgb(0x3f3f00), 0, fs);
      float ph = std::fmod((float)a.time, 1.1f) / 0.55f; float t = ph < 1 ? ph : 2 - ph; t = t * t * (3 - 2 * t);
      transformVerts(dl, start, ImVec2(p.x + ts.x * 0.5f, p.y + ts.y * 0.5f), -20.f * PI / 180.f, 1 + 0.09f * t);
    }
    float bx = (W - btnW) * 0.5f, by = y0 + logoH + gap;
    auto row = [&](float y) { ImGui::SetCursorScreenPos(ImVec2(bx, y)); };
    if (focusPending && a.input.padCount() > 0) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    if (a.input.padCount() > 0) focusPending = false;   // a pad picked up later still gets its highlight
    row(by);
    if (mcButton("Singleplayer", ImVec2(btnW, btnH))) a.pushScreen(makeWorldsScreen());
    row(by += btnH + bgap);
    if (mcButton("Multiplayer", ImVec2(btnW, btnH))) a.pushScreen(makeMultiplayerScreen());
    row(by += btnH + bgap);
    if (mcButton("Players & Skins", ImVec2(btnW, btnH))) a.pushScreen(makePlayersScreen());
    row(by += btnH + bgap);
    if (mcButton("How to Play", ImVec2(btnW, btnH))) a.pushScreen(makeHowToScreen());
    row(by += btnH + bgap);
    float half = (btnW - bgap) * 0.5f;
    if (mcButton("Options...", ImVec2(half, btnH))) a.pushScreen(makeOptionsScreen());
    ImGui::SameLine(0, bgap);
    if (mcButton("Quit Game", ImVec2(half, btnH))) a.quit = true;
    // version (bottom left) and storage line (bottom right)
    std::string ver = versionText();
    bool isNew = a.settings.extra.value("seenVersion", "") != DQ_VERSION;
    ImVec2 vs = textSize(F_BODY, ver.c_str(), 0, 14.f / 15.f);
    ImVec2 vp(8 * z, H - 6 * z - vs.y);
    ImGui::SetCursorScreenPos(vp);
    bool vClick = ImGui::InvisibleButton("##version", vs);
    bool vHot = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible());
    textShadow(dl, F_BODY, vp, vHot ? col::rgb(0xffffa0) : col::white, ver.c_str(), ImVec2(2, 2) * z, col::rgb(0x3f3f3f), 0, 14.f / 15.f);
    if (vHot) dl->AddLine(ImVec2(vp.x, vp.y + vs.y), ImVec2(vp.x + vs.x, vp.y + vs.y), col::rgb(0xffffa0), 1 * z);
    if (isNew) {
      ImVec2 tp(vp.x + vs.x + 8 * z, vp.y); ImVec2 ts = textSize(F_SMALL, "New");
      dl->AddRectFilled(tp, tp + ImVec2(ts.x + 10 * z, vs.y), col::rgb(0xc8261e), 2 * z);
      text(dl, F_SMALL, tp + ImVec2(5 * z, (vs.y - ts.y) * 0.5f), col::white, "New");
    }
    if (vClick) { ui::playClick(); markSeen(a); a.pushScreen(makeNewsScreen()); }
    const char* foot = "Worlds save automatically in the game folder";
    ImVec2 fs = textSize(F_MID, foot);
    textShadow(dl, F_MID, ImVec2(W - 8 * z - fs.x, H - 6 * z - fs.y), IM_COL32(255, 255, 255, 217), foot, ImVec2(2, 2) * z, col::rgb(0x3f3f3f, 217));
    ImGui::End();
  }
};

std::unique_ptr<Screen> makeTitleScreen() { return std::make_unique<TitleScreen>(); }

} // namespace dq
