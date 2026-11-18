// ui/theme.cpp — styles.css in ImGui (see theme.h).
#include "ui/book_ui.h"
#include "core/tool_parts.h"
#include "common/ui_scale.h"
#include "ui/theme.h"
#include "ui/osk.h"
#include "ui/screen.h"
#include "ui/paged.h"
#include "ui/book.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/icons.h"
#include "assets/font_files.h"
#include "assets/gui_textures.h"
#include "audio/audio.h"
#include "render/gl.h"
#include "render/instruments.h"
#include "core/item_glint.h"
#include <imgui_internal.h>
#include "imgui_hooks/nav_hooks.h"
#include <imgui_stdlib.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

namespace dq {
namespace ui {

namespace {
struct FontSet { ImFont* f[F_COUNT]{}; float zoom = 0, dpi = 1; std::string uiFile, titleFile; float textK = 1; };
FontSet g_fonts;
const float FONT_PX[F_COUNT] = {12.5f, 15, 13, 17, 20, 24, 32, 64, 84, 12};
unsigned g_spriteTex = 0;
float g_zoom = 1;

float computeZoom(const App& a) {
  // Monitor tier (2K 125%, 3K 150%, 4K 200%), capped so the HUD still fits this window.
  int edge = std::max(0, std::min(a.width, a.height));
  if (a.window) {
    int idx = SDL_GetWindowDisplayIndex(a.window);
    if (idx < 0) idx = 0;
    SDL_Rect b{};
    if (SDL_GetDisplayBounds(idx, &b) == 0 && b.w > 0 && b.h > 0) edge = std::min(b.w, b.h);
  }
  return uiZoomFromSetting(a.settings.guiScale, autoUiZoom(a.width, a.height, edge));
}

void buildFonts(App& a, float zoom) {
  ImGuiIO& io = ImGui::GetIO();
  float dpi = 1; int dw, dh; SDL_GL_GetDrawableSize(a.window, &dw, &dh); if (a.width > 0) dpi = std::max(1.f, (float)dw / a.width);
  // the chosen fonts (Options → User Interface: assets/font_files.h), each sized by its own em so text keeps its size
  const std::string uiF = fontPath(a.settings.extra.value("fontUi", std::string(DEFAULT_UI_FONT)), DEFAULT_UI_FONT).string();
  const std::string tF = fontPath(a.settings.extra.value("fontTitle", std::string(DEFAULT_TITLE_FONT)), DEFAULT_TITLE_FONT).string();
  const float textK = textScale(a.settings.extra);   // (Options → Text Size)
  if (g_fonts.zoom == zoom && g_fonts.dpi == dpi && g_fonts.f[0] && g_fonts.uiFile == uiF && g_fonts.titleFile == tF && g_fonts.textK == textK) return;
  bool had = g_fonts.f[0] != nullptr || io.Fonts->IsBuilt();
  if (had) ImGui_ImplOpenGL3_DestroyFontsTexture();
  io.Fonts->Clear();
  ImFontConfig cfg; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2030, 0x203A, 0x2190, 0x21FF, 0x2212, 0x2212, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0};
  static const ImWchar ascii[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};
  bool haveUi = fs::exists(uiF), haveT = fs::exists(tF);
  const float emUi = haveUi ? fontEmRatio(uiF) : 1.2f, emT = haveT ? fontEmRatio(tF) : 1.f;
  for (int i = 0; i < F_COUNT; i++) {
    bool display = i == F_H3 || i == F_DISP || i == F_TITLE || i == F_HUGE;
    // ImGui sizes a font by its ascent+descent, CSS by its em: Pixelify Sans is 1.2 em tall, Jersey 15 1.0 em (each
    // chosen font measured the same way)
    float size = std::round(FONT_PX[i] * zoom * dpi * textK * (display ? emT : emUi));
    if (i == F_MONO) { ImFontConfig c2; c2.SizePixels = std::max(13.f, std::round(13 * zoom * dpi)); g_fonts.f[i] = io.Fonts->AddFontDefault(&c2); continue; }
    const std::string& file = display ? tF : uiF;
    bool have = display ? haveT : haveUi;
    g_fonts.f[i] = have ? io.Fonts->AddFontFromFileTTF(file.c_str(), size, &cfg, display ? ascii : ranges) : io.Fonts->AddFontDefault();
    // a chosen font missing some characters (a middle dot, arrows, dashes): those come from the game's own font
    const std::string own = (assetsDir() / "fonts" / (display ? DEFAULT_TITLE_FONT : DEFAULT_UI_FONT)).string();
    if (have && fs::path(file) != fs::path(own) && fs::exists(own)) {
      ImFontConfig m = cfg; m.MergeMode = true;
      const float ownSize = std::round(FONT_PX[i] * zoom * dpi * textK * fontEmRatio(own));
      io.Fonts->AddFontFromFileTTF(own.c_str(), ownSize, &m, display ? ascii : ranges);
    }
  }
  // symbols the pixel font doesn't have (♥ ▲ ▼ arrows ✕ ⚔ ✦ ◯ ▢ △), painted as glyphs of their own
  struct Sym { ImWchar cp; const char* rows[9]; };
  static const Sym SYMS[] = {
    {0x2665, {".###.###.", "#########", "#########", "#########", "#########", ".#######.", "..#####..", "...###...", "....#...."}},
    {0x25B2, {"....#....", "...###...", "...###...", "..#####..", "..#####..", ".#######.", ".#######.", "#########", "........."}},
    {0x25BC, {".........", "#########", ".#######.", ".#######.", "..#####..", "..#####..", "...###...", "...###...", "....#...."}},
    {0x2192, {".........", ".....#...", ".....##..", "########.", "#########", "########.", ".....##..", ".....#...", "........."}},
    {0x2190, {".........", "...#.....", "..##.....", ".########", "#########", ".########", "..##.....", "...#.....", "........."}},
    {0x2191, {"....#....", "...###...", "..#####..", ".#######.", "...###...", "...###...", "...###...", "...###...", "...###..."}},
    {0x2193, {"...###...", "...###...", "...###...", "...###...", "...###...", ".#######.", "..#####..", "...###...", "....#...."}},
    {0x2715, {"#.......#", "##.....##", ".##...##.", "..##.##..", "...###...", "..##.##..", ".##...##.", "##.....##", "#.......#"}},
    {0x2694, {"#.......#", ".#.....#.", "..#...#..", "...#.#...", "....#....", "...#.#...", ".##...##.", ".#.....#.", "#.......#"}},
    {0x2726, {"....#....", "....#....", "...###...", "..#####..", "#########", "..#####..", "...###...", "....#....", "....#...."}},
    {0x25EF, {"..#####..", ".#.....#.", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", ".#.....#.", "..#####.."}},
    {0x25A2, {".#######.", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", ".#######."}},
    {0x25B3, {"....#....", "...#.#...", "...#.#...", "..#...#..", "..#...#..", ".#.....#.", ".#.....#.", "#########", "........."}},
  };
  struct Pending { int rect; const Sym* sym; int px; };
  std::vector<Pending> pend;
  for (int i = 0; i < F_COUNT; i++) {
    bool display = i == F_H3 || i == F_DISP || i == F_TITLE || i == F_HUGE;
    if (display || i == F_MONO || !haveUi) continue;
    float size = std::round(FONT_PX[i] * zoom * dpi * textK * emUi);
    int px = std::max(5, (int)std::round(size * 0.52f));
    for (auto& sy : SYMS) pend.push_back({io.Fonts->AddCustomRectFontGlyph(g_fonts.f[i], sy.cp, px, px, px + std::round(size * 0.1f), ImVec2(std::round(size * 0.05f), std::round(size * 0.77f) - px)), &sy, px});
  }
  io.Fonts->Build();
  {
    unsigned char* tex = nullptr; int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&tex, &tw, &th);
    for (auto& pd : pend) {
      const ImFontAtlasCustomRect* r = io.Fonts->GetCustomRectByIndex(pd.rect);
      if (!r || !r->IsPacked()) continue;
      for (int y = 0; y < pd.px; y++) for (int x = 0; x < pd.px; x++) {
        bool on = pd.sym->rows[y * 9 / pd.px][x * 9 / pd.px] == '#';
        unsigned char* d = tex + ((size_t)(r->Y + y) * tw + r->X + x) * 4;
        d[0] = d[1] = d[2] = 255; d[3] = on ? 255 : 0;
      }
    }
  }
  io.FontGlobalScale = 1.f / dpi;
  if (had) ImGui_ImplOpenGL3_CreateFontsTexture();
  g_fonts.zoom = zoom; g_fonts.dpi = dpi; g_fonts.uiFile = uiF; g_fonts.titleFile = tF; g_fonts.textK = textK;
  // the App's font slots point at these from now on
  a.fontUi = g_fonts.f[F_BODY]; a.fontSmall = g_fonts.f[F_MID]; a.fontUiBig = g_fonts.f[F_BIG]; a.fontTitle = g_fonts.f[F_TITLE];
  a.uiScale = zoom;
}

// 9×9 sprites from hud.js installHudSprites, plus the CSS filters (poison, chill, wither, glowing shields)
void buildSprites() {
  const char* heart[9] = {".kkk.kkk.", "kRRRkRRRk", "kRWRRRRRk", "kRRRRRRRk", "kRRRRRRRk", ".kRRRRRk.", "..kRRRk..", "...kRk...", "....k...."};
  const char* bubble[9] = {"..kkkkk..", ".kBBBBBk.", "kBWWBBBBk", "kBWBBBBBk", "kBBBBBBBk", "kBBBBBBBk", "kBBBBBBBk", ".kBBBBBk.", "..kkkkk.."};
  const char* pop[9] = {".........", "..k...k..", ".k.B.B.k.", "...B.B...", ".kB...Bk.", "...B.B...", ".k.B.B.k.", "..k...k..", "........."};
  const char* shield[9] = {"kkkkkkkkk", "kSSSSLLLk", "kSWSSLLLk", "kSSSSLLLk", "kSSSSLLLk", ".kSSSLLk.", "..kSSLk..", "...kSk...", "....k...."};
  auto pal = [](char c) -> u32 { switch (c) { case 'k': return 0x140808; case 'R': return 0xe5484d; case 'W': return 0xffb3b0; case 'D': return 0x3a1a1a;
    case 'S': return 0xcfd6de; case 'L': return 0x90a0b0; case 'B': return 0x5aa6f0; default: return 0xffffffffu; } };
  std::vector<u8> px((size_t)9 * 9 * SP_COUNT * 4, 0);
  // CSS filter helpers: hue-rotate / saturate / brightness / grayscale on a colour
  auto filter = [](vec3 c, float hue, float sat, float bright, float gray) {
    float a = hue * PI / 180.f, cs = std::cos(a), sn = std::sin(a);
    mat3 m(0.213f + cs * 0.787f - sn * 0.213f, 0.213f - cs * 0.213f + sn * 0.143f, 0.213f - cs * 0.213f - sn * 0.787f,
           0.715f - cs * 0.715f - sn * 0.715f, 0.715f + cs * 0.285f + sn * 0.140f, 0.715f - cs * 0.715f + sn * 0.715f,
           0.072f - cs * 0.072f + sn * 0.928f, 0.072f - cs * 0.072f - sn * 0.283f, 0.072f + cs * 0.928f + sn * 0.072f);
    c = glm::clamp(m * c, 0.f, 1.f);
    float l = glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f));
    c = glm::mix(vec3(l), c, sat);
    l = glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f));
    c = glm::mix(c, vec3(l), gray);
    return glm::clamp(c * bright, 0.f, 1.f);
  };
  auto put = [&](int sprite, const char* const* rows, auto mapChar, int tint) {
    for (int y = 0; y < 9; y++) for (int x = 0; x < 9; x++) {
      char ch = mapChar(rows[y][x], x);
      u32 c = pal(ch); if (c == 0xffffffffu) continue;
      vec3 v(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f);
      if (tint == HT_POISON) v = filter(v, 100, 0.9f, 0.95f, 0);
      else if (tint == HT_CHILL) v = filter(v, 195, 0.7f, 1.15f, 0);
      else if (tint == HT_WITHER) v = filter(v, 0, 1, 0.35f, 1);
      else if (tint == 100) v = filter(v * 1.25f, 160, 1, 1, 0);
      u8* d = &px[(((size_t)sprite * 9 + y) * 9 + x) * 4];
      d[0] = (u8)(v.r * 255); d[1] = (u8)(v.g * 255); d[2] = (u8)(v.b * 255); d[3] = 255;
    }
  };
  auto same = [](char c, int) { return c; };
  auto half = [](char c, int x) { return (c == 'R' || c == 'W') && x > 4 ? 'D' : c; };
  auto empty = [](char c, int) { return (c == 'R' || c == 'W') ? 'D' : c; };
  for (int t : {HT_NONE, HT_POISON, HT_CHILL, HT_WITHER}) { put(t + 0, heart, same, t); put(t + 1, heart, half, t); put(t + 2, heart, empty, t); }
  put(SP_SHIELD, shield, same, 0); put(SP_SHIELD_GLOW, shield, same, 100);
  put(SP_BUBBLE, bubble, same, 0); put(SP_BUBBLE_POP, pop, same, 0);
  g_spriteTex = makeTexture2D(9, 9 * SP_COUNT, px.data(), true, false, false);
}
}

