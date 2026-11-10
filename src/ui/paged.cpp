// ui/paged.cpp — BTA 8.0's paged menu screen (see paged.h). Geometry follows ScreenPaged, OptionsCategory and
// ButtonComponent: top band 24 units, bottom band 28, the page list a quarter of the width with 20-unit rows centred
// when they fit, an 8-unit strip, the page from leftWidth + 16 units, categories 24 units with their rows 16 in,
// rows 24 units with a 100 x 20 control at the right and a 20 x 20 reset button beside it.
#include "ui/paged.h"
#include "ui/ui.h"
#include "client/app.h"
#include "assets/gui_textures.h"
#include "render/gl.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace dq {
namespace ui {

namespace {
constexpr ImU32 C_WHITE = IM_COL32(255, 255, 255, 255), C_HOT = IM_COL32(255, 255, 160, 255), C_GREY = IM_COL32(127, 127, 127, 255),
  C_LABEL = IM_COL32(160, 160, 160, 255), C_LINE = IM_COL32(127, 127, 127, 95);
ImU32 shadowOf(ImU32 c) {   // Minecraft's text shadow: the colour at a quarter
  return IM_COL32(((c >> IM_COL32_R_SHIFT) & 255) / 4, ((c >> IM_COL32_G_SHIFT) & 255) / 4, ((c >> IM_COL32_B_SHIFT) & 255) / 4, (c >> IM_COL32_A_SHIFT) & 255);
}
void shadowText(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t) {
  float o = std::max(1.f, std::round(1.5f * Z()));
  text(dl, f, ImVec2(p.x + o, p.y + o), shadowOf(c), t);
  text(dl, f, p, c, t);
}
bool hotItem() { return ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible()); }
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
// a small grey button with a circular arrow (BTA's icon_reset)
bool resetButton(ImVec2 p, float s) {
  ImGui::SetCursorScreenPos(p);
  ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
  bool hit = ImGui::InvisibleButton("##reset", ImVec2(s, s));
  ImGui::PopItemFlag();
  bool hot = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 b(p.x + s, p.y + s);
  dl->AddRectFilled(p, b, hot ? IM_COL32(125, 135, 196, 255) : IM_COL32(110, 110, 110, 255));
  dl->AddRectFilled(p, ImVec2(b.x, p.y + s * 0.1f), IM_COL32(255, 255, 255, 80));
  dl->AddRectFilled(ImVec2(p.x, b.y - s * 0.15f), b, IM_COL32(0, 0, 0, 80));
  dl->AddRect(p, b, hot ? C_WHITE : IM_COL32(0, 0, 0, 255), 0, 0, std::max(1.f, s * 0.08f));
  ImVec2 c(p.x + s * 0.5f, p.y + s * 0.52f);
  float r = s * 0.24f, th = std::max(1.5f, s * 0.09f);
  ImU32 ic = hot ? C_HOT : IM_COL32(230, 230, 230, 255);
  dl->PathArcTo(c, r, 3.14159f * 0.15f, 3.14159f * 1.75f, 16);
  dl->PathStroke(ic, 0, th);
  ImVec2 tip(c.x + r * std::cos(3.14159f * 0.15f), c.y + r * std::sin(3.14159f * 0.15f));
  dl->AddTriangleFilled(ImVec2(tip.x + s * 0.12f, tip.y - s * 0.02f), ImVec2(tip.x - s * 0.1f, tip.y - s * 0.02f), ImVec2(tip.x + s * 0.01f, tip.y + s * 0.14f), ic);
  if (hit) playClick();
  if (hot) ImGui::SetTooltip("Reset to default");
  return hit;
}
// Minecraft's text field: black, a grey frame (white while typing)
bool mcTextField(const char* id, std::string* s, const char* hint, ImVec2 size, int flags, size_t maxLen) {
  float fh = textSize(F_BODY, "Ag").y;
  ImGui::PushFont(font(F_BODY));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::round(size.y * 0.2f), std::max(0.f, (size.y - fh) * 0.5f)));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.f, Z()));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 255));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 255));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 255));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(160, 160, 160, 255));
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(224, 224, 224, 255));
  ImGui::SetNextItemWidth(size.x);
  bool ch = hint ? ImGui::InputTextWithHint(id, hint, s, flags) : ImGui::InputText(id, s, flags);
  if (ImGui::IsItemActive()) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), C_WHITE, 0, 0, std::max(1.f, Z()));
  ImGui::PopStyleColor(5); ImGui::PopStyleVar(3); ImGui::PopFont();
  if (maxLen && s->size() > maxLen) s->resize(maxLen);
  return ch;
}
}

