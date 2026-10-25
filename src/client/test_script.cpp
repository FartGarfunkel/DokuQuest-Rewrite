// client/test_script.cpp — scripted playtests: `dokuquest --smoke-script steps.json` drives the real game the
// way a person does (keys, mouse, a virtual controller), runs commands, checks state and takes screenshots,
// so bugs can be reproduced headless. Each step waits `wait` frames (default 1) after the previous one.
//   {"steps":[{"wait":120,"do":"give","item":"chest"}, {"do":"key","key":"E","hold":3},
//             {"do":"pad","button":"a"}, {"do":"expect","screen":"inventory"}, {"do":"shot","path":"/tmp/a.png"}]}
// do: key{key,hold} type{text} mouse{button,hold,x?,y?} move{dx,dy} pad{button,hold} padaxis{axis,value,hold}
//     cmd{text} look{yaw,pitch (degrees)} lookAt{x,y,z} tp{x,y,z} give{item,n} select{slot} setBlock{x,y,z,key}
//     expect{screen | block:[x,y,z],is | entity,near | held | inv:slot,item,n | count:item,n | cursor:item |
//            container:[x,y,z],slot,item,n | blockEntity:[x,y,z],has:text} report shot{path} save leave open{world} quit
// "rel":true makes x,y,z (setBlock, lookAt, tp, expect block/container) offsets from the block the player stands in.
// pad buttons: a b x y back guide start ls rs lb rb up down left right, and lt / rt (the triggers, pulled fully).
// The log gets "TEST PASS/FAIL ..." lines and a final "TEST DONE passes=N fails=M".
#include "client/test_script.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/screen.h"
#include "sim/commands.h"
#include "sim/player.h"
#include "core/registry.h"
#include "world/world.h"
#include <SDL.h>
#include <map>