ImFont* font(FontId f) { return g_fonts.f[f] ? g_fonts.f[f] : ImGui::GetFont(); }
float fontPx(FontId f) { return FONT_PX[f]; }
float Z() { return g_zoom; }
bool navVisible() { ImGuiContext& g = *GImGui; return g.NavCursorVisible && g.NavHighlightItemUnderNav; }
ImU32 withAlpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | ((ImU32)clampv((int)(((c >> 24) & 255) * a), 0, 255) << 24); }
void playClick() { if (app().audio) app().audio->play("click"); }

// ---- tooltips -----------------------------------------------------------------------------------------------
static LastTip g_lastTip;

// ---- the controller's button hints --------------------------------------------------------------------------
std::string padHintName(int k) {
  const std::string st = app().input.padStyle();
  static const char* const XB[16] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "View", "Menu", "L3", "R3", "D-pad", "D-pad", "D-pad", "D-pad"};
  static const char* const PS[16] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Create", "Options", "L3", "R3", "D-pad", "D-pad", "D-pad", "D-pad"};
  // (Nintendo layout: the swap is already applied, so the confirm button is the one printed A)
  static const char* const NI[16] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "-", "+", "L3", "R3", "D-pad", "D-pad", "D-pad", "D-pad"};
  if (k < 0 || k > 15) return "";
  return st == "playstation" ? PS[k] : st == "nintendo" ? NI[k] : XB[k];
}
namespace { struct Hint { int k1, k2; std::string what; }; std::vector<Hint> g_hints; unsigned long long g_hintFrame = 0; }
void padHint(int k1, int k2, const char* what) {
  if (g_hintFrame != app().frame) { g_hints.clear(); g_hintFrame = app().frame; }
  for (auto& h : g_hints) if (h.k1 == k1 && h.k2 == k2) return;
  g_hints.push_back({k1, k2, what});
}
void drawPadHints(App& a) {
  Screen* s = a.topScreen();
  if (!s || !a.input.usingPad() || a.input.setupActive || oskTextActive()) return;
  if (s->id() == std::string("photo_mode") || s->id() == std::string("chat")) return;   // (their own lines)
  std::vector<Hint> hs = {{0, -1, "Select"}, {1, -1, s->id() == std::string("title") || s->id() == std::string("death") ? "" : "Back"}};
  if (g_hintFrame + 1 >= a.frame) for (auto& h : g_hints) hs.push_back(h);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float z = baseZoom(), padX = 6 * z, gap = 14 * z, h = textSize(F_SMALL, "Ag").y + 6 * z;
  std::vector<std::pair<std::string, std::string>> items;
  float total = 0;
  for (auto& x : hs) {
    if (x.what.empty()) continue;
    std::string b = padHintName(x.k1) + (x.k2 >= 0 ? "/" + padHintName(x.k2) : "");
    total += textSize(F_SMALL, b.c_str()).x + padX * 2 + 4 * z + textSize(F_SMALL, x.what.c_str()).x + gap;
    items.push_back({b, x.what});
  }
  if (items.empty()) return;
  // (bottom right, in the corner: the screens keep their own buttons along the bottom middle)
  float x = std::round(a.width - (total - gap) - 18 * z), y = std::round(a.height - h - 8 * z);
  dl->AddRectFilled(ImVec2(x - 10 * z, y - 4 * z), ImVec2(x + total - gap + 10 * z, y + h + 4 * z), IM_COL32(0, 0, 0, 150), 6 * z);
  for (auto& [b, what] : items) {
    const float bw = textSize(F_SMALL, b.c_str()).x + padX * 2;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + h), col::rgb(0xe0e0e0), 4 * z);
    text(dl, F_SMALL, ImVec2(x + padX, y + 3 * z), col::ink, b.c_str());
    x += bw + 4 * z;
    text(dl, F_SMALL, ImVec2(x, y + 3 * z), col::white, what.c_str());
    x += textSize(F_SMALL, what.c_str()).x + gap;
  }
}
bool tipsFollowPad() { return app().input.usingPad(); }
bool navItemRect(ImVec2& a, ImVec2& b) {
  ImGuiContext& g = *GImGui;
  ImGuiWindow* w = g.NavWindow;
  if (!w || g.NavId == 0 || !g.NavCursorVisible) return false;
  ImRect r = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g.NavLayer]);
  if (r.GetWidth() <= 0 || r.GetHeight() <= 0) return false;
  a = r.Min; b = r.Max;
  return true;
}
ImVec2 tipPlace(ImVec2 size, const ImVec2* ra, const ImVec2* rb) {
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  const float z = Z(), m = 4 * z;
  ImVec2 a, b, p;
  bool item = false;
  if (tipsFollowPad()) {
    if (ra && rb) { a = *ra; b = *rb; item = true; }
    else item = navItemRect(a, b);
  }
  if (item) {
    p = ImVec2(b.x + 8 * z, a.y);
    if (p.x + size.x > ds.x - m) p.x = a.x - 8 * z - size.x;   // no room on the right: its left
    if (p.x < m) { p.x = a.x; p.y = b.y + 6 * z; }              // nor there: under it
  } else {
    const ImVec2 mp = ImGui::GetIO().MousePos;
    p = ImVec2(mp.x + 14 * z, mp.y + 10 * z);
  }
  p.x = std::max(m, std::min(p.x, ds.x - size.x - m));
  p.y = std::max(m, std::min(p.y, ds.y - size.y - m));
  p = ImVec2(std::round(p.x), std::round(p.y));
  g_lastTip = {app().frame, item, a, b, p, size};
  return p;
}
const LastTip& lastTooltip() { return g_lastTip; }
std::vector<std::string> navLabels(bool withRect) {
  std::vector<std::string> out;
  for (auto& [c, s] : dqNavSeen()) if (s.frame >= 0 && s.frame + 1 >= (long long)app().frame && s.shown) out.push_back(withRect && s.rectFrame + 1 >= (long long)app().frame ? s.label + " @" + std::to_string((int)s.minX) + "," + std::to_string((int)s.minY) : s.label);
  return out;
}
void tipNextAt(const ImVec2* ra, const ImVec2* rb) {
  if (!tipsFollowPad()) return;   // (the mouse: ImGui's own placement)
  ImVec2 a, b;
  if (ra && rb) { a = *ra; b = *rb; } else if (!navItemRect(a, b)) return;
  // the size isn't known yet: right of the item (left when it is on the right of the display), top aligned (bottom
  // aligned when it is low down)
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  const bool left = (a.x + b.x) * 0.5f > ds.x * 0.62f, up = (a.y + b.y) * 0.5f > ds.y * 0.62f;
  // on the right of the display there's no room beside it: under it (over it when low down), right edges lined up, so
  // the row it belongs to stays readable (put on its left, it covered the row's own label)
  const ImVec2 at = left ? ImVec2(b.x, up ? a.y - 4 * Z() : b.y + 4 * Z()) : ImVec2(b.x + 8 * Z(), up ? b.y : a.y);
  g_lastTip = {app().frame, true, a, b, at, ImVec2(0, 0)};
  ImGui::SetNextWindowPos(at, ImGuiCond_Always, ImVec2(left ? 1.f : 0.f, up ? 1.f : 0.f));
}
void setTooltip(const char* fmt, ...) {
  va_list args; va_start(args, fmt);
  tipNextAt();
  ImGui::SetTooltipV(fmt, args);
  va_end(args);
}