// ---- the launcher's look (launcher/ui.cpp): its dirt, its grey buttons, made the same way (launcher.html's seeded
// mulberry32), so the game's menus and the launcher's Options match ----
namespace {
struct Mulberry { u32 a = 1234567; double next() { a += 0x6D2B79F5u; u32 t = (a ^ (a >> 15)) * (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; } };
struct ClassicTex { unsigned dirt = 0, btn = 0, btnHi = 0; };
const ClassicTex& classicTex() {
  static ClassicTex t;
  if (t.dirt) return t;
  Mulberry rnd;
  static const u32 dirt[6] = {0x7a5636, 0x6b4a2e, 0x86603d, 0x5c3f27, 0x937051, 0x4e3420};
  std::vector<u8> px(16 * 16 * 4);
  for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) {
    double r = rnd.next(); u32 c = dirt[r < 0.3 ? 0 : r < 0.55 ? 1 : r < 0.72 ? 2 : r < 0.86 ? 3 : r < 0.93 ? 4 : 5];
    u8* p = &px[(j * 16 + i) * 4]; p[0] = c >> 16; p[1] = (c >> 8) & 255; p[2] = c & 255; p[3] = 255;
  }
  t.dirt = makeTexture2D(16, 16, px.data(), true, false, true);
  auto grey = [&](int r, int g, int b) {
    std::vector<u8> q(100 * 10 * 4);
    for (int j = 0; j < 10; j++) for (int i = 0; i < 100; i++) {
      double n = (rnd.next() - 0.5) * 22; u8* p = &q[(j * 100 + i) * 4];
      p[0] = (u8)clampv((int)(r + n), 0, 255); p[1] = (u8)clampv((int)(g + n), 0, 255); p[2] = (u8)clampv((int)(b + n), 0, 255); p[3] = 255;
    }
    return makeTexture2D(100, 10, q.data(), true, false, false);
  };
  t.btn = grey(111, 111, 111); t.btnHi = grey(124, 133, 190);
  return t;
}
}

// the launcher's dirt (64 design pixels a tile, anchored to the screen), darkened by `dark` (0..1)
void classicDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, float dark) {
  if (b.x <= a.x || b.y <= a.y) return;
  const ClassicTex& t = classicTex();
  dl->AddImage((ImTextureID)(intptr_t)t.dirt, a, b, ImVec2(a.x / tilePx, a.y / tilePx), ImVec2(b.x / tilePx, b.y / tilePx));
  if (dark > 0) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(dark * 255)));
}

// the launcher's grey button: the noisy grey (blue-grey under the mouse or a pad's focus), a light bevel top-left, a
// dark one bottom-right, a black frame (white when hot), light text with a dark shadow
bool classicButton(const char* lbl, ImVec2 size, bool disabled, FontId f) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(disabled);
  bool clicked = ImGui::InvisibleButton(lbl, size, ImGuiButtonFlags_EnableNav);
  ImGui::EndDisabled();
  const bool hot = !disabled && hotItem(), held = !disabled && ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float z = std::max(1.f, Z());
  ImVec2 a(p.x, p.y + (held ? 1 : 0)), b(p.x + size.x, p.y + size.y + (held ? 1 : 0));
  const float w2 = 2 * z;
  // the resource pack's own button when it has one (its 200 x 20 picture: the ends kept, the middle stretched, scaled
  // to the button's height), as the menus wore it before; the launcher's grey bevelled one otherwise
  const GuiSprite sp = guiSprite(disabled ? "widget/button_disabled" : hot ? "widget/button_highlighted" : "widget/button");
  if (sp.valid()) {
    const float k = size.y / 20.f, cap = std::min(4.f * k, size.x * 0.5f);
    const float du = (sp.uv1.x - sp.uv0.x) * (4.f / 200.f);
    const ImTextureID id = (ImTextureID)(intptr_t)sp.tex;
    dl->AddImage(id, a, ImVec2(a.x + cap, b.y), ImVec2(sp.uv0.x, sp.uv0.y), ImVec2(sp.uv0.x + du, sp.uv1.y));
    dl->AddImage(id, ImVec2(a.x + cap, a.y), ImVec2(b.x - cap, b.y), ImVec2(sp.uv0.x + du, sp.uv0.y), ImVec2(sp.uv1.x - du, sp.uv1.y));
    dl->AddImage(id, ImVec2(b.x - cap, a.y), b, ImVec2(sp.uv1.x - du, sp.uv0.y), ImVec2(sp.uv1.x, sp.uv1.y));
  } else {
    const ClassicTex& t = classicTex();
    dl->AddImage((ImTextureID)(intptr_t)(hot ? t.btnHi : t.btn), a, b);
    dl->AddRectFilled(ImVec2(a.x + w2, a.y + w2), ImVec2(b.x - w2, a.y + 2 * w2), IM_COL32(255, 255, 255, 82));
    dl->AddRectFilled(ImVec2(a.x + w2, a.y + 2 * w2), ImVec2(a.x + 2 * w2, b.y - w2), IM_COL32(255, 255, 255, 82));
    dl->AddRectFilled(ImVec2(a.x + w2, b.y - 3 * w2), ImVec2(b.x - w2, b.y - w2), IM_COL32(0, 0, 0, 82));
    dl->AddRectFilled(ImVec2(b.x - 2 * w2, a.y + 2 * w2), ImVec2(b.x - w2, b.y - 3 * w2), IM_COL32(0, 0, 0, 82));
    if (disabled) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110));
    dl->AddRect(a, b, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255), 0, 0, w2);
  }
  std::string tx = lbl; size_t h = tx.find("##"); if (h != std::string::npos) tx.resize(h);
  tx = ellipsize(f, tx, size.x - 12 * z);
  const ImU32 tc = disabled ? IM_COL32(160, 160, 160, 255) : hot ? IM_COL32(255, 255, 160, 255) : IM_COL32(224, 224, 224, 255);
  ImVec2 ts = textSize(f, tx.c_str());
  ImVec2 tp(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f) - 1);
  text(dl, f, ImVec2(tp.x + w2 * 0.75f, tp.y + w2 * 0.75f), IM_COL32(56, 56, 56, 255), tx.c_str());
  text(dl, f, tp, tc, tx.c_str());
  if (clicked) playClick();
  return clicked;
}

