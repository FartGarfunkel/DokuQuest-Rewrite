// ui/theme.cpp — styles.css in ImGui (see theme.h).
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/icons.h"
#include "assets/gui_textures.h"
#include "audio/audio.h"
#include "render/gl.h"
#include "render/instruments.h"
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

namespace dq {
namespace ui {

namespace {
struct FontSet { ImFont* f[F_COUNT]{}; float zoom = 0, dpi = 1; };
FontSet g_fonts;
const float FONT_PX[F_COUNT] = {12.5f, 15, 13, 17, 20, 24, 32, 64, 84, 12};
unsigned g_spriteTex = 0;
float g_zoom = 1;

float computeZoom(const App& a) {
  const std::string& g = a.settings.guiScale;
  float pct = 0;
  if (g == "small") pct = 80; else if (g == "normal") pct = 100; else if (g == "large") pct = 125;
  else if (!g.empty() && isdigit((unsigned char)g[0])) pct = (float)std::atof(g.c_str());
  if (pct >= 50) return clampv(pct / 100.f, 0.5f, 2.f);
  return clampv(std::min(a.width / 1920.f, a.height / 1080.f), 1.f, 2.f);
}

void buildFonts(App& a, float zoom) {
  ImGuiIO& io = ImGui::GetIO();
  float dpi = 1; int dw, dh; SDL_GL_GetDrawableSize(a.window, &dw, &dh); if (a.width > 0) dpi = std::max(1.f, (float)dw / a.width);
  if (g_fonts.zoom == zoom && g_fonts.dpi == dpi && g_fonts.f[0]) return;
  bool had = g_fonts.f[0] != nullptr || io.Fonts->IsBuilt();
  if (had) ImGui_ImplOpenGL3_DestroyFontsTexture();
  io.Fonts->Clear();
  ImFontConfig cfg; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2030, 0x203A, 0x2190, 0x21FF, 0x2212, 0x2212, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0};
  static const ImWchar ascii[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};
  std::string uiF = (assetsDir() / "fonts" / "PixelifySans.ttf").string(), tF = (assetsDir() / "fonts" / "Jersey15.ttf").string();
  bool haveUi = fs::exists(uiF), haveT = fs::exists(tF);
  for (int i = 0; i < F_COUNT; i++) {
    bool display = i == F_H3 || i == F_DISP || i == F_TITLE || i == F_HUGE;
    // ImGui sizes a font by its ascent+descent, CSS by its em: Pixelify Sans is 1.2 em tall, Jersey 15 1.0 em
    float size = std::round(FONT_PX[i] * zoom * dpi * (display ? 1.f : 1.2f));
    if (i == F_MONO) { ImFontConfig c2; c2.SizePixels = std::max(13.f, std::round(13 * zoom * dpi)); g_fonts.f[i] = io.Fonts->AddFontDefault(&c2); continue; }
    const std::string& file = display ? tF : uiF;
    bool have = display ? haveT : haveUi;
    g_fonts.f[i] = have ? io.Fonts->AddFontFromFileTTF(file.c_str(), size, &cfg, display ? ascii : ranges) : io.Fonts->AddFontDefault();
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
    float size = std::round(FONT_PX[i] * zoom * dpi * 1.2f);
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
  g_fonts.zoom = zoom; g_fonts.dpi = dpi;
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

void init(App& a) {
  g_zoom = computeZoom(a);
  buildFonts(a, g_zoom);
  buildSprites();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 6; s.ChildRounding = 0; s.FrameRounding = 4; s.PopupRounding = 4; s.GrabRounding = 3; s.ScrollbarRounding = 3;
  s.WindowBorderSize = 0; s.FrameBorderSize = 2; s.PopupBorderSize = 2; s.ChildBorderSize = 0;
  s.ItemSpacing = ImVec2(8, 8); s.ScrollbarSize = 10; s.GrabMinSize = 14;
  ImVec4* c = s.Colors;
  auto C = [](u32 hex, float a = 1) { return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); };
  c[ImGuiCol_Text] = C(0xefe4cc); c[ImGuiCol_TextDisabled] = C(0xb3a48c);
  c[ImGuiCol_WindowBg] = C(0x1b1612, 0.95f); c[ImGuiCol_ChildBg] = C(0, 0); c[ImGuiCol_PopupBg] = C(0x1b1612, 0.98f);
  c[ImGuiCol_Border] = C(0x4d3f33); c[ImGuiCol_BorderShadow] = C(0, 0);
  c[ImGuiCol_FrameBg] = C(0x120e0b); c[ImGuiCol_FrameBgHovered] = C(0x1a1410); c[ImGuiCol_FrameBgActive] = C(0x1a1410);
  c[ImGuiCol_CheckMark] = C(0xff8a3d); c[ImGuiCol_SliderGrab] = C(0xff8a3d); c[ImGuiCol_SliderGrabActive] = C(0xffa060);
  c[ImGuiCol_Button] = C(0x3a3027); c[ImGuiCol_ButtonHovered] = C(0x4a3c2f); c[ImGuiCol_ButtonActive] = C(0x2a221b);
  c[ImGuiCol_Header] = C(0x3a3027); c[ImGuiCol_HeaderHovered] = C(0x4a3c2f); c[ImGuiCol_HeaderActive] = C(0x5a4a3a);
  c[ImGuiCol_ScrollbarBg] = C(0x120e0b, 0.6f); c[ImGuiCol_ScrollbarGrab] = C(0x6d5a47); c[ImGuiCol_ScrollbarGrabHovered] = C(0x7c6650); c[ImGuiCol_ScrollbarGrabActive] = C(0x8c7660);
  c[ImGuiCol_Separator] = C(0x4d3f33); c[ImGuiCol_NavCursor] = C(0xff8a3d); c[ImGuiCol_TextSelectedBg] = C(0xff8a3d, 0.35f);
  c[ImGuiCol_ModalWindowDimBg] = C(0x080604, 0.55f);
  // pads are fed by beginFrame (standard layout, pad maps, Nintendo swap)
  ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
}

void feedNav(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  Input& in = a.input;
  if (in.padCount() == 0 || in.setupActive) { if (in.padCount() == 0) io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad; return; }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  auto key = [&](ImGuiKey k, int b) { io.AddKeyEvent(k, in.padButtonDown(b)); };
  key(ImGuiKey_GamepadFaceDown, 0); key(ImGuiKey_GamepadFaceRight, 1);
  key(ImGuiKey_GamepadDpadUp, 12); key(ImGuiKey_GamepadDpadDown, 13); key(ImGuiKey_GamepadDpadLeft, 14); key(ImGuiKey_GamepadDpadRight, 15);
  vec2 l(0);
  for (auto& p : in.pads()) {
    if (in.selected() >= 0) { auto& ids = in.sourceList()[in.selected()].pads; if (std::find(ids.begin(), ids.end(), p.id) == ids.end()) continue; }
    if (glm::length(p.lstick) > glm::length(l)) l = p.lstick;
  }
  auto ana = [&](ImGuiKey k, float v) { io.AddKeyAnalogEvent(k, v > 0.3f, std::max(0.f, v)); };
  ana(ImGuiKey_GamepadLStickLeft, -l.x); ana(ImGuiKey_GamepadLStickRight, l.x); ana(ImGuiKey_GamepadLStickUp, -l.y); ana(ImGuiKey_GamepadLStickDown, l.y);
}

static float g_baseZoom = 1;
float baseZoom() { return g_baseZoom; }
void localZoom(float k) {
  g_zoom = g_baseZoom * k;
  ImGui::GetIO().FontGlobalScale = k / std::max(1.f, g_fonts.dpi);
}

void beginFrame(App& a) {
  float z = std::round(computeZoom(a) * 20.f) / 20.f;
  if (z != g_zoom || !g_fonts.f[0]) { g_zoom = z; buildFonts(a, z); }
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

// ---- buttons --------------------------------------------------------------------------------------------
static bool itemHot() { return ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible()); }

bool mcButton(const char* lbl, ImVec2 size, bool disabled) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(disabled);
  bool clicked = ImGui::InvisibleButton(lbl, size);
  ImGui::EndDisabled();
  bool hot = !disabled && itemHot(), held = !disabled && ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float z = Z(), oy = held ? 1 : 0;
  ImVec2 a(p.x, p.y + oy), b(p.x + size.x, p.y + size.y + oy);
  // a resource pack's own button pictures, stretched like .pack-buttons .mcbtn
  GuiSprite ps = disabled ? GuiSprite() : guiSprite(hot ? "widget/button_highlighted" : "widget/button");
  if (disabled) dl->AddRectFilled(a, b, col::rgb(0x2c2c2c));
  else if (ps.valid()) dl->AddImage((ImTextureID)ps.id(), a, b, ImVec2(ps.uv0.x, ps.uv0.y), ImVec2(ps.uv1.x, ps.uv1.y));
  else {
    dl->AddRectFilled(a, b, hot ? col::rgb(0x7d87c4) : col::rgb(0x6e6e6e));
    drawTiled(dl, "gui_button", a, b, 64 * z, hot ? IM_COL32(227, 247, 255, 255) : IM_COL32(217, 217, 217, 255));
    if (hot) dl->AddRectFilled(a, b, col::rgb(0x7d87c4, 70));
    // inset 2px 2px light, inset -2px -4px dark
    dl->AddRectFilled(a, ImVec2(b.x, a.y + 2 * z), IM_COL32(255, 255, 255, hot ? 89 : 82));
    dl->AddRectFilled(a, ImVec2(a.x + 2 * z, b.y), IM_COL32(255, 255, 255, hot ? 89 : 82));
    ImU32 dk = hot ? IM_COL32(10, 10, 60, 89) : IM_COL32(0, 0, 0, 82);
    dl->AddRectFilled(ImVec2(a.x, b.y - 4 * z), b, dk); dl->AddRectFilled(ImVec2(b.x - 2 * z, a.y), b, dk);
  }
  dl->AddRect(a, b, hot && !ps.valid() ? IM_COL32_WHITE : IM_COL32_BLACK, 0, 0, 2 * z);
  // label without the ##id part
  std::string t = lbl; size_t h = t.find("##"); if (h != std::string::npos) t.resize(h);
  t = ellipsize(F_BTN, t, size.x - 16 * z);
  ImU32 tc = disabled ? col::rgb(0xa0a0a0) : hot ? col::rgb(0xffffa0) : col::rgb(0xe0e0e0);
  textCentered(dl, F_BTN, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tc, t.c_str(), disabled ? 0 : col::rgb(0x383838));
  if (clicked) playClick();
  return clicked;
}

bool button(const char* lbl, BtnStyle st, bool tiny, float width, bool disabled) {
  float z = Z();
  std::string t = lbl; size_t hh = t.find("##"); if (hh != std::string::npos) t.resize(hh);
  FontId f = tiny ? F_MID : F_BODY;
  ImVec2 ts = textSize(f, t.c_str());
  float h = (tiny ? 30 : 44) * z, padX = (tiny ? 10 : 18) * z;
  float w = width > 0 ? width : width < 0 ? ImGui::GetContentRegionAvail().x : ts.x + padX * 2 + 4 * z;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(disabled);
  bool clicked = ImGui::InvisibleButton(lbl, ImVec2(w, h));
  ImGui::EndDisabled();
  bool hot = !disabled && itemHot(), held = !disabled && ImGui::IsItemActive(), focus = ImGui::IsItemFocused() && navVisible();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float al = disabled ? 0.45f : 1;
  ImVec2 a(p.x, p.y + (held ? 1 : 0)), b(p.x + w, p.y + h + (held ? 1 : 0));
  float r = 4 * z;
  ImU32 border = col::line, tc = col::text;
  if (st == BTN_PRIMARY) {
    dl->AddRectFilledMultiColor(a, b, withAlpha(col::rgb(0xff9a52), al), withAlpha(col::rgb(0xff9a52), al), withAlpha(col::emberDeep, al), withAlpha(col::emberDeep, al));
    border = col::rgb(0xffb07a); tc = col::rgb(0x1a0d05);
  } else if (st == BTN_GHOST) {
    border = hot ? col::line : 0; tc = hot ? col::text : col::muted;
  } else {
    dl->AddRectFilledMultiColor(a, b, withAlpha(col::slotHi, al), withAlpha(col::slotHi, al), withAlpha(col::slot, al), withAlpha(col::slot, al));
    if (st == BTN_DANGER) { border = col::rgb(0x7a3a33); tc = col::rgb(0xffb3a9); }
    else if (hot) border = col::lineHi;
  }
  if (st != BTN_GHOST) {
    dl->AddRectFilled(ImVec2(a.x, b.y - (held ? 1 : 3) * z), b, IM_COL32(0, 0, 0, (int)(89 * al)));
    dl->AddRectFilled(a, ImVec2(b.x, a.y + 2 * z), IM_COL32(255, 255, 255, (int)(15 * al)));
    if (st == BTN_DANGER && hot) border = col::rgb(0xa8402e);
  }
  if (border) dl->AddRect(a, b, withAlpha(border, al), r, 0, 2 * z);
  if (focus) dl->AddRect(a - ImVec2(4, 4) * z, b + ImVec2(4, 4) * z, col::ember, r + 2 * z, 0, 2 * z);
  ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  if (st == BTN_PRIMARY) text(dl, f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f + 1 * z), withAlpha(col::rgb(0xffdcbe, 102), al), t.c_str());
  text(dl, f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), withAlpha(tc, al), t.c_str());
  if (clicked) playClick();
  return clicked;
}