void init(App& a) {
  g_zoom = computeZoom(a);
  DQ_LOG("interface %d%%", (int)std::lround(g_zoom * 100.f));
  buildFonts(a, g_zoom);
  buildSprites();
  ImGuiStyle& s = ImGui::GetStyle();
  // square and grey like the paged screens and the launcher's Options tab (black fields with a grey frame, white checks)
  s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 0; s.PopupRounding = 0; s.GrabRounding = 0; s.ScrollbarRounding = 0;
  s.WindowBorderSize = 0; s.FrameBorderSize = 2; s.PopupBorderSize = 2; s.ChildBorderSize = 0;
  s.ItemSpacing = ImVec2(8, 8); s.ScrollbarSize = 10; s.GrabMinSize = 14;
  ImVec4* c = s.Colors;
  auto C = [](u32 hex, float a = 1) { return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); };
  c[ImGuiCol_Text] = C(0xe0e0e0); c[ImGuiCol_TextDisabled] = C(0xa0a0a0);
  c[ImGuiCol_WindowBg] = C(0x101010, 0.94f); c[ImGuiCol_ChildBg] = C(0, 0); c[ImGuiCol_PopupBg] = C(0x101010, 0.98f);
  c[ImGuiCol_Border] = C(0xa0a0a0); c[ImGuiCol_BorderShadow] = C(0, 0);
  c[ImGuiCol_FrameBg] = C(0x000000); c[ImGuiCol_FrameBgHovered] = C(0x141414); c[ImGuiCol_FrameBgActive] = C(0x1c1c1c);
  c[ImGuiCol_CheckMark] = C(0xffffff); c[ImGuiCol_SliderGrab] = C(0xc6c6c6); c[ImGuiCol_SliderGrabActive] = C(0xffffff);
  c[ImGuiCol_Button] = C(0x6f6f6f); c[ImGuiCol_ButtonHovered] = C(0x7e88bf); c[ImGuiCol_ButtonActive] = C(0x5a5a5a);
  c[ImGuiCol_Header] = C(0x404040); c[ImGuiCol_HeaderHovered] = C(0x5a5f80); c[ImGuiCol_HeaderActive] = C(0x6e6e8e);
  c[ImGuiCol_ScrollbarBg] = C(0x000000, 0.6f); c[ImGuiCol_ScrollbarGrab] = C(0x808080); c[ImGuiCol_ScrollbarGrabHovered] = C(0xa0a0a0); c[ImGuiCol_ScrollbarGrabActive] = C(0xc0c0c0);
  c[ImGuiCol_Separator] = C(0x000000); c[ImGuiCol_NavCursor] = C(0xffffff); c[ImGuiCol_TextSelectedBg] = C(0x3050ff, 0.45f);
  c[ImGuiCol_ModalWindowDimBg] = C(0x101010, 0.6f);
  // pads are fed by beginFrame (standard layout, pad maps, Nintendo swap)
  ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
}