namespace dq {

namespace {
struct Later { u64 frame; std::function<void()> fn; };
struct Runner {
  std::vector<json> steps; size_t next = 0; u64 due = 0; bool on = false, done = false;
  std::vector<Later> later;
  SDL_Joystick* vjoy = nullptr;
  int passes = 0, fails = 0;
};
Runner R;

void pushKey(SDL_Scancode sc, bool down) {
  SDL_Event e{}; e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  e.key.state = down ? SDL_PRESSED : SDL_RELEASED; e.key.keysym.scancode = sc; e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
  e.key.windowID = SDL_GetWindowID(app().window);
  SDL_PushEvent(&e);
}
void pushMouseButton(int button, bool down, int x, int y) {
  SDL_Event e{}; e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  e.button.button = (Uint8)button; e.button.state = down ? SDL_PRESSED : SDL_RELEASED; e.button.clicks = 1;
  e.button.x = x; e.button.y = y; e.button.windowID = SDL_GetWindowID(app().window);
  SDL_PushEvent(&e);
}
void pushMotion(int x, int y, int dx, int dy) {
  SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = x; e.motion.y = y; e.motion.xrel = dx; e.motion.yrel = dy;
  e.motion.windowID = SDL_GetWindowID(app().window);
  SDL_PushEvent(&e);
}
int padButton(const std::string& n) {
  static const std::map<std::string, int> M = {
      {"a", SDL_CONTROLLER_BUTTON_A}, {"b", SDL_CONTROLLER_BUTTON_B}, {"x", SDL_CONTROLLER_BUTTON_X}, {"y", SDL_CONTROLLER_BUTTON_Y},
      {"back", SDL_CONTROLLER_BUTTON_BACK}, {"guide", SDL_CONTROLLER_BUTTON_GUIDE}, {"start", SDL_CONTROLLER_BUTTON_START},
      {"ls", SDL_CONTROLLER_BUTTON_LEFTSTICK}, {"rs", SDL_CONTROLLER_BUTTON_RIGHTSTICK}, {"lb", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
      {"rb", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER}, {"up", SDL_CONTROLLER_BUTTON_DPAD_UP}, {"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
      {"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT}, {"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT}};
  auto it = M.find(n); return it == M.end() ? -1 : it->second;
}
int padAxis(const std::string& n) {
  static const std::map<std::string, int> M = {{"leftx", SDL_CONTROLLER_AXIS_LEFTX}, {"lefty", SDL_CONTROLLER_AXIS_LEFTY},
      {"rightx", SDL_CONTROLLER_AXIS_RIGHTX}, {"righty", SDL_CONTROLLER_AXIS_RIGHTY}, {"lt", SDL_CONTROLLER_AXIS_TRIGGERLEFT}, {"rt", SDL_CONTROLLER_AXIS_TRIGGERRIGHT}};
  auto it = M.find(n); return it == M.end() ? -1 : it->second;
}
bool ensurePad() {
  if (R.vjoy) return true;
  SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
  int idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
  if (idx < 0) { DQ_WARN("test: no virtual controller (%s)", SDL_GetError()); return false; }
  R.vjoy = SDL_JoystickOpen(idx);
  DQ_LOG("test: virtual controller attached (%s)", R.vjoy ? "open" : SDL_GetError());
  return R.vjoy != nullptr;
}
std::string screenStack(App& a) {
  std::string s;
  for (auto& sc : a.screens) { if (!s.empty()) s += " > "; s += sc->id(); }
  return s.empty() ? "(none)" : s;
}
void result(bool ok, const std::string& what) {
  (ok ? R.passes : R.fails)++;
  DQ_LOG("TEST %s %s", ok ? "PASS" : "FAIL", what.c_str());
}
float deg(const json& j, const char* k, float d) { return j.value(k, d) * (float)PI / 180.f; }

void runStep(App& a, const json& st) {
  std::string op = st.value("do", "wait");
  u64 f = a.frame;
  GameSession* s = a.session.get();
  Player* p = s ? s->player : nullptr;
  int hold = std::max(1, st.value("hold", 2));
  if (op == "wait") return;
  // "rel": coordinates from the player's feet block
  ivec3 base = p && st.value("rel", false) ? ivec3(glm::floor(p->pos)) : ivec3(0);
  auto X = [&](const json& j, const char* k, int d = 0) { return j.value(k, d) + (k[0] == 'x' ? base.x : k[0] == 'y' ? base.y : base.z); };
  auto P3 = [&](const json& arr) { return ivec3(arr[0].get<int>() + base.x, arr[1].get<int>() + base.y, arr[2].get<int>() + base.z); };
  if (op == "key") {
    SDL_Scancode sc = SDL_GetScancodeFromName(st.value("key", "").c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) { DQ_WARN("test: unknown key '%s'", st.value("key", "").c_str()); return; }
    pushKey(sc, true); R.later.push_back({f + hold, [sc] { pushKey(sc, false); }});
  } else if (op == "type") {
    SDL_Event e{}; e.type = SDL_TEXTINPUT; std::string t = st.value("text", "");
    std::snprintf(e.text.text, sizeof e.text.text, "%s", t.c_str()); e.text.windowID = SDL_GetWindowID(a.window);
    SDL_PushEvent(&e);
  } else if (op == "mouse") {
    std::string b = st.value("button", "left");
    int btn = b == "right" ? SDL_BUTTON_RIGHT : b == "middle" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
    int x = st.value("x", a.width / 2), y = st.value("y", a.height / 2);
    if (st.contains("x")) pushMotion(x, y, 0, 0);
    R.later.push_back({f + 1, [btn, x, y] { pushMouseButton(btn, true, x, y); }});
    R.later.push_back({f + 1 + hold, [btn, x, y] { pushMouseButton(btn, false, x, y); }});
  } else if (op == "move") {
    pushMotion(a.width / 2, a.height / 2, st.value("dx", 0), st.value("dy", 0));
  } else if (op == "pad" && (st.value("button", "") == "lt" || st.value("button", "") == "rt")) {
    int ax = st.value("button", "") == "lt" ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    if (!ensurePad()) return;
    SDL_JoystickSetVirtualAxis(R.vjoy, ax, 32767);
    R.later.push_back({f + hold, [ax] { if (R.vjoy) SDL_JoystickSetVirtualAxis(R.vjoy, ax, 0); }});
  } else if (op == "pad") {
    int b = padButton(st.value("button", "a"));
    if (b < 0 || !ensurePad()) return;
    SDL_JoystickSetVirtualButton(R.vjoy, b, SDL_PRESSED);
    R.later.push_back({f + hold, [b] { if (R.vjoy) SDL_JoystickSetVirtualButton(R.vjoy, b, SDL_RELEASED); }});
  } else if (op == "padaxis") {
    int ax = padAxis(st.value("axis", "leftx"));
    if (ax < 0 || !ensurePad()) return;
    SDL_JoystickSetVirtualAxis(R.vjoy, ax, (Sint16)std::clamp(st.value("value", 0.f) * 32767.f, -32768.f, 32767.f));
    R.later.push_back({f + hold, [ax] { if (R.vjoy) SDL_JoystickSetVirtualAxis(R.vjoy, ax, 0); }});
  } else if (op == "cmd") {
    if (!s) return;
    std::string out = runCommand(*s->level, p, st.value("text", ""));
    DQ_LOG("test: %s -> %s", st.value("text", "").c_str(), out.c_str());
  } else if (op == "look" && p) {
    p->yaw = deg(st, "yaw", p->yaw * 180.f / (float)PI); p->pitch = deg(st, "pitch", 0);
  } else if (op == "lookAt" && p) {
    vec3 t(X(st, "x") + 0.5f, X(st, "y") + 0.5f, X(st, "z") + 0.5f);
    vec3 d = glm::normalize(t - (p->pos + vec3(0, p->eye, 0)));
    p->pitch = std::asin(std::clamp(d.y, -1.f, 1.f)); p->yaw = std::atan2(-d.x, -d.z);
  } else if (op == "tp" && p) {
    if (st.value("rel", false)) p->pos = vec3(base) + vec3(st.value("x", 0.f) + 0.5f, st.value("y", 0.f), st.value("z", 0.f) + 0.5f);
    else p->pos = vec3(st.value("x", p->pos.x), st.value("y", p->pos.y), st.value("z", p->pos.z));
    p->vel = vec3(0);
  } else if (op == "give" && p) {
    int left = p->give(ItemStack(st.value("item", "stone"), st.value("n", 1), st.value("d", 0)));
    if (left) DQ_WARN("test: give %s left %d", st.value("item", "").c_str(), left);
  } else if (op == "select" && p) {
    p->selected = std::clamp(st.value("slot", 0), 0, 8);
  } else if (op == "setBlock" && s) {
    BlockId id = reg().blockId(st.value("key", "air"));
    s->world().setBlock(X(st, "x"), X(st, "y"), X(st, "z"), id, (u8)st.value("meta", 0));
  } else if (op == "expect") {
    if (st.contains("screen")) {
      std::string want = st["screen"].is_null() ? "(none)" : st["screen"].get<std::string>();
      std::string top = a.topScreen() ? a.topScreen()->id() : "(none)";
      result(top == want, "screen " + want + " (open: " + screenStack(a) + ")");
    }
    if (st.contains("block") && s) {
      ivec3 b = P3(st["block"]);
      std::string got = reg().block(s->world().getBlock(b.x, b.y, b.z)).key;
      result(got == st.value("is", "air"), "block " + st["block"].dump() + " is " + st.value("is", "air") + " (got " + got + ")");
    }
    if (st.contains("entity") && s && p) {
      std::string t = st["entity"]; float r = st.value("near", 16.f); int n = 0;
      for (auto& e : s->level->entities()) if (e && !e->removed && e->type == t && glm::distance(e->pos, p->pos) < r) n++;
      bool ok = st.value("none", false) ? n == 0 : n >= st.value("count", 1);
      result(ok, "entity " + t + " within " + std::to_string((int)r) + " count " + std::to_string(n));
    }
    auto stackOk = [&](const ItemStack& got, const std::string& what) {
      std::string want = st.value("item", "");
      bool ok = want.empty() ? got.empty() : got.id == want && (!st.contains("n") || got.n == st.value("n", 1));
      result(ok, what + " has " + (want.empty() ? std::string("nothing") : want + (st.contains("n") ? " x" + std::to_string(st.value("n", 1)) : "")) +
                     " (got " + (got.empty() ? std::string("nothing") : got.id + " x" + std::to_string(got.n)) + ")");
    };
    if (st.contains("inv") && p) { int i = st["inv"]; stackOk(i >= 0 && i < (int)p->inv.size() ? p->inv[i] : ItemStack(), "inv slot " + std::to_string(i)); }
    if (st.contains("cursor") && p) { std::string w = st["cursor"].is_string() ? st["cursor"].get<std::string>() : ""; bool ok = w.empty() ? p->cursor.empty() : p->cursor.id == w; result(ok, "cursor " + (w.empty() ? std::string("empty") : w) + " (got " + (p->cursor.empty() ? std::string("empty") : p->cursor.id + " x" + std::to_string(p->cursor.n)) + ")"); }
    if (st.contains("count") && p) {
      std::string id = st["count"]; int n = 0;
      for (auto& x : p->inv) if (x.id == id) n += x.n;
      int want = st.value("n", 1);
      result(st.value("atLeast", false) ? n >= want : n == want, "player carries " + std::to_string(want) + " " + id + " (got " + std::to_string(n) + ")");
    }
    if (st.contains("container") && s) {
      ivec3 b = P3(st["container"]); int i = st.value("slot", 0);
      json* e = s->world().blockEntity(b.x, b.y, b.z);
      ItemStack got = e && e->contains("items") && (*e)["items"].is_array() && i < (int)(*e)["items"].size() ? ItemStack::fromJson((*e)["items"][i]) : ItemStack();
      stackOk(got, "container " + st["container"].dump() + " slot " + std::to_string(i));
    }
    if (st.contains("blockEntity") && s) {
      ivec3 b = P3(st["blockEntity"]);
      json* e = s->world().blockEntity(b.x, b.y, b.z);
      std::string dump = e ? e->dump() : "null", want = st.value("has", "");
      result(dump.find(want) != std::string::npos, "block entity " + st["blockEntity"].dump() + " has '" + want + "' (got " + dump.substr(0, 200) + ")");
    }
    if (st.contains("held") && p) result(p->held().id == st["held"].get<std::string>(), "held " + st["held"].get<std::string>() + " (got " + p->held().id + ")");
  } else if (op == "report") {
    std::string msg = "screens: " + screenStack(a);
    if (p) {
      char b[256]; std::snprintf(b, sizeof b, " | pos %.1f %.1f %.1f yaw %.0f pitch %.0f hp %.1f realm %d held %s x%d", p->pos.x, p->pos.y, p->pos.z,
                                 p->yaw * 180 / PI, p->pitch * 180 / PI, p->hp, p->realm, p->held().id.c_str(), p->held().n);
      msg += b;
      std::map<std::string, int> near;
      for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->realm == p->realm && glm::distance(e->pos, p->pos) < st.value("near", 48.f)) near[e->type]++;
      msg += " | near:";
      for (auto& [k, n] : near) msg += " " + k + "=" + std::to_string(n);
    }
    DQ_LOG("test report: %s", msg.c_str());
  } else if (op == "shot") {
    a.takeScreenshot(st.value("path", "/tmp/dq_test.png"));
  } else if (op == "save" && s) {
    s->save();
  } else if (op == "leave") {
    a.leaveWorld(st.value("toTitle", true));
  } else if (op == "open") {
    a.openWorld(a.savesDir() / st.value("world", "Test"));
  } else if (op == "quit") {
    a.quit = true;
  } else DQ_WARN("test: unknown step '%s'", op.c_str());
}
} // namespace

void testScriptFrame(App& a) {
  if (!R.on) {
    if (R.done || a.args.smokeScript.is_null()) return;
    const json& j = a.args.smokeScript;
    const json& arr = j.is_array() ? j : j.value("steps", json::array());
    for (auto& s : arr) R.steps.push_back(s);
    R.on = true; R.due = a.frame + (R.steps.empty() ? 1 : R.steps[0].value("wait", 1));
    DQ_LOG("test: script with %zu steps", R.steps.size());
  }
  for (size_t i = 0; i < R.later.size();) {
    if (R.later[i].frame <= a.frame) { auto fn = std::move(R.later[i].fn); R.later.erase(R.later.begin() + i); fn(); }
    else i++;
  }
  while (R.next < R.steps.size() && a.frame >= R.due) {
    const json st = R.steps[R.next++];
    try { runStep(a, st); } catch (const std::exception& e) { result(false, std::string("step threw: ") + e.what()); }
    if (R.next < R.steps.size()) { R.due = a.frame + std::max(0, R.steps[R.next].value("wait", 1)); if (R.due > a.frame) break; }
  }
  if (R.next >= R.steps.size() && R.later.empty() && !R.done) {
    R.done = true; R.on = false;
    DQ_LOG("TEST DONE passes=%d fails=%d", R.passes, R.fails);
    if (a.args.smokeScript.is_object() ? a.args.smokeScript.value("quitAtEnd", true) : true) a.quit = true;
  }
}

} // namespace dq
