// input/input.cpp — actions from keys, mouse buttons and controllers (input/input.js, gamepad.js, padsetup.js).
#include "input/input.h"

namespace dq {

namespace {
constexpr float PAD_DEAD = 0.17f;
std::vector<ActInfo> buildTable() {
  using V = std::vector<std::string>;
  std::vector<ActInfo> t = {
    {Act::Forward, "forward", "Walk forward", V{"KeyW", "ArrowUp"}, {}, true, false},
    {Act::Back, "back", "Walk back", V{"KeyS", "ArrowDown"}, {}, true, false},
    {Act::Left, "left", "Strafe left", V{"KeyA", "ArrowLeft"}, {}, true, false},
    {Act::Right, "right", "Strafe right", V{"KeyD", "ArrowRight"}, {}, true, false},
    {Act::Jump, "jump", "Jump / climb", V{"Space"}, {0}, true, true},
    {Act::Sneak, "sneak", "Sneak", V{"ShiftLeft", "ShiftRight"}, {1}, true, true},
    {Act::Sprint, "sprint", "Sprint", V{"ControlLeft", "ControlRight"}, {10}, true, true},
    {Act::Crawl, "crawl", "Crawl", V{"KeyC"}, {}, true, false},
    {Act::Attack, "attack", "Attack / mine", V{"Mouse0"}, {7}, true, true},
    {Act::Use, "use", "Use / place", V{"Mouse2"}, {6}, true, true},
    {Act::PickBlock, "pick", "Pick block", V{"Mouse1"}, {11}, true, true},
    {Act::Inventory, "inventory", "Inventory", V{"KeyE"}, {3}, true, true},
    {Act::Drop, "drop", "Drop item (Ctrl: stack)", V{"KeyQ"}, {2}, true, true},
    {Act::SwapHands, "swap", "Swap hands", V{"KeyF"}, {13}, true, true},
    {Act::PlaceMode, "place", "Placing mode (slabs, stairs)", V{"KeyR"}, {12}, true, true},
    {Act::View, "view", "Camera: first / shoulder / behind / front", V{"F5", "KeyV"}, {8}, true, true},
    {Act::Shoulder, "shoulder", "Camera: swap shoulder", V{"KeyX"}, {}, true, false},
    {Act::Chat, "chat", "Chat", V{"KeyT", "Enter", "NumpadEnter"}, {}, true, false},
    {Act::Command, "command", "Command", V{"Slash"}, {}, true, false},
    {Act::Skills, "skills", "Skills", V{"KeyK"}, {}, true, false},
    {Act::Map, "map", "Look at your map", V{"KeyM"}, {}, true, false},
    {Act::Spell, "spell", "Change spell", V{"KeyG"}, {}, true, false},
    {Act::Stable, "stable", "Call companions (stable)", V{"KeyH"}, {}, true, false},
    {Act::Journal, "journal", "Journal", V{"KeyJ"}, {}, true, false},
    {Act::Spellbook, "spellbook", "Spellbook", V{"KeyB"}, {}, true, false},
    {Act::CastQuick, "castQuick", "Cast spell (no staff needed)", V{"KeyZ"}, {}, true, false},
    {Act::HideHud, "hideHud", "Hide the HUD", V{"F1"}, {}, true, false},
    {Act::Screenshot, "screenshot", "Screenshot", V{"F2"}, {}, true, false},
    {Act::Debug, "debug", "Debug info", V{"F3"}, {}, true, false},
  };
  for (int i = 0; i < 9; i++) {
    static const char* ids[] = {"slot1", "slot2", "slot3", "slot4", "slot5", "slot6", "slot7", "slot8", "slot9"};
    static const char* labels[] = {"Hotbar slot 1", "Hotbar slot 2", "Hotbar slot 3", "Hotbar slot 4", "Hotbar slot 5", "Hotbar slot 6", "Hotbar slot 7", "Hotbar slot 8", "Hotbar slot 9"};
    static const char* digits[] = {"Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9"};
    t.push_back({(Act)((int)Act::Hot1 + i), ids[i], labels[i], V{digits[i]}, {}, true, false});
  }
  // controller-only rows (PAD_ACTIONS that have no key row)
  t.push_back({Act::HotPrev, "prev", "Hotbar left", V{}, {4, 14}, false, true});
  t.push_back({Act::HotNext, "next", "Hotbar right", V{}, {5, 15}, false, true});
  t.push_back({Act::Pause, "pause", "Pause", V{"Escape"}, {9}, false, true});
  // C++ additions (multiplayer list, zoom): bindable keys, no defaults for zoom
  t.push_back({Act::PlayerList, "playerList", "Player list", V{"Tab"}, {}, true, false});
  t.push_back({Act::Zoom, "zoom", "Zoom", V{}, {}, true, false});
  return t;
}
const char* const MENU_IDS[] = {"menuUp", "menuDown", "menuLeft", "menuRight", "menuOk", "menuBack", "menuTabLeft", "menuTabRight"};
const char* const EXTRA_MENU_IDS[] = {"menuX", "menuY", "menuStart"};
}

const std::vector<ActInfo>& actionTable() { static std::vector<ActInfo> t = buildTable(); return t; }

const char* actName(Act a) {
  int i = (int)a;
  if (a >= Act::MenuUp && a <= Act::MenuTabRight) return MENU_IDS[i - (int)Act::MenuUp];
  if (a >= Act::MenuX && a <= Act::MenuStart) return EXTRA_MENU_IDS[i - (int)Act::MenuX];
  for (auto& r : actionTable()) if (r.act == a) return r.id;
  return "?";
}
const char* actLabel(Act a) { for (auto& r : actionTable()) if (r.act == a) return r.label; return actName(a); }

void Input::defaults() {
  for (auto& k : keyCodes) k.clear();
  for (auto& p : padCodes) p.clear();
  for (auto& r : actionTable()) { keyCodes[(int)r.act] = r.keys; padCodes[(int)r.act] = r.pad; }
  // fixed menu keys / buttons
  keyCodes[(int)Act::MenuUp] = {"ArrowUp"}; keyCodes[(int)Act::MenuDown] = {"ArrowDown"};
  keyCodes[(int)Act::MenuLeft] = {"ArrowLeft"}; keyCodes[(int)Act::MenuRight] = {"ArrowRight"};
  keyCodes[(int)Act::MenuOk] = {"Enter", "NumpadEnter"}; keyCodes[(int)Act::MenuBack] = {"Escape"};
  keyCodes[(int)Act::Pause] = {"Escape"};
  padCodes[(int)Act::MenuUp] = {12}; padCodes[(int)Act::MenuDown] = {13}; padCodes[(int)Act::MenuLeft] = {14}; padCodes[(int)Act::MenuRight] = {15};
  padCodes[(int)Act::MenuOk] = {0}; padCodes[(int)Act::MenuBack] = {1}; padCodes[(int)Act::MenuTabLeft] = {4}; padCodes[(int)Act::MenuTabRight] = {5};
  padCodes[(int)Act::MenuX] = {2}; padCodes[(int)Act::MenuY] = {3}; padCodes[(int)Act::MenuStart] = {9};
}

void Input::reloadBinds() {
  defaults();
  if (!settings) return;
  // keyBinds / padBinds in the original's format; anything else (older number-only saves) is ignored
  for (auto& r : actionTable()) {
    if (settings->keyBinds.is_object()) {
      auto it = settings->keyBinds.find(r.id);
      if (it != settings->keyBinds.end() && it->is_array()) { keyCodes[(int)r.act].clear(); for (auto& c : *it) if (c.is_string()) keyCodes[(int)r.act].push_back(c); }
    }
    if (settings->padBinds.is_object()) {
      auto it = settings->padBinds.find(r.id);
      if (it != settings->padBinds.end() && it->is_array()) { padCodes[(int)r.act].clear(); for (auto& c : *it) if (c.is_number_integer()) padCodes[(int)r.act].push_back(c); }
    }
  }
  keyCodes[(int)Act::Pause] = {"Escape"};   // Esc always pauses and can't be changed
  for (auto& p : padList) p.mapped = settings->padMaps.is_object() && settings->padMaps.contains(p.name);
}

void Input::init(Settings* s) {
  settings = s;
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");   // positions, like the browser's standard mapping
  if (s && !s->keyBinds.is_object()) s->keyBinds = json::object();
  if (s && !s->padBinds.is_object()) s->padBinds = json::object();
  // drop the foundation's number-only binds so the settings file matches the original's format
  if (s) for (auto* j : {&s->keyBinds, &s->padBinds}) for (auto it = j->begin(); it != j->end();) { if (!it->is_array()) it = j->erase(it); else ++it; }
  reloadBinds();
}
SDL_Scancode Input::keyFor(Act a) const {
  for (auto& c : keyCodes[(int)a]) { SDL_Scancode sc = codeToScancode(c); if (sc != SDL_SCANCODE_UNKNOWN) return sc; }
  return SDL_SCANCODE_UNKNOWN;
}
void Input::bindKey(Act a, SDL_Scancode sc) {
  keyCodes[(int)a] = {scancodeToCode(sc)};
  if (settings) settings->keyBinds[actName(a)] = keyCodes[(int)a];
}
void Input::saveBinds() {}

void Input::beginFrame() {
  prev = cur; pulse.fill(false); mouseDelta = vec2(0); wheelY = 0; typed.clear(); lastCode.clear(); lastPadButton = -1; dropAllFlag = false;
  std::memcpy(prevKeys, keys, sizeof keys);
  for (auto& p : padList) p.prevB = p.b;
}

void Input::openDevice(int index) {
  SDL_JoystickID iid = SDL_JoystickGetDeviceInstanceID(index);
  for (auto& p : padList) if (p.id == iid) return;
  PadState p;
  if (SDL_IsGameController(index)) { p.ctl = SDL_GameControllerOpen(index); if (p.ctl) p.joy = SDL_GameControllerGetJoystick(p.ctl); }
  else p.joy = SDL_JoystickOpen(index);
  if (!p.joy) return;
  p.id = SDL_JoystickInstanceID(p.joy);
  const char* n = p.ctl ? SDL_GameControllerName(p.ctl) : SDL_JoystickName(p.joy);
  p.name = n ? n : "Controller";
  p.mapped = settings && settings->padMaps.is_object() && settings->padMaps.contains(p.name);
  DQ_LOG("controller connected: %s%s", p.name.c_str(), p.ctl ? "" : " (not in the controller database)");
  padList.push_back(p);
}

void Input::handle(const SDL_Event& e) {
  switch (e.type) {
    case SDL_KEYDOWN:
      if (e.key.keysym.scancode < SDL_NUM_SCANCODES) {
        keys[e.key.keysym.scancode] = true;
        if (!e.key.repeat) {
          lastCode = scancodeToCode(e.key.keysym.scancode);
          // one-shot latches (input.js pressCode)
          if (!textActive) for (auto& c : keyCodes[(int)Act::Forward]) if (c == lastCode) { double now = nowSeconds(); if (lastW >= 0 && now - lastW < 0.28) doubleW = true; lastW = now; }
          if (!textActive) for (auto& c : keyCodes[(int)Act::Sneak]) if (c == lastCode) sneakLatch = !sneakLatch;
          if (!textActive) for (auto& c : keyCodes[(int)Act::Drop]) if (c == lastCode && (e.key.keysym.mod & KMOD_CTRL)) dropAllFlag = true;
        }
      }
      lastWasPad = false; break;
    case SDL_KEYUP: if (e.key.keysym.scancode < SDL_NUM_SCANCODES) keys[e.key.keysym.scancode] = false; break;
    case SDL_MOUSEBUTTONDOWN: if (e.button.button < 8) { mouseBtn[e.button.button] = true; lastCode = "Mouse" + std::to_string(e.button.button - 1); } lastWasPad = false; break;
    case SDL_MOUSEBUTTONUP: if (e.button.button < 8) mouseBtn[e.button.button] = false; break;
    case SDL_MOUSEMOTION: if (capture) mouseDelta += vec2((float)e.motion.xrel, (float)e.motion.yrel); break;
    case SDL_MOUSEWHEEL: wheelY += e.wheel.preciseY != 0 ? e.wheel.preciseY : (float)e.wheel.y; break;
    case SDL_TEXTINPUT: typed += e.text.text; break;
    case SDL_JOYDEVICEADDED: openDevice(e.jdevice.which); break;
    case SDL_JOYDEVICEREMOVED:
      padList.erase(std::remove_if(padList.begin(), padList.end(), [&](PadState& p) {
        if (p.id != e.jdevice.which) return false;
        DQ_LOG("controller disconnected: %s", p.name.c_str());
        if (p.ctl) SDL_GameControllerClose(p.ctl); else SDL_JoystickClose(p.joy);
        return true; }), padList.end());
      break;
    default: break;
  }
}

static float axisVal(SDL_Joystick* j, int a) { return a >= 0 && a < SDL_JoystickNumAxes(j) ? SDL_JoystickGetAxis(j, a) / 32767.f : 0.f; }

void Input::readPad(PadState& p) {
  std::array<bool, 17> b{}; std::array<float, 4> ax{};
  // an analog button counts by how far it's pulled: on past 45%, off below 25% (gamepad.js padBtn)
  auto hyst = [&](int k, float v, float on, float off) { return p.prevB[k] ? v > off : v > on; };
  const json* map = nullptr;
  if (settings && settings->padMaps.is_object()) { auto it = settings->padMaps.find(p.name); if (it != settings->padMaps.end() && it->is_object()) map = &*it; }
  p.mapped = map != nullptr;
  SDL_Joystick* j = p.joy;
  if (map) {
    // padsetup.js padFromMap
    auto val = [&](const json& src) -> bool {
      if (!src.is_object()) return false;
      if (src.contains("b")) { int bi = src["b"]; return bi >= 0 && bi < SDL_JoystickNumButtons(j) && SDL_JoystickGetButton(j, bi); }
      if (src.contains("h")) { int hi = src["h"], m = src.value("m", 0); return hi >= 0 && hi < SDL_JoystickNumHats(j) && (SDL_JoystickGetHat(j, hi) & m) == m && m; }
      if (src.contains("a")) {
        float a = axisVal(j, src["a"]), v = src.value("v", 1.f), base = src.value("base", 0.f);
        bool hat = std::fabs(base) > 1.05f || std::fabs(std::fabs(v) - 1) >= 0.06f;
        float tol = hat ? 0.12f : std::fabs(v - base) * 0.5f;
        return std::fabs(a - v) < tol;
      }
      return false;
    };
    const json& mb = (*map)["b"]; const json& ma = (*map)["ax"];
    for (int i = 0; i < 16; i++) if (mb.is_array() && i < (int)mb.size()) b[i] = val(mb[i]);
    for (int i = 0; i < 4; i++) if (ma.is_array() && i < (int)ma.size() && ma[i].is_object()) {
      const json& s = ma[i]; ax[i] = clampv((axisVal(j, s.value("a", 0)) - s.value("base", 0.f)) * s.value("s", 1.f), -1.f, 1.f);
    }
  } else if (p.ctl) {
    static const SDL_GameControllerButton order[] = {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
      SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
      SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
      SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_GUIDE};
    for (int k = 0; k < 17; k++) if (order[k] != SDL_CONTROLLER_BUTTON_INVALID) b[k] = SDL_GameControllerGetButton(p.ctl, order[k]);
    auto ga = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(p.ctl, a) / 32767.f; };
    b[6] = hyst(6, ga(SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0.45f, 0.25f);
    b[7] = hyst(7, ga(SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0.45f, 0.25f);
    p.lt = ga(SDL_CONTROLLER_AXIS_TRIGGERLEFT); p.rt = ga(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    ax = {ga(SDL_CONTROLLER_AXIS_LEFTX), ga(SDL_CONTROLLER_AXIS_LEFTY), ga(SDL_CONTROLLER_AXIS_RIGHTX), ga(SDL_CONTROLLER_AXIS_RIGHTY)};
  } else {
    // a pad nobody knows: buttons in their own order, LX LY (LT) RX RY (RT), hat as the d-pad (gamepad.js fallback)
    int nb = SDL_JoystickNumButtons(j), na = SDL_JoystickNumAxes(j);
    for (int k = 0; k < std::min(nb, 12); k++) b[k] = SDL_JoystickGetButton(j, k);
    if (na >= 6) {
      b[6] = b[6] || hyst(6, axisVal(j, 2), 0.3f, 0.1f); b[7] = b[7] || hyst(7, axisVal(j, 5), 0.3f, 0.1f);
      ax = {axisVal(j, 0), axisVal(j, 1), axisVal(j, 3), axisVal(j, 4)};
    } else for (int i = 0; i < std::min(na, 4); i++) ax[i] = axisVal(j, i);
    if (SDL_JoystickNumHats(j) > 0) { Uint8 h = SDL_JoystickGetHat(j, 0); b[12] = h & SDL_HAT_UP; b[13] = h & SDL_HAT_DOWN; b[14] = h & SDL_HAT_LEFT; b[15] = h & SDL_HAT_RIGHT; }
  }
  if (settings && settings->padLayout == "nintendo") { std::swap(b[0], b[1]); std::swap(b[2], b[3]); }
  // radial dead zone, rescaled (gamepad.js stick)
  auto stick = [](float x, float y) { float m = std::hypot(x, y); if (m < PAD_DEAD) return vec2(0); float k = std::min(1.f, (m - PAD_DEAD) / (1 - PAD_DEAD)) / m; return vec2(x * k, y * k); };
  p.lstick = stick(ax[0], ax[1]); p.rstick = stick(ax[2], ax[3]);
  p.ax = {p.lstick.x, p.lstick.y, p.rstick.x, p.rstick.y};
  if (!p.ctl) { p.lt = b[6] ? 1.f : 0.f; p.rt = b[7] ? 1.f : 0.f; }
  p.b = b;
}

bool Input::codeDown(const std::string& c) const {
  if (startsWith(c, "Mouse")) { int m = std::atoi(c.c_str() + 5) + 1; return m > 0 && m < 8 && mouseBtn[m]; }
  SDL_Scancode sc = codeToScancode(c);
  return sc != SDL_SCANCODE_UNKNOWN && keys[sc];
}

void Input::endFrame() {
  nav = ivec2(0); scrollPx = 0;
  for (auto& p : padList) {
    readPad(p);
    for (int k = 0; k < 17; k++) if (p.edge(k)) { lastWasPad = true; if (lastPadButton < 0 && k < 16) lastPadButton = k; }
    if (glm::length(p.lstick) > 0.3f || glm::length(p.rstick) > 0.3f) lastWasPad = true;
    if (setupActive) continue;
    // navigation: d-pad edges, or the left stick with key-repeat (0.38 s, then every 0.11 s)
    int nx = (p.edge(15) ? 1 : 0) - (p.edge(14) ? 1 : 0), ny = (p.edge(13) ? 1 : 0) - (p.edge(12) ? 1 : 0);
    int sx = std::fabs(p.lstick.x) > 0.6f ? (p.lstick.x > 0 ? 1 : -1) : 0, sy = std::fabs(p.lstick.y) > 0.6f ? (p.lstick.y > 0 ? 1 : -1) : 0;
    if ((sx || sy) && !nx && !ny) {
      if (ivec2(sx, sy) != p.navDir) { p.navDir = ivec2(sx, sy); p.navT = 0.38f; nx = sx; ny = sx ? 0 : sy; }
      else { p.navT -= (float)dt; if (p.navT <= 0) { p.navT = 0.11f; nx = sx; ny = sx ? 0 : sy; } }
    } else if (!sx && !sy) p.navDir = ivec2(0);
    if (nx || ny) nav = ivec2(nx, ny);
    if (std::fabs(p.rstick.y) > 0.25f) scrollPx += (p.rstick.y > 0 ? 1.f : -1.f) * (std::fabs(p.rstick.y) - 0.25f) / 0.75f * 900.f * (float)dt;
  }
  // arrow keys step through grids too
  if (!textActive) {
    if (keyPressed(SDL_SCANCODE_LEFT)) nav = ivec2(-1, 0); if (keyPressed(SDL_SCANCODE_RIGHT)) nav = ivec2(1, 0);
    if (keyPressed(SDL_SCANCODE_UP)) nav = ivec2(0, -1); if (keyPressed(SDL_SCANCODE_DOWN)) nav = ivec2(0, 1);
  }
  for (int a = 0; a < (int)Act::Count; a++) {
    bool v = false;
    bool menuAct = (a >= (int)Act::MenuUp && a <= (int)Act::MenuTabRight) || a >= (int)Act::MenuX || a == (int)Act::Pause;
    if (!textActive || menuAct) for (auto& c : keyCodes[a]) if (codeDown(c)) { v = true; break; }
    if (!setupActive) for (auto& p : padList) for (int k : padCodes[a]) if (k >= 0 && k < 17 && p.b[k]) v = true;
    cur[a] = v;
  }
  // keyboard toggles and double-tap W (input.js updateMoveFromKeys)
  bool kSneak = false, kSprint = false, kFwd = false;
  if (!textActive) {
    for (auto& c : keyCodes[(int)Act::Sneak]) kSneak |= codeDown(c);
    for (auto& c : keyCodes[(int)Act::Sprint]) kSprint |= codeDown(c);
    for (auto& c : keyCodes[(int)Act::Forward]) kFwd |= codeDown(c);
  }
  bool padSneak = false;
  if (!setupActive) for (auto& p : padList) for (int k : padCodes[(int)Act::Sneak]) padSneak |= k < 17 && p.b[k];
  if (settings && settings->toggleSneak) cur[(int)Act::Sneak] = sneakLatch || padSneak;
  if (settings && settings->toggleSprint && kSprint) sprintLatch = true;
  if (!kFwd) { doubleW = false; sprintLatch = false; }
  bool sprint = kSprint || doubleW || (settings && settings->toggleSprint && sprintLatch && kFwd);
  // controllers: the stick click latches sprint until the stick comes back to the middle; sneak tapped twice
  // crawls; the camera button taps to change view, held to swap shoulders (gamepad.js)
  bool padView = false;
  if (!setupActive) for (auto& p : padList) {
    for (int k : padCodes[(int)Act::Sprint]) if (k < 17 && p.edge(k)) p.sprintLatch = !p.sprintLatch;
    if (glm::length(p.lstick) < 0.3f) p.sprintLatch = false;
    sprint |= p.sprintLatch;
    for (int k : padCodes[(int)Act::Sneak]) if (k < 17 && p.edge(k)) { double now = nowSeconds(); if (p.sneakTap >= 0 && now - p.sneakTap < 0.32) pulse[(int)Act::Crawl] = true; p.sneakTap = now; }
    bool on = false; for (int k : padCodes[(int)Act::View]) on |= k < 17 && p.b[k];
    if (on) { if (p.viewHold < 0.45f && p.viewHold + dt >= 0.45f) pulse[(int)Act::Shoulder] = true; p.viewHold += (float)dt; padView = true; }
    else { if (p.viewHold > 0 && p.viewHold < 0.45f) pulse[(int)Act::View] = true; p.viewHold = 0; }
  }
  cur[(int)Act::Sprint] = sprint;
  // the pad's view button acts on release (tap) — only keys make View a plain press
  if (padView) { bool k = false; if (!textActive) for (auto& c : keyCodes[(int)Act::View]) k |= codeDown(c); cur[(int)Act::View] = k; }
}

bool Input::padButtonDown(int k) const { for (auto& p : padList) if (k >= 0 && k < 17 && p.b[k]) return true; return false; }
bool Input::padButtonPressed(int k) const { for (auto& p : padList) if (k >= 0 && k < 17 && p.edge(k)) return true; return false; }

bool Input::rawSnapshot(int i, RawPad& out) const {
  if (i < 0 || i >= (int)padList.size() || !padList[i].joy) return false;
  SDL_Joystick* j = padList[i].joy;
  out.name = padList[i].name;
  out.buttons.resize(SDL_JoystickNumButtons(j)); for (size_t k = 0; k < out.buttons.size(); k++) out.buttons[k] = SDL_JoystickGetButton(j, (int)k);
  out.axes.resize(SDL_JoystickNumAxes(j)); for (size_t k = 0; k < out.axes.size(); k++) out.axes[k] = SDL_JoystickGetAxis(j, (int)k) / 32767.f;
  out.hats.resize(SDL_JoystickNumHats(j)); for (size_t k = 0; k < out.hats.size(); k++) out.hats[k] = SDL_JoystickGetHat(j, (int)k);
  return true;
}

vec2 Input::moveAxis() const {
  vec2 m(0);
  if (down(Act::Forward)) m.y += 1; if (down(Act::Back)) m.y -= 1;
  if (down(Act::Right)) m.x += 1; if (down(Act::Left)) m.x -= 1;
  if (!setupActive) for (auto& p : padList) { m.x += p.lstick.x; m.y -= p.lstick.y; }
  if (glm::length(m) > 1) m = glm::normalize(m);
  return m;
}
vec2 Input::lookDelta() const {
  float sens = settings ? settings->sens : 1, ps = settings ? settings->padSens : 1;
  vec2 d = mouseDelta * 0.0022f * sens;
  if (settings && settings->invertY) d.y = -d.y;
  // gamepad.js: rx·|rx|·padSens·2.8·dt (×1.15 sideways)
  if (!setupActive) for (auto& p : padList) {
    float k = ps * 2.8f * (float)dt;
    d += vec2(p.rstick.x * std::fabs(p.rstick.x) * k * 1.15f, p.rstick.y * std::fabs(p.rstick.y) * k * (settings && settings->invertY ? -1.f : 1.f));
  }
  return d;
}
void Input::setCapture(SDL_Window* w, bool on) { (void)w; capture = on; SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE); }
int Input::padCount() const { return (int)padList.size(); }

} // namespace dq
