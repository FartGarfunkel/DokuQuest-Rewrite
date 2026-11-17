// ui/osk.h — the on-screen keyboard: a controller player types into any text box (chat, signs, world and player
// names, server addresses, books, renaming, search boxes...). It comes up while an ImGui text box is being edited and
// the controller was the last thing used, and types into that box through ImGui's own input queue, so every text box
// works without changes. Letters / symbols / accents pages, shift and caps, space, erase, cursor moves, done, cancel.
// Pad: d-pad / left stick move, A presses the key, B erases, Y space, X shift, L3 caps, LT / RT symbols,
// LB / RB move the text cursor, Start done (Enter on a one-line box), Back cancels the edit.
// A on a text box starts editing it (ImGui itself only does that on Y).
// Used by the game (oskGame, per split-screen viewport) and the launcher (its own pad reading, launcher_main.cpp).
#pragma once
#include "common/common.h"
#include <imgui.h>
#include <array>
#include <string>
#include <unordered_map>

union SDL_Event;

namespace dq {
class App;
namespace ui {

// one frame of the controller(s) that drive the keyboard, standard layout (0 A 1 B 2 X 3 Y 4 LB 5 RB 6 LT 7 RT
// 8 Back 9 Start 10 L3 11 R3 12-15 d-pad up/down/left/right)
struct OskPad {
  std::array<bool, 17> down{}, was{};
  vec2 stick{0};
  bool pressed(int k) const { return down[k] && !was[k]; }
};

struct Osk {
  bool open = false;
  ImGuiID field = 0;
  int page = 0;            // 0 letters, 1 symbols, 2 accents
  int shift = 0;           // 0 off, 1 the next key, 2 caps lock
  int row = 1, col = 0;
  ivec2 repDir{0}; float repT = 0;   // held direction: repeat
  int repBtn = -1; float repBtnT = 0;   // held B / LB / RB: repeat
  std::unordered_map<ImGuiID, bool> navKinds;   // items ImGui's navigation landed on: a text box or not
  int closing = 0;         // done / cancel sent: frames waited for the text box to let go
};

// The keyboard for the current ImGui context, called after the frame's widgets. padInUse: the controller was the last
// thing used here. Returns true while the keyboard is up: the caller keeps the controller away from the menus and the game.
bool oskFrame(Osk& k, const OskPad& pad, bool padInUse, float dt, ImFont* font, float z, const std::string& layout);
bool oskTextActive();      // a text box is being edited in the current ImGui context
// a text box was being edited in some ImGui context (the main one, the journal's pages, a viewport) last frame, and its text
bool oskSeenEditing(App& a, std::string* text);
// The game: the keyboard for the current ImGui context (one per split-screen viewport), driven by the selected
// person's controllers; mutes those controllers for the game while it is up.
void oskGame(App& a);
// The launcher (one ImGui context; its pads are read straight from SDL here): every SDL event, then
// oskLauncherBeforeNewFrame between the backend's NewFrame and ImGui::NewFrame (keeps the pad away from the launcher's
// own navigation while the keyboard is up), then oskLauncherFrame after the frame's widgets.
void oskLauncherEvent(const ::SDL_Event& e);
void oskLauncherBeforeNewFrame();
void oskLauncherFrame(ImFont* font);

} // namespace ui
} // namespace dq