// a page's picture: "block/x" or "item/x" is the active pack's flat picture (as the launcher shows them), else an item's icon
void pageIcon(ImDrawList* dl, const std::string& icon, ImVec2 p, float s) {
  if (icon.rfind("block/", 0) == 0 || icon.rfind("item/", 0) == 0) {
    GuiSprite g = guiTexture(icon + ".png");
    if (g.valid()) {
      float v1 = g.h > g.w && g.h > 0 ? (float)g.w / g.h : 1.f;   // an animation strip: its first frame
      dl->AddImage((ImTextureID)g.id(), p, ImVec2(p.x + s, p.y + s), ImVec2(0, 0), ImVec2(1, v1));
      return;
    }
    drawIcon(dl, icon.substr(icon.find('/') + 1), p, s);
    return;
  }
  drawIcon(dl, icon, p, s);
}

float menuUnit(App& a) {
  float u = 2.f * Z();
  u = std::min(u, std::min(a.width / 400.f, a.height / 240.f));   // BTA's screens never get smaller than 400 x 240 units here
  return std::max(1.f, u);
}

void menuDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, float tile, ImU32 tint, float scrollY) {
  if (tile < 2 || b.x <= a.x || b.y <= a.y) return;
  // the launcher's brown dirt: the darker the tint asked for, the darker the dirt (BTA's 0x202020 page, 0x404040 bands)
  const int lum = (int)((tint >> IM_COL32_R_SHIFT) & 255);
  if (true) { classicDirt(dl, a, b, tile, lum <= 40 ? 0.62f : 0.3f); return; }
  GuiSprite s = guiTexture("gui/options_background.png");
  if (!s.valid()) s = guiTexture("gui/background.png");
  if (!s.valid()) { dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255)); drawTiled(dl, "dirt", a, b, tile, tint); return; }
  dl->PushClipRect(a, b, true);
  float x0 = std::floor(a.x / tile) * tile, y0 = std::floor((a.y + scrollY) / tile) * tile - scrollY;
  for (float y = y0; y < b.y; y += tile)
    for (float x = x0; x < b.x; x += tile)
      dl->AddImage((ImTextureID)s.id(), ImVec2(x, y), ImVec2(x + tile, y + tile), ImVec2(s.uv0.x, s.uv0.y), ImVec2(s.uv1.x, s.uv1.y), tint);
  dl->PopClipRect();
}

void tooltipBox(ImDrawList* dl, ImVec2 p, const std::string& t, float maxW) {
  float z = Z(), pad = 5 * z;
  ImVec2 ts = textSize(F_BODY, t.c_str(), maxW);
  ImVec2 io = ImGui::GetIO().DisplaySize;
  p.x = std::min(p.x, io.x - ts.x - pad * 2 - 4 * z); p.y = std::min(p.y, io.y - ts.y - pad * 2 - 4 * z);
  p.x = std::max(p.x, 2 * z); p.y = std::max(p.y, 2 * z);
  ImVec2 b(p.x + ts.x + pad * 2, p.y + ts.y + pad * 2);
  dl->AddRectFilled(p, b, IM_COL32(16, 0, 16, 240));
  float bw = std::max(1.f, 1.5f * z);
  dl->AddRectFilledMultiColor(ImVec2(p.x + bw, p.y + bw), ImVec2(p.x + 2 * bw, b.y - bw), IM_COL32(80, 0, 255, 80), IM_COL32(80, 0, 255, 80), IM_COL32(40, 0, 127, 80), IM_COL32(40, 0, 127, 80));
  dl->AddRectFilledMultiColor(ImVec2(b.x - 2 * bw, p.y + bw), ImVec2(b.x - bw, b.y - bw), IM_COL32(80, 0, 255, 80), IM_COL32(80, 0, 255, 80), IM_COL32(40, 0, 127, 80), IM_COL32(40, 0, 127, 80));
  dl->AddRectFilled(ImVec2(p.x + bw, p.y + bw), ImVec2(b.x - bw, p.y + 2 * bw), IM_COL32(80, 0, 255, 80));
  dl->AddRectFilled(ImVec2(p.x + bw, b.y - 2 * bw), ImVec2(b.x - bw, b.y - bw), IM_COL32(40, 0, 127, 80));
  text(dl, F_BODY, ImVec2(p.x + pad, p.y + pad), IM_COL32(255, 255, 255, 255), t.c_str(), maxW);
}