bool checkbox(const char* lbl, bool* v, const char* desc) {
  ImGui::PushFont(font(F_BODY));
  bool ch = ImGui::Checkbox(lbl, v);
  ImGui::PopFont();
  if (desc && *desc) { ImGui::Indent(ImGui::GetFrameHeight() + 8 * Z()); label(F_SMALL, col::dim, desc, 0); ImGui::Unindent(ImGui::GetFrameHeight() + 8 * Z()); }
  if (ch) playClick();
  return ch;
}

bool slider(const char* id, float* v, float mn, float mx, float step, const char* shown) {
  // input[type=range] with accent-color ember: a thin light track, ember to the left of the round knob
  float z = Z(), w = ImGui::GetContentRegionAvail().x, h = 20 * z;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(w, h));
  bool ch = false;
  float r = 7 * z, x0 = p.x + r, x1 = p.x + w - r;
  if (ImGui::IsItemActive()) {
    float t = clampv((ImGui::GetIO().MousePos.x - x0) / std::max(1.f, x1 - x0), 0.f, 1.f);
    float nv = mn + t * (mx - mn);
    if (step > 0) nv = clampv(std::round((nv - mn) / step) * step + mn, mn, mx);
    if (nv != *v) { *v = nv; ch = true; }
  }
  // controller / arrows: left and right change a focused slider straight away (ui.js menuNav)
  bool focus = ImGui::IsItemFocused() && navVisible();
  if (focus && !ImGui::IsItemActive()) {
    int dx = app().input.navStep().x;
    if (dx) { *v = clampv(*v + step * dx, mn, mx); if (step > 0) *v = std::round((*v - mn) / step) * step + mn; ch = true; }
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float t = mx > mn ? clampv((*v - mn) / (mx - mn), 0.f, 1.f) : 0, kx = x0 + (x1 - x0) * t, cy = p.y + h * 0.5f;
  dl->AddRectFilled(ImVec2(p.x + 2 * z, cy - 3 * z), ImVec2(p.x + w - 2 * z, cy + 3 * z), IM_COL32(220, 220, 220, 235), 3 * z);
  dl->AddRect(ImVec2(p.x + 2 * z, cy - 3 * z), ImVec2(p.x + w - 2 * z, cy + 3 * z), IM_COL32(120, 120, 120, 200), 3 * z);
  dl->AddRectFilled(ImVec2(p.x + 2 * z, cy - 3 * z), ImVec2(kx, cy + 3 * z), col::ember, 3 * z);
  dl->AddCircleFilled(ImVec2(kx, cy), r, ImGui::IsItemHovered() || ImGui::IsItemActive() ? col::rgb(0xff9d5c) : col::ember, 20);
  if (focus) dl->AddRect(p - ImVec2(2, 0) * z, p + ImVec2(w + 2 * z, h), col::ember, 4 * z, 0, 2 * z);
  (void)shown;
  return ch;
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
  if (ImGui::IsItemActive()) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), col::ember, 4 * Z(), 0, 2 * Z());
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
  // backdrop (.panel-screen)
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)a.width, (float)a.height), IM_COL32(8, 6, 4, 140));
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
  // body background first (drawn under everything): dirt at 30%, 2px border, header in planks at 42%
  ImVec2 b0 = pos, b1 = pos + ImVec2(width, std::max(size.y, 60 * z));
  dl->AddRectFilled(b0, b1, col::rgb(0x1b1612, 242), 6 * z);
  drawTiled(dl, "dirt", b0 + ImVec2(2, 2) * z, b1 - ImVec2(2, 2) * z, 64 * z, IM_COL32(77, 77, 77, 120));
  // header
  headH = 32 * z + 24 * z;
  ImVec2 h1 = b0 + ImVec2(width, headH);
  dl->AddRectFilled(b0, h1, col::panel2, 6 * z, ImDrawFlags_RoundCornersTop);
  drawTiled(dl, "planks", b0 + ImVec2(2, 2) * z, h1, 64 * z, IM_COL32(107, 107, 107, 200));
  dl->AddRectFilled(ImVec2(b0.x, h1.y - 2 * z), h1, col::rgb(0x1a120c));
  dl->AddRectFilled(ImVec2(b0.x, h1.y - 4 * z), ImVec2(h1.x, h1.y - 2 * z), IM_COL32(255, 220, 160, 20));
  ImVec2 ts = textSize(F_DISP, title);
  textShadow(dl, F_DISP, ImVec2(b0.x + 16 * z, b0.y + (headH - ts.y) * 0.5f), col::rgb(0xf6e2a8), title, ImVec2(2, 2) * z, col::rgb(0x2a1608));
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
  float maxBody = a.height - 32 * z - headH - footH;
  if (footerH > 0) maxBody -= 2 * z;
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, std::max(80.f, maxBody)));
  ImGui::BeginChild("##body", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_None);
  bodyMin = ImGui::GetWindowPos(); bodyMax = bodyMin + ImGui::GetWindowSize();
  padScrollHere();
}
void Panel::endBody() { ImGui::EndChild(); }
void Panel::beginFooter() {
  float z = Z();
  hasFooter = true;
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(ImVec2(pos.x, p.y - 8 * z), ImVec2(pos.x + width, p.y - 6 * z), col::line);
  ImGui::SetCursorScreenPos(ImVec2(p.x + 16 * z, p.y + 4 * z));
  ImGui::BeginGroup();
}
void Panel::endFooter() { ImGui::EndGroup(); ImGui::Dummy(ImVec2(1, 8 * Z())); }
void Panel::end() {
  float z = Z();
  ImVec2 sz = ImGui::GetWindowSize();
  ImGui::GetWindowDrawList()->AddRect(pos, pos + ImVec2(width, sz.y), col::rgb(0x4a3c2c), 6 * z, 0, 2 * z);
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
  drawIcon(dl, st.id, p + ImVec2(in, in), s - in * 2, alpha);
  const ItemDef* d = st.def();
  int dur = 0;
  if (d && d->hasTool) dur = d->tool.durability;
  else if (d && d->has("armor")) dur = d->props["armor"].value("durability", 0);
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
