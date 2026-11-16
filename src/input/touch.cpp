// input/touch.cpp — the phone's on-screen controls (input/touch.h).
#include "input/touch.h"
#include "input/input.h"
#include "assets/icons.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace dq {

int hudHotbarSlotAt(float x, float y);                // ui/hud.cpp: the hotbar slot under a point (-1: none)
bool hudHotbarBox(vec2& lo, vec2& hi);                // ui/hud.cpp: where this frame's hotbar slots are
extern float g_minimapBottom;                         // ui/hud.cpp: the minimap's lower edge (0: no minimap)

namespace {
constexpr double HOLD_S = 0.38;      // a finger held still this long mines; lifted sooner (and still), it was a tap
constexpr double DROP_ALL_S = 0.5;   // Drop held this long drops the whole stack
using T = TouchControls;

struct BtnDef { const char* id; const char* label; const char* icon; };
const BtnDef BTN[T::BtnCount] = {
  {"jump", "Jump", nullptr}, {"mine", "Mine", "iron_pickaxe"}, {"use", "Use", nullptr}, {"sneak", "Sneak", nullptr},
  {"cast", "Cast", "nether_star"}, {"dodge", "Dodge", nullptr}, {"sprint", "Sprint", nullptr}, {"drop", "Drop", nullptr},
  {"swap", "Swap", nullptr}, {"pause", "", nullptr}, {"bag", "Bag", "chest"}, {"more", "More", nullptr},
  {"map", "Map", "filled_map"}, {"journal", "Journal", "writable_book"}, {"spells", "Spells", "enchanted_book"}, {"chat", "Chat", nullptr},
};
// the More panel: everything else (act < 0: -1 drop the stack, -2 zoom on/off)
struct MoreDef { const char* id; const char* label; int act; const char* icon; };
const MoreDef MORE[] = {
  {"skills", "Skills", (int)Act::Skills, "book"}, {"spellbook", "Spellbook", (int)Act::Spellbook, "enchanted_book"},
  {"spell", "Next spell", (int)Act::Spell, nullptr}, {"castLeft", "Quick spell", (int)Act::CastLeft, nullptr},
  {"stable", "Companions", (int)Act::Stable, "saddle"}, {"crawl", "Crawl", (int)Act::Crawl, nullptr},
  {"place", "Placing mode", (int)Act::PlaceMode, nullptr}, {"hotbarRow", "Next row", (int)Act::HotbarRow, nullptr},
  {"view", "Camera", (int)Act::View, nullptr}, {"zoom", "Zoom", -2, "spyglass"},
  {"pick", "Pick block", (int)Act::PickBlock, nullptr}, {"dropAll", "Drop stack", -1, nullptr},
  {"command", "Command", (int)Act::Command, nullptr}, {"photoMode", "Photo mode", (int)Act::PhotoMode, nullptr},
  {"screenshot", "Screenshot", (int)Act::Screenshot, nullptr}, {"hideHud", "Hide HUD", (int)Act::HideHud, nullptr},
};
constexpr int MORE_N = (int)(sizeof MORE / sizeof MORE[0]);

struct Layout {
  vec2 c[T::BtnCount]; float r[T::BtnCount];
  vec2 stickC; float stickR, zoneR, slop, b;
  vec2 panel0, panel1; vec2 cell0[MORE_N], cell1[MORE_N];
};
// in window coordinates, the size of a button (b) from the screen's height and the Size option
Layout layoutFor(float W, float H, float size) {
  Layout L{};
  const float b = std::clamp(H * 0.15f * size, 48.f, 260.f), m = H * 0.035f;
  L.b = b;
  auto put = [&](int k, float x, float y, float r) { L.c[k] = vec2(x, y); L.r[k] = r * b; };
  // the right thumb's arc
  put(T::Jump, W - m - 0.7f * b, H - m - 1.15f * b, 0.62f);
  put(T::Sneak, W - m - 2.05f * b, H - m - 0.55f * b, 0.45f);
  put(T::Mine, W - m - 0.7f * b, H - m - 2.6f * b, 0.5f);
  put(T::Use, W - m - 2.05f * b, H - m - 1.85f * b, 0.47f);
  put(T::Cast, W - m - 2.0f * b, H - m - 3.2f * b, 0.42f);
  put(T::Dodge, W - m - 3.25f * b, H - m - 1.75f * b, 0.42f);
  // the move stick and its Sprint button
  L.stickC = vec2(m + 1.3f * b, H - m - 1.35f * b);
  L.stickR = 0.85f * b; L.zoneR = L.stickR * 1.7f;
  put(T::Sprint, L.stickC.x + L.stickR * 1.2f, L.stickC.y - L.stickR * 1.25f, 0.36f);
  // by the hotbar: Drop on its right, Swap on its left (past the off hand's slot)
  vec2 lo, hi;
  if (!hudHotbarBox(lo, hi)) { lo = vec2(W * 0.5f - 0.3f * H, H - m - 0.42f * b); hi = vec2(W * 0.5f + 0.3f * H, H - m); }
  const float hy = (lo.y + hi.y) * 0.5f, slot = hi.y - lo.y;
  put(T::Drop, hi.x + 0.6f * b, hy, 0.38f);
  put(T::Swap, lo.x - slot - 0.65f * b, hy, 0.38f);
  // along the top right (under the minimap when it's up there)
  const float top = g_minimapBottom > 0 ? g_minimapBottom + m * 0.5f : m;
  const int topRow[] = {T::Pause, T::Bag, T::More, T::Map, T::Journal, T::Spells, T::Chat};
  for (int i = 0; i < 7; i++) put(topRow[i], W - m - 0.42f * b - i * 0.92f * b, top + 0.42f * b, 0.38f);
  L.slop = H * 0.022f;
  // the More panel: four across, in the middle of the screen
  const int cols = 4, rows = (MORE_N + cols - 1) / cols;
  const float cw = 1.6f * b, ch = 0.62f * b, gap = 0.12f * b, pad = 0.2f * b;
  const vec2 sz(cols * cw + (cols - 1) * gap + 2 * pad, rows * ch + (rows - 1) * gap + 2 * pad);
  L.panel0 = vec2(W * 0.5f - sz.x * 0.5f, std::max(m, H * 0.45f - sz.y * 0.5f)); L.panel1 = L.panel0 + sz;
  for (int i = 0; i < MORE_N; i++) {
    L.cell0[i] = L.panel0 + vec2(pad + (i % cols) * (cw + gap), pad + (i / cols) * (ch + gap));
    L.cell1[i] = L.cell0[i] + vec2(cw, ch);
  }
  return L;
}
bool inBox(vec2 p, vec2 a, vec2 b) { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; }
} // namespace