bool mcSlider(const char* id, float* v, float mn, float mx, float step, const std::string& shown, ImVec2 size, bool* released) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
  const bool hot = hotItem(), active = ImGui::IsItemActive(), focus = ImGui::IsItemFocused() && navVisible();
  const float hw = std::max(4.f, size.y * 0.4f);   // the handle is 8 units of a 20-unit slider
  bool ch = false;
  auto snap = [&](float nv) { nv = clampv(nv, mn, mx); if (step > 0) nv = clampv(std::round((nv - mn) / step) * step + mn, mn, mx); return nv; };
  if (active) {
    float t = clampv((ImGui::GetIO().MousePos.x - p.x - hw * 0.5f) / std::max(1.f, size.x - hw), 0.f, 1.f);
    float nv = snap(mn + t * (mx - mn));
    if (nv != *v) { *v = nv; ch = true; }
  }
  if (ImGui::IsItemDeactivated() && released) *released = true;
  if (focus && !active) {
    int dx = app().input.navStep().x;
    if (dx) { float nv = snap(*v + (step > 0 ? step : (mx - mn) / 20) * dx); if (nv != *v) { *v = nv; ch = true; if (released) *released = true; } }
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 b(p.x + size.x, p.y + size.y);
  // the launcher's slider: the grey button picture darkened for the track, a grey button 8 units wide for the handle
  const ClassicTex& ct = classicTex();
  const float z2 = 2 * std::max(1.f, Z());
  dl->AddImage((ImTextureID)(intptr_t)ct.btn, p, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(110, 110, 110, 255));
  dl->AddRectFilled(p, b, IM_COL32(0, 0, 0, 90));
  dl->AddRect(p, b, IM_COL32(0, 0, 0, 255), 0, 0, z2);
  float t = mx > mn ? clampv((*v - mn) / (mx - mn), 0.f, 1.f) : 0;
  float hx = std::round(p.x + (size.x - hw) * t);
  ImVec2 h0(hx, p.y), h1(hx + hw, b.y);
  dl->AddImage((ImTextureID)(intptr_t)(hot ? ct.btnHi : ct.btn), h0, h1);
  dl->AddRectFilled(ImVec2(hx + z2, p.y + z2), ImVec2(hx + hw - z2, p.y + 2 * z2), IM_COL32(255, 255, 255, 82));
  dl->AddRectFilled(ImVec2(hx + z2, b.y - 3 * z2), ImVec2(hx + hw - z2, b.y - z2), IM_COL32(0, 0, 0, 82));
  dl->AddRect(h0, h1, hot ? C_WHITE : IM_COL32(0, 0, 0, 255), 0, 0, z2);
  std::string s = ellipsize(F_MID, shown, size.x - 8 * Z());
  textCentered(dl, F_MID, ImVec2((p.x + b.x) * 0.5f, (p.y + b.y) * 0.5f), hot ? C_HOT : IM_COL32(224, 224, 224, 255), s.c_str(), IM_COL32(56, 56, 56, 255));
  return ch;
}

