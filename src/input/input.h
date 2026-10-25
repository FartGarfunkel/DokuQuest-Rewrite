// input/input.h — keyboard, mouse and controllers mapped to game actions (input/input.js, controls.js,
// gamepad.js, padsetup.js). Bindings are rebindable and saved in settings in the original's format:
//   keyBinds { actionId: ["KeyW", "ArrowUp", "Mouse0", ...] }   (browser key codes)
//   padBinds { actionId: [0, 14] }                               (standard pad layout numbers 0..15)
//   padMaps  { "<pad name>": { b: [...16], ax: [...4] } }        (per-pad button map from the setup wizard)
// Every controller is read into the standard layout first (0 A/bottom 1 B/right 2 X/left 3 Y/top 4 LB 5 RB
// 6 LT 7 RT 8 Back 9 Start 10 L3 11 R3 12-15 d-pad up/down/left/right 16 Guide), so a pad SDL doesn't know
// works once it has been set up, and the Nintendo layout option swaps A/B and X/Y like the original.
#pragma once
#include "common/settings.h"
#include <SDL.h>
#include <array>

namespace dq {

enum class Act : int {
  Forward, Back, Left, Right, Jump, Sneak, Sprint, Attack, Use, PickBlock, Inventory, Drop, Chat, Command, Pause,
  HideHud, Screenshot, Debug, View, SwapHands, PlaceMode, Hot1, Hot2, Hot3, Hot4, Hot5, Hot6, Hot7, Hot8, Hot9,
  HotNext, HotPrev, Map, PlayerList, Zoom,
  // menu navigation (controllers / keys in menus; fixed, not rebindable)
  MenuUp, MenuDown, MenuLeft, MenuRight, MenuOk, MenuBack, MenuTabLeft, MenuTabRight,
  // added with the full controls port (ids follow the original's KEY_ACTIONS)
  Crawl, Shoulder, Skills, Spell, Stable, Journal, Spellbook, CastQuick,
  MenuX, MenuY, MenuStart,
  Count
};
const char* actName(Act a);         // the original action id ("forward", "pick", "slot1", ...)
const char* actLabel(Act a);        // "Walk forward", "Pick block", ...

// The rebindable actions, in the order Settings → Controls lists them (KEY_ACTIONS then the pad-only ones).
struct ActInfo { Act act; const char* id; const char* label; std::vector<std::string> keys; std::vector<int> pad; bool keyRow, padRow; };
const std::vector<ActInfo>& actionTable();
std::string keyName(const std::string& code);              // "KeyW" -> "W", "Mouse0" -> "Left click"
extern const char* const PAD_BUTTON_NAMES[17];               // "A", "B", ... "D-pad →", "Guide"
std::string padButtonName(int standardIndex, const std::string& layout);   // layout-aware (✕ / B jumps...)
std::string scancodeToCode(SDL_Scancode sc);                 // SDL -> browser code ("KeyW")
SDL_Scancode codeToScancode(const std::string& code);

struct PadState {
  SDL_GameController* ctl = nullptr;   // known to SDL's controller database
  SDL_Joystick* joy = nullptr;         // always set (the controller's joystick, or a raw joystick)
  SDL_JoystickID id = -1;
  std::string name;
  bool mapped = false;                 // read through a saved padMaps entry
  std::array<bool, 17> b{}, prevB{};   // standard layout
  std::array<float, 4> ax{};           // LX LY RX RY (dead zone applied, -1..1)
  vec2 lstick{0}, rstick{0};
  float lt = 0, rt = 0;
  float navT = 0; ivec2 navDir{0};     // stick repeat for menus
  float viewHold = 0; double sneakTap = -1; bool sprintLatch = false;
  // A device only drives the game once someone uses it (like the browser, which shows a pad only after a
  // button press): a button, or a stick pushed well past its resting point. Wheels, flight sticks, motion
  // sensors and other joysticks that rest off-centre would otherwise walk the player on their own.
  bool active = false;
  std::array<float, 4> rest{}; bool hasRest = false;   // raw LX LY RX RY when it was connected (unknown pads)
  // a worn stick that doesn't spring back to the middle: where it has sat still (off-centre) for 2 s counts as
  // "let go" too, so it can't creep the player along on its own
  std::array<vec2, 2> driftAt{}, driftLast{}; std::array<float, 2> driftT{}; std::array<bool, 2> drifted{};
  bool edge(int k) const { return b[k] && !prevB[k]; }
  // split screen: what this pad did this frame, worked out once and shared by whichever local player owns it
  bool used = false;                   // did something besides Start: it already plays for the first player
  ivec2 navOut{0}; float scrollOut = 0;
  bool pCrawl = false, pView = false, pShoulder = false, viewOn = false;
};

// Who drives one local player in split screen: the keyboard + mouse and/or some controllers (by joystick instance id).
struct InputSource { bool kb = false; std::vector<SDL_JoystickID> pads; };

// raw joystick state, for the controller setup wizard
struct RawPad { std::vector<u8> buttons; std::vector<float> axes; std::vector<u8> hats; std::string name; };

class Input {
public:
  void init(Settings* s);
  void beginFrame();                 // call before polling events
  void handle(const SDL_Event& e);   // feed every SDL event
  void endFrame();
  bool down(Act a) const { return f->cur[(int)a]; }
  bool pressed(Act a) const { return (f->cur[(int)a] && !f->prev[(int)a]) || f->pulse[(int)a]; }
  bool released(Act a) const { return !f->cur[(int)a] && f->prev[(int)a]; }
  // keyboard / mouse only (a pad button doesn't count): the Inventory key and the Q / E tab keys can share a key
  bool keyOnlyPressed(Act a) const { return f->kcur[(int)a] && !f->kprev[(int)a]; }
  // ---- split screen: read the game through one local player's devices (select(i)), or everything merged (select(-1)) ----
  void setSources(const std::vector<InputSource>& s) { sources = s; if (sel >= (int)sources.size()) select(-1); }
  void select(int i) { sel = (i >= 0 && i < (int)sources.size()) ? i : -1; f = &fr[sel + 1]; }
  int selected() const { return sel; }
  const std::vector<InputSource>& sourceList() const { return sources; }
  bool sharesKey(Act a, Act b) const;   // some key code is bound to both actions
  vec2 moveAxis() const;             // x = strafe right, y = forward
  vec2 lookDelta() const;            // radians this frame (mouse + right stick * dt)
  float wheel() const { return kbSel() ? wheelY : 0.f; }
  void setCapture(SDL_Window* w, bool on);
  bool captured() const { return capture; }
  bool usingPad() const { return lastWasPad; }
  int padCount() const;
  std::vector<PadState>& pads() { return padList; }
  SDL_Scancode keyFor(Act a) const;  // first key bound (for hints)
  void bindKey(Act a, SDL_Scancode sc);
  void saveBinds();                  // (binds already live in settings; kept for compatibility)
  void reloadBinds();                // after Settings → Controls changed settings.keyBinds / padBinds / padMaps
  double dt = 0.016;
  std::string typed;                 // raw text typed this frame (chat)