void feedNav(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  GImGui->TestEngineHookItems = true; dqNavFrame() = (long long)a.frame;   // (the item hooks: which item the highlight is on, imgui_hooks/nav_hooks.cpp)
  Input& in = a.input;
  if (in.padCount() == 0 || in.setupActive) { if (in.padCount() == 0) io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad; return; }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  // The ONLY place pads reach ImGui (every button in the standard layout, the Nintendo swap already applied, and the
  // left stick). There used to be a second feeder (ui_main.cpp) with a different stick threshold: with the stick resting
  // between the two, each frame queued a press and a release of the same key, ImGui trickles such pairs over frames, and
  // its input queue grew a step per frame. Every later press then arrived later and later: the controller "stopped
  // working" in the menus, the pause screen most of all (tools/playtests/pause_pad.json).
  static const ImGuiKey MAP[16] = {ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft, ImGuiKey_GamepadFaceUp,
    ImGuiKey_GamepadL1, ImGuiKey_GamepadR1, ImGuiKey_GamepadL2, ImGuiKey_GamepadR2, ImGuiKey_GamepadBack, ImGuiKey_GamepadStart,
    ImGuiKey_GamepadL3, ImGuiKey_GamepadR3, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight};
  for (int k = 0; k < 16; k++) io.AddKeyEvent(MAP[k], in.padButtonDown(k));
  vec2 l(0);
  for (auto& p : in.pads()) {
    if (in.selected() >= 0) { auto& ids = in.sourceList()[in.selected()].pads; if (std::find(ids.begin(), ids.end(), p.id) == ids.end()) continue; }
    if (glm::length(p.lstick) > glm::length(l)) l = p.lstick;
  }
  auto ana = [&](ImGuiKey k, float v) { io.AddKeyAnalogEvent(k, v > 0.35f, std::max(0.f, v)); };
  ana(ImGuiKey_GamepadLStickLeft, -l.x); ana(ImGuiKey_GamepadLStickRight, l.x); ana(ImGuiKey_GamepadLStickUp, -l.y); ana(ImGuiKey_GamepadLStickDown, l.y);
  // the highlight's repeat while a direction is held, as Input::endFrame steps the game's own grids: the first step at
  // once, the next after 0.35 s, then ten a second quickening to twenty (ImGui moves at 0.72 x the delay and 0.8 x the
  // rate it is given). The keyboard keeps ImGui's quicker typing repeat.
  static unsigned long long heldFrame = 0; static float held = 0;
  if (heldFrame != a.frame) {   // (once a frame, whichever context asks first)
    heldFrame = a.frame;
    const bool dir = in.padButtonDown(12) || in.padButtonDown(13) || in.padButtonDown(14) || in.padButtonDown(15) || std::max(std::fabs(l.x), std::fabs(l.y)) > 0.35f;
    held = dir ? held + (float)a.dt : 0;
  }
  if (in.usingPad()) {
    io.KeyRepeatDelay = 0.35f / 0.72f;
    io.KeyRepeatRate = (held < 1.2f ? 0.1f : held < 2.4f ? 0.07f : 0.05f) / 0.8f;
  } else { io.KeyRepeatDelay = 0.275f; io.KeyRepeatRate = 0.05f; }
}