// ---- the frame ----------------------------------------------------------------------------------------------
void Paged::begin(App& a, const char* id, const std::string& title, const std::vector<PagedPage>& pages, float rightFrac, float topU, float bottomU) {
  unit = menuUnit(a);
  const float U = unit;
  W = (float)a.width; H = (float)a.height; inWorld = a.session != nullptr; plain_ = false;
  top = std::round(topU * U); bottom = std::round(H - bottomU * U);
  leftW = pages.empty() ? 0 : std::floor(W * 0.25f);
  rightW = std::floor(W * rightFrac);
  float pageX0 = pages.empty() ? 0 : leftW + 8 * U;
  cx = pageX0 + 8 * U;
  cw = std::max(120 * U, W - rightW - 16 * U - cx);
  pageChanged = false; tipText.clear(); tipAtMouse = false; rowSeq = 0;
  pendCat.clear(); curCat.clear(); catOpen = true; catShown = false;
  const int n = (int)pages.size();
  if (n) {
    page = clampv(page, 0, n - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) { page = (page + n - 1) % n; pageChanged = true; playClick(); }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) { page = (page + 1) % n; pageChanged = true; playClick(); }
  }
  if (pageChanged) focusFirst_ = true;
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(W, H));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::round(6 * U));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(192, 192, 192, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(212, 212, 212, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, IM_COL32(230, 230, 230, 255));
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PushFont(font(F_MID));
  drawBands(a);
  // the title, centred in the top band
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 ts = textSize(F_BIG, title.c_str());
  shadowText(dl, F_BIG, ImVec2(std::round(W * 0.5f - ts.x * 0.5f), std::round((top - ts.y) * 0.5f)), C_WHITE, title.c_str());
  // the page list
  if (n) {
    ImGui::SetCursorScreenPos(ImVec2(0, top));
    ImGui::BeginChild("##pagelist", ImVec2(leftW, bottom - top), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
    const float rowH = 20 * U, listH = rowH * n, regionH = bottom - top;
    float y0 = listH < regionH ? std::floor((regionH - listH) * 0.5f) : 0;
    ImDrawList* ld = ImGui::GetWindowDrawList();
    for (int i = 0; i < n; i++) {
      ImGui::SetCursorPos(ImVec2(4 * U, y0 + rowH * i));
      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(i);
      bool hit = ImGui::InvisibleButton("##page", ImVec2(std::max(8.f, leftW - 8 * U - (listH > regionH ? 6 * U : 0)), rowH), ImGuiButtonFlags_EnableNav);
      bool hot = hotItem();
      ImGui::PopID();
      if (hit && page != i) { page = i; pageChanged = true; focusFirst_ = true; playClick(); }
      ImU32 c = i == page ? C_WHITE : C_GREY;
      if (hot) c = C_HOT;
      float isz = 16 * U;
      if (!pages[i].icon.empty()) pageIcon(ld, pages[i].icon, ImVec2(p.x, p.y + (rowH - isz) * 0.5f), isz);
      ImVec2 ns = textSize(F_MID, pages[i].name.c_str());
      std::string nm = ellipsize(F_MID, pages[i].name, leftW - 8 * U - 19 * U - 6 * U);
      shadowText(ld, F_MID, ImVec2(p.x + 19 * U, std::round(p.y + (rowH - ns.y) * 0.5f)), c, nm.c_str());
    }
    ImGui::SetCursorPos(ImVec2(0, y0 + listH));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
  }
}

void Paged::beginPlain(App& a, const char* id, const std::string& title) {
  unit = menuUnit(a);
  const float U = unit;
  W = (float)a.width; H = (float)a.height; inWorld = a.session != nullptr; plain_ = true;
  top = 0; bottom = H; leftW = 0; rightW = 0; cx = 16 * U; cw = W - 32 * U;
  pageChanged = false; tipText.clear(); tipAtMouse = false;
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(W, H));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::round(6 * U));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(192, 192, 192, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(212, 212, 212, 255));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, IM_COL32(230, 230, 230, 255));
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PushFont(font(F_MID));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  menuDirt(dl, ImVec2(0, 0), ImVec2(W, H), 32 * U, IM_COL32(64, 64, 64, 255));
  ImVec2 ts = textSize(F_BIG, title.c_str());
  shadowText(dl, F_BIG, ImVec2(std::round(W * 0.5f - ts.x * 0.5f), std::round(20 * U - ts.y * 0.5f)), C_WHITE, title.c_str());
}

void Paged::textAt(const std::string& t, ImVec2 pos, ImU32 c, FontId f) { shadowText(ImGui::GetWindowDrawList(), f, pos, c, t.c_str()); }
void Paged::centerText(const std::string& t, float y, ImU32 c, FontId f) {
  ImVec2 ts = textSize(f, t.c_str());
  shadowText(ImGui::GetWindowDrawList(), f, ImVec2(std::round(W * 0.5f - ts.x * 0.5f), std::round(y)), c, t.c_str());
}
bool Paged::fieldAt(const char* id, std::string* s, ImVec2 pos, float wU, const char* hint, int flags, size_t maxLen) {
  ImGui::SetCursorScreenPos(pos);
  return mcTextField(id, s, hint, ImVec2(std::round(wU * unit), std::round(20 * unit)), flags, maxLen);
}

void Paged::takeFocus() {
  if (!focusFirst_) return;
  focusFirst_ = false;
  if (navVisible()) ImGui::SetKeyboardFocusHere();
}

void Paged::drawBands(App& app) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float U = unit, tile = 32 * U;
  {   // the launcher's dirt in and out of a world (the Options tab look)
    menuDirt(dl, ImVec2(0, top), ImVec2(W, bottom), tile, IM_COL32(32, 32, 32, 255));
    menuDirt(dl, ImVec2(0, 0), ImVec2(W, top), tile, IM_COL32(64, 64, 64, 255));
    menuDirt(dl, ImVec2(0, bottom), ImVec2(W, H), tile, IM_COL32(64, 64, 64, 255));
    if (leftW > 0) menuDirt(dl, ImVec2(leftW, top), ImVec2(leftW + 8 * U, bottom), tile, IM_COL32(64, 64, 64, 255));
    dl->AddLine(ImVec2(0, top), ImVec2(W, top), IM_COL32(0, 0, 0, 255), std::max(1.f, U));
    dl->AddLine(ImVec2(0, bottom), ImVec2(W, bottom), IM_COL32(0, 0, 0, 255), std::max(1.f, U));
  }
  (void)app;
}

void Paged::beginContent() {
  const float U = unit;
  float pageX0 = leftW > 0 ? leftW + 8 * U : 0;
  ImGui::SetCursorScreenPos(ImVec2(pageX0, top));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(cx - pageX0, 2 * U));
  ImGui::BeginChild("##page", ImVec2(W - rightW - pageX0, bottom - top), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
  ImGui::PopStyleVar();
  if (pageChanged) ImGui::SetScrollY(0);
  padScrollHere();
  pendCat.clear(); curCat.clear(); catOpen = true; catShown = false;
  contentOpen = true;
}

