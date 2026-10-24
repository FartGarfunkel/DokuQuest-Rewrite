#include "input/input.h"

namespace dq {

static const char* ACT_NAMES[] = {"forward", "back", "left", "right", "jump", "sneak", "sprint", "attack", "use", "pickBlock", "inventory", "drop", "chat",
  "command", "pause", "hideHud", "screenshot", "debug", "view", "swapHands", "placeMode", "hot1", "hot2", "hot3", "hot4", "hot5", "hot6", "hot7", "hot8",
  "hot9", "hotNext", "hotPrev", "map", "playerList", "zoom", "menuUp", "menuDown", "menuLeft", "menuRight", "menuOk", "menuBack", "menuTabLeft", "menuTabRight"};
static_assert(sizeof(ACT_NAMES) / sizeof(ACT_NAMES[0]) == (int)Act::Count, "action names");
const char* actName(Act a) { return ACT_NAMES[(int)a]; }

void Input::defaults() {
  keyBind.fill(SDL_SCANCODE_UNKNOWN); mouseBind.fill(0); padBind.fill(0);
  auto K = [&](Act a, SDL_Scancode s) { keyBind[(int)a] = s; };
  K(Act::Forward, SDL_SCANCODE_W); K(Act::Back, SDL_SCANCODE_S); K(Act::Left, SDL_SCANCODE_A); K(Act::Right, SDL_SCANCODE_D);
  K(Act::Jump, SDL_SCANCODE_SPACE); K(Act::Sneak, SDL_SCANCODE_LSHIFT); K(Act::Sprint, SDL_SCANCODE_LCTRL);
  K(Act::Inventory, SDL_SCANCODE_E); K(Act::Drop, SDL_SCANCODE_Q); K(Act::Chat, SDL_SCANCODE_T); K(Act::Command, SDL_SCANCODE_SLASH);
  K(Act::Pause, SDL_SCANCODE_ESCAPE); K(Act::HideHud, SDL_SCANCODE_F1); K(Act::Screenshot, SDL_SCANCODE_F2); K(Act::Debug, SDL_SCANCODE_F3);
  K(Act::View, SDL_SCANCODE_F5); K(Act::SwapHands, SDL_SCANCODE_F); K(Act::PlaceMode, SDL_SCANCODE_R); K(Act::Map, SDL_SCANCODE_M);
  K(Act::PlayerList, SDL_SCANCODE_TAB); K(Act::Zoom, SDL_SCANCODE_C);
  for (int i = 0; i < 9; i++) keyBind[(int)Act::Hot1 + i] = (SDL_Scancode)(SDL_SCANCODE_1 + i);
  K(Act::MenuUp, SDL_SCANCODE_UP); K(Act::MenuDown, SDL_SCANCODE_DOWN); K(Act::MenuLeft, SDL_SCANCODE_LEFT); K(Act::MenuRight, SDL_SCANCODE_RIGHT);
  K(Act::MenuOk, SDL_SCANCODE_RETURN); K(Act::MenuBack, SDL_SCANCODE_ESCAPE);
  mouseBind[(int)Act::Attack] = SDL_BUTTON_LEFT; mouseBind[(int)Act::Use] = SDL_BUTTON_RIGHT; mouseBind[(int)Act::PickBlock] = SDL_BUTTON_MIDDLE;
  auto P = [&](Act a, int b) { padBind[(int)a] = b + 1; };
  P(Act::Jump, SDL_CONTROLLER_BUTTON_A); P(Act::Inventory, SDL_CONTROLLER_BUTTON_Y); P(Act::Drop, SDL_CONTROLLER_BUTTON_B);
  P(Act::Sneak, SDL_CONTROLLER_BUTTON_RIGHTSTICK); P(Act::Sprint, SDL_CONTROLLER_BUTTON_LEFTSTICK); P(Act::Pause, SDL_CONTROLLER_BUTTON_START);
  P(Act::HotNext, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); P(Act::HotPrev, SDL_CONTROLLER_BUTTON_LEFTSHOULDER); P(Act::Chat, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  P(Act::View, SDL_CONTROLLER_BUTTON_BACK); P(Act::PickBlock, SDL_CONTROLLER_BUTTON_DPAD_DOWN); P(Act::PlayerList, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  P(Act::MenuUp, SDL_CONTROLLER_BUTTON_DPAD_UP); P(Act::MenuDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN); P(Act::MenuLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  P(Act::MenuRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT); P(Act::MenuOk, SDL_CONTROLLER_BUTTON_A); P(Act::MenuBack, SDL_CONTROLLER_BUTTON_B);
  P(Act::MenuTabLeft, SDL_CONTROLLER_BUTTON_LEFTSHOULDER); P(Act::MenuTabRight, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  padBind[(int)Act::Attack] = -(SDL_CONTROLLER_AXIS_TRIGGERRIGHT + 1);
  padBind[(int)Act::Use] = -(SDL_CONTROLLER_AXIS_TRIGGERLEFT + 1);
}

void Input::init(Settings* s) {
  settings = s;
  defaults();
  if (s && s->keyBinds.is_object()) for (int a = 0; a < (int)Act::Count; a++) {
    auto it = s->keyBinds.find(ACT_NAMES[a]);
    if (it != s->keyBinds.end() && it->is_number_integer()) keyBind[a] = (SDL_Scancode)it->get<int>();
  }
  if (s && s->padBinds.is_object()) for (int a = 0; a < (int)Act::Count; a++) {
    auto it = s->padBinds.find(ACT_NAMES[a]);
    if (it != s->padBinds.end() && it->is_number_integer()) padBind[a] = it->get<int>();
  }
}
void Input::bindKey(Act a, SDL_Scancode sc) { keyBind[(int)a] = sc; }
void Input::saveBinds() {
  if (!settings) return;
  for (int a = 0; a < (int)Act::Count; a++) { settings->keyBinds[ACT_NAMES[a]] = (int)keyBind[a]; settings->padBinds[ACT_NAMES[a]] = padBind[a]; }
}

void Input::beginFrame() {
  prev = cur; mouseDelta = vec2(0); wheelY = 0; typed.clear();
  for (auto& p : padList) p.prevBtn = p.btn;
}

void Input::handle(const SDL_Event& e) {
  switch (e.type) {
    case SDL_KEYDOWN: if (e.key.keysym.scancode < SDL_NUM_SCANCODES) keys[e.key.keysym.scancode] = true; lastWasPad = false; break;
    case SDL_KEYUP: if (e.key.keysym.scancode < SDL_NUM_SCANCODES) keys[e.key.keysym.scancode] = false; break;
    case SDL_MOUSEBUTTONDOWN: if (e.button.button < 8) mouseBtn[e.button.button] = true; lastWasPad = false; break;
    case SDL_MOUSEBUTTONUP: if (e.button.button < 8) mouseBtn[e.button.button] = false; break;
    case SDL_MOUSEMOTION: if (capture) mouseDelta += vec2((float)e.motion.xrel, (float)e.motion.yrel); break;
    case SDL_MOUSEWHEEL: wheelY += (float)e.wheel.y; break;
    case SDL_TEXTINPUT: typed += e.text.text; break;
    case SDL_CONTROLLERDEVICEADDED: {
      SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which);
      if (c) { PadState p; p.ctl = c; p.id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)); const char* n = SDL_GameControllerName(c); p.name = n ? n : "Controller"; padList.push_back(p); DQ_LOG("controller connected: %s", p.name.c_str()); }
      break;
    }
    case SDL_CONTROLLERDEVICEREMOVED:
      padList.erase(std::remove_if(padList.begin(), padList.end(), [&](PadState& p) { if (p.id == e.cdevice.which) { SDL_GameControllerClose(p.ctl); return true; } return false; }), padList.end());
      break;
    default: break;
  }
}

void Input::endFrame() {
  // poll pads
  for (auto& p : padList) {
    auto ax = [&](SDL_GameControllerAxis a) { float v = SDL_GameControllerGetAxis(p.ctl, a) / 32767.f; return std::fabs(v) < 0.15f ? 0.f : v; };
    p.lstick = {ax(SDL_CONTROLLER_AXIS_LEFTX), ax(SDL_CONTROLLER_AXIS_LEFTY)};
    p.rstick = {ax(SDL_CONTROLLER_AXIS_RIGHTX), ax(SDL_CONTROLLER_AXIS_RIGHTY)};
    p.lt = ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT); p.rt = ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) p.btn[b] = SDL_GameControllerGetButton(p.ctl, (SDL_GameControllerButton)b);
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) if (p.btn[b] && !p.prevBtn[b]) lastWasPad = true;
    if (glm::length(p.lstick) > 0.3f || glm::length(p.rstick) > 0.3f) lastWasPad = true;
  }
  for (int a = 0; a < (int)Act::Count; a++) {
    bool v = false;
    if (keyBind[a] != SDL_SCANCODE_UNKNOWN && keys[keyBind[a]]) v = true;
    if (mouseBind[a] && mouseBtn[mouseBind[a]]) v = true;
    for (auto& p : padList) {
      int b = padBind[a];
      if (b > 0 && p.btn[b - 1]) v = true;
      if (b < 0) { int axis = -b - 1; float val = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? p.lt : axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? p.rt : 0; if (val > 0.4f) v = true; }
    }
    cur[a] = v;
  }
  if (keys[SDL_SCANCODE_RCTRL]) cur[(int)Act::Sprint] = true;
}

vec2 Input::moveAxis() const {
  vec2 m(0);
  if (down(Act::Forward)) m.y += 1; if (down(Act::Back)) m.y -= 1;
  if (down(Act::Right)) m.x += 1; if (down(Act::Left)) m.x -= 1;
  for (auto& p : padList) { m.x += p.lstick.x; m.y -= p.lstick.y; }
  if (glm::length(m) > 1) m = glm::normalize(m);
  return m;
}
vec2 Input::lookDelta() const {
  float sens = settings ? settings->sens : 1, ps = settings ? settings->padSens : 1;
  vec2 d = mouseDelta * 0.0022f * sens;
  for (auto& p : padList) d += vec2(p.rstick.x * std::fabs(p.rstick.x), p.rstick.y * std::fabs(p.rstick.y)) * (float)dt * 3.2f * ps;
  if (settings && settings->invertY) d.y = -d.y;
  return d;
}
void Input::setCapture(SDL_Window* w, bool on) { capture = on; SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE); }
int Input::padCount() const { return (int)padList.size(); }

} // namespace dq