TouchControls::Finger* TouchControls::find(SDL_FingerID id) {
  for (auto& f : fingers) if (f.id == id) return &f;
  return nullptr;
}
bool TouchControls::held(Btn b) const {
  for (auto& f : fingers) if (f.role == Button && f.btn == b) return true;
  return false;
}

void TouchControls::beginFrame() {
  lookAcc = vec2(0);
  pulse.fill(false);
  dropOne = dropAll = false; tap = false;
  menuScroll = 0; menuDragStarted = false;
}

bool TouchControls::buttonAt(const std::string& name, float Wd, float Hd, vec2& centre) const {
  const Layout L = layoutFor(Wd, Hd, size);
  if (name == "stick") { centre = L.stickC; return true; }
  for (int k = 0; k < BtnCount; k++) if (name == BTN[k].id) { centre = L.c[k]; return true; }
  for (int i = 0; i < MORE_N; i++) if (name == MORE[i].id) { centre = (L.cell0[i] + L.cell1[i]) * 0.5f; return true; }
  return false;
}

void TouchControls::press(int btn) {
  switch (btn) {
    case Sneak: sneakOn = !sneakOn; break;
    case Sprint: sprintOn = !sprintOn; break;
    case Cast: pulse[(int)Act::CastQuick] = true; break;
    case Dodge: pulse[(int)Act::Dodge] = true; break;
    case Swap: pulse[(int)Act::SwapHands] = true; break;
    case Pause: pulse[(int)Act::Pause] = true; break;
    case Bag: pulse[(int)Act::Inventory] = true; break;
    case More: moreOpen = !moreOpen; break;
    case Map: pulse[(int)Act::Map] = true; break;
    case Journal: pulse[(int)Act::Journal] = true; break;
    case Spells: pulse[(int)Act::Spellbook] = true; break;
    case Chat: pulse[(int)Act::Chat] = true; break;
    default: break;   // (Jump, Mine, Use: held; Drop: on release, or held)
  }
}

