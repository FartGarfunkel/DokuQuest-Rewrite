// input/touch.h — on-screen touch controls for phones (the help book's Touch page): a move stick wherever the left
// thumb lands (pushed up past its ring: sprint), look by dragging anywhere else, tap to use, press and hold to mine,
// and buttons for jump, sneak, mine, use, the bag, the view and pause; a tap on the hotbar picks that slot.
// The fingers feed Input as one more device of the first player. They show once a finger is used and hide again
// when a controller or a keyboard is. In menus, touches stay the mouse (SDL turns a finger into the pointer).
#pragma once
#include "common/common.h"
#include <SDL.h>
#include <vector>

struct ImDrawList;

namespace dq {

class TouchControls {
public:
  enum Btn { Jump, Sneak, Mine, Use, Bag, View, Pause, BtnCount };
  bool enabled = false;      // Android (Input::init)
  bool inUse = false;        // a finger was used last: the controls show

  void beginFrame();                                  // Input::beginFrame
  void handle(const SDL_Event& e, bool playing);      // finger events (playing: in the world, no menu open)
  void endFrame(double dt, bool playing);             // Input::endFrame: holds and the results below
  void releaseAll();                                  // every finger let go (a menu opened, the app went away)
  void draw(ImDrawList* dl, float W, float H) const;  // the stick and the buttons (in the world, while inUse)

  // this frame
  vec2 move{0};              // x = strafe right, y = forward (-1..1)
  vec2 look{0};              // radians, before the look sensitivity
  bool attack = false, use = false, jump = false, sneak = false, sprint = false;
  bool pulseBag = false, pulsePause = false, pulseView = false;
  int pulseSlot = -1;
  float menuScroll = 0;      // a menu dragged up or down by a finger: pixels to scroll it this frame (Input::padScroll)
  bool menuDragStarted = false;   // this frame a finger's drag became a scroll: the button it pressed is let go (ui/theme.cpp padScrollHere)

private:
  enum Role { None, Stick, Look, Button, Hotbar };
  struct Finger { SDL_FingerID id = 0; Role role = None; int btn = -1; vec2 start{0}, last{0}; double t0 = 0; float travel = 0; bool mining = false; };
  std::vector<Finger> fingers;
  float W = 0, H = 0;        // the window, in the coordinates the HUD draws in
  bool sneakOn = false;      // Sneak is a toggle under a thumb
  int useFrames = 0;         // a tap holds Use down this many more frames
  vec2 lookAcc{0};
  // the finger on a menu (the pointer): dragged mostly up or down, it scrolls the list under it
  bool menuDown = false; SDL_FingerID menuId = 0; vec2 menuStart{0}, menuLast{0}; int menuDir = 0;   // 0 not yet, 1 scrolls, 2 doesn't
  Finger* find(SDL_FingerID id);
  bool held(Btn b) const;
};

} // namespace dq
