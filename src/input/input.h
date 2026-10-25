// input/input.h — keyboard, mouse and controllers mapped to game actions (input/controls.js,
// gamepad.js). Bindings are rebindable and saved in settings (keyBinds / padBinds).
#pragma once
#include "common/settings.h"
#include <SDL.h>
#include <array>

namespace dq {

enum class Act : int {
  Forward, Back, Left, Right, Jump, Sneak, Sprint, Attack, Use, PickBlock, Inventory, Drop, Chat, Command, Pause,
  HideHud, Screenshot, Debug, View, SwapHands, PlaceMode, Hot1, Hot2, Hot3, Hot4, Hot5, Hot6, Hot7, Hot8, Hot9,
  HotNext, HotPrev, Map, PlayerList, Zoom,
  Spell, Spellbook, CastQuick, Skills, Journal,   // magic.js G, spellcraft.js B / Z, skills.js K, rpg.js J
  // menu navigation (controllers / keys in menus)
  MenuUp, MenuDown, MenuLeft, MenuRight, MenuOk, MenuBack, MenuTabLeft, MenuTabRight,
  Count
};
const char* actName(Act a);

struct PadState {
  SDL_GameController* ctl = nullptr;
  SDL_JoystickID id = -1;
  std::string name;
  vec2 lstick{0}, rstick{0};
  float lt = 0, rt = 0;
  std::array<bool, SDL_CONTROLLER_BUTTON_MAX> btn{}, prevBtn{};
};

class Input {
public:
  void init(Settings* s);
  void beginFrame();                 // call before polling events
  void handle(const SDL_Event& e);   // feed every SDL event
  void endFrame();
  bool down(Act a) const { return cur[(int)a]; }
  bool pressed(Act a) const { return cur[(int)a] && !prev[(int)a]; }
  bool released(Act a) const { return !cur[(int)a] && prev[(int)a]; }
  vec2 moveAxis() const;             // x = strafe right, y = forward
  vec2 lookDelta() const;            // radians this frame (mouse + right stick * dt)
  float wheel() const { return wheelY; }
  void setCapture(SDL_Window* w, bool on);
  bool captured() const { return capture; }
  bool usingPad() const { return lastWasPad; }
  int padCount() const;
  std::vector<PadState>& pads() { return padList; }
  SDL_Scancode keyFor(Act a) const { return keyBind[(int)a]; }
  void bindKey(Act a, SDL_Scancode sc);
  void saveBinds();
  double dt = 0.016;
  // raw text typed this frame (chat)
  std::string typed;
private:
  Settings* settings = nullptr;
  std::array<bool, (int)Act::Count> cur{}, prev{};
  std::array<SDL_Scancode, (int)Act::Count> keyBind{};
  std::array<int, (int)Act::Count> mouseBind{};          // SDL_BUTTON_* or 0
  std::array<int, (int)Act::Count> padBind{};            // SDL_GameControllerButton + 1, or negative for axes
  bool keys[SDL_NUM_SCANCODES]{};
  bool mouseBtn[8]{};
  vec2 mouseDelta{0};
  float wheelY = 0;
  bool capture = false, lastWasPad = false;
  std::vector<PadState> padList;
  void defaults();
};

} // namespace dq