void TouchControls::handle(const SDL_Event& e, bool playing) {
  if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERMOTION && e.type != SDL_FINGERUP) return;
  if (e.tfinger.touchId == SDL_MOUSE_TOUCHID) return;   // a mouse plugged into the phone stays the mouse
  SDL_Window* w = SDL_GetWindowFromID(e.tfinger.windowID);
  if (!w) w = SDL_GetKeyboardFocus();
  if (w) {
    int iw = 0, ih = 0; SDL_GetWindowSize(w, &iw, &ih);
    if (iw > 0 && ih > 0 && (iw != (int)W || ih != (int)H)) { DQ_LOG("touch: the window is %dx%d", iw, ih); laidOut = false; }
    if (iw > 0 && ih > 0) { W = (float)iw; H = (float)ih; }
  }
  if (W <= 0 || H <= 0) return;
  const vec2 p(e.tfinger.x * W, e.tfinger.y * H);
  const Layout L = layoutFor(W, H, size);
  if (e.type == SDL_FINGERDOWN) {
    keyUsed = false;
    if (!playing || !visible()) {   // (a menu: the finger is the pointer)
      if (!playing) { menuDown = true; menuId = e.tfinger.fingerId; menuStart = menuLast = p; menuDir = 0; }
      return;
    }
    Finger f; f.id = e.tfinger.fingerId; f.start = f.last = p; f.t0 = nowSeconds();
    if (moreOpen) {   // the panel takes the touch: an entry, or anywhere else closes it
      f.role = Panel;
      for (int i = 0; i < MORE_N; i++) if (inBox(p, L.cell0[i], L.cell1[i])) {
        const int a = MORE[i].act;
        if (a == -1) dropAll = true;
        else if (a == -2) zoomOn = !zoomOn;
        else if (a >= 0 && a < ACTS) pulse[a] = true;
      }
      moreOpen = false;   // (an entry acts and the panel goes; a touch anywhere else just closes it)
    } else {
      for (int k = 0; k < BtnCount && f.role == None; k++) if (glm::length(p - L.c[k]) < L.r[k] * 1.15f) { f.role = Button; f.btn = k; }
      if (f.role == None) { const int s = hudHotbarSlotAt(p.x, p.y); if (s >= 0) { f.role = Hotbar; if (s < 9) pulse[(int)Act::Hot1 + s] = true; } }
      bool stick = false, look = false;
      for (auto& o : fingers) { stick |= o.role == Stick; look |= o.role == Look; }
      if (f.role == None && !stick && glm::length(p - L.stickC) < L.zoneR) f.role = Stick;
      else if (f.role == None && !look) f.role = Look;
      if (f.role == Button) press(f.btn);
    }
    fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& o) { return o.id == f.id; }), fingers.end());
    fingers.push_back(f);
    return;
  }
  if (menuDown && e.tfinger.fingerId == menuId) {
    if (e.type == SDL_FINGERUP) menuDown = false;
    else if (!playing) {
      const vec2 d = p - menuLast, all = p - menuStart;
      menuLast = p;
      if (menuDir == 0 && glm::length(all) >= L.slop) {
        menuDir = std::fabs(all.y) > std::fabs(all.x) ? 1 : 2;   // (sideways: a slider)
        menuDragStarted = menuDir == 1;
      }
      if (menuDir == 1) menuScroll -= d.y;   // the list follows the finger
    }
  }
  Finger* f = find(e.tfinger.fingerId);
  if (!f) return;
  if (e.type == SDL_FINGERMOTION) {
    const vec2 d = p - f->last;
    f->last = p;
    const float was = f->travel;
    f->travel = std::max(f->travel, glm::length(p - f->start));
    // the view turns once the finger has really moved (a tap or a hold doesn't nudge the aim), from where it started
    if (f->role == Look && f->travel >= L.slop) lookAcc += was < L.slop ? p - f->start : d;
    return;
  }
  // a finger lifted
  if (playing && f->role == Look && !f->mining && f->travel < L.slop) {
    if (aimAtTouch) { tap = true; tapAt = p; }   // (client/play.cpp: what's there is hit or used)
    else useFrames = 2;
  }
  if (f->role == Button && f->btn == Drop && !f->fired) dropOne = true;
  if (f->role == Stick) sprintOn = false;   // (Sprint lasts while you keep moving)
  const SDL_FingerID id = f->id;
  fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& o) { return o.id == id; }), fingers.end());
}

