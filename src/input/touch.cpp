// input/touch.cpp — the phone's on-screen controls (input/touch.h).
#include "input/touch.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace dq {

int hudHotbarSlotAt(float x, float y);   // ui/hud.cpp: the hotbar slot under a point (-1: none)
extern float g_minimapBottom;            // ui/hud.cpp: the minimap's lower edge (0: no minimap)

namespace {
constexpr double HOLD_S = 0.38;   // a finger held still this long mines; lifted sooner (and still), it was a tap: use
struct Layout { vec2 c[TouchControls::BtnCount]; float r[TouchControls::BtnCount]; vec2 stickHome; float stickR, slop; };
// in window coordinates: the action buttons round the right thumb, the menu buttons along the top right
Layout layoutFor(float W, float H) {
  using T = TouchControls;
  Layout L{};
  const float b = std::clamp(H * 0.15f, 56.f, 230.f), m = H * 0.035f;
  L.c[T::Jump] = {W - m - 0.65f * b, H - m - 1.25f * b};  L.r[T::Jump] = 0.6f * b;
  L.c[T::Sneak] = {W - m - 2.0f * b, H - m - 0.6f * b};   L.r[T::Sneak] = 0.45f * b;
  L.c[T::Mine] = {W - m - 0.65f * b, H - m - 2.75f * b};  L.r[T::Mine] = 0.5f * b;
  L.c[T::Use] = {W - m - 2.0f * b, H - m - 2.0f * b};     L.r[T::Use] = 0.45f * b;
  const float top = g_minimapBottom > 0 ? g_minimapBottom + m * 0.5f : m;   // (under the minimap when it's up there)
  L.c[T::Pause] = {W - m - 0.4f * b, top + 0.4f * b};     L.r[T::Pause] = 0.36f * b;
  L.c[T::Bag] = {W - m - 1.3f * b, top + 0.4f * b};       L.r[T::Bag] = 0.36f * b;
  L.c[T::View] = {W - m - 2.2f * b, top + 0.4f * b};      L.r[T::View] = 0.36f * b;
  L.stickHome = {m + 1.35f * b, H - m - 1.45f * b};
  L.stickR = 0.85f * b;
  L.slop = H * 0.025f;
  return L;
}
const char* const LABEL[TouchControls::BtnCount] = {"Jump", "Sneak", "Mine", "Use", "Bag", "View", ""};
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
  pulseBag = pulsePause = pulseView = false; pulseSlot = -1;
}

void TouchControls::handle(const SDL_Event& e, bool playing) {
  if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERMOTION && e.type != SDL_FINGERUP) return;
  if (e.tfinger.touchId == SDL_MOUSE_TOUCHID) return;   // a mouse plugged into the phone stays the mouse
  SDL_Window* w = SDL_GetWindowFromID(e.tfinger.windowID);
  if (!w) w = SDL_GetKeyboardFocus();
  if (w) {
    int iw = 0, ih = 0; SDL_GetWindowSize(w, &iw, &ih);
    if (iw > 0 && ih > 0 && (iw != (int)W || ih != (int)H)) DQ_LOG("touch: the window is %dx%d", iw, ih);   // (the emulator test aims its taps by it)
    if (iw > 0 && ih > 0) { W = (float)iw; H = (float)ih; }
  }
  if (W <= 0 || H <= 0) return;
  const vec2 p(e.tfinger.x * W, e.tfinger.y * H);
  const Layout L = layoutFor(W, H);
  if (e.type == SDL_FINGERDOWN) {
    inUse = true;
    if (!playing) return;   // (a menu: the finger is the pointer)
    Finger f; f.id = e.tfinger.fingerId; f.start = f.last = p; f.t0 = nowSeconds();
    for (int k = 0; k < BtnCount && f.role == None; k++) if (glm::length(p - L.c[k]) < L.r[k] * 1.2f) { f.role = Button; f.btn = k; }
    if (f.role == None) { const int s = hudHotbarSlotAt(p.x, p.y); if (s >= 0) { f.role = Hotbar; pulseSlot = s; } }
    bool stick = false, look = false;
    for (auto& o : fingers) { stick |= o.role == Stick; look |= o.role == Look; }
    if (f.role == None && !stick && p.x < W * 0.42f && p.y > H * 0.2f) f.role = Stick;
    else if (f.role == None && !look) f.role = Look;
    if (f.role == Button) {
      if (f.btn == Sneak) sneakOn = !sneakOn;
      if (f.btn == Bag) pulseBag = true;
      if (f.btn == Pause) pulsePause = true;
      if (f.btn == View) pulseView = true;
    }
    fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& o) { return o.id == f.id; }), fingers.end());
    fingers.push_back(f);
    return;
  }
  Finger* f = find(e.tfinger.fingerId);
  if (!f) return;
  if (e.type == SDL_FINGERMOTION) {
    const vec2 d = p - f->last;
    f->last = p;
    f->travel = std::max(f->travel, glm::length(p - f->start));
    // the view turns once the finger has really moved: a tap or a hold doesn't nudge the aim
    if (f->role == Look && f->travel >= L.slop) lookAcc += d;
    return;
  }
  if (playing && f->role == Look && !f->mining && f->travel < L.slop) useFrames = 2;
  const SDL_FingerID id = f->id;
  fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& o) { return o.id == id; }), fingers.end());
}

