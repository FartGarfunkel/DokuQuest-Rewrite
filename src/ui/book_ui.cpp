// ui/book_ui.cpp — see book_ui.h
#include <chrono>
#include <cmath>
#include "ui/book_ui.h"
#include "render/journal_model.h"
#include <string_view>
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "ui/book.h"
#include "ui/cinematic.h"
#include "client/app.h"
#include "client/session.h"
#include "render/gl.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <cfloat>
#include <stb_image_write.h>
#include "common/fs.h"
#include "sim/player.h"
#include "sim/level.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace dq {
namespace {
ImGuiContext* g_ctx = nullptr;
GLuint g_fbo = 0, g_tex = 0; int g_w = 0, g_h = 0;
vec2 g_lo{0}, g_hi{0}; bool g_rect = false; bool g_drawing = false;
ImVec2 g_spA, g_spB; bool g_spread = false, g_hadSpread = false; vec4 g_crop(0, 0, 1, 1);
std::vector<vec4> g_ribs, g_ribsShown;   // the ribbons drawn this frame (picture pixels), and last frame's in crop space
vec4 g_spreadUV(0, 0, 1, 1);

void ensureTarget(int w, int h) {
  if (g_fbo && w == g_w && h == g_h) return;
  if (!g_fbo) { glGenFramebuffers(1, &g_fbo); glGenTextures(1, &g_tex); }
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g_w = w; g_h = h;
}
}  // namespace

