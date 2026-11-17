// ui/osk.cpp — the on-screen keyboard (see osk.h). It never takes ImGui's focus: it is drawn on the foreground layer
// and types through ImGui's input queue (characters, Backspace, arrows, Enter, Escape) into whichever text box is
// being edited, so the box stays active and every text box in the game and the launcher works unchanged.
#include "ui/osk.h"
#include "ui/theme.h"
#include "input/input.h"
#include "client/app.h"
#include <imgui_internal.h>
#include <SDL.h>
#include <map>

namespace dq {
namespace ui {

namespace {
enum KeyKind { K_CHAR, K_SHIFT, K_PAGE, K_SPACE, K_LEFT, K_RIGHT, K_ERASE, K_DONE };
struct SpecialKey { KeyKind kind; float w; };
const SpecialKey BOTTOM[] = {{K_SHIFT, 1.5f}, {K_PAGE, 1.5f}, {K_SPACE, 2}, {K_LEFT, 1}, {K_RIGHT, 1}, {K_ERASE, 1}, {K_DONE, 2}};
constexpr int NBOTTOM = (int)(sizeof(BOTTOM) / sizeof(BOTTOM[0]));
constexpr int ROWS = 5, COLS = 10;
const char* const PAGE_NAME[3] = {"abc", "&123", "\xC3\xA0\xC3\xA9"};   // the page key shows the next page: abc, &123, àé

// letters, symbols, accents: four rows of ten keys each (codepoints; Latin-1, which the interface fonts carry)
u32 pageChar(int page, int row, int col) {
  static const char* const ascii[2][4] = {{"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-"},
                                          {"1234567890", "!@#$%^&*()", "-_=+[]{}\\|", ";:'\"<>/?~`"}};
  static const u32 latin[4][10] = {{0xA1, 0xBF, 0xAB, 0xBB, 0xB0, 0xB1, 0xA7, 0xA9, 0xB7, 0xB6},
                                   {0xE0, 0xE1, 0xE2, 0xE4, 0xE3, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9},
                                   {0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF1, 0xF2, 0xF3, 0xF4},
                                   {0xF6, 0xF5, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFF, 0xDF}};
  return page < 2 ? (u32)(u8)ascii[page][row][col] : latin[row][col];
}
u32 upper(u32 c) {
  if (c >= 'a' && c <= 'z') return c - 32;
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
  return c;
}
int rowLen(int row) { return row < 4 ? COLS : NBOTTOM; }
// a key's left edge and width in key units (a row is 10 units)
void keySpan(int row, int col, float& x, float& w) {
  if (row < 4) { x = (float)col; w = 1; return; }
  x = 0; for (int i = 0; i < col; i++) x += BOTTOM[i].w;
  w = BOTTOM[col].w;
}
void tap(ImGuiKey k) { ImGuiIO& io = ImGui::GetIO(); io.AddKeyEvent(k, true); io.AddKeyEvent(k, false); }

// the navigation highlight sits on a text box. ImGui keeps the id and flags of the item a move or the first focus
// lands on only until it is applied (the next frame), so each keyboard remembers what those results named.
// (Sliders are "inputable" too, but A makes a slider active at once, which oskFrame checks first.)
void noteNavResults(Osk& k) {
  ImGuiContext& g = *GImGui;
  if (k.navKinds.size() > 512) k.navKinds.clear();
  for (const ImGuiNavItemData* r : {&g.NavMoveResultLocal, &g.NavMoveResultLocalVisible, &g.NavMoveResultOther, &g.NavInitResult, &g.NavTabbingResultFirst})
    if (r->ID != 0) k.navKinds[r->ID] = (r->ItemFlags & ImGuiItemFlags_Inputable) != 0;
}
bool navOnTextBox(const Osk& k) {
  ImGuiContext& g = *GImGui;
  auto it = k.navKinds.find(g.NavId);
  return it != k.navKinds.end() && it->second;
}
} // namespace

bool oskTextActive() {
  ImGuiContext* c = ImGui::GetCurrentContext();
  if (!c) return false;
  ImGuiContext& g = *c;
  return g.ActiveId != 0 && g.InputTextState.ID == g.ActiveId && !(g.InputTextState.Flags & ImGuiInputTextFlags_ReadOnly);
}

bool oskFrame(Osk& k, const OskPad& pad, bool padInUse, float dt, ImFont* font, float z, const std::string& layout) {
  ImGuiContext& g = *GImGui;
  ImGuiIO& io = g.IO;
  const bool editing = oskTextActive();
  noteNavResults(k);
  if (!k.open) {
    if (editing && padInUse) {
      k.open = true; k.field = g.ActiveId; k.closing = 0; k.page = 0; k.shift = 0; k.row = 1; k.col = 0; k.repDir = ivec2(0); k.repBtn = -1;
      tap(ImGuiKey_End);   // a box opened from the pad starts with everything selected: put the cursor at the end instead
      return true;
    }
    // ImGui starts editing a text box from a controller only on Y; A does it too here (on a button or a slider A has
    // already made that item active, so this only reaches a text box)
    if (padInUse && !editing && pad.pressed(0) && g.ActiveId == 0 && g.NavId != 0 && navOnTextBox(k)) { io.AddKeyEvent(ImGuiKey_GamepadFaceUp, true); ImGui::SetNavCursorVisible(true); }
    static const bool dbg = std::getenv("DQ_OSK_DEBUG") != nullptr;
    if (dbg && pad.pressed(0)) { auto it = k.navKinds.find(g.NavId); DQ_LOG("osk: A on %08x (cursor %d, activated %08x, active %08x, %s)", g.NavId, (int)g.NavCursorVisible, g.NavActivateId, g.ActiveId, it == k.navKinds.end() ? "unknown" : it->second ? "text box" : "not a text box"); }
    return false;
  }
  if (!editing || g.ActiveId != k.field || !padInUse) { k.open = false; return false; }
  if (k.closing) { if (++k.closing > 10) k.open = false; return k.open; }   // done / cancel sent: wait for the box to let go

  const bool multiline = (g.InputTextState.Flags & ImGuiInputTextFlags_Multiline) != 0;
  auto type = [&](u32 c) { io.AddInputCharacter(k.shift ? upper(c) : c); if (k.shift == 1) k.shift = 0; };
  auto done = [&]() { if (multiline) ImGui::ClearActiveID(); else tap(ImGuiKey_Enter); k.closing = 1; };
  auto press = [&](int row, int col) {
    if (row < 4) { type(pageChar(k.page, row, col)); return; }
    switch (BOTTOM[col].kind) {
      case K_SHIFT: k.shift = (k.shift + 1) % 3; break;
      case K_PAGE: k.page = (k.page + 1) % 3; break;
      case K_SPACE: io.AddInputCharacter(' '); break;
      case K_LEFT: tap(ImGuiKey_LeftArrow); break;
      case K_RIGHT: tap(ImGuiKey_RightArrow); break;
      case K_ERASE: tap(ImGuiKey_Backspace); break;
      case K_DONE: done(); break;
      default: break;
    }
  };
  // move the highlight: d-pad or left stick, repeating while held
  ivec2 dir(0);
  if (pad.down[12]) dir.y = -1; else if (pad.down[13]) dir.y = 1; else if (pad.down[14]) dir.x = -1; else if (pad.down[15]) dir.x = 1;
  else if (std::fabs(pad.stick.x) > 0.5f || std::fabs(pad.stick.y) > 0.5f) {
    if (std::fabs(pad.stick.x) > std::fabs(pad.stick.y)) dir.x = pad.stick.x > 0 ? 1 : -1; else dir.y = pad.stick.y > 0 ? 1 : -1;
  }
  bool step = false;
  if (dir != k.repDir) { k.repDir = dir; k.repT = 0.38f; step = dir != ivec2(0); }
  else if (dir != ivec2(0) && (k.repT -= dt) <= 0) { k.repT = 0.09f; step = true; }
  if (step && dir.x) k.col = (k.col + dir.x + rowLen(k.row)) % rowLen(k.row);
  if (step && dir.y) {
    float x, w; keySpan(k.row, k.col, x, w); const float cx = x + w * 0.5f;
    k.row = (k.row + dir.y + ROWS) % ROWS;
    int best = 0; float bd = 1e9f;
    for (int c = 0; c < rowLen(k.row); c++) { keySpan(k.row, c, x, w); float d = std::fabs(x + w * 0.5f - cx); if (d < bd) { bd = d; best = c; } }
    k.col = best;
  }
  // buttons; B and LB / RB repeat while held
  auto rep = [&](int b) {
    if (pad.pressed(b)) { k.repBtn = b; k.repBtnT = 0.4f; return true; }
    if (k.repBtn == b && pad.down[b] && (k.repBtnT -= dt) <= 0) { k.repBtnT = 0.05f; return true; }
    return false;
  };
  if (pad.pressed(0)) press(k.row, k.col);
  if (rep(1)) tap(ImGuiKey_Backspace);
  if (pad.pressed(3)) io.AddInputCharacter(' ');
  if (pad.pressed(2)) k.shift = k.shift ? 0 : 1;
  if (pad.pressed(10)) k.shift = k.shift == 2 ? 0 : 2;
  if (pad.pressed(6)) k.page = (k.page + 2) % 3;
  if (pad.pressed(7)) k.page = (k.page + 1) % 3;
  if (rep(4)) tap(ImGuiKey_LeftArrow);
  if (rep(5)) tap(ImGuiKey_RightArrow);
  if (pad.pressed(9) && !k.closing) done();
  if (pad.pressed(8) && !k.closing) { tap(ImGuiKey_Escape); k.closing = 1; }

  // ---- draw (foreground: over every menu, never part of ImGui's navigation) ----
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 ds = io.DisplaySize;
  const float fs = font->FontSize * io.FontGlobalScale, small = fs * 0.8f;
  const float padPx = 10 * z, gap = 4 * z, keyH = std::max(fs + 16 * z, 34 * z);
  const float W = std::min(ds.x - 16 * z, 660 * z), unit = (W - 2 * padPx - (COLS - 1) * gap) / COLS;
  std::string hints;
  auto hint = [&](std::initializer_list<int> btns, const char* what) {
    if (!hints.empty()) hints += "   \xC2\xB7   ";
    bool first = true;
    for (int b : btns) { if (!first) hints += "/"; hints += padButtonName(b, layout); first = false; }
    hints += " "; hints += what;
  };
  hint({0}, "type"); hint({1}, "erase"); hint({3}, "space"); hint({2}, "shift"); hint({10}, "caps"); hint({6, 7}, "page");
  hint({4, 5}, "cursor"); hint({9}, multiline ? "done" : "enter"); hint({8}, "cancel");
  const float hintsH = font->CalcTextSizeA(small, FLT_MAX, W - 2 * padPx, hints.c_str()).y;
  const float previewH = fs + 14 * z;
  const float H = padPx + previewH + gap * 2 + ROWS * keyH + (ROWS - 1) * gap + gap * 2 + hintsH + padPx;
  ImVec2 p0((ds.x - W) * 0.5f, ds.y - H - 8 * z);
  // the text box itself under the keyboard: put the keyboard at the top instead
  if (g.PlatformImeData.WantVisible && g.PlatformImeData.InputPos.y + g.PlatformImeData.InputLineHeight > p0.y - 6 * z) p0.y = 8 * z;
  p0 = ImVec2(std::round(p0.x), std::round(p0.y));
  const ImVec2 p1 = p0 + ImVec2(W, H);
  dropShadow(dl, p0, p1, 4 * z, 10 * z, 0.5f);
  dl->AddRectFilled(p0, p1, col::pn);
  bevel(dl, p0, p1, col::pnHi, col::pnLo, 2 * z);
  dl->AddRect(p0 - ImVec2(1, 1), p1 + ImVec2(1, 1), col::black, 0, 0, 1);

  // what the box holds, with the cursor
  {
    ImVec2 b0 = p0 + ImVec2(padPx, padPx), b1 = ImVec2(p1.x - padPx, b0.y + previewH);
    dl->AddRectFilled(b0, b1, col::sl);
    bevel(dl, b0, b1, col::slLo, col::slHi, 2 * z);
    const ImGuiInputTextState& st = g.InputTextState;
    const char* t = st.TextA.Data ? st.TextA.Data : "";
    const int len = std::min(st.TextLen, std::max(0, st.TextA.Size - 1));
    const int cur = std::clamp(st.GetCursorPos(), 0, len);
    auto shown = [&](int from, int to) {
      std::string s;
      for (int i = from; i < to;) {
        unsigned c = 0; int n = ImTextCharFromUtf8(&c, t + i, t + to); if (n <= 0) break;
        if (st.Flags & ImGuiInputTextFlags_Password) s += "\xE2\x80\xA2";
        else if (c == '\n') s += "\xC2\xB6";
        else s.append(t + i, (size_t)n);
        i += n;
      }
      return s;
    };
    std::string before = shown(0, cur), after = shown(cur, len);
    const float avail = b1.x - b0.x - 16 * z, wb = font->CalcTextSizeA(fs, FLT_MAX, 0, before.c_str()).x;
    const float off = wb > avail * 0.85f ? avail * 0.85f - wb : 0;
    ImVec2 tp(b0.x + 8 * z + off, b0.y + (previewH - fs) * 0.5f);
    dl->PushClipRect(b0 + ImVec2(4 * z, 0), b1 - ImVec2(4 * z, 0), true);
    dl->AddText(font, fs, ImVec2(std::round(tp.x), std::round(tp.y)), col::text, before.c_str());
    dl->AddText(font, fs, ImVec2(std::round(tp.x + wb), std::round(tp.y)), col::text, after.c_str());
    if (std::fmod(ImGui::GetTime(), 1.0) < 0.6) dl->AddRectFilled(ImVec2(tp.x + wb, tp.y), ImVec2(tp.x + wb + std::max(1.f, 2 * z), tp.y + fs), col::ember);
    dl->PopClipRect();
  }

  // the keys
  const float ky0 = p0.y + padPx + previewH + gap * 2;
  for (int r = 0; r < ROWS; r++) for (int c = 0; c < rowLen(r); c++) {
    float x, w; keySpan(r, c, x, w);
    ImVec2 a(p0.x + padPx + x * (unit + gap), ky0 + r * (keyH + gap));
    ImVec2 b(a.x + w * unit + (w - 1) * gap, a.y + keyH);
    const bool hot = r == k.row && c == k.col;
    btnFace(dl, a, b, hot, hot && pad.down[0]);
    if (hot) dl->AddRect(a - ImVec2(1, 1) * z, b + ImVec2(1, 1) * z, col::ember, 3 * z, 0, 2 * z);
    char buf[8]; const char* label = buf; ImU32 fg = hot ? col::ember : col::text;
    if (r < 4) { u32 ch = pageChar(k.page, r, c); ImTextCharToUtf8(buf, k.shift ? upper(ch) : ch); }
    else switch (BOTTOM[c].kind) {
      case K_SHIFT: label = k.shift == 2 ? "CAPS" : "Shift"; if (k.shift) fg = col::gold; break;
      case K_PAGE: label = PAGE_NAME[(k.page + 1) % 3]; break;
      case K_SPACE: label = "Space"; break;
      case K_LEFT: label = "\xE2\x86\x90"; break;
      case K_RIGHT: label = "\xE2\x86\x92"; break;
      case K_ERASE: label = "Erase"; break;
      case K_DONE: label = multiline ? "Done" : "Enter"; break;
      default: label = ""; break;
    }
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
    ImVec2 tp(std::round((a.x + b.x - ts.x) * 0.5f), std::round((a.y + b.y - ts.y) * 0.5f) - (hot && pad.down[0] ? 0 : 1 * z));
    dl->AddText(font, fs, tp + ImVec2(1, 1) * z, IM_COL32(0, 0, 0, 160), label);
    dl->AddText(font, fs, tp, fg, label);
  }
  // what the buttons do
  dl->AddText(font, small, ImVec2(p0.x + padPx, p1.y - padPx - hintsH), col::muted, hints.c_str(), nullptr, W - 2 * padPx);
  return true;
}

namespace { struct { u64 frame = ~0ull; std::string text; } g_seen; }
bool oskSeenEditing(App& a, std::string* text) {
  const bool on = g_seen.frame != ~0ull && g_seen.frame + 1 >= a.frame;
  if (text) *text = on ? g_seen.text : std::string();
  return on;
}

// ---- the game ----------------------------------------------------------------------------------------------
void oskGame(App& a) {
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  if (!ctx) return;
  static std::map<ImGuiContext*, Osk> perContext;   // one keyboard per split-screen viewport (and the main one)
  Input& in = a.input;
  const int sel = in.selected();
  const std::vector<SDL_JoystickID>* ids = sel >= 0 ? &in.sourceList()[sel].pads : nullptr;
  OskPad pad; std::vector<PadState*> mine;
  for (auto& p : in.pads()) {
    if (ids && std::find(ids->begin(), ids->end(), p.id) == ids->end()) continue;
    mine.push_back(&p);
    for (int b = 0; b < 17; b++) { pad.down[b] = pad.down[b] || p.rb[b]; pad.was[b] = pad.was[b] || p.rprevB[b]; }
    if (glm::length(p.rl) > glm::length(pad.stick)) pad.stick = p.rl;
  }
  // the keyboard + mouse person types on the keyboard unless the controller was the last thing they touched
  const bool padInUse = !mine.empty() && !in.setupActive && (ids && !in.sourceList()[sel].kb ? true : in.usingPad());
  Osk& k = perContext[ctx];
  if (oskTextActive()) {   // (for oskSeenEditing: whichever context has the text box)
    const ImGuiInputTextState& st = ctx->InputTextState;
    g_seen.frame = a.frame; g_seen.text = st.TextA.Data ? std::string(st.TextA.Data, (size_t)std::max(0, st.TextLen)) : std::string();
  }
  if (oskFrame(k, pad, padInUse, (float)a.dt, font(F_BTN), Z(), a.settings.padLayout))
    for (PadState* p : mine) p->oskMute = true;   // the game and the menus see these pads let go next frame
}

// ---- the launcher ------------------------------------------------------------------------------------------
namespace {
struct LauncherOsk {
  Osk k; bool padLast = false, up = false;
  std::array<bool, 17> was{}, hush{};   // hush: held when the keyboard went away, kept from the launcher until let go
  std::map<SDL_JoystickID, SDL_GameController*> pads;
  u64 lastTicks = 0;
};
LauncherOsk& L() { static LauncherOsk s; return s; }
const ImGuiKey PAD_KEYS[16] = {ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft, ImGuiKey_GamepadFaceUp,
  ImGuiKey_GamepadL1, ImGuiKey_GamepadR1, ImGuiKey_GamepadL2, ImGuiKey_GamepadR2, ImGuiKey_GamepadBack, ImGuiKey_GamepadStart,
  ImGuiKey_GamepadL3, ImGuiKey_GamepadR3, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight};
OskPad readSdlPads() {
  auto& s = L();
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (!SDL_IsGameController(i)) continue;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    if (!s.pads.count(id)) if (SDL_GameController* c = SDL_GameControllerOpen(i)) s.pads[id] = c;
  }
  OskPad p; p.was = s.was;
  static const SDL_GameControllerButton order[16] = {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
  for (auto& [id, c] : s.pads) {
    for (int b = 0; b < 16; b++) if (order[b] != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(c, order[b])) p.down[b] = true;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.45f * 32767) p.down[6] = true;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.45f * 32767) p.down[7] = true;
    vec2 st(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f, SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f);
    if (glm::length(st) > 0.25f && glm::length(st) > glm::length(p.stick)) p.stick = st;
  }
  s.was = p.down;
  return p;
}
}