static float g_baseZoom = 1;
float baseZoom() { return g_baseZoom; }
void localZoom(float k) {
  g_zoom = g_baseZoom * k;
  ImGui::GetIO().FontGlobalScale = k / std::max(1.f, g_fonts.dpi);
}

void beginFrame(App& a) {
  float z = std::round(computeZoom(a) * 20.f) / 20.f;
  if (z != g_zoom || !g_fonts.f[0]) g_zoom = z;
  buildFonts(a, g_zoom);   // (rebuilds only when the size or the chosen fonts changed)
  g_baseZoom = g_zoom;
  ImGuiIO& io = ImGui::GetIO();
  a.input.textActive = io.WantTextInput || (a.session && a.session->uiWantsText);
  feedNav(a);
}

// ---- text ---------------------------------------------------------------------------------------------
static float sizeOf(FontId f, float scale) { return font(f)->FontSize * ImGui::GetIO().FontGlobalScale * scale; }
ImVec2 textSize(FontId f, const char* t, float wrap, float scale) { return font(f)->CalcTextSizeA(sizeOf(f, scale), FLT_MAX, wrap, t); }
void text(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, float wrap, float scale) { dl->AddText(font(f), sizeOf(f, scale), ImVec2(std::round(p.x), std::round(p.y)), c, t, nullptr, wrap); }
void textShadow(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, ImVec2 off, ImU32 sh, float wrap, float scale) {
  text(dl, f, p + off, sh, t, wrap, scale); text(dl, f, p, c, t, wrap, scale);
}
void textOutline(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, float w, ImU32 o, float scale) {
  for (ImVec2 d : {ImVec2(w, 0), ImVec2(-w, 0), ImVec2(0, w), ImVec2(0, -w)}) text(dl, f, p + d, o, t, 0, scale);
  text(dl, f, p, c, t, 0, scale);
}
void textCentered(ImDrawList* dl, FontId f, ImVec2 center, ImU32 c, const char* t, ImU32 shadow, ImVec2 off) {
  ImVec2 s = textSize(f, t); ImVec2 p(center.x - s.x * 0.5f, center.y - s.y * 0.5f);
  if (shadow) text(dl, f, p + off * Z(), shadow, t);
  text(dl, f, p, c, t);
}
void label(FontId f, ImU32 c, const char* t, float wrap) {
  ImGui::PushFont(font(f)); ImGui::PushStyleColor(ImGuiCol_Text, c);
  if (wrap >= 0) { ImGui::PushTextWrapPos(wrap > 0 ? ImGui::GetCursorPosX() + wrap : 0.f); ImGui::TextUnformatted(t); ImGui::PopTextWrapPos(); }
  else ImGui::TextUnformatted(t);
  ImGui::PopStyleColor(); ImGui::PopFont();
}
std::string ellipsize(FontId f, const std::string& s, float maxW) {
  if (textSize(f, s.c_str()).x <= maxW) return s;
  std::string t = s;
  while (!t.empty() && textSize(f, (t + "\xE2\x80\xA6").c_str()).x > maxW) { t.pop_back(); while (!t.empty() && ((u8)t.back() & 0xC0) == 0x80) t.pop_back(); }
  return t + "\xE2\x80\xA6";
}

// ---- backgrounds ----------------------------------------------------------------------------------------
void drawTiled(ImDrawList* dl, const char* tileName, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint) {
  int t = reg().tile(tileName);
  IconRef r = t >= 0 ? itemIcons().tile(t) : IconRef();
  if (!r.valid() || tilePx < 2) return;
  dl->PushClipRect(a, b, true);
  for (float y = a.y; y < b.y; y += tilePx) for (float x = a.x; x < b.x; x += tilePx)
    dl->AddImage((ImTextureID)(intptr_t)r.tex, ImVec2(x, y), ImVec2(x + tilePx, y + tilePx), ImVec2(r.uv0.x, r.uv0.y), ImVec2(r.uv1.x, r.uv1.y), tint);
  dl->PopClipRect();
}
void radialVignette(ImDrawList* dl, ImVec2 c, ImVec2 R, float inner, ImU32 outer, float reach) {
  const int N = 64;
  ImU32 clear = outer & 0x00FFFFFF;
  auto ring = [&](float f0, float f1, ImU32 c0, ImU32 c1) {
    dl->PrimReserve(N * 6, N * 4);
    for (int i = 0; i < N; i++) {
      float a0 = (float)i / N * TAU, a1 = (float)(i + 1) / N * TAU;
      ImVec2 d0(std::cos(a0) * R.x, std::sin(a0) * R.y), d1(std::cos(a1) * R.x, std::sin(a1) * R.y);
      ImDrawIdx b = (ImDrawIdx)dl->_VtxCurrentIdx;
      ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
      dl->PrimWriteVtx(c + d0 * f0, uv, c0); dl->PrimWriteVtx(c + d1 * f0, uv, c0);
      dl->PrimWriteVtx(c + d1 * f1, uv, c1); dl->PrimWriteVtx(c + d0 * f1, uv, c1);
      dl->PrimWriteIdx(b); dl->PrimWriteIdx(b + 1); dl->PrimWriteIdx(b + 2);
      dl->PrimWriteIdx(b); dl->PrimWriteIdx(b + 2); dl->PrimWriteIdx(b + 3);
    }
  };
  ring(inner, 1, clear, outer);
  ring(1, reach, outer, outer);
}
void bevel(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 tl, ImU32 br, float w) {
  dl->AddRectFilled(a, ImVec2(b.x, a.y + w), tl); dl->AddRectFilled(a, ImVec2(a.x + w, b.y), tl);
  dl->AddRectFilled(ImVec2(a.x, b.y - w), b, br); dl->AddRectFilled(ImVec2(b.x - w, a.y), b, br);
}

void btnFace(ImDrawList* dl, ImVec2 a, ImVec2 b, bool hot, bool held, float al) {
  const float z = Z();
  if (held) { a.y += 1 * z; b.y += 1 * z; }   // :active { translateY(1px) }
  dl->AddRectFilledMultiColor(a, b, withAlpha(col::slotHi, al), withAlpha(col::slotHi, al), withAlpha(col::slot, al), withAlpha(col::slot, al));
  dl->AddRectFilled(ImVec2(a.x, b.y - (held ? 1 : 3) * z), b, IM_COL32(0, 0, 0, (int)(89 * al)));   // inset 0 -3px 0 rgba(0,0,0,.35)
  dl->AddRectFilled(a, ImVec2(b.x, a.y + 2 * z), IM_COL32(255, 255, 255, (int)(15 * al)));        // inset 0 2px 0 rgba(255,255,255,.06)
  dl->AddRect(a + ImVec2(1, 1) * z, b - ImVec2(1, 1) * z, withAlpha(hot ? col::lineHi : col::line, al), 3 * z, 0, 2 * z);   // border: 2px, radius 4
}

void dropShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float offY, float blur, float alpha) {
  if (bookUiDrawing()) return;   // (on the journal's pages nothing floats over the paper: no shadow under a panel)
  const int N = 12;
  const int la = (int)std::lround(255.f * (1.f - std::pow(1.f - alpha, 1.f / N)));   // N layers that add up to `alpha` in the middle
  for (int j = 0; j < N; j++) {
    float e = blur - 2 * blur * (j + 0.5f) / N;   // +blur (outermost) .. -blur (innermost): the fade across the edge
    ImVec2 p0(a.x - e, a.y + offY - e), p1(b.x + e, b.y + offY + e);
    if (p1.x <= p0.x || p1.y <= p0.y) continue;
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, la), std::max(0.f, e));
  }
}

// the lines of `t` as ImGui would wrap them: [begin, end) pairs
static void wrapLines(FontId f, const char* t, float wrap, float scale, std::vector<std::pair<const char*, const char*>>& out) {
  ImFont* fo = font(f); const float k = sizeOf(f, scale) / fo->FontSize;
  const char* end = t + std::strlen(t);
  for (const char* s = t;;) {
    const char* nl = (const char*)std::memchr(s, '\n', (size_t)(end - s));
    const char* le = nl ? nl : end; const char* p = s;
    if (p == le) out.push_back({p, p});
    while (p < le) {
      const char* w = wrap > 0 ? fo->CalcWordWrapPositionA(k, p, le, wrap) : le;
      if (w <= p) { w = p + 1; while (w < le && ((u8)*w & 0xC0) == 0x80) w++; }
      out.push_back({p, w});
      p = w;
      while (p < le && *p == ' ') p++;   // the space a line broke at
    }
    if (!nl) break;
    s = nl + 1;
  }
}
float textLines(ImDrawList* dl, FontId f, ImVec2 pos, ImU32 c, const char* t, float wrap, float lineH, float scale) {
  std::vector<std::pair<const char*, const char*>> L; wrapLines(f, t, wrap, scale, L);
  const float sz = sizeOf(f, scale), off = (lineH - sz) * 0.5f;
  for (size_t i = 0; i < L.size(); i++)
    if (L[i].second > L[i].first) dl->AddText(font(f), sz, ImVec2(std::round(pos.x), std::round(pos.y + i * lineH + off)), c, L[i].first, L[i].second);
  return (float)L.size() * lineH;
}
ImVec2 textLinesSize(FontId f, const char* t, float wrap, float lineH, float scale) {
  std::vector<std::pair<const char*, const char*>> L; wrapLines(f, t, wrap, scale, L);
  const float sz = sizeOf(f, scale); float w = 0;
  for (auto& l : L) w = std::max(w, font(f)->CalcTextSizeA(sz, FLT_MAX, 0, l.first, l.second).x);
  return ImVec2(w, (float)L.size() * lineH);
}
float baselineOf(FontId f, float scale) { ImFont* fo = font(f); return fo->Ascent * sizeOf(f, scale) / fo->FontSize; }

// ---- buttons --------------------------------------------------------------------------------------------
static bool itemHot() { return ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible()); }

// every menu button is the launcher's grey button (ui/paged.cpp classicButton), so the game's menus match the
// launcher's Options tab and the paged screens
bool greyButton(const char* lbl, ImVec2 size, bool disabled) { return classicButton(lbl, size, disabled, F_MID); }

bool button(const char* lbl, BtnStyle st, bool tiny, float width, bool disabled) {
  (void)st;   // one kind of button: the grey one (the style only ever changed its colour)
  float z = Z();
  std::string t = lbl; size_t hh = t.find("##"); if (hh != std::string::npos) t.resize(hh);
  const FontId f = F_MID;
  ImVec2 ts = textSize(f, t.c_str());
  float h = (tiny ? 30 : 40) * z, padX = (tiny ? 12 : 20) * z;
  float w = width > 0 ? width : width < 0 ? ImGui::GetContentRegionAvail().x : ts.x + padX * 2 + 4 * z;
  return classicButton(lbl, ImVec2(w, h), disabled, f);
}

bool checkbox(const char* lbl, bool* v, const char* desc) {
  ImGui::PushFont(font(F_BODY));
  bool ch = ImGui::Checkbox(lbl, v);
  ImGui::PopFont();
  if (desc && *desc) { ImGui::Indent(ImGui::GetFrameHeight() + 8 * Z()); label(F_SMALL, col::dim, desc, 0); ImGui::Unindent(ImGui::GetFrameHeight() + 8 * Z()); }
  if (ch) playClick();
  return ch;
}

bool slider(const char* id, float* v, float mn, float mx, float step, const char* shown, float width) {
  // the slider (ui/paged.cpp greySlider): a dark track, a button-faced handle, the value written on it
  const float z = Z(), w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
  return greySlider(id, v, mn, mx, step, shown ? std::string(shown) : std::string(), ImVec2(w, 20 * z), nullptr);
}

bool combo(const char* id, int* cur, const std::vector<std::string>& items, float width) {
  if (width != 0) ImGui::SetNextItemWidth(width);
  bool ch = false;
  const char* preview = *cur >= 0 && *cur < (int)items.size() ? items[*cur].c_str() : "";
  if (ImGui::BeginCombo(id, preview)) {
    for (int i = 0; i < (int)items.size(); i++) {
      bool sel = i == *cur;
      if (ImGui::Selectable(items[i].c_str(), sel)) { *cur = i; ch = true; }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  if (ch) playClick();
  return ch;
}

bool inputText(const char* id, std::string* s, const char* hint, size_t maxLen, float width, int flags) {
  ImGui::SetNextItemWidth(width);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * Z(), 10 * Z()));
  bool ch = hint ? ImGui::InputTextWithHint(id, hint, s, flags) : ImGui::InputText(id, s, flags);
  ImGui::PopStyleVar();
  if (ImGui::IsItemActive()) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255), 0, 0, 2 * Z());
  if (maxLen && s->size() > maxLen) s->resize(maxLen);
  return ch;
}

void padScrollHere() {
  float d = app().input.padScroll();
  if (d == 0) return;
  ImGuiContext& g = *GImGui;
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || g.NavId == 0 || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    ImGui::SetScrollY(ImGui::GetScrollY() + d);
}