void TouchControls::endFrame(double, bool playing) {
  move = vec2(0); look = vec2(0);
  attack = use = jump = sneak = sprint = false;
  if (!playing) { if (!fingers.empty() || useFrames) releaseAll(); pulseBag = pulsePause = pulseView = false; pulseSlot = -1; return; }
  const Layout L = layoutFor(W, H);
  const double now = nowSeconds();
  for (auto& f : fingers) {
    if (f.role == Stick) {
      vec2 d = (f.last - f.start) / L.stickR;
      sprint = d.y < -1.3f;   // pushed up past the ring
      const float len = glm::length(d);
      if (len > 1) d /= len;
      if (len < 0.12f) d = vec2(0);
      move = vec2(d.x, -d.y);
    } else if (f.role == Look) {
      if (!f.mining && f.travel < L.slop && now - f.t0 >= HOLD_S) f.mining = true;
      attack |= f.mining;
    }
  }
  attack |= held(Mine);
  jump = held(Jump);
  use = held(Use) || useFrames > 0;
  if (useFrames > 0) useFrames--;
  sneak = sneakOn;
  if (W > 0 && H > 0) look = lookAcc * (3.6f / std::max(W, H));
}

void TouchControls::releaseAll() {
  fingers.clear();
  useFrames = 0; lookAcc = vec2(0);
  move = look = vec2(0);
  attack = use = jump = sprint = false;
}

void TouchControls::draw(ImDrawList* dl, float Wd, float Hd) const {
  if (!dl || Wd <= 0 || Hd <= 0) return;
  const Layout L = layoutFor(Wd, Hd);
  const ImU32 ink = IM_COL32(236, 226, 205, 235), rim = IM_COL32(236, 226, 205, 150);
  ImFont* font = ImGui::GetFont();
  for (int k = 0; k < BtnCount; k++) {
    const bool on = held((Btn)k) || (k == Sneak && sneakOn);
    const ImVec2 c(L.c[k].x, L.c[k].y);
    const float r = L.r[k];
    dl->AddCircleFilled(c, r, on ? IM_COL32(236, 226, 205, 120) : IM_COL32(16, 20, 18, 115), 40);
    dl->AddCircle(c, r, rim, 40, std::max(1.5f, r * 0.045f));
    if (k == Pause) {   // two bars
      const float bw = r * 0.16f, bh = r * 0.5f;
      dl->AddRectFilled(ImVec2(c.x - bw * 2.1f, c.y - bh), ImVec2(c.x - bw * 0.6f, c.y + bh), ink, 2);
      dl->AddRectFilled(ImVec2(c.x + bw * 0.6f, c.y - bh), ImVec2(c.x + bw * 2.1f, c.y + bh), ink, 2);
      continue;
    }
    const float fs = std::max(12.f, r * 0.42f);
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, LABEL[k]);
    dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ink, LABEL[k]);
  }
  // the move stick: where the thumb landed (a faint ring at its usual place while no thumb is on it)
  const Finger* s = nullptr;
  for (auto& f : fingers) if (f.role == Stick) s = &f;
  const vec2 base = s ? s->start : L.stickHome;
  vec2 knob = base;
  if (s) { vec2 d = s->last - s->start; const float len = glm::length(d); if (len > L.stickR) d *= L.stickR / len; knob = base + d; }
  dl->AddCircleFilled(ImVec2(base.x, base.y), L.stickR, IM_COL32(16, 20, 18, s ? 95 : 60), 48);
  dl->AddCircle(ImVec2(base.x, base.y), L.stickR, IM_COL32(236, 226, 205, s ? 150 : 90), 48, 2.f);
  dl->AddCircleFilled(ImVec2(knob.x, knob.y), L.stickR * 0.42f, IM_COL32(236, 226, 205, s ? (sprint ? 215 : 150) : 85), 32);
  // above the ring: push up past it to sprint
  const float a = L.stickR * 0.16f, ty = base.y - L.stickR * 1.3f;
  dl->AddTriangleFilled(ImVec2(base.x, ty - a), ImVec2(base.x - a, ty + a * 0.6f), ImVec2(base.x + a, ty + a * 0.6f), IM_COL32(236, 226, 205, sprint ? 220 : (s ? 120 : 70)));
}

} // namespace dq
