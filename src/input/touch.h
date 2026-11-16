// input/touch.h — the phone's on-screen controls, so everything the game does is in reach of a finger:
//  - a fixed move stick bottom left (pushed up past its ring, or with the Sprint button: sprint)
//  - drag anywhere else to look; aiming either where the finger is (tap a block to use it or place on it, tap a monster
//    to hit it, hold on a block to mine it) or at the centre crosshair (tap: use, hold: mine), Options → Controls
//  - buttons round the right thumb: Jump, Mine, Use, Sneak (on/off), Cast (the quick spell), Dodge; by the hotbar: Drop
//    (held: the stack) and Swap hands; along the top: Pause, Bag, More, Map, Journal, Spells, Chat
//  - More: a panel with every other action (skills, spells, companions, crawl, placing mode, the camera, zoom, ...)
//  - a tap on the hotbar picks that slot
// The fingers feed Input as part of the first player's keyboard-and-mouse device. Options → Controls → Touch Controls:
// Auto (hidden while a controller is connected), On, Off. In menus a finger is the pointer, and dragging a list scrolls it.
#pragma once
#include "common/common.h"
#include <SDL.h>
#include <array>
#include <vector>

struct ImDrawList;

namespace dq {

class TouchControls {
public:
  enum Btn { Jump, Mine, Use, Sneak, Cast, Dodge, Sprint, Drop, Swap, Pause, Bag, More, Map, Journal, Spells, Chat, BtnCount };
  enum Mode { Auto = 0, On = 1, Off = 2 };
  static constexpr int ACTS = 64;   // room for every Input action (input.cpp checks Act::Count fits)

  // settings (Input copies them from Settings' extra "touch" each frame) and what Input knows of the other devices
  bool enabled = false;       // a phone (Input::init; DQ_TOUCH=1 on a PC, to try them)
  int mode = Auto;
  bool aimAtTouch = true;     // aim where the finger is (false: at the crosshair)
  float size = 1, opacity = 1, lookSpeed = 1;
  bool padConnected = false;  // a controller is connected (Auto hides the controls)
  bool keyUsed = false;       // a key was pressed since the last touch (Auto hides the controls)
  bool visible() const { return enabled && mode != Off && (mode == On || (!padConnected && !keyUsed)); }

  void beginFrame();                                  // Input::beginFrame
  void handle(const SDL_Event& e, bool playing);      // finger events (playing: in the world, no menu open)
  void endFrame(double dt, bool playing);             // Input::endFrame: holds and the results below
  void releaseAll();                                  // every finger let go (a menu opened, the app went away)
  void draw(ImDrawList* dl, float W, float H) const;  // the controls (in the world, while visible)
  // where a button is drawn (tests aim their fingers by it): jump mine use sneak cast dodge sprint drop swap pause bag
  // more map journal spells chat, stick; and the More panel's entries by their action names (skills, crawl, ...)
  bool buttonAt(const std::string& name, float W, float H, vec2& centre) const;

  // ---- this frame ----
  vec2 move{0};              // x = strafe right, y = forward (-1..1)
  vec2 look{0};              // radians, before the look sensitivity
  std::array<bool, ACTS> down{}, pulse{};   // by Act: held, and pressed this frame
  bool dropOne = false, dropAll = false;    // the Drop button: tapped (one), held (the stack)
  bool aiming = false; vec2 aimAt{0};       // aimAtTouch: a finger points at the world here (window coordinates)
  bool tap = false; vec2 tapAt{0};          // aimAtTouch: a quick tap there (client/play.cpp: a monster is hit, else use)
  float menuScroll = 0;      // a menu dragged up or down by a finger: pixels to scroll it this frame (Input::padScroll)
  bool menuDragStarted = false;   // this frame a finger's drag became a scroll: the button it pressed is let go (ui/theme.cpp padScrollHere)
  bool moreOpen = false;     // the More panel is up

private:
  enum Role { None, Stick, Look, Button, Hotbar, Panel };
  struct Finger { SDL_FingerID id = 0; Role role = None; int btn = -1; vec2 start{0}, last{0}; double t0 = 0; float travel = 0; bool mining = false, fired = false; };
  std::vector<Finger> fingers;
  float W = 0, H = 0;        // the window, in the coordinates the HUD draws in
  bool sneakOn = false, sprintOn = false, zoomOn = false;   // toggles under a thumb
  int useFrames = 0;         // a tap (crosshair aim) holds Use down this many more frames
  vec2 lookAcc{0};
  bool laidOut = false;      // the buttons' places went to the log once (the emulator test aims by them)
  // the finger on a menu (the pointer): dragged mostly up or down, it scrolls the list under it
  bool menuDown = false; SDL_FingerID menuId = 0; vec2 menuStart{0}, menuLast{0}; int menuDir = 0;   // 0 not yet, 1 scrolls, 2 doesn't
  Finger* find(SDL_FingerID id);
  bool held(Btn b) const;
  void press(int btn);       // a button going down
};

} // namespace dq