// light pages: the menus written for dark panels (light grey text: the options, the panels, the editors) in ink on the
// paper instead — every light, greyish letter that isn't sitting on a dark box of its own (a button, a slot, a field)
// turns to the book's ink; bright coloured ones darken. Letters already in ink (the book screens) are left as they are.
void inkOnPaper(ImDrawData* dd, ImVec2 white) {
  struct Box { float x0, y0, x1, y1; };
  auto lum = [](ImU32 c) { return ((c & 255) * 0.299f + (c >> 8 & 255) * 0.587f + (c >> 16 & 255) * 0.114f) / 255.f; };
  auto sat = [](ImU32 c) { const int r = c & 255, g = c >> 8 & 255, b = c >> 16 & 255; const int mx = std::max({r, g, b}), mn = std::min({r, g, b}); return mx ? (mx - mn) / (float)mx : 0.f; };
  const ImU32 INK = IM_COL32(0x3b, 0x2a, 0x1a, 255), MUTED = IM_COL32(0x80, 0x6b, 0x56, 255);
  for (ImDrawList* dl : dd->CmdLists) {
    std::vector<Box> dark;
    ImDrawVert* V = dl->VtxBuffer.Data;
    const ImDrawIdx* I = dl->IdxBuffer.Data;
    for (const ImDrawCmd& cmd : dl->CmdBuffer) {
      if (cmd.UserCallback) continue;
      if (cmd.GetTexID() != ImGui::GetIO().Fonts->TexID) {   // a picture (a button's sprite, an icon): letters on it keep their colour
        for (unsigned t = 0; t + 2 < cmd.ElemCount; t += 3) {
          const ImVec2 p0 = V[cmd.VtxOffset + I[cmd.IdxOffset + t]].pos, p1 = V[cmd.VtxOffset + I[cmd.IdxOffset + t + 1]].pos, p2 = V[cmd.VtxOffset + I[cmd.IdxOffset + t + 2]].pos;
          Box bx{std::min({p0.x, p1.x, p2.x}), std::min({p0.y, p1.y, p2.y}), std::max({p0.x, p1.x, p2.x}), std::max({p0.y, p1.y, p2.y})};
          const float ar = (bx.x1 - bx.x0) * (bx.y1 - bx.y0);
          if (bx.x1 - bx.x0 > 6 && bx.y1 - bx.y0 > 6 && ar < dd->DisplaySize.x * dd->DisplaySize.y * 0.08f) dark.push_back(bx);   // (not the book's own pages)
        }
        continue;
      }
      for (unsigned t = 0; t + 2 < cmd.ElemCount; t += 3) {
        ImDrawVert* v[3] = {&V[cmd.VtxOffset + I[cmd.IdxOffset + t]], &V[cmd.VtxOffset + I[cmd.IdxOffset + t + 1]], &V[cmd.VtxOffset + I[cmd.IdxOffset + t + 2]]};
        const bool fill = v[0]->uv.x == white.x && v[0]->uv.y == white.y && v[1]->uv.x == white.x && v[2]->uv.x == white.x;
        if (fill) {
          const ImU32 c = v[0]->col;
          if ((c >> 24) > 150 && lum(c) < 0.35f) {
            Box b{std::min({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), std::min({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}), std::max({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), std::max({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y})};
            if (b.x1 - b.x0 > 6 && b.y1 - b.y0 > 6) dark.push_back(b);
          }
          continue;
        }
        // a letter (a glyph's two triangles: recolour each vertex once — a light colour is only ever changed to a dark one)
        const float cx = (v[0]->pos.x + v[1]->pos.x + v[2]->pos.x) / 3, cy = (v[0]->pos.y + v[1]->pos.y + v[2]->pos.y) / 3;
        bool onDark = false;
        for (const Box& b : dark) if (cx >= b.x0 && cx <= b.x1 && cy >= b.y0 && cy <= b.y1) { onDark = true; break; }
        if (onDark) continue;
        for (ImDrawVert* q : v) {
          const ImU32 c = q->col; const float L = lum(c);
          if (L < 0.5f) continue;
          const ImU32 a = c & IM_COL32_A_MASK;
          if (sat(c) < 0.22f) q->col = ((L > 0.78f ? INK : MUTED) & ~IM_COL32_A_MASK) | a;
          else q->col = IM_COL32((int)((c & 255) * 0.5f), (int)((c >> 8 & 255) * 0.5f), (int)((c >> 16 & 255) * 0.5f), c >> 24);
        }
      }
    }
  }
}

bool isBookScreen(const Screen* s) {
  if (!s) return false;
  const std::string id = s->id();
  // what isn't a page of the journal: the title, chat (typed at the bottom of your view), the station scenes (worked in
  // the world), photo mode and laying out the HUD, death, loading
  static const char* NOT[] = {"title", "chat", "photo_mode", "hud_edit", "death", "loading", "connecting", "sleep", "held_book", "lectern_scene", "npc-talk"};   // (npc-talk: the villagers' dialogue box stays as it always was, over the world)   // (a book read in your hands, or on a lectern: that book itself)
  for (const char* n : NOT) if (id == n) return false;
  if (id.rfind("cinematic", 0) == 0) return false;
  return s->wantsMouse();
}

bool bookUiOn(App& a) {
  static const bool devSelf = std::getenv("DQ_DEV_BOOKSELF") != nullptr;   // (dev: the book in third person too, sent as to the others, to see it as they do)
  if (!a.session || a.session->split() || (a.session->viewMode != 0 && !devSelf) || !a.session->player || a.session->player->dead) return false;
  if (!a.settings.extra.value("bookMenus", true)) return false;
  return isBookScreen(a.topScreen());
}

namespace {
u64 g_stationAsked = 0;  int g_stationPad = -1;   // (the frame the scene last asked for the bag)
bool g_quad = false; vec2 g_q[4]; bool g_overBook = false;
// where in the quad (0..1, 0..1) a window point is (a few Newton steps on the bilinear map); false: outside
struct BookPlane { mat4 proj{1}, frame{1}; float w = 0, h = 0, top = 0, tilt = 0; bool on = false; } g_plane;
// a window point onto the held book's pages: where on the crop (0..1, 0..1) the ray through it meets them
bool planeUV(vec2 p, vec2& uv) {
  if (!g_plane.on) return false;
  App& a = app();
  const vec2 ndc(p.x / std::max(1, a.width) * 2 - 1, 1 - p.y / std::max(1, a.height) * 2);
  const vec4 sp = g_spreadUV;
  for (int sd : {1, -1}) {
    const mat4 F = glm::rotate(g_plane.frame, -sd * g_plane.tilt, vec3(0, 1, 0));   // (that half, turned up from the spine)
    const mat4 inv = glm::inverse(g_plane.proj * F);
    vec4 a0 = inv * vec4(ndc, -1, 1), a1 = inv * vec4(ndc, 1, 1);
    a0 /= a0.w; a1 /= a1.w;
    if (std::fabs(a1.z - a0.z) < 1e-7f) continue;
    const float t = (g_plane.top - a0.z) / (a1.z - a0.z);
    const vec3 q = vec3(a0) + (vec3(a1) - vec3(a0)) * t;
    if (q.x * sd < 0) continue;   // (the other half's)
    const float su = (sp.x + sp.z) * 0.5f + q.x / g_plane.w * (sp.z - sp.x), sv = (sp.y + sp.w) * 0.5f - q.y / g_plane.h * (sp.w - sp.y);
    uv = vec2(su, sv);
    return su >= 0 && su <= 1 && sv >= 0 && sv <= 1;
  }
  return false;
}
bool quadUV(vec2 p, vec2& uv) {
  if (g_plane.on) return planeUV(p, uv);
  uv = vec2(0.5f);
  for (int it = 0; it < 12; it++) {
    const vec2 P = (1 - uv.y) * ((1 - uv.x) * g_q[0] + uv.x * g_q[1]) + uv.y * ((1 - uv.x) * g_q[3] + uv.x * g_q[2]);
    const vec2 du = (1 - uv.y) * (g_q[1] - g_q[0]) + uv.y * (g_q[2] - g_q[3]), dv = (1 - uv.x) * (g_q[3] - g_q[0]) + uv.x * (g_q[2] - g_q[1]);
    const float det = du.x * dv.y - du.y * dv.x;
    if (std::fabs(det) < 1e-6f) return false;
    const vec2 e = p - P;
    uv += vec2((e.x * dv.y - e.y * dv.x) / det, (du.x * e.y - du.y * e.x) / det);
  }
  return uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
}
}  // namespace
void bookUiSetBook(const mat4& proj, const mat4& frame, float w, float h, float top, float tilt) {
  g_plane.proj = proj; g_plane.frame = frame; g_plane.w = w; g_plane.h = h; g_plane.top = top; g_plane.tilt = tilt; g_plane.on = true;
}
void bookUiStationBag(int padCur) { g_stationAsked = app().frame; g_stationPad = padCur; }
bool bookUiMouseOverBook() { return g_overBook; }
namespace { std::string g_screenTip; }
bool bookUiScreenTip(const std::string& t) { if (!g_drawing) return false; g_screenTip = t; return true; }
void bookUiDrawScreenTip() {
  if (g_screenTip.empty()) return;
  const float z = ui::Z();
  const ImVec2 m = ImGui::GetIO().MousePos, s = ui::textSize(ui::F_MID, g_screenTip.c_str());
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  fg->AddRectFilled(ImVec2(m.x + 14 * z, m.y - 4 * z), ImVec2(m.x + 26 * z + s.x, m.y + 4 * z + s.y), ui::col::rgb(0x1a120c, 235), 3 * z);
  ui::text(fg, ui::F_MID, ImVec2(m.x + 20 * z, m.y), ui::col::rgb(0xf0e0b8), g_screenTip.c_str());
  g_screenTip.clear();
}
void bookUiDebugQuad(ImDrawList* dl) { if (!g_quad || !std::getenv("DQ_DEV_QUAD")) return; for (int i = 0; i < 4; i++) dl->AddLine(ImVec2(g_q[i].x, g_q[i].y), ImVec2(g_q[(i + 1) % 4].x, g_q[(i + 1) % 4].y), IM_COL32(255, 0, 255, 255), 2); }
bool bookUiPointOverBook(vec2 p) { vec2 uv; return g_quad && app().frame <= g_stationAsked + 2 && quadUV(p, uv); }
void bookUiSetQuad(const vec2 q[4]) {
  for (int i = 0; i < 4; i++) g_q[i] = q[i];
  g_quad = true;
}
bool bookUiStationOn(App& a) {
  if (!a.session || a.session->split() || a.session->viewMode != 0 || !a.session->player || a.session->player->dead) return false;
  if (!a.settings.extra.value("bookMenus", true) || cinematicLeaning()) return false;
  const Screen* t = a.topScreen();
  return t && std::string(t->id()).rfind("cinematic", 0) == 0 && a.frame <= g_stationAsked + 2;
}

bool bookUiDark(App& a) { return a.settings.extra.value("bookDark", true); }
unsigned bookUiTexture() { return g_tex; }
bool bookUiDrawing() { return g_drawing; }
void bookUiSpread(ImVec2 a, ImVec2 b) { if (g_drawing) { g_spA = a; g_spB = b; g_spread = true; } }
bool bookUiCrop(vec4& uv) { uv = g_crop; return g_hadSpread; }
void bookUiRibbon(ImVec2 a, ImVec2 b) { if (g_drawing) g_ribs.push_back(vec4(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y))); }
bool bookUiJournal(JournalPic& p) {
  p.tex = g_tex; p.texRect = vec4(g_crop.x, 1 - g_crop.y, g_crop.z, 1 - g_crop.w);
  p.spread = g_spreadUV; p.ribbons = g_ribsShown;
  return g_hadSpread && g_tex;
}
bool bookUiSnapshot(JournalPic& p) {
  if (!g_tex || !g_fbo || !g_hadSpread) return false;
  static GLuint prevTex = 0, prevFbo = 0; static int pw = 0, ph = 0;
  GLint prevDraw = 0, prevRead = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
  if (!prevFbo) { glGenFramebuffers(1, &prevFbo); glGenTextures(1, &prevTex); }
  if (pw != g_w || ph != g_h) {
    glBindTexture(GL_TEXTURE_2D, prevTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_w, g_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prevTex, 0);
    pw = g_w; ph = g_h;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
  glBlitFramebuffer(0, 0, g_w, g_h, 0, 0, g_w, g_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
  p.tex = prevTex; p.texRect = vec4(g_crop.x, 1 - g_crop.y, g_crop.z, 1 - g_crop.w);
  p.spread = g_spreadUV; p.ribbons = g_ribsShown;
  return true;
}
std::string bookUiLayout() { JournalPic p; p.spread = g_spreadUV; p.ribbons = g_ribsShown; return journalLayoutString(p); }
static vec2 g_size(1);
vec2 bookUiSize() { return g_size; }
void bookUiSetRect(vec2 lo, vec2 hi) { g_lo = lo; g_hi = hi; g_rect = hi.x - lo.x > 8 && hi.y - lo.y > 8; }

void drawStationBag(App& a, int padCur);
void bookUiRender(App& a, bool station) {
  ImGuiContext* mainCtx = ImGui::GetCurrentContext();
  ImGuiIO& mio = ImGui::GetIO();
  ui::setBookTheme(bookUiDark(a));   // (the drawn book's paper as the option says, from the first page on)
  const ImVec2 fbScale = mio.DisplayFramebufferScale;
  const ImGuiConfigFlags cfg = mio.ConfigFlags; const ImGuiBackendFlags bf = mio.BackendFlags;
  ImGuiStyle style = ImGui::GetStyle();
  std::vector<char> keyDown(ImGuiKey_GamepadStart, 0);
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++) keyDown[k] = ImGui::IsKeyDown((ImGuiKey)k);
  const ImVec2 mpos = mio.MousePos; bool mdown[5]; for (int b = 0; b < 5; b++) mdown[b] = mio.MouseDown[b];
  const float wheel = mio.MouseWheel, wheelH = mio.MouseWheelH;
  const std::string typed = a.input.typed;
  // the pages' size on the window (last frame's), a picture that size: the interface drawn at its own scale on them
  // (the picture drawn larger than the pages by how little of it the drawn book took last time: its text as sharp as the screen's)
  vec2 sz = g_rect ? g_hi - g_lo : vec2(a.height * 1.0f, a.height * 0.84f);
  if (station) sz = vec2(a.height * 1.0f, a.height * 0.84f);
  {   // (held steady: the picture's size follows the book's size on the window, which follows the picture — it went round
      // and round by a pixel or two a frame and the whole book shook. Only a real change of size moves it now)
    static vec2 kept(0);
    sz = glm::round(sz / 16.f) * 16.f;
    if (kept.x <= 0 || std::fabs(sz.x - kept.x) > kept.x * 0.06f || std::fabs(sz.y - kept.y) > kept.y * 0.06f) kept = sz;
    sz = kept;
  }   // (the book in the off hand: one steady size — following its size on the window, the picture changed size every frame and the book shimmered)
  sz = glm::min(sz, vec2(a.width * 2.5f, a.height * 2.5f));   // (the whole picture: the book shown is the part of it the screen's book takes)
  const int W = std::max(64, (int)std::lround(sz.x)), H = std::max(64, (int)std::lround(sz.y));
  ensureTarget((int)std::lround(W * fbScale.x), (int)std::lround(H * fbScale.y));
  g_size = vec2((float)W, (float)H);
  if (!g_ctx) { g_ctx = ImGui::CreateContext(mio.Fonts); ImGui::SetCurrentContext(mainCtx); }
  ImGui::SetCurrentContext(g_ctx);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr; io.LogFilename = nullptr;
  io.ConfigFlags = cfg; io.BackendFlags = bf;
  io.DisplaySize = ImVec2((float)W, (float)H); io.DisplayFramebufferScale = fbScale;
  io.DeltaTime = std::max(1e-4f, (float)a.dt);
  io.MouseDrawCursor = !station || std::getenv("DQ_DEV_QUAD");   // (at a station the window keeps its own pointer) (the pointer drawn on the pages: you see it in the book, and so do the others, moving your things)
  ImGui::GetStyle() = style;
  {   // the paper shows through the menus' panels: a window's own background is the page
    ImGuiStyle& st = ImGui::GetStyle();
    for (ImGuiCol c : {ImGuiCol_WindowBg, ImGuiCol_ChildBg}) st.Colors[c].w *= 0.25f;
  }
  // the mouse where you point on the pages
  g_overBook = false;
  if (station) {   // (a book at an angle: cast onto its pages (bookUiSetBook); off it, the scene has the mouse)
    vec2 uv;
    if (g_quad && mpos.x > -FLT_MAX / 2 && quadUV(vec2(mpos.x, mpos.y), uv)) {
      g_overBook = true;
      io.AddMousePosEvent((g_crop.x + uv.x * (g_crop.z - g_crop.x)) * W, (g_crop.y + uv.y * (g_crop.w - g_crop.y)) * H);
    } else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  } else if (g_rect && mpos.x > -FLT_MAX / 2) {
    const vec2 u = (vec2(mpos.x, mpos.y) - g_lo) / (g_hi - g_lo);
    io.AddMousePosEvent(u.x * W, u.y * H);
  } else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  for (int b = 0; b < 5; b++) io.AddMouseButtonEvent(b, mdown[b]);
  io.AddMouseWheelEvent(wheelH, wheel);
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++) io.AddKeyEvent((ImGuiKey)k, keyDown[k] != 0);
  if (!typed.empty()) io.AddInputCharactersUTF8(typed.c_str());
  const int sw = a.width, sh = a.height; const float su = a.uiScale;
  a.width = W; a.height = H;
  const float k = clampv(std::min(W / 900.f, H / 600.f) / std::max(0.1f, ui::baseZoom()), 0.35f, 1.f);
  ui::localZoom(k); a.uiScale = ui::Z();
  ui::feedNav(a);
  ImGui::NewFrame();
  ImGui::PushFont(a.fontUi);
  g_drawing = true; g_spread = false; g_ribs.clear();
  if (station) drawStationBag(a, g_stationPad);   // (the scene's own UI is drawn over the world as ever)
  else {
  for (size_t i = 0; i < a.screens.size(); i++) if (i + 1 == a.screens.size() || a.screens[i + 1]->showsWorld()) a.screens[i]->draw(a);
  uiAfterScreens(a);
  }
  g_drawing = false;
  {   // (the menus dim the world behind them on the background layer: on the pages that's only the paper, left clean)
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // a menu with no drawn book of its own (the pause menu, options...): the journal's spread under it all the same
    if (!g_spread) { g_drawing = true; ui::drawBookBackdrop(bg, (float)W, (float)H); g_drawing = false; }
  }
  g_hadSpread = g_spread;
  if (g_spread) {   // the drawn book and its bookmark ribbons either side
    const float bw = g_spB.x - g_spA.x, bh = g_spB.y - g_spA.y;
    // the crop: the book and its ribbons (a little room round them)
    vec4 px(g_spA.x - bw * 0.01f, g_spA.y - bh * 0.01f, g_spB.x + bw * 0.01f, g_spB.y + bh * 0.01f);
    for (const vec4& r : g_ribs) px = vec4(std::min(px.x, r.x - 2), std::min(px.y, r.y - 2), std::max(px.z, r.z + 2), std::max(px.w, r.w + 2));
    px = vec4(std::max(0.f, px.x), std::max(0.f, px.y), std::min((float)W, px.z), std::min((float)H, px.w));
    g_crop = vec4(px.x / W, px.y / H, px.z / W, px.w / H);
    auto rel = [&](vec4 r) { return vec4((r.x - px.x) / (px.z - px.x), (r.y - px.y) / (px.w - px.y), (r.z - px.x) / (px.z - px.x), (r.w - px.y) / (px.w - px.y)); };
    g_spreadUV = rel(vec4(g_spA.x, g_spA.y, g_spB.x, g_spB.y));
    g_ribsShown.clear();
    for (const vec4& r : g_ribs) g_ribsShown.push_back(rel(r));
    // the arrows in the bottom corners: more pages (the next menu, the last one) — Q / E or LB / RB turn to them
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImU32 ink = bookUiDark(a) ? IM_COL32(230, 205, 150, 220) : IM_COL32(90, 50, 20, 220);
    const float s = bh * 0.035f, y = g_spB.y - bh * 0.06f;
    for (int d : {-1, 1}) {
      if (station) break;   // (the bag has no other pages)
      const float x = d > 0 ? g_spB.x - bw * 0.05f : g_spA.x + bw * 0.05f;
      const ImVec2 m = ImGui::GetIO().MousePos;
      const bool hot = std::fabs(m.x - x) < s * 1.6f && std::fabs(m.y - y) < s * 1.6f;
      fg->AddTriangleFilled(ImVec2(x + d * s, y), ImVec2(x - d * s * 0.6f, y - s * 0.8f), ImVec2(x - d * s * 0.6f, y + s * 0.8f), hot ? IM_COL32(255, 230, 150, 255) : ink);
      if (hot && ImGui::IsMouseClicked(0)) menuTabStep(d);   // (the corner arrows turn the page: the next / last menu)
    }
  } else { g_crop = vec4(0, 0, 1, 1); g_spreadUV = vec4(0, 0, 1, 1); g_ribsShown.clear(); }
  const bool wantText = io.WantTextInput;
  ImGui::PopFont();
  ImGui::Render();
  ImDrawData* dd = ImGui::GetDrawData();
  if (!bookUiDark(a)) inkOnPaper(dd, ImGui::GetIO().Fonts->TexUvWhitePixel);
  ui::localZoom(1);
  a.width = sw; a.height = sh; a.uiScale = su;
  ImGui::SetCurrentContext(mainCtx);
  if (a.session && !station) a.session->uiWantsText = wantText;   // (each frame: it used to stay on once a field on the pages had been typed in, and the game took no keys after)
  // onto the pages: the paper first (dark or light, as the book screens), the interface over it
  GLint prevFbo = 0, vp[4]; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glViewport(0, 0, g_w, g_h);
  if (g_spread) glClearColor(0, 0, 0, 0);   // (outside the drawn book: nothing, cut away)
  else if (bookUiDark(a)) glClearColor(0.11f, 0.10f, 0.12f, 1); else glClearColor(0.93f, 0.90f, 0.80f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(dd);
  {   // the fold down the middle and the pages' edges: drawn last, over the interface, as the paper's own shading
    ImGui::SetCurrentContext(g_ctx);
    ImDrawList fold(ImGui::GetDrawListSharedData());
    fold._ResetForNewFrame(); fold.PushClipRectFullScreen(); fold.PushTextureID(ImGui::GetIO().Fonts->TexID);
    if (!g_spread) {   // (a drawn book has its own fold)
    const float mid = W * 0.5f, dk = bookUiDark(a) ? 1.f : 0.6f;
    for (int i = 0; i < 6; i++) { const float w = (6 - i) * 3.f; fold.AddRectFilled(ImVec2(mid - w, 0), ImVec2(mid + w, (float)H), IM_COL32(0, 0, 0, (int)((10 + i * 9) * dk))); }
    for (int i = 0; i < 4; i++) { const float e = i * 2.f; fold.AddRect(ImVec2(e, e), ImVec2(W - e, H - e), IM_COL32(0, 0, 0, (int)((60 - i * 14) * dk)), 0, 0, 2); }
    ImDrawData d; d.Valid = true; d.DisplayPos = ImVec2(0, 0); d.DisplaySize = ImVec2((float)W, (float)H); d.FramebufferScale = fbScale;
    d.AddDrawList(&fold);
    ImGui::SetCurrentContext(mainCtx);
    ImGui_ImplOpenGL3_RenderDrawData(&d);
    } else ImGui::SetCurrentContext(mainCtx);
  }
  // a few times a second, with others about: a small copy of the pages for them (the book in your hands, seen by them)
  bool someoneNear = std::getenv("DQ_DEV_BOOKSELF") != nullptr;
  if (a.session && a.session->player && a.session->level && a.session->level->net)
    for (auto& e : a.session->level->entities()) if (e.get() != a.session->player && !e->removed && e->isPlayer() && e->realm == a.session->player->realm && glm::distance(e->pos, a.session->player->pos) < 16) { someoneNear = true; break; }
  if (someoneNear) {   // (only with someone near enough to read it over your shoulder)
    static double lastPic = -1;
    const double now = a.session->frame.time;
    if (now - lastPic > 0.15) {
      lastPic = now;
      const int x0 = (int)(g_crop.x * g_w), y0 = (int)((1 - g_crop.w) * g_h), cw = std::max(1, (int)((g_crop.z - g_crop.x) * g_w)), ch = std::max(1, (int)((g_crop.w - g_crop.y) * g_h));
      std::vector<u8> px((size_t)cw * ch * 4);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(x0, y0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      // (up to 800 wide, averaged down so the writing stays legible, flipped upright; outside the book a key colour the
      // others cut away: render/char_layer.cpp)
      const int ow = std::min(800, cw), oh = std::max(8, ow * ch / cw);
      std::vector<u8> out((size_t)ow * oh * 3);
      for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) {
        const int sx0 = x * cw / ow, sx1 = std::max(sx0 + 1, (x + 1) * cw / ow), sy0 = (oh - 1 - y) * ch / oh, sy1 = std::max(sy0 + 1, (oh - y) * ch / oh);
        float acc[4] = {0, 0, 0, 0}; int n = 0;
        for (int sy = sy0; sy < sy1 && sy < ch; sy++) for (int sx = sx0; sx < sx1 && sx < cw; sx++) {
          const u8* s = &px[((size_t)sy * cw + sx) * 4];
          const float al = s[3] / 255.f;
          for (int c = 0; c < 3; c++) acc[c] += s[c] * al;
          acc[3] += al; n++;
        }
        u8* d = &out[((size_t)y * ow + x) * 3];
        const float al = n ? acc[3] / n : 0;
        if (al < 0.5f) { d[0] = 255; d[1] = 0; d[2] = 255; }
        else for (int c = 0; c < 3; c++) d[c] = (u8)std::clamp(acc[c] / std::max(1e-4f, acc[3]), 0.f, 255.f);
      }
      static size_t lastHash = 0; static std::string lastLay;
      size_t hsh = std::hash<std::string_view>()(std::string_view((const char*)out.data(), out.size()));
      const std::string lay = bookUiLayout();
      if (hsh == lastHash && lay == lastLay && !a.session->player->bookPic.empty()) goto sent;
      lastHash = hsh; lastLay = lay;
      {
      std::vector<u8> jpg;
      stbi_write_jpg_to_func([](void* ctx, void* data, int n) { auto* v = (std::vector<u8>*)ctx; v->insert(v->end(), (u8*)data, (u8*)data + n); }, &jpg, ow, oh, 3, out.data(), 72);
      Player& me = *a.session->player;
      me.bookPic = base64Encode(jpg.data(), jpg.size()); me.bookLay = lay; me.bookPicV++; me.bookPicAt = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      }
      sent:;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
}

} // namespace dq