void Paged::endContent() {
  if (!contentOpen) return;
  ImGui::Dummy(ImVec2(cw, 6 * unit));
  // the shadows under the top band and over the bottom one (drawn on the page so they lie over the rows)
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->PushClipRect(wp, wp + ws, false);
    dl->AddRectFilledMultiColor(ImVec2(wp.x, top), ImVec2(wp.x + ws.x, top + 4 * unit), IM_COL32(0, 0, 0, 140), IM_COL32(0, 0, 0, 140), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(ImVec2(wp.x, bottom - 4 * unit), ImVec2(wp.x + ws.x, bottom), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 140), IM_COL32(0, 0, 0, 140));
    dl->PopClipRect();
  }
  ImGui::EndChild();
  contentOpen = false;
}

int Paged::footer(const std::vector<std::string>& labels, const std::vector<bool>& disabled) {
  const float U = unit;
  int n = (int)labels.size(), hit = -1;
  if (!n) return -1;
  float bw = std::max(40.f, 200.f - (n - 1) * 40.f), total = n * (bw + 4) - 4;
  for (int i = 0; i < n; i++) {
    ImVec2 pos(std::round(W * 0.5f - total * U * 0.5f + (bw + 4) * U * i), std::round(H - 24 * U));
    ImGui::PushID(9000 + i);
    if (buttonAt(labels[i].c_str(), pos, bw, 20, i < (int)disabled.size() && disabled[i])) hit = i;
    ImGui::PopID();
  }
  return hit;
}

void Paged::end() {
  if (contentOpen) endContent();
  if (!tipText.empty()) {
    ImVec2 p = tipAtMouse ? ImGui::GetIO().MousePos + ImVec2(12, -10) * Z() : tipPos;
    tooltipBox(ImGui::GetForegroundDrawList(), p, tipText, std::min(W * 0.45f, 420 * Z()));
  }
  ImGui::PopFont();
  ImGui::End();
  ImGui::PopStyleColor(6);
  ImGui::PopStyleVar(5);
}

// ---- rows ---------------------------------------------------------------------------------------------------
bool Paged::matches(const char* label) const {
  if (filter.empty()) return true;
  return lower(label ? label : "").find(lower(filter)) != std::string::npos;
}

void Paged::flushCategory() {
  if (catShown || pendCat.empty()) return;
  catShown = true;
  const float U = unit;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushID(("cat/" + pendCat).c_str());
  bool clicked = ImGui::InvisibleButton("##cat", ImVec2(cw, 24 * U), ImGuiButtonFlags_EnableNav);
  bool hot = hotItem();
  ImGui::PopID();
  const std::string key = std::to_string(page) + "/" + pendCat;
  if (clicked && !searching()) { if (folded.count(key)) folded.erase(key); else folded.insert(key); playClick(); }
  bool open = searching() || !folded.count(key);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 c = hot ? C_HOT : C_GREY;
  // ▽ open, ▷ folded
  float cyy = p.y + 2 * U + 10 * U, s = 3 * U;
  ImVec2 tc(p.x + 3 * U, cyy);
  if (open) dl->AddTriangle(ImVec2(tc.x - s, tc.y - s * 0.6f), ImVec2(tc.x + s, tc.y - s * 0.6f), ImVec2(tc.x, tc.y + s * 0.8f), c, std::max(1.f, U * 0.6f));
  else dl->AddTriangle(ImVec2(tc.x - s * 0.6f, tc.y - s), ImVec2(tc.x - s * 0.6f, tc.y + s), ImVec2(tc.x + s * 0.8f, tc.y), c, std::max(1.f, U * 0.6f));
  float xo = 10 * U;
  if (pendIcon && *pendIcon) { pageIcon(dl, pendIcon, ImVec2(p.x + 11 * U, p.y + 2 * U + 2 * U), 16 * U); xo += 20 * U; }
  ImVec2 ts = textSize(F_MID, pendCat.c_str());
  shadowText(dl, F_MID, ImVec2(p.x + xo, std::round(cyy - ts.y * 0.5f)), c, pendCat.c_str());
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 24 * U));
  ImGui::Dummy(ImVec2(cw, 0));
}

bool Paged::category(const std::string& name, const char* icon) {
  curCat = name; pendCat = name; pendIcon = icon; catShown = false;
  catOpen = searching() || !folded.count(std::to_string(page) + "/" + name);
  if (!searching()) flushCategory();
  return catOpen;
}