void TouchControls::endFrame(double, bool playing) {
  if (W <= 0 || H <= 0) if (SDL_Window* w = SDL_GetKeyboardFocus()) {   // (before the first touch: the buttons are drawn already)
    int iw = 0, ih = 0; SDL_GetWindowSize(w, &iw, &ih);
    if (iw > 0 && ih > 0) { W = (float)iw; H = (float)ih; }
  }
  move = vec2(0); look = vec2(0);
  down.fill(false); aiming = false;
  if (!playing || !visible()) {
    if (!fingers.empty() || useFrames) releaseAll();
    if (!playing) moreOpen = false;
    if (!visible()) { pulse.fill(false); dropOne = dropAll = false; tap = false; }
    return;
  }
  const Layout L = layoutFor(W, H, size);
  if (!laidOut && W > 0) {   // (the emulator test aims its fingers by these)
    laidOut = true;
    std::string s;
    for (int k = 0; k < BtnCount; k++) s += strFormat(" %s=%d,%d", BTN[k].id, (int)L.c[k].x, (int)L.c[k].y);
    DQ_LOG("touch: window %dx%d, buttons%s stick=%d,%d", (int)W, (int)H, s.c_str(), (int)L.stickC.x, (int)L.stickC.y);
  }
  const double now = nowSeconds();
  bool sprint = sprintOn, attack = false;
  for (auto& f : fingers) {
    if (f.role == Stick) {
      vec2 d = (f.last - L.stickC) / L.stickR;
      if (d.y < -1.25f) sprint = true;   // pushed up past the ring
      const float len = glm::length(d);
      if (len > 1) d /= len;
      if (len < 0.15f) d = vec2(0);
      move = vec2(d.x, -d.y);
    } else if (f.role == Look) {
      if (!f.mining && f.travel < L.slop && now - f.t0 >= HOLD_S) f.mining = true;
      attack |= f.mining;
      // aiming where the finger is: while it rests on the world (and while it mines), not while it turns the view
      if (aimAtTouch && (f.mining || f.travel < L.slop)) { aiming = true; aimAt = f.last; }
    } else if (f.role == Button && f.btn == Drop && !f.fired && now - f.t0 >= DROP_ALL_S) { f.fired = true; dropAll = true; }
  }
  if (move == vec2(0)) sprint = sprint && sprintOn;
  down[(int)Act::Attack] = attack || held(Mine);
  down[(int)Act::Jump] = held(Jump);
  down[(int)Act::Use] = held(Use) || useFrames > 0;
  if (useFrames > 0) useFrames--;
  down[(int)Act::Sneak] = sneakOn;
  down[(int)Act::Sprint] = sprint;
  down[(int)Act::Zoom] = zoomOn;
  if (W > 0 && H > 0) look = lookAcc * (2.6f / H) * lookSpeed;
}

void TouchControls::releaseAll() {
  fingers.clear();
  useFrames = 0; lookAcc = vec2(0);
  move = look = vec2(0);
  down.fill(false); aiming = false; sprintOn = false;
}

