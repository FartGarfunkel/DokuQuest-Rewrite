// launcher/ui.cpp — the launcher's look, from launcher.html's CSS: dirt background drawn from the same
// seeded noise, grey/green Minecraft-style buttons (.mcbtn), dark panels (.news/.side), Pixelify Sans.
#include "launcher/launcher.h"
#include "assets/tileset.h"
#include <glad/gl.h>
#include <imgui_internal.h>
#include <cstdarg>

namespace dq {

const char* tabId(LTab t) {
  switch (t) { case LTab::News: return "news"; case LTab::Server: return "server"; case LTab::Studio: return "studio"; case LTab::Options: return "options"; default: return ""; }
}

static ImVec4 rgba(int r, int g, int b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }

void applyLauncherStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 4; s.ChildRounding = 4; s.FrameRounding = 0; s.PopupRounding = 2; s.GrabRounding = 0; s.ScrollbarRounding = 2; s.TabRounding = 0;
  s.WindowBorderSize = 2; s.ChildBorderSize = 2; s.FrameBorderSize = 2; s.PopupBorderSize = 2;
  s.WindowPadding = ImVec2(14, 14); s.FramePadding = ImVec2(8, 5); s.ItemSpacing = ImVec2(10, 8); s.ItemInnerSpacing = ImVec2(8, 6);
  s.ScrollbarSize = 12; s.GrabMinSize = 12;
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = rgba(242, 237, 228);
  c[ImGuiCol_TextDisabled] = rgba(128, 120, 108);   // input hints
  c[ImGuiCol_WindowBg] = rgba(14, 11, 8, 0.97f);
  c[ImGuiCol_ChildBg] = rgba(10, 8, 6, 0.78f);
  c[ImGuiCol_PopupBg] = rgba(14, 11, 8, 0.98f);
  c[ImGuiCol_Border] = rgba(0, 0, 0);
  c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = rgba(0, 0, 0);
  c[ImGuiCol_FrameBgHovered] = rgba(18, 16, 14);
  c[ImGuiCol_FrameBgActive] = rgba(24, 22, 20);
  c[ImGuiCol_TitleBg] = rgba(42, 34, 27); c[ImGuiCol_TitleBgActive] = rgba(42, 34, 27); c[ImGuiCol_TitleBgCollapsed] = rgba(42, 34, 27);
  c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0.25f);
  c[ImGuiCol_ScrollbarGrab] = rgba(110, 110, 110); c[ImGuiCol_ScrollbarGrabHovered] = rgba(125, 135, 196); c[ImGuiCol_ScrollbarGrabActive] = rgba(140, 150, 210);
  c[ImGuiCol_CheckMark] = rgba(125, 135, 196);
  c[ImGuiCol_SliderGrab] = rgba(125, 135, 196); c[ImGuiCol_SliderGrabActive] = rgba(160, 168, 220);
  c[ImGuiCol_Button] = rgba(110, 110, 110); c[ImGuiCol_ButtonHovered] = rgba(125, 135, 196); c[ImGuiCol_ButtonActive] = rgba(100, 108, 160);
  c[ImGuiCol_Header] = rgba(58, 48, 39); c[ImGuiCol_HeaderHovered] = rgba(72, 60, 48); c[ImGuiCol_HeaderActive] = rgba(90, 74, 58);
  c[ImGuiCol_Separator] = rgba(255, 255, 255, 0.10f); c[ImGuiCol_SeparatorHovered] = rgba(255, 255, 255, 0.2f); c[ImGuiCol_SeparatorActive] = rgba(255, 255, 255, 0.3f);
  c[ImGuiCol_ResizeGrip] = rgba(0, 0, 0, 0); c[ImGuiCol_ResizeGripHovered] = rgba(125, 135, 196, 0.5f); c[ImGuiCol_ResizeGripActive] = rgba(125, 135, 196);
  c[ImGuiCol_Tab] = rgba(34, 28, 23); c[ImGuiCol_TabHovered] = rgba(58, 48, 39); c[ImGuiCol_TabSelected] = rgba(58, 48, 39);
  c[ImGuiCol_TableHeaderBg] = rgba(42, 34, 27); c[ImGuiCol_TableBorderStrong] = rgba(77, 63, 51); c[ImGuiCol_TableBorderLight] = rgba(77, 63, 51, 0.5f);
  c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = rgba(255, 255, 255, 0.03f);
  c[ImGuiCol_TextSelectedBg] = rgba(125, 135, 196, 0.45f);
  c[ImGuiCol_NavCursor] = rgba(255, 255, 255);
  c[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.6f);
}