bool Paged::rowBegin(const char* label, const char* tip, float ctrlU, bool isDefault, ImVec2& ctrlPos, bool& resetHit) {
  resetHit = false;
  if (!catOpen || !matches(label)) return false;
  flushCategory();
  const float U = unit;
  const float indent = curCat.empty() ? 0 : 16 * U;
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float x = p.x + indent, right = p.x + cw;
  const float ctrlW = std::min(ctrlU * U, (cw - indent) * 0.62f);
  ctrlPos = ImVec2(std::round(right - ctrlW), std::round(p.y + 2 * U));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool winHot = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  const bool rowHot = winHot && ImGui::IsMouseHoveringRect(ImVec2(x, p.y + 2 * U), ImVec2(right, p.y + 22 * U));
  const bool labelHot = winHot && ImGui::IsMouseHoveringRect(ImVec2(x, p.y + 2 * U), ImVec2(ctrlPos.x - 20 * U, p.y + 22 * U));
  ImVec2 ts = textSize(F_MID, label);
  std::string lb = ellipsize(F_MID, label, std::max(20 * U, ctrlPos.x - x - 28 * U));
  float lw = textSize(F_MID, lb.c_str()).x;
  shadowText(dl, F_MID, ImVec2(x, std::round(p.y + 12 * U - ts.y * 0.5f)), rowHot ? C_HOT : C_WHITE, lb.c_str());
  float lineEnd = ctrlPos.x - 8 * U - (isDefault ? 0 : 20 * U);
  if (lineEnd > x + lw + 8 * U) dl->AddRectFilled(ImVec2(x + lw + 8 * U, p.y + 12 * U), ImVec2(lineEnd, p.y + 12 * U + std::max(1.f, U * 0.5f)), C_LINE);
  if (labelHot && tip && *tip) { tipText = tip; tipAtMouse = true; }
  ImGui::PushID(curCat.c_str());
  ImGui::PushID(label);
  if (!isDefault) resetHit = resetButton(ImVec2(ctrlPos.x - 20 * U, ctrlPos.y), 20 * U);
  ImGui::SetCursorScreenPos(ctrlPos);
  takeFocus();
  rowNextY_ = p.y + 24 * U; rowX_ = p.x;
  return true;
}

void Paged::rowEnd(const char* tip, bool isDefault, bool* resetByPad) {
  // the control just drawn: its description shows while a pad or the keys have it focused; Y puts the default back
  if (ImGui::IsItemFocused() && navVisible()) {
    if (tip && *tip && tipText.empty()) { tipText = tip; tipAtMouse = false; tipPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4 * unit); }
    if (resetByPad && !isDefault && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) { *resetByPad = true; playClick(); }
  }
  ImGui::PopID(); ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(rowX_, rowNextY_));
  ImGui::Dummy(ImVec2(cw, 0));
}

void Paged::ownLeftRight() {
  if (!(ImGui::IsItemFocused() && navVisible())) return;
  ImGuiID id = ImGui::GetItemID();
  for (ImGuiKey k : {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, ImGuiKey_LeftArrow, ImGuiKey_RightArrow})
    ImGui::SetKeyOwner(k, id);
}

bool Paged::toggle(const char* label, bool* v, bool def, const char* tip) {
  ImVec2 cp; bool reset;
  if (!rowBegin(label, tip, 100, *v == def, cp, reset)) return false;
  const float U = unit;
  bool ch = classicButton(*v ? "ON###t" : "OFF###t", ImVec2(std::round(100 * U > cw * 0.62f ? cw * 0.62f : 100 * U), 20 * U));
  if (ch) *v = !*v;
  bool padReset = false;
  rowEnd(tip, *v == def, &padReset);
  if ((reset || padReset) && *v != def) { *v = def; ch = true; }
  return ch;
}

bool Paged::cycle(const char* label, int* cur, const std::vector<std::string>& names, int def, const char* tip, float ctrlU) {
  if (names.empty()) return false;
  ImVec2 cp; bool reset;
  int n = (int)names.size();
  *cur = clampv(*cur, 0, n - 1);
  if (!rowBegin(label, tip, ctrlU, *cur == def, cp, reset)) return false;
  const float U = unit;
  float w = std::min(ctrlU * U, (cw - (curCat.empty() ? 0 : 16 * U)) * 0.62f);
  std::string t = names[*cur] + "###c";
  bool ch = false;
  if (classicButton(t.c_str(), ImVec2(std::round(w), 20 * U))) { *cur = (*cur + 1) % n; ch = true; }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *cur = (*cur + n - 1) % n; ch = true; playClick(); }
  ownLeftRight();
  if (ImGui::IsItemFocused() && navVisible()) { int dx = app().input.navStep().x; if (dx) { *cur = ((*cur + dx) % n + n) % n; ch = true; playClick(); } }
  bool padReset = false;
  rowEnd(tip, *cur == def, &padReset);
  if ((reset || padReset) && *cur != def) { *cur = def; ch = true; }
  return ch;
}

bool Paged::range(const char* label, float* v, float mn, float mx, float step, const std::string& shown, float def, const char* tip, bool* released, float ctrlU) {
  ImVec2 cp; bool reset;
  const bool isDef = std::fabs(*v - def) < std::max(1e-4f, step * 0.5f);
  if (!rowBegin(label, tip, ctrlU, isDef, cp, reset)) return false;
  const float U = unit;
  float w = std::min(ctrlU * U, (cw - (curCat.empty() ? 0 : 16 * U)) * 0.62f);
  bool ch = mcSlider("##r", v, mn, mx, step, shown, ImVec2(std::round(w), 20 * U), released);
  ownLeftRight();
  bool padReset = false;
  rowEnd(tip, isDef, &padReset);
  if ((reset || padReset) && !isDef) { *v = def; ch = true; if (released) *released = true; }
  return ch;
}

