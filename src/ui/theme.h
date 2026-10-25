// ui/theme.h — the look of styles.css in Dear ImGui: Doku panels (dirt-textured body, planks header,
// Jersey 15 titles), the classic grey menu buttons, .btn buttons, sunk inventory slots, item icons with
// counts and durability bars, the HUD sprites (hearts, shields, bubbles), text with pixel shadows.
// Sizes are CSS pixels times the interface zoom Z() (settings.guiScale: "auto" or a percentage).
#pragma once
#include "common/common.h"
#include "core/itemstack.h"
#include <imgui.h>

namespace dq {
class App;
namespace ui {

// ---- palette (:root and .inv variables) ----
namespace col {
constexpr ImU32 rgb(u32 hex, u8 a = 255) { return IM_COL32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, a); }
constexpr ImU32 ink = rgb(0x0f0c0a), panel2 = rgb(0x2a221b), slot = rgb(0x221c17), slotHi = rgb(0x3a3027), line = rgb(0x4d3f33),
  lineHi = rgb(0x7c6650), text = rgb(0xefe4cc), muted = rgb(0xb3a48c), ember = rgb(0xff8a3d), emberDeep = rgb(0xc4521b), heart = rgb(0xe5484d),
  xp = rgb(0x9bd34a), frost = rgb(0x8fe3ff), gold = rgb(0xe2b84f), epic = rgb(0xc88cff), bad = rgb(0xff6b5e),
  pn = rgb(0x2b2520), pnHi = rgb(0x6d5a47), pnLo = rgb(0x0c0907), sl = rgb(0x17130f), slHi = rgb(0x3c3128), slLo = rgb(0x050403),
  ink2 = rgb(0xe9dcc0), dim = rgb(0xa8987e), white = IM_COL32_WHITE, black = IM_COL32_BLACK;
}

enum FontId { F_SMALL, F_BODY, F_MID, F_BTN, F_BIG, F_H3, F_DISP, F_TITLE, F_HUGE, F_MONO, F_COUNT };
// F_SMALL 12.5 · F_BODY 15 · F_MID 13 · F_BTN 17 · F_BIG 20 (Pixelify) · F_H3 24 · F_DISP 32 · F_TITLE 56 · F_HUGE 84 (Jersey) · F_MONO 12
ImFont* font(FontId f);
float fontPx(FontId f);            // the CSS size the font stands for (before zoom)
float Z();                         // interface zoom (hud.js uiZoomFor)
inline float px(float cssPx) { return cssPx * Z(); }

void beginFrame(App& a);           // fonts at the current zoom (rebuilds the atlas when it changes), pad → ImGui nav
void init(App& a);                 // style, sprites; after App::initImGui
bool navVisible();                 // keyboard / controller focus is being shown

// text
ImVec2 textSize(FontId f, const char* t, float wrap = 0, float scale = 1);
void text(ImDrawList* dl, FontId f, ImVec2 pos, ImU32 c, const char* t, float wrap = 0, float scale = 1);
void textShadow(ImDrawList* dl, FontId f, ImVec2 pos, ImU32 c, const char* t, ImVec2 off, ImU32 shadow, float wrap = 0, float scale = 1);
void textOutline(ImDrawList* dl, FontId f, ImVec2 pos, ImU32 c, const char* t, float w, ImU32 outline, float scale = 1);
void textCentered(ImDrawList* dl, FontId f, ImVec2 center, ImU32 c, const char* t, ImU32 shadow = 0, ImVec2 off = ImVec2(2, 2));
void label(FontId f, ImU32 c, const char* t, float wrap = -1);   // ImGui layout text in a font and colour

// tiles (block textures from the tile set, resource pack aware) for backgrounds
void drawTiled(ImDrawList* dl, const char* tileName, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint);

// .mcbtn — the classic grey menu button (title, pause)
bool mcButton(const char* label, ImVec2 size, bool disabled = false);
// .btn — panel buttons
enum BtnStyle { BTN_NORMAL, BTN_PRIMARY, BTN_GHOST, BTN_DANGER };
bool button(const char* label, BtnStyle st = BTN_NORMAL, bool tiny = false, float width = 0, bool disabled = false);
// form widgets in the panel style (label above or left); they read pad left/right on sliders
bool checkbox(const char* label, bool* v, const char* desc = nullptr);
bool slider(const char* id, float* v, float mn, float mx, float step, const char* shown);
bool combo(const char* id, int* cur, const std::vector<std::string>& items, float width = -1);
bool inputText(const char* id, std::string* s, const char* hint = nullptr, size_t maxLen = 0, float width = -1, int flags = 0);
void padScrollHere();              // right stick scrolls the current scrolling region

// .panel-screen with a .panel: backdrop, header (title + close button), body (scrolls), optional footer
class Panel {
public:
  // returns false if the window is hidden; closed is set when the header button was pressed
  bool begin(const char* id, const char* title, float maxW, const char* closeLabel = "Back", float fixedH = 0);
  void beginBody(float footerH = 0);
  void endBody();
  void beginFooter();
  void endFooter();
  void end();
  bool closed = false;
  ImVec2 bodyMin, bodyMax;
  float width = 0;
private:
  ImDrawList* dl = nullptr; ImVec2 pos, size; float headH = 0, footH = 0; bool hasFooter = false;
};

// slots and items
struct SlotLook { bool sel = false, focus = false, hover = false, out = false, ready = false, full = false, inv = true, dimmed = false; ImU32 bg = 0; };
void drawSlot(ImDrawList* dl, ImVec2 p, float size, const SlotLook& lk);
void drawItem(ImDrawList* dl, const ItemStack& s, ImVec2 p, float size, float alpha = 1, bool count = true);
void drawIcon(ImDrawList* dl, const std::string& key, ImVec2 p, float size, float alpha = 1);
void drawGlint(ImDrawList* dl, const std::string& key, ImVec2 p, float size, float alpha = 1);   // the enchantment shimmer (ui/icon_glint.cpp)

// HUD sprites (hud.js installHudSprites): 9×9 pixel art
enum Sprite { SP_HEART, SP_HEART_HALF, SP_HEART_EMPTY, SP_SHIELD = 12, SP_SHIELD_GLOW, SP_BUBBLE, SP_BUBBLE_POP, SP_COUNT };
enum HeartTint { HT_NONE = 0, HT_POISON = 3, HT_CHILL = 6, HT_WITHER = 9 };
void drawSprite(ImDrawList* dl, int sprite, ImVec2 p, float size, float alpha = 1);

// small helpers
inline ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
inline ImVec2 operator-(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
inline ImVec2 operator*(ImVec2 a, float k) { return ImVec2(a.x * k, a.y * k); }
ImU32 withAlpha(ImU32 c, float a);
void bevel(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 tl, ImU32 br, float w);   // 2-colour border (CSS outset/inset)
// radial-gradient(ellipse at c, transparent inner, colour at radius) — and solid beyond, to the screen edge
void radialVignette(ImDrawList* dl, ImVec2 c, ImVec2 radius, float inner, ImU32 outer, float reach = 3);
std::string ellipsize(FontId f, const std::string& s, float maxW);
void playClick();                  // the menu click sound

} // namespace ui
} // namespace dq