  // ---- extras used by menus ----
  bool dropAll() const { return dropAllFlag; }          // Ctrl + drop
  bool shiftHeld() const { return kbSel() && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]); }
  bool ctrlHeld() const { return kbSel() && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]); }
  bool keyDown(SDL_Scancode sc) const { return kbSel() && sc > 0 && sc < SDL_NUM_SCANCODES && keys[sc]; }
  bool keyPressed(SDL_Scancode sc) const { return kbSel() && sc > 0 && sc < SDL_NUM_SCANCODES && keys[sc] && !prevKeys[sc]; }
  ivec2 navStep() const { return f->nav; }               // d-pad / stick (with repeat) / arrow keys this frame
  float padScroll() const { return f->scrollPx; }        // right stick: pixels to scroll a menu this frame
  bool padButtonDown(int k) const;                       // any pad, standard layout
  bool padButtonPressed(int k) const;
  // Settings → Controls: the next key / mouse button / pad button pressed (consumed by the reader)
  std::string lastCode;              // browser code of a key or mouse button that went down this frame
  int lastPadButton = -1;            // standard index pressed this frame
  bool setupActive = false;          // controller setup wizard running: pads drive nothing
  bool rawSnapshot(int padIndex, RawPad& out) const;
  bool textActive = false;           // a text box has focus: keys don't trigger actions
  void releaseAll();                 // every key, button and latch let go (window lost focus, a menu took over)
private:
  Settings* settings = nullptr;
  struct Frame { std::array<bool, (int)Act::Count> cur{}, prev{}, pulse{}, kcur{}, kprev{}; ivec2 nav{0}; float scrollPx = 0; };
  Frame fr[5];                       // [0] everything merged, [1 + i] local player i's own devices
  Frame* f = &fr[0];
  int sel = -1;
  std::vector<InputSource> sources;
  bool kbSel() const { return sel < 0 || sources[sel].kb; }
  bool padSel(const PadState& p) const { return sel < 0 || std::find(sources[sel].pads.begin(), sources[sel].pads.end(), p.id) != sources[sel].pads.end(); }
  void computeFrame(Frame& F, bool kb, const std::vector<SDL_JoystickID>* ids);
  std::array<std::vector<std::string>, (int)Act::Count> keyCodes;
  std::array<std::vector<int>, (int)Act::Count> padCodes;
  bool keys[SDL_NUM_SCANCODES]{}, prevKeys[SDL_NUM_SCANCODES]{};
  bool mouseBtn[8]{};
  vec2 mouseDelta{0};
  float wheelY = 0;
  bool capture = false, lastWasPad = false, dropAllFlag = false;
  double lastW = -1; bool doubleW = false, sneakLatch = false, sprintLatch = false;
  std::vector<PadState> padList;
  void defaults();
  bool codeDown(const std::string& c) const;
  void readPad(PadState& p);
  void openDevice(int index);
};

} // namespace dq