void TouchControls::draw(ImDrawList* dl, float Wd, float Hd) const {
  if (!dl || Wd <= 0 || Hd <= 0) return;
  const Layout L = layoutFor(Wd, Hd, size);
  const float op = std::clamp(opacity, 0.2f, 1.f);
  auto A = [&](int a) { return (int)std::lround(std::clamp(a * op, 0.f, 255.f)); };
  const ImU32 ink = IM_COL32(240, 232, 214, A(240)), rim = IM_COL32(240, 232, 214, A(150)), fill = IM_COL32(14, 18, 16, A(120)),
              lit = IM_COL32(255, 176, 92, A(150)), latched = IM_COL32(150, 210, 90, A(140));
  ImFont* font = ImGui::GetFont();
  const float fs = font->FontSize;   // (the pixel font at its own size: scaled to odd sizes its letters read wrong)
  auto label = [&](const char* t, vec2 c, float fs, ImU32 col) {
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
    dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, t);
  };
  auto icon = [&](const char* key, vec2 c, float s) {
    if (!key) return false;
    const IconRef ic = itemIcons().get(key);
    if (!ic.valid()) return false;
    dl->AddImage((ImTextureID)ic.tex, ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f), ImVec2(ic.uv0.x, ic.uv0.y), ImVec2(ic.uv1.x, ic.uv1.y), IM_COL32(255, 255, 255, A(255)));
    return true;
  };
  for (int k = 0; k < BtnCount; k++) {
    const bool on = held((Btn)k) || (k == More && moreOpen), latch = (k == Sneak && sneakOn) || (k == Sprint && sprintOn);
    const vec2 c = L.c[k];
    const float r = L.r[k];
    dl->AddCircleFilled(ImVec2(c.x, c.y), r, on ? lit : latch ? latched : fill, 40);
    dl->AddCircle(ImVec2(c.x, c.y), r, rim, 40, std::max(1.5f, r * 0.05f));
    if (k == Pause) {   // two bars
      const float bw = r * 0.16f, bh = r * 0.46f;
      dl->AddRectFilled(ImVec2(c.x - bw * 2.1f, c.y - bh), ImVec2(c.x - bw * 0.6f, c.y + bh), ink, 2);
      dl->AddRectFilled(ImVec2(c.x + bw * 0.6f, c.y - bh), ImVec2(c.x + bw * 2.1f, c.y + bh), ink, 2);
      continue;
    }
    if (k == Jump) {   // an arrow up over the word
      const float a = r * 0.22f; const vec2 t(c.x, c.y - r * 0.32f);
      dl->AddTriangleFilled(ImVec2(t.x, t.y - a), ImVec2(t.x - a * 1.2f, t.y + a * 0.7f), ImVec2(t.x + a * 1.2f, t.y + a * 0.7f), ink);
      label(BTN[k].label, vec2(c.x, c.y + r * 0.24f), fs, ink);
      continue;
    }
    if (icon(BTN[k].icon, vec2(c.x, c.y - r * 0.18f), r * 0.9f)) label(BTN[k].label, vec2(c.x, c.y + r * 0.55f), fs, ink);
    else label(BTN[k].label, c, fs, ink);
  }
  // the move stick: its ring, and the knob where the thumb holds it
  const Finger* s = nullptr;
  for (auto& f : fingers) if (f.role == Stick) s = &f;
  vec2 knob = L.stickC;
  bool sprint = sprintOn;
  if (s) { vec2 d = s->last - L.stickC; const float len = glm::length(d); sprint = sprint || d.y < -1.25f * L.stickR; if (len > L.stickR) d *= L.stickR / len; knob = L.stickC + d; }
  dl->AddCircleFilled(ImVec2(L.stickC.x, L.stickC.y), L.stickR, IM_COL32(14, 18, 16, A(s ? 110 : 80)), 48);
  dl->AddCircle(ImVec2(L.stickC.x, L.stickC.y), L.stickR, IM_COL32(240, 232, 214, A(s ? 170 : 120)), 48, std::max(2.f, L.b * 0.02f));
  dl->AddCircleFilled(ImVec2(knob.x, knob.y), L.stickR * 0.42f, sprint ? IM_COL32(150, 210, 90, A(210)) : IM_COL32(240, 232, 214, A(s ? 170 : 110)), 32);
  // above the ring: push up past it to sprint
  const float a = L.stickR * 0.14f, ty = L.stickC.y - L.stickR * 1.18f;
  dl->AddTriangleFilled(ImVec2(L.stickC.x, ty - a), ImVec2(L.stickC.x - a, ty + a * 0.6f), ImVec2(L.stickC.x + a, ty + a * 0.6f), IM_COL32(240, 232, 214, A(sprint ? 230 : 110)));
  // the More panel
  if (moreOpen) {
    dl->AddRectFilled(ImVec2(L.panel0.x, L.panel0.y), ImVec2(L.panel1.x, L.panel1.y), IM_COL32(14, 18, 16, 225), L.b * 0.12f);
    dl->AddRect(ImVec2(L.panel0.x, L.panel0.y), ImVec2(L.panel1.x, L.panel1.y), IM_COL32(240, 232, 214, 170), L.b * 0.12f, 0, 2.f);
    for (int i = 0; i < MORE_N; i++) {
      const vec2 a0 = L.cell0[i], a1 = L.cell1[i], c = (a0 + a1) * 0.5f;
      const bool on = MORE[i].act == -2 && zoomOn;
      dl->AddRectFilled(ImVec2(a0.x, a0.y), ImVec2(a1.x, a1.y), on ? IM_COL32(150, 210, 90, 150) : IM_COL32(40, 46, 42, 235), L.b * 0.08f);
      dl->AddRect(ImVec2(a0.x, a0.y), ImVec2(a1.x, a1.y), IM_COL32(240, 232, 214, 120), L.b * 0.08f, 0, 1.5f);
      const float h = a1.y - a0.y;
      const bool hasIcon = icon(MORE[i].icon, vec2(a0.x + h * 0.55f, c.y), h * 0.7f);
      label(MORE[i].label, vec2(hasIcon ? c.x + h * 0.3f : c.x, c.y), fs, IM_COL32(240, 232, 214, 245));
    }
  }
}

} // namespace dq