// ---- textures: launcher.html draws its dirt and button pictures with a seeded generator (mulberry32)
namespace {
struct Mulberry { u32 a = 1234567; double next() { a += 0x6D2B79F5u; u32 t = (a ^ (a >> 15)) * (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; } };
unsigned makeTex(int w, int h, const std::vector<u8>& px, bool repeat) {
  GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  return t;
}
}

void makeLauncherTextures(Launcher& L) {
  Mulberry rnd;
  static const u32 dirt[6] = {0x7a5636, 0x6b4a2e, 0x86603d, 0x5c3f27, 0x937051, 0x4e3420};
  std::vector<u8> px(16 * 16 * 4);
  for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) {
    double r = rnd.next(); u32 c = dirt[r < 0.3 ? 0 : r < 0.55 ? 1 : r < 0.72 ? 2 : r < 0.86 ? 3 : r < 0.93 ? 4 : 5];
    u8* p = &px[(j * 16 + i) * 4]; p[0] = c >> 16; p[1] = (c >> 8) & 255; p[2] = c & 255; p[3] = 255;
  }
  L.texDirt = makeTex(16, 16, px, true);
  auto grey = [&](int r, int g, int b) {
    std::vector<u8> q(100 * 10 * 4);
    for (int j = 0; j < 10; j++) for (int i = 0; i < 100; i++) {
      double n = (rnd.next() - 0.5) * 22; u8* p = &q[(j * 100 + i) * 4];
      p[0] = (u8)clampv((int)(r + n), 0, 255); p[1] = (u8)clampv((int)(g + n), 0, 255); p[2] = (u8)clampv((int)(b + n), 0, 255); p[3] = 255;
    }
    return makeTex(100, 10, q, false);
  };
  L.texBtn = grey(111, 111, 111); L.texBtnHi = grey(124, 133, 190); L.texGo = grey(78, 136, 46); L.texGoHi = grey(100, 160, 64);
  if (auto icon = readBinary(assetsDir() / "icon.png")) {
    int w, h; std::vector<u8> rgba;
    if (loadPngMem(icon->data(), icon->size(), w, h, rgba)) L.texIcon = makeTex(w, h, rgba, false);
  }
}

void drawDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, const Launcher& L, float dark) {
  const float tile = 64;   // background-size: 64px
  dl->AddImage((ImTextureID)(intptr_t)L.texDirt, a, b, ImVec2(a.x / tile, a.y / tile), ImVec2(b.x / tile, b.y / tile));
  if (dark > 0) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(dark * 255)));
}

void shadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, ImU32 shadow, float off, const char* text, const char* end) {
  float sz = f->FontSize;
  if (off > 0) dl->AddText(f, sz, ImVec2(pos.x + off, pos.y + off), shadow, text, end);
  dl->AddText(f, sz, pos, col, text, end);
}