bool Paged::action(const char* label, const char* btn, const char* tip, bool disabled, float ctrlU) {
  ImVec2 cp; bool reset;
  if (!rowBegin(label, tip, ctrlU, true, cp, reset)) return false;
  const float U = unit;
  float w = std::min(ctrlU * U, (cw - (curCat.empty() ? 0 : 16 * U)) * 0.62f);
  std::string t = std::string(btn) + "###a";
  bool hit = classicButton(t.c_str(), ImVec2(std::round(w), 20 * U), disabled);
  rowEnd(tip, true, nullptr);
  return hit;
}

bool Paged::textField(const char* label, std::string* s, const char* hint, const char* tip, float ctrlU, int flags, size_t maxLen) {
  ImVec2 cp; bool reset;
  if (!rowBegin(label, tip, ctrlU, true, cp, reset)) return false;
  const float U = unit;
  float w = std::min(ctrlU * U, (cw - (curCat.empty() ? 0 : 16 * U)) * 0.62f);
  bool ch = mcTextField("##tx", s, hint, ImVec2(std::round(w), 20 * U), flags, maxLen);
  rowEnd(tip, true, nullptr);
  return ch;
}

bool Paged::custom(const char* label, const char* tip, float ctrlU, ImVec2* ctrl, bool isDefault, const std::function<void()>& reset) {
  ImVec2 cp; bool rh;
  if (!rowBegin(label, tip, ctrlU, isDefault || !reset, cp, rh)) { customOpen_ = false; return false; }
  if (rh && reset) reset();
  *ctrl = cp;
  customOpen_ = true; customTip_ = tip ? tip : "";
  return true;
}

void Paged::customEnd() {
  if (!customOpen_) return;
  customOpen_ = false;
  rowEnd(customTip_.c_str(), true, nullptr);
}

void Paged::note(const std::string& t, ImU32 c) {
  if (searching() || !catOpen || t.empty()) return;
  flushCategory();
  const float U = unit;
  const float indent = curCat.empty() ? 0 : 16 * U;
  ImVec2 p = ImGui::GetCursorScreenPos();
  float wrap = cw - indent;
  ImVec2 ts = textSize(F_BODY, t.c_str(), wrap);
  ui::text(ImGui::GetWindowDrawList(), F_BODY, ImVec2(p.x + indent, p.y + 2 * U), c ? c : C_LABEL, t.c_str(), wrap);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ts.y + 6 * U));
  ImGui::Dummy(ImVec2(cw, 0));
}

void Paged::gap(float units) { ImGui::Dummy(ImVec2(cw, units * unit)); }

bool Paged::buttonAt(const char* label, ImVec2 pos, float wU, float hU, bool disabled) {
  ImGui::SetCursorScreenPos(pos);
  return classicButton(label, ImVec2(std::round(wU * unit), std::round(hU * unit)), disabled);
}

bool Paged::rowButton(const char* label, float wU, bool disabled) {
  if (searching() || !catOpen) return false;
  flushCategory();
  const float U = unit;
  const float indent = curCat.empty() ? 0 : 16 * U;
  ImVec2 p = ImGui::GetCursorScreenPos();
  float w = std::min(wU * U, cw - indent);
  ImGui::SetCursorScreenPos(ImVec2(p.x + indent, p.y + 2 * U));
  bool hit = classicButton(label, ImVec2(std::round(w), 20 * U), disabled);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 24 * U));
  ImGui::Dummy(ImVec2(cw, 0));
  return hit;
}

bool Paged::listRow(const char* id, float hU, bool selected, bool* dbl, float wU) {
  const float U = unit;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImVec2 sz(cw, std::round(hU * U));
  const ImVec2 start = p;
  if (wU > 0 && wU * U < cw) { sz.x = std::round(wU * U); p.x = std::round(p.x + (cw - sz.x) * 0.5f); ImGui::SetCursorScreenPos(p); }
  if (selected) takeFocus();
  ImGui::PushID(id);
  bool hit = ImGui::InvisibleButton("##row", sz, ImGuiButtonFlags_EnableNav);
  bool hov = ImGui::IsItemHovered();
  bool foc = ImGui::IsItemFocused() && navVisible();
  ImGui::PopID();
  if (dbl) *dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Minecraft's selected slot: a grey frame round a black box; under the mouse a faint lift
  if (selected) { dl->AddRectFilled(p - ImVec2(2, 2) * U, p + sz + ImVec2(2, 2) * U, IM_COL32(128, 128, 128, 255)); dl->AddRectFilled(p - ImVec2(1, 1) * U, p + sz + ImVec2(1, 1) * U, IM_COL32(0, 0, 0, 255)); }
  else if (hov || foc) dl->AddRectFilled(p, p + sz, IM_COL32(255, 255, 255, 20));
  if (foc && !selected) dl->AddRect(p - ImVec2(1, 1) * U, p + sz + ImVec2(1, 1) * U, IM_COL32(160, 160, 160, 255), 0, 0, std::max(1.f, U * 0.5f));
  ImGui::SetCursorScreenPos(ImVec2(start.x, p.y + sz.y + 4 * U));
  ImGui::Dummy(ImVec2(cw, 0));
  lastRowMin_ = p; lastRowMax_ = p + sz;
  return hit;
}

} // namespace ui
} // namespace dq