// ---- panels ---------------------------------------------------------------------------------------------
bool Panel::begin(const char* id, const char* title, float maxW, const char* closeLabel, float fixedH) {
  App& a = app();
  float z = Z(), pad = 16 * z;
  closed = false;
  book_ = bookUiDrawing(); left_ = false; id_ = id;
  if (book_) {   // the open journal: the title heading the left page, the close button under it
    ui::bookBackdropPages((float)a.width, (float)a.height, bL, bR, bS);
    width = bS.x;
    ImGui::SetNextWindowPos(bL); ImGui::SetNextWindowSize(bS);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * z, 8 * z));
    ImGui::Begin((std::string(id) + "L").c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(font(F_BODY));
    dl = ImGui::GetWindowDrawList(); pos = bL; size = bS;
    ui::bookPageTitle(a, title);
    if (closeLabel) {
      std::string lbl = std::string(closeLabel) + "##close";
      if (button(lbl.c_str(), BTN_GHOST, false, std::min(bS.x, textSize(F_BODY, closeLabel).x + 40 * z))) closed = true;
      ImGui::Dummy(ImVec2(1, 4 * z));
    }
    headH = ImGui::GetCursorScreenPos().y - bL.y;
    left_ = true;
    return true;
  }
  // backdrop (.panel-screen)
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)a.width, (float)a.height), IM_COL32(16, 16, 16, 160));   // the dim over the world
  width = std::min(maxW * z, a.width - pad * 2);
  float maxH = a.height - pad * 2;
  ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (fixedH > 0) ImGui::SetNextWindowSize(ImVec2(width, std::min(fixedH * z, maxH)));
  else ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, maxH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * z, 8 * z));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  bool open = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | (fixedH > 0 ? 0 : ImGuiWindowFlags_AlwaysAutoResize));
  ImGui::PopStyleColor();
  ImGui::PushFont(font(F_BODY));
  dl = ImGui::GetWindowDrawList();
  pos = ImGui::GetWindowPos(); size = ImGui::GetWindowSize();
  hasFooter = false; footH = 0;
  // the launcher's look (its Options tab, the paged screens): the darker brown dirt for the body, a lighter dirt band for
  // the title with the name centred in white, black lines between, a black frame
  const float U = 2 * z, tile = 32 * U;
  ImVec2 b0 = pos, b1 = pos + ImVec2(width, std::max(size.y, 60 * z));
  classicDirt(dl, b0, b1, tile, 0.62f);
  headH = 32 * z + 24 * z;
  ImVec2 h1 = b0 + ImVec2(width, headH);
  classicDirt(dl, b0, h1, tile, 0.3f);
  dl->AddLine(ImVec2(b0.x, h1.y), ImVec2(h1.x, h1.y), IM_COL32(0, 0, 0, 255), U);
  dl->AddRect(b0, b1, IM_COL32(0, 0, 0, 255), 0, 0, U);
  ImVec2 ts = textSize(F_BIG, title);
  textShadow(dl, F_BIG, ImVec2(std::floor(b0.x + (width - ts.x) * 0.5f), std::floor(b0.y + (headH - ts.y) * 0.5f)), IM_COL32(255, 255, 255, 255), title, ImVec2(2, 2) * z, IM_COL32(63, 63, 63, 255));
  if (closeLabel) {
    float bw = textSize(F_BODY, closeLabel).x + 40 * z;
    ImGui::SetCursorScreenPos(ImVec2(b0.x + width - 16 * z - bw, b0.y + (headH - 44 * z) * 0.5f));
    std::string lbl = std::string(closeLabel) + "##close";
    if (button(lbl.c_str(), BTN_GHOST, false, bw)) closed = true;
  }
  ImGui::SetCursorScreenPos(ImVec2(b0.x, b0.y + headH));
  return open;
}
void Panel::beginBody(float footerH) {
  App& a = app();
  float z = Z();
  footH = footerH * z;
  if (book_) {
    if (!split_ && left_) beginRightPage();
    const float h = (left_ ? bL.y + bS.y : bR.y + bS.y) - ImGui::GetCursorScreenPos().y - footH;
    ImGui::BeginChild("##body", ImVec2(bS.x, std::max(40.f, h)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    bodyMin = ImGui::GetWindowPos(); bodyMax = bodyMin + ImGui::GetWindowSize();
    padScrollHere();
    return;
  }
  float maxBody = a.height - 32 * z - headH - footH;
  if (footerH > 0) maxBody -= 2 * z;
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, std::max(80.f, maxBody)));
  ImGui::BeginChild("##body", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_None);
  bodyMin = ImGui::GetWindowPos(); bodyMax = bodyMin + ImGui::GetWindowSize();
  padScrollHere();
}
void Panel::endBody() { ImGui::EndChild(); }
void Panel::beginRightPage() {
  ImGui::PopFont();
  ImGui::End();   // (the left page)
  ImGui::SetNextWindowPos(bR); ImGui::SetNextWindowSize(bS);
  ImGui::Begin((id_ + "R").c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PushFont(font(F_BODY));
  dl = ImGui::GetWindowDrawList(); pos = bR;
  left_ = false;
}
void Panel::nextPage() {
  if (!book_ || !left_) return;
  ImGui::EndChild();
  beginRightPage();
  const float z = Z();
  ImGui::BeginChild("##body", ImVec2(bS.x, std::max(40.f, bR.y + bS.y - ImGui::GetCursorScreenPos().y - footH)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
  (void)z;
  bodyMin = ImGui::GetWindowPos(); bodyMax = bodyMin + ImGui::GetWindowSize();
  padScrollHere();
}
void Panel::beginFooter() {
  float z = Z();
  hasFooter = true;
  ImVec2 p = ImGui::GetCursorScreenPos();
  if (!book_) dl->AddRectFilled(ImVec2(pos.x, p.y - 8 * z), ImVec2(pos.x + width, p.y - 6 * z), IM_COL32(0, 0, 0, 255));   // the black line over the button band
  ImGui::SetCursorScreenPos(ImVec2(p.x + 16 * z, p.y + 4 * z));
  ImGui::BeginGroup();
}
void Panel::endFooter() { ImGui::EndGroup(); ImGui::Dummy(ImVec2(1, 8 * Z())); }
void Panel::end() {
  float z = Z();
  ImVec2 sz = ImGui::GetWindowSize();
  if (book_ && left_) beginRightPage();   // (nothing written on the right page: it shows blank)
  if (!book_) ImGui::GetWindowDrawList()->AddRect(pos, pos + ImVec2(width, sz.y), IM_COL32(0, 0, 0, 255), 0, 0, 2 * z);
  ImGui::PopFont();
  ImGui::End();
  ImGui::PopStyleVar(2);
}

// ---- slots ------------------------------------------------------------------------------------------------
void drawSlot(ImDrawList* dl, ImVec2 p, float s, const SlotLook& lk) {
  float z = Z();
  ImVec2 b = p + ImVec2(s, s);
  if (lk.inv) {
    ImU32 bg = lk.bg ? lk.bg : lk.out && lk.ready ? col::rgb(0x2d2418) : lk.hover ? col::rgb(0x2a221b) : col::sl;
    dl->AddRectFilled(p, b, bg);
    float w = 2 * z;
    bevel(dl, p, b, col::slLo, col::slHi, w);
    dl->AddRectFilled(p + ImVec2(w, w), ImVec2(b.x - w, p.y + w + 1 * z), IM_COL32(0, 0, 0, 153));
    dl->AddRectFilled(p + ImVec2(w, w), ImVec2(p.x + w + 1 * z, b.y - w), IM_COL32(0, 0, 0, 153));
    if (lk.out && lk.ready) dl->AddRect(p + ImVec2(w, w), b - ImVec2(w, w), withAlpha(col::gold, 0.45f), 0, 0, 2 * z);
    if (lk.sel) dl->AddRect(p + ImVec2(w, w), b - ImVec2(w, w), col::gold, 0, 0, 2 * z);
    if (lk.focus) { dl->AddRect(p - ImVec2(3, 3) * z, b + ImVec2(3, 3) * z, col::black, 0, 0, 2 * z); dl->AddRect(p - ImVec2(1, 1) * z, b + ImVec2(1, 1) * z, col::gold, 0, 0, 3 * z); }
  } else {
    // HUD slot (.slot): rounded, dark, the selected one framed in cream with an ember inset
    float r = 3 * z;
    dl->AddRectFilled(p, b, col::slot, r);
    dl->AddRectFilled(p + ImVec2(2, 2) * z, ImVec2(b.x - 2 * z, p.y + 4 * z), IM_COL32(0, 0, 0, 89));
    dl->AddRectFilled(p + ImVec2(2, 2) * z, ImVec2(p.x + 4 * z, b.y - 2 * z), IM_COL32(0, 0, 0, 89));
    if (lk.sel) {
      dl->AddRect(p - ImVec2(2, 2) * z, b + ImVec2(2, 2) * z, col::black, r + 2 * z, 0, 2 * z);
      dl->AddRect(p, b, col::rgb(0xf6ead0), r, 0, 2 * z);
      dl->AddRect(p + ImVec2(2, 2) * z, b - ImVec2(2, 2) * z, col::rgb(0xff8a3d, 128), 0, 0, 2 * z);
    } else dl->AddRect(p, b, col::rgb(0x0b0907), r, 0, 2 * z);
  }
  if (lk.full) { ImVec2 q(b.x - 9 * z, p.y + 2 * z); dl->AddRectFilled(q - ImVec2(1, 1) * z, q + ImVec2(8, 8) * z, col::black); dl->AddRectFilled(q, q + ImVec2(7, 7) * z, col::ember, 1 * z); }
}

void drawIcon(ImDrawList* dl, const std::string& key, ImVec2 p, float s, float alpha) {
  // the compass, death compass and clock show their live face (game/instruments.js .ico.dyn-*)
  GLuint itex = 0; vec4 iuv;
  if (instrumentIcon(key, itex, iuv)) { dl->AddImage((ImTextureID)(intptr_t)itex, p, p + ImVec2(s, s), ImVec2(iuv.x, iuv.y), ImVec2(iuv.z, iuv.w), IM_COL32(255, 255, 255, (int)(255 * alpha))); return; }
  IconRef r = itemIcons().get(key);
  if (!r.valid()) return;
  dl->AddImage((ImTextureID)(intptr_t)r.tex, p, p + ImVec2(s, s), ImVec2(r.uv0.x, r.uv0.y), ImVec2(r.uv1.x, r.uv1.y), IM_COL32(255, 255, 255, (int)(255 * alpha)));
}

void drawItem(ImDrawList* dl, const ItemStack& st, ImVec2 p, float s, float alpha, bool count) {
  if (st.empty()) return;
  float in = s * 0.12f;
  const std::string pk = partToolModelKey(st);   // a mixed tool or a gem set in one: its own icon (core/tool_parts.h)
  IconRef dyn = pk.empty() ? IconRef() : itemIcons().dynamic(pk);
  if (dyn.valid()) dl->AddImage((ImTextureID)(intptr_t)dyn.tex, p + ImVec2(in, in), p + ImVec2(s - in, s - in), ImVec2(dyn.uv0.x, dyn.uv0.y), ImVec2(dyn.uv1.x, dyn.uv1.y), IM_COL32(255, 255, 255, (int)(255 * alpha)));
  else drawIcon(dl, st.id, p + ImVec2(in, in), s - in * 2, alpha);
  if (itemGlints(st)) drawGlint(dl, st.id, p + ImVec2(in, in), s - in * 2, alpha);
  const ItemDef* d = st.def();
  int dur = 0;
  if (d && (d->hasTool || d->has("armor"))) dur = toolMaxDurability(st);
  if (dur && st.d) {
    float f = std::max(0.f, 1 - (float)st.d / dur);
    ImVec2 a(p.x + in, p.y + s - 4 * Z() - 3 * Z()), b(p.x + s - in, p.y + s - 4 * Z());
    dl->AddRectFilled(a, b, col::rgb(0x111111));
    float hue = f * 110.f / 360.f; float r, g, bb; ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 0.9f, r, g, bb);
    dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * f, b.y), ImGui::GetColorU32(ImVec4(r, g, bb, alpha)));
  }
  if (count && st.n > 1) {
    std::string n = std::to_string(st.n);
    float k = s * 0.42f / fontPx(F_H3) / Z();
    ImVec2 ts = textSize(F_H3, n.c_str(), 0, k);
    ImVec2 q(p.x + s - 3 * Z() - ts.x, p.y + s - 1 * Z() - ts.y * 0.92f);
    text(dl, F_H3, q + ImVec2(2, 2) * Z(), withAlpha(col::rgb(0x1a1a1a), alpha), n.c_str(), 0, k);
    text(dl, F_H3, q, withAlpha(col::rgb(0xfff6e0), alpha), n.c_str(), 0, k);
  }
}

void drawSprite(ImDrawList* dl, int sp, ImVec2 p, float s, float alpha) {
  if (!g_spriteTex) return;
  float v0 = (float)sp / (float)SP_COUNT, v1 = (float)(sp + 1) / (float)SP_COUNT;
  dl->AddImage((ImTextureID)(intptr_t)g_spriteTex, p, p + ImVec2(s, s), ImVec2(0, v0), ImVec2(1, v1), IM_COL32(255, 255, 255, (int)(255 * alpha)));
}

} // namespace ui
} // namespace dq