bool mcButton(Launcher& L, const char* label, ImVec2 size, BtnKind kind, bool enabled, ImFont* font) {
  ImFont* f = font ? font : L.fMid;
  const char* end = ImGui::FindRenderedTextEnd(label);
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label, end);
  if (size.x <= 0) size.x = ts.x + 36;
  if (size.y <= 0) size.y = 40;
  if (!enabled) ImGui::BeginDisabled();
  bool pressed = ImGui::InvisibleButton(label, size);
  bool hot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
  bool held = enabled && ImGui::IsItemActive();
  if (!enabled) ImGui::EndDisabled();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  if (held) { a.y += 1; b.y += 1; }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  unsigned tex = kind == BtnKind::Go ? (hot ? L.texGoHi : L.texGo) : (hot ? L.texBtnHi : L.texBtn);
  dl->AddImage((ImTextureID)(intptr_t)tex, a, b);
  // inset 2px 2px rgba(255,255,255,.32), inset -2px -4px rgba(0,0,0,.32)
  dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, a.y + 4), IM_COL32(255, 255, 255, 82));
  dl->AddRectFilled(ImVec2(a.x + 2, a.y + 4), ImVec2(a.x + 4, b.y - 2), IM_COL32(255, 255, 255, 82));
  dl->AddRectFilled(ImVec2(a.x + 2, b.y - 6), ImVec2(b.x - 2, b.y - 2), IM_COL32(0, 0, 0, 82));
  dl->AddRectFilled(ImVec2(b.x - 4, a.y + 4), ImVec2(b.x - 2, b.y - 6), IM_COL32(0, 0, 0, 82));
  if (!enabled) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110));
  dl->AddRect(a, b, hot ? C_WHITE : IM_COL32(0, 0, 0, 255), 0, 0, 2);
  ImVec2 tp(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f) - 1);
  ImU32 col = !enabled ? IM_COL32(160, 160, 160, 255) : hot ? IM_COL32(255, 255, 160, 255) : IM_COL32(224, 224, 224, 255);
  shadowText(dl, f, tp, col, IM_COL32(56, 56, 56, 255), 2, label, end);
  return pressed && enabled;
}

void beginDokuPanel(const char* id, ImVec2 size, bool scroll, ImGuiWindowFlags extra) {
  ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, (scroll ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) | extra);
  ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRectFullScreen();
  dl->AddRect(ImVec2(a.x + 2, a.y + 2), ImVec2(a.x + sz.x - 2, a.y + sz.y - 2), IM_COL32(255, 255, 255, 18), 3, 0, 2);   // inset 0 0 0 2px rgba(255,255,255,.07)
  dl->PopClipRect();
}
void endDokuPanel() { ImGui::EndChild(); }

void sideHeading(Launcher& L, const char* text) {
  std::string up = text; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
  ImGui::PushFont(L.fSmall);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_DIM));
  ImGui::TextUnformatted(up.c_str());
  ImGui::PopStyleColor(); ImGui::PopFont();
}

void dimText(const char* fmt, ...) {
  char buf[2048]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_DIM));
  ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(buf); ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

bool tabButton(Launcher& L, const char* label, bool active, ImVec2 size) {
  ImFont* f = L.fUi;
  const char* end = ImGui::FindRenderedTextEnd(label);
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label, end);
  if (size.x <= 0) size.x = ts.x + 26;
  if (size.y <= 0) size.y = 34;
  bool pressed = ImGui::InvisibleButton(label, size);
  bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilledMultiColor(a, b, IM_COL32(58, 48, 39, 240), IM_COL32(58, 48, 39, 240), IM_COL32(34, 28, 23, 240), IM_COL32(34, 28, 23, 240));
  dl->AddRect(a, b, active ? C_EMBER : hot ? IM_COL32(124, 102, 80, 255) : IM_COL32(77, 63, 51, 255), 4, 0, 2);
  dl->AddText(f, f->FontSize, ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f)), active ? IM_COL32(226, 184, 79, 255) : IM_COL32(239, 228, 204, 255), label, end);
  return pressed;
}

void openUrl(const std::string& url) {
  if (SDL_OpenURL(url.c_str()) != 0) DQ_WARN("could not open %s: %s", url.c_str(), SDL_GetError());
}
void openPath(const fs::path& p) {
  std::error_code ec; fs::create_directories(p, ec);
  std::string g = fs::absolute(p, ec).generic_string();
#ifdef _WIN32
  openUrl("file:///" + g);
#else
  openUrl("file://" + g);
#endif
}

} // namespace dq
