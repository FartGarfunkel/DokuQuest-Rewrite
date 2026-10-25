// client/split.cpp — local split screen for up to four people (game.js addLocal / removeLocal / layoutViews /
// renderViews, gamepad.js claim, hud.js nameTags, ui.js party list).
//
// Each person is a LocalPlayer: their Player, their camera (FrameCtx), their viewport, the devices that drive them
// (the first person keeps keyboard + mouse; others own a controller) and their own menus (inventory, death...).
// The session's `player`, `frame`, `target`, `viewMode`... mirror the SELECTED person, so everything written for one
// player (layers, HUD, controller glue) works per person just by selecting them first.
// Each viewport also gets its own ImGui context (shared font atlas) so each person's HUD and menus lay themselves out
// in their own rectangle with their own controller navigating; the draw data is shifted into place when drawn.
#include "client/session.h"
#include "client/app.h"
#include "render/sky.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>

namespace dq {

void GameSession::selectLocal(int i) {
  if (i == cur || i < 0 || i >= (int)locals.size()) return;
  LocalPlayer& o = locals[cur];
  o.player = player; o.frame = frame; o.target = target; o.viewMode = viewMode; o.hudHidden = hudHidden; o.debugOverlay = debugOverlay;
  o.breaking = breaking; o.breakBlock = breakBlock; o.breakProgress = breakProgress;
  LocalPlayer& n = locals[i];
  player = n.player; frame = n.frame; target = n.target; viewMode = n.viewMode; hudHidden = n.hudHidden; debugOverlay = n.debugOverlay;
  breaking = n.breaking; breakBlock = n.breakBlock; breakProgress = n.breakProgress;
  cur = i;
}

void GameSession::enterLocal(int i) {
  selectLocal(i);
  if (!split() || entered) return;
  app.input.select(cur);
  std::swap(app.screens, locals[cur].screens);   // topScreen() / pushScreen() are now this person's
  entered = true;
}

void GameSession::leaveLocal() {
  if (!entered) return;
  std::swap(app.screens, locals[cur].screens);
  app.input.select(-1);
  entered = false;
}

int GameSession::renderDist() const {
  int R = userRenderDist >= 0 ? userRenderDist : app.settings.renderDist, n = (int)locals.size();
  return n >= 3 ? std::min(R, 4) : n == 2 ? std::min(R, 6) : R;   // game.js renderDist()
}

void GameSession::lendRenderDist(bool on) {
  if (on && userRenderDist < 0) { userRenderDist = app.settings.renderDist; app.settings.renderDist = renderDist(); }
  else if (!on && userRenderDist >= 0) { app.settings.renderDist = userRenderDist; userRenderDist = -1; }
}

// 1 = full, 2 = side by side (or stacked), 3-4 = quarters (the third person of three gets the whole bottom)
void GameSession::layoutLocals() {
  int W = app.width, H = app.height, n = (int)locals.size();
  const std::string& m = app.settings.split;
  bool vertical = m == "vertical" || (m != "horizontal" && W > H * 1.2);
  auto set = [&](int i, int x, int y, int w, int h) { locals[i].view = {x, y, w, h}; };
  int hw = W / 2, hh = H / 2;
  if (n <= 1) set(0, 0, 0, W, H);
  else if (n == 2) { if (vertical) { set(0, 0, 0, hw, H); set(1, hw, 0, W - hw, H); } else { set(0, 0, 0, W, hh); set(1, 0, hh, W, H - hh); } }
  else {
    set(0, 0, 0, hw, hh); set(1, hw, 0, W - hw, hh);
    if (n == 3) set(2, 0, hh, W, H - hh); else { set(2, 0, hh, hw, H - hh); set(3, hw, hh, W - hw, H - hh); }
  }
}

void GameSession::routeInput() {
  std::vector<InputSource> v;
  if (split()) for (auto& l : locals) v.push_back(l.src);
  app.input.setSources(v);
}

// A controller nobody plays with pressed Start: that person joins (gamepad.js claim). A controller the first person already
// plays with (it did something besides Start) keeps pausing instead.
void GameSession::checkJoin() {
  Input& in = app.input;
  if (level->net || locals.size() >= 4 || !app.screens.empty() || entered || in.setupActive) return;
  for (auto& p : in.pads()) {
    if (!p.active || !p.edge(9)) continue;
    bool owned = !split() && p.used;
    for (auto& l : locals) if (std::find(l.src.pads.begin(), l.src.pads.end(), p.id) != l.src.pads.end()) owned = true;
    if (owned) continue;
    if (addLocal(p.id) >= 0) break;
  }
}

int GameSession::addLocal(SDL_JoystickID pad) {
  if (level->net || locals.size() >= 4 || locals.empty()) return -1;
  selectLocal(0);
  int i = (int)locals.size();
  if (i == 1) {   // the first person keeps the keyboard and the controllers they have been playing with
    locals[0].src.kb = true;
    for (auto& p : app.input.pads()) if (p.used && p.id != pad) locals[0].src.pads.push_back(p.id);
  }
  auto np = std::make_unique<Player>();
  np->name = "Player " + std::to_string(i + 1);
  np->skin = app.settings.skin;
  Player* p0 = locals[0].player;
  json saved = level->loadPlayer(np->name);   // their inventory from last time, by name
  bool placed = false;
  if (!saved.is_null()) { np->load(saved); placed = np->realm == p0->realm && glm::distance(np->pos, p0->pos) < 64.f; }
  else np->mode = level->info.mode;
  if (!placed) {   // newcomers join next to the first player
    vec3 right(std::cos(p0->yaw), 0, -std::sin(p0->yaw));
    np->pos = p0->pos + right * (1.1f + 0.5f * (i - 1)) + vec3(0, 0.1f, 0);
    np->realm = p0->realm; np->yaw = p0->yaw; np->pitch = 0; np->vel = vec3(0);
  }
  Player* ent = static_cast<Player*>(level->spawn(std::move(np)));
  locals.emplace_back();
  LocalPlayer& L = locals.back();
  L.index = i; L.player = ent; L.frame.settings = &app.settings; L.src.kb = false; L.src.pads = {pad};
  joinedFrame = app.frame;
  layoutLocals(); routeInput();
  app.toast(ent->name + " joined", "good");
  DQ_LOG("split screen: %s joined (%d players)", ent->name.c_str(), (int)locals.size());
  return i;
}

void GameSession::removeLocal(int i) {
  if (i <= 0 || i >= (int)locals.size() || entered) return;
  selectLocal(0);
  LocalPlayer& L = locals[i];
  std::string name = L.player ? L.player->name : "";
  if (L.player) { level->savePlayer(L.player->name, L.player->save()); L.player->removed = true; }
  freeLocalUi(L);
  locals.erase(locals.begin() + i);
  for (int k = 0; k < (int)locals.size(); k++) locals[k].index = k;
  if (locals.size() == 1) {   // back to one person: their menus go back on the shared stack (under whatever is open)
    auto& mine = locals[0].screens;
    for (size_t k = 0; k < mine.size(); k++) app.screens.insert(app.screens.begin() + k, std::move(mine[k]));
    mine.clear();
    locals[0].src.pads.clear(); locals[0].src.kb = true;
  }
  layoutLocals(); routeInput();
  app.toast(name + " left", "");
}

void GameSession::freeLocalUi(LocalPlayer& lp) {
  if (!lp.ui) return;
  ImGuiContext* keep = ImGui::GetCurrentContext();
  ImGui::DestroyContext((ImGuiContext*)lp.ui);
  lp.ui = nullptr;
  if (keep && keep != (ImGuiContext*)lp.ui) ImGui::SetCurrentContext(keep);
}

// ------------------------------------------------------------------------------------------------ drawing
void GameSession::splitRender() {
  int dw, dh; SDL_GL_GetDrawableSize(app.window, &dw, &dh);
  float sx = (float)dw / std::max(1, app.width), sy = (float)dh / std::max(1, app.height);
  struct Lend { GameSession& s; explicit Lend(GameSession& g) : s(g) { s.lendRenderDist(true); } ~Lend() { s.lendRenderDist(false); } } lend(*this);
  glEnable(GL_SCISSOR_TEST);
  for (int i = 0; i < (int)locals.size(); i++) {
    enterLocal(i);
    const LocalView& v = locals[i].view;
    int px = (int)std::lround(v.x * sx), pw = std::max(1, (int)std::lround(v.w * sx));
    int ph = std::max(1, (int)std::lround(v.h * sy)), py = dh - (int)std::lround(v.y * sy) - ph;
    glViewport(px, py, pw, ph); glScissor(px, py, pw, ph);
    glClearColor(0.05f, 0.04f, 0.035f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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
    glEnable(GL_SCISSOR_TEST); glScissor(px, py, pw, ph); glViewport(px, py, pw, ph);   // (a layer may have changed them)
    leaveLocal();
  }
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, dw, dh);
  selectLocal(0);
}

static const u32 PLAYER_COL[4] = {0xffb07a, 0x8fe3ff, 0xb6f06a, 0xe7a6ff};   // .phead.p1 .. p4

// hud.js nameTags: the other people's names over their heads, and who you are in the corner of your own viewport
void GameSession::drawNameTags() {
  using namespace ui;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  float W = (float)app.width, H = (float)app.height, z = Z();
  for (int j = 0; j < (int)locals.size(); j++) {
    Player* p = j == cur ? player : locals[j].player;
    if (j == cur || !p || p->dead || p->realm != player->realm) continue;
    glm::dvec3 at(p->pos.x, p->pos.y + 2.15, p->pos.z);
    if (glm::distance(at, frame.cam.pos) > 64) continue;
    vec4 c = frame.cam.viewProj * vec4(frame.cam.rel(at), 1.f);
    if (c.w <= 0.05f) continue;
    float x = (c.x / c.w * 0.5f + 0.5f) * W, y = (1.f - (c.y / c.w * 0.5f + 0.5f)) * H;
    if (x < -50 || x > W + 50 || y < -20 || y > H + 20) continue;
    ImVec2 ts = textSize(F_MID, p->name.c_str());
    ImVec2 q(x - ts.x * 0.5f, y - ts.y);
    dl->AddRectFilled(q - ImVec2(4 * z, 1 * z), q + ImVec2(ts.x + 4 * z, ts.y + 1 * z), IM_COL32(0, 0, 0, 120), 3 * z);
    text(dl, F_MID, q, col::rgb(PLAYER_COL[j & 3]), p->name.c_str());
  }
  if (player) {
    ImVec2 ts = textSize(F_MID, player->name.c_str());
    ImVec2 q(W - ts.x - 10 * z, 6 * z);
    dl->AddRectFilled(q - ImVec2(5 * z, 2 * z), q + ImVec2(ts.x + 5 * z, ts.y + 2 * z), IM_COL32(0, 0, 0, 120), 3 * z);
    text(dl, F_MID, q, col::rgb(PLAYER_COL[cur & 3]), player->name.c_str());
  }
}

// Everybody's HUD and menus. Each viewport has an ImGui context of its own: the same HUD and screen code runs with
// App::width / height set to the viewport, the interface zoomed down to fit, that person's menus swapped in and their
// controller feeding the navigation; the draw data is then shifted to the viewport's place in the window.
void GameSession::splitUi() {
  ImGuiContext* mainCtx = ImGui::GetCurrentContext();
  ImGuiIO& mio = ImGui::GetIO();
  const ImVec2 full = mio.DisplaySize, fbScale = mio.DisplayFramebufferScale;
  const ImGuiConfigFlags cfg = mio.ConfigFlags; const ImGuiBackendFlags bf = mio.BackendFlags;
  ImGuiStyle style = ImGui::GetStyle();
  // the keyboard + mouse person gets the main context's keys and mouse
  std::vector<char> keyDown(ImGuiKey_GamepadStart, 0);
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++) keyDown[k] = ImGui::IsKeyDown((ImGuiKey)k);
  ImVec2 mpos = mio.MousePos; bool mdown[5]; for (int b = 0; b < 5; b++) mdown[b] = mio.MouseDown[b];
  float wheel = mio.MouseWheel, wheelH = mio.MouseWheelH;
  std::string typed = app.input.typed;
  const int sw = app.width, sh = app.height; const float su = app.uiScale;
  bool wantText = false;
  for (int i = 0; i < (int)locals.size(); i++) {
    LocalPlayer& L = locals[i];
    if (!L.ui) { L.ui = ImGui::CreateContext(mio.Fonts); ImGui::SetCurrentContext(mainCtx); }
    const LocalView v = L.view;
    bool kb = L.src.kb;
    ImGui::SetCurrentContext((ImGuiContext*)L.ui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags = kb ? cfg : (cfg & ~ImGuiConfigFlags_NavEnableKeyboard);
    io.BackendFlags = bf;
    io.DisplaySize = ImVec2((float)v.w, (float)v.h); io.DisplayFramebufferScale = fbScale;
    io.DeltaTime = std::max(1e-4f, (float)app.dt);
    ImGui::GetStyle() = style;
    if (kb) {
      io.AddMousePosEvent(mpos.x, mpos.y);
      for (int b = 0; b < 5; b++) io.AddMouseButtonEvent(b, mdown[b]);
      io.AddMouseWheelEvent(wheelH, wheel);
      for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++) io.AddKeyEvent((ImGuiKey)k, keyDown[k] != 0);
      if (!typed.empty()) io.AddInputCharactersUTF8(typed.c_str());
    } else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    enterLocal(i);
    app.width = v.w; app.height = v.h;
    float k = clampv(std::min(v.w / 900.f, v.h / 600.f) / std::max(0.1f, ui::baseZoom()), 0.35f, 1.f);   // a smaller interface in a smaller viewport
    ui::localZoom(k); app.uiScale = ui::Z();
    ui::feedNav(app);
    ImGui::NewFrame();
    ImGui::PushFont(app.fontUi);
    drawHud();
    drawNameTags();
    for (size_t j = 0; j < app.screens.size(); j++) if (j + 1 == app.screens.size() || app.screens[j + 1]->showsWorld()) app.screens[j]->draw(app);
    uiAfterScreens(app);
    ImGui::GetForegroundDrawList()->AddRect(ImVec2(0.5f, 0.5f), ImVec2(v.w - 0.5f, v.h - 0.5f), IM_COL32(0, 0, 0, 200));
    wantText = wantText || io.WantTextInput;
    ImGui::PopFont();
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    dd->DisplayPos = ImVec2((float)-v.x, (float)-v.y); dd->DisplaySize = full;   // layout (0,0) lands at the viewport's corner
    ui::localZoom(1);
    app.width = sw; app.height = sh; app.uiScale = su;
    leaveLocal();
    ImGui::SetCurrentContext(mainCtx);
    ImGui_ImplOpenGL3_RenderDrawData(dd);
  }
  uiWantsText = wantText;
}

} // namespace dq