void oskLauncherEvent(const SDL_Event& e) {
  auto& s = L();
  switch (e.type) {
    case SDL_CONTROLLERBUTTONDOWN: s.padLast = true; break;
    case SDL_CONTROLLERAXISMOTION: if (std::abs((int)e.caxis.value) > 20000) s.padLast = true; break;
    case SDL_KEYDOWN: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEWHEEL: case SDL_TEXTINPUT: case SDL_FINGERDOWN: s.padLast = false; break;
    case SDL_CONTROLLERDEVICEREMOVED: {
      auto it = s.pads.find(e.cdevice.which);
      if (it != s.pads.end()) { SDL_GameControllerClose(it->second); s.pads.erase(it); }
      break;
    }
    default: break;
  }
}

void oskLauncherBeforeNewFrame() {
  auto& s = L();
  bool any = s.up; for (bool h : s.hush) any = any || h;
  if (!any) return;
  // the pad's queued key changes go nowhere while the keyboard is up (and a button that closed it stays let go)
  ImGuiContext& g = *GImGui;
  auto held = [&](ImGuiKey key) {
    if (s.up) return true;
    for (int b = 0; b < 16; b++) if (PAD_KEYS[b] == key) return s.hush[b];
    return false;
  };
  for (int n = g.InputEventsQueue.Size - 1; n >= 0; n--) {
    const ImGuiInputEvent& e = g.InputEventsQueue[n];
    if (e.Type == ImGuiInputEventType_Key && ImGui::IsGamepadKey(e.Key.Key) && held(e.Key.Key)) g.InputEventsQueue.erase(g.InputEventsQueue.Data + n);
  }
  ImGuiIO& io = g.IO;
  for (int b = 0; b < 16; b++) if (held(PAD_KEYS[b])) io.AddKeyEvent(PAD_KEYS[b], false);
  if (s.up) for (ImGuiKey k : {ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown}) io.AddKeyAnalogEvent(k, false, 0);
}

void oskLauncherFrame(ImFont* font) {
  auto& s = L();
  const u64 now = SDL_GetTicks64();
  const float dt = s.lastTicks ? std::min(0.1f, (now - s.lastTicks) / 1000.f) : 0.016f;
  s.lastTicks = now;
  OskPad pad = readSdlPads();
  const bool wasUp = s.up;
  s.up = oskFrame(s.k, pad, s.padLast && !s.pads.empty(), dt, font, 1, "");
  for (int b = 0; b < 16; b++) s.hush[b] = s.up ? pad.down[b] : (s.hush[b] || (wasUp && pad.down[b])) && pad.down[b];
}

} // namespace ui
} // namespace dq
