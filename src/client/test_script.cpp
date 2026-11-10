// client/test_script.cpp — scripted playtests: `dokuquest --smoke-script steps.json` drives the real game the
// way a person does (keys, mouse, a virtual controller), runs commands, checks state and takes screenshots,
// so bugs can be reproduced headless. Each step waits `wait` frames (default 1) after the previous one.
//   {"steps":[{"wait":120,"do":"give","item":"chest"}, {"do":"key","key":"E","hold":3},
//             {"do":"pad","button":"a"}, {"do":"expect","screen":"inventory"}, {"do":"shot","path":"/tmp/a.png"}]}
// do: key{key,hold} type{text} mouse{button,hold,x?,y?} move{dx,dy} pad{button,hold,id?} padaxis{axis,value,hold,id?}
// (local = which split-screen player the look/tp/give/... steps act on; id = which virtual controller, 0..3: they attach on first use; Start on a free one joins split screen)
//     cmd{text} look{yaw,pitch (degrees)} lookAt{x,y,z} tp{x,y,z} give{item,n,clear?,hand?} select{slot} setBlock{x,y,z,key}
//     anchor (remember the player's block; x/y/z, from/to and block in steps with "rel":true are relative to it)
//     fill{from:[x,y,z],to:[x,y,z],key}
//     expect{screen | block:[x,y,z],is | entity,near} report shot{path} save leave open{world} unjoin{local} quit
//     hurt{entity,amount,tool,type,at} (strike the nearest creature of that type as the player, e.g. tool "axe")
//     kill (the player / "local" dies)  drop{path} (a file dropped on the window) screen{name} (open a screen; "close" pops) data{key,value} wear{item,slot?}
//     expect{screen | block:[x,y,z],is | entity,near | held | inv:slot,item,n | count:item,n | cursor:item |
//            container:[x,y,z],slot,item,n | blockEntity:[x,y,z],has:text} lookAt{entity,job?,dist?: aim at (and stand dist south of) the nearest creature of a type} report{near?,ents?: a type, to list where each stands} shot{path} save leave open{world} quit
// give takes "d" (wear) and "tag" (enchantments, gear xp). pad buttons: a b x y back guide start ls rs lb rb up down left right, lt / rt (triggers).
// The log gets "TEST PASS/FAIL ..." lines and a final "TEST DONE passes=N fails=M".
#include "sim/blockfx.h"
#include "world/rug.h"
#include "sim/magic.h"
#include "client/test_script.h"
#include "gen/village_pieces.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/screen.h"
#include "ui/ui.h"
#include "ui/villager_screens.h"
#include "ui/magic_screens.h"
#include "ui/content_screens.h"
#include "sim/commands.h"
#include "sim/player.h"
#include "sim/combat.h"
#include "render/instruments.h"
#include "core/registry.h"
#include "core/tool_parts.h"
#include "sim/crafting.h"
#include "sim/villagers.h"
#include "sim/machines.h"
#include "world/world.h"
#include <SDL.h>
#include <map>
#include "sim/quiver.h"
#include "gen/structures_api.h"
#include "gen/structures.h"

namespace dq {
void primeFormidi(Level& lv, World& w, int x, int y, int z, const std::string& key);   // sim/wither_storm.cpp

namespace {
struct Later { u64 frame; std::function<void()> fn; };
struct Runner {
  std::vector<json> steps; size_t next = 0; u64 due = 0; bool on = false, done = false;
  std::vector<Later> later;
  SDL_Joystick* vjoy = nullptr;               // virtual pad 0 (the one every step used before)
  std::vector<SDL_Joystick*> vjoys;           // virtual pads 0..3 (split screen: a step's "id" picks one)
  int passes = 0, fails = 0;
  ivec3 anchor{0};           // "anchor": the player's block; steps with "rel": true are relative to it
  ivec3 lastFound{0};        // the first block the last "find" found (explode "at": "rug")
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
bool ensurePad(int id = 0) {
  SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
  while ((int)R.vjoys.size() <= id) {
    int idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    if (idx < 0) { DQ_WARN("test: no virtual controller (%s)", SDL_GetError()); return false; }
    SDL_Joystick* j = SDL_JoystickOpen(idx);
    DQ_LOG("test: virtual controller %d attached (%s)", (int)R.vjoys.size(), j ? "open" : SDL_GetError());
    if (!j) return false;
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MIN);    // triggers rest at the minimum
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
    R.vjoys.push_back(j);
    if (R.vjoys.size() == 1) R.vjoy = j;
  }
  return true;
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
ivec3 at(const json& st, const char* k = nullptr) {
  ivec3 v = k ? ivec3(st[k][0].get<int>(), st[k][1].get<int>(), st[k][2].get<int>()) : ivec3(st.value("x", 0), st.value("y", 0), st.value("z", 0));
  return st.value("rel", false) ? v + R.anchor : v;
}

void runStep(App& a, const json& st) {
  std::string op = st.value("do", "wait");
  u64 f = a.frame;
  GameSession* s = a.session.get();
  Player* p = s ? s->player : nullptr;
  { int li = st.value("local", 0); if (s && li > 0 && li < (int)s->locals.size()) p = s->locals[li].player; }   // "local": 1..3 aims look/tp/give/... at a split-screen partner
  int hold = std::max(0, st.value("hold", 2));   // 0: down and up in the same frame (a quick click at a low frame rate)
  if (op == "wait") return;
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
    // the pointer itself goes there too: ImGui's SDL backend reads the system pointer each frame while no button is
    // down, which put a made-up motion event straight back where the real pointer sat (clicks on the menu tabs missed)
    if (st.contains("x")) { SDL_WarpMouseInWindow(a.window, x, y); pushMotion(x, y, 0, 0); }
    if (st.value("hover", false)) return;   // just move the pointer (tooltips)
    R.later.push_back({f + 1, [btn, x, y] { pushMouseButton(btn, true, x, y); }});
    R.later.push_back({f + 1 + hold, [btn, x, y] { pushMouseButton(btn, false, x, y); }});
  } else if (op == "move") {
    pushMotion(a.width / 2, a.height / 2, st.value("dx", 0), st.value("dy", 0));
  } else if (op == "pad" && (st.value("button", "") == "lt" || st.value("button", "") == "rt")) {
    int ax = st.value("button", "") == "lt" ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    int pid = st.value("id", 0);
    if (!ensurePad(pid)) return;
    SDL_Joystick* vj = R.vjoys[pid];
    SDL_JoystickSetVirtualAxis(vj, ax, 32767);
    // a trigger at rest is the axis minimum, not 0 (0 reads as half pulled, past the 25% let-go mark: LT stayed held
    // after every test press, so the next press never counted as one and a held use kept placing blocks)
    R.later.push_back({f + hold, [ax, vj] { SDL_JoystickSetVirtualAxis(vj, ax, SDL_JOYSTICK_AXIS_MIN); }});
  } else if (op == "pad") {
    int b = padButton(st.value("button", "a")), pid = st.value("id", 0);
    if (b < 0 || !ensurePad(pid)) return;
    SDL_Joystick* vj = R.vjoys[pid];
    SDL_JoystickSetVirtualButton(vj, b, SDL_PRESSED);
    R.later.push_back({f + hold, [b, vj] { SDL_JoystickSetVirtualButton(vj, b, SDL_RELEASED); }});
  } else if (op == "padaxis") {
    int ax = padAxis(st.value("axis", "leftx")), pid = st.value("id", 0);
    if (ax < 0 || !ensurePad(pid)) return;
    SDL_Joystick* vj = R.vjoys[pid];
    bool trig = ax == SDL_CONTROLLER_AXIS_TRIGGERLEFT || ax == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    float v = st.value("value", 0.f); if (trig) v = v * 2 - 1;   // a trigger's 0..1 spans the whole axis
    SDL_JoystickSetVirtualAxis(vj, ax, (Sint16)std::clamp(v * 32767.f, -32768.f, 32767.f));
    Sint16 rest = trig ? SDL_JOYSTICK_AXIS_MIN : 0;
    R.later.push_back({f + hold, [ax, vj, rest] { SDL_JoystickSetVirtualAxis(vj, ax, rest); }});
  } else if (op == "cmd") {
    if (!s) return;
    if (s->level->info.rules.is_object()) s->level->info.rules["commands"] = true;   // tests may always use commands
    std::string out = runCommand(*s->level, p, st.value("text", ""));
    DQ_LOG("test: %s -> %s", st.value("text", "").c_str(), out.c_str());
  } else if (op == "look" && p) {
    p->yaw = deg(st, "yaw", p->yaw * 180.f / (float)PI); p->pitch = deg(st, "pitch", 0);
  } else if (op == "anchor" && p) {
    R.anchor = ivec3(glm::floor(p->pos));
    DQ_LOG("test: anchor %d %d %d", R.anchor.x, R.anchor.y, R.anchor.z);
  } else if (op == "fill" && s) {
    ivec3 a0 = at(st, "from"), a1 = at(st, "to"); BlockId id = reg().blockId(st.value("key", "air"));
    if (!id && st.value("key", "air") != "air") { result(false, "fill: no block called " + st.value("key", "")); return; }
    for (int x = std::min(a0.x, a1.x); x <= std::max(a0.x, a1.x); x++) for (int y = std::min(a0.y, a1.y); y <= std::max(a0.y, a1.y); y++)
      for (int z = std::min(a0.z, a1.z); z <= std::max(a0.z, a1.z); z++) s->world().setBlock(x, y, z, id, (u8)st.value("meta", 0));
  } else if (op == "lookAt" && p) {
    ivec3 b = at(st);
    vec3 t(b.x + st.value("fx", 0.5f), b.y + st.value("fy", 0.5f), b.z + st.value("fz", 0.5f));   // fx/fy/fz: where in the cell to aim
    if (st.contains("entity") && s) {   // lookAt{entity:"villager",job?}: aim at the nearest one's head (it may have wandered off its spot)
      const std::string want = st["entity"].get<std::string>(), job = st.value("job", "");
      Entity* best = nullptr; float bd = 1e9f;
      for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->realm == p->realm && e->type == want) {
        if (!job.empty() && !(e->data.is_object() && e->data.contains("npc") && e->data["npc"].is_object() && e->data["npc"].value("j", "") == job)) continue;
        float d = glm::distance(e->pos, p->pos); if (d < bd) { bd = d; best = e.get(); }
      }
      if (!best) { result(false, "lookAt: no " + want + (job.empty() ? "" : " (" + job + ")") + " near"); return; }
      if (st.contains("dist")) { p->pos = vec3(best->pos.x, p->pos.y, best->pos.z + st.value("dist", 2.5f)); p->vel = vec3(0); }   // stand that far south of it first
      t = best->pos + vec3(0, best->h * st.value("eh", 0.9f), 0);   // its head ("eh": that far up it; 0 its feet): stalls and counters stand in front of the chest
    }
    vec3 d = glm::normalize(t - (p->pos + vec3(0, p->eye, 0)));
    p->pitch = std::asin(std::clamp(d.y, -1.f, 1.f)); p->yaw = std::atan2(-d.x, -d.z);
  } else if (op == "tp" && p) {
    if (st.value("rel", false)) { ivec3 b = at(st); p->pos = vec3(b) + vec3(0.5f, 0, 0.5f); }
    else p->pos = vec3(st.value("x", p->pos.x), st.value("y", p->pos.y), st.value("z", p->pos.z));
    p->vel = vec3(0);
  } else if (op == "give" && p) {
    ItemStack gs(st.value("item", "stone"), st.value("n", 1), st.value("d", 0));
    if (st.contains("tag") && st["tag"].is_object()) gs.tag = st["tag"];   // e.g. {"en":{"sharp":3},"gx":900}
    if (st.value("offhand", false)) { if (p->offhand.empty()) p->offhand.resize(1); p->offhand[0] = gs; return; }   // straight into the other hand
    if (st.value("clear", false)) for (auto& it : p->inv) it = ItemStack();   // an empty bag first (whatever the spawn put in it)
    if (st.value("hand", false) && p->selected < (int)p->inv.size()) { p->inv[p->selected] = gs; return; }   // straight into the held slot
    int left = p->give(gs);
    if (left) DQ_WARN("test: give %s left %d", st.value("item", "").c_str(), left);
  } else if (op == "be" && s) {   // set a block entity (a monster cage: {"type":"spawner","mob":"zombie"})
    ivec3 b = at(st);
    if (json* e = s->world().blockEntity(b.x, b.y, b.z, true)) *e = st.value("data", json::object());
  } else if (op == "select" && p) {
    p->selected = std::clamp(st.value("slot", 0), 0, 8);
  } else if (op == "setBlock" && s) {
    BlockId id = reg().blockId(st.value("key", "air")); ivec3 b = at(st);
    s->world().setBlock(b.x, b.y, b.z, id, (u8)st.value("meta", 0));
  } else if (op == "expect") {
    if (st.contains("screen")) {
      std::string want = st["screen"].is_null() ? "(none)" : st["screen"].get<std::string>();
      std::string top = a.topScreen() ? a.topScreen()->id() : "(none)";
      std::string open = screenStack(a);
      int li = st.value("local", -1);   // split screen: that person's own menu ("local": 0 is the first person)
      if (li >= 0 && s && s->split() && li < (int)s->locals.size() && !s->entered) {
        auto& v = s->locals[li].screens; top = v.empty() ? "(none)" : v.back()->id(); open = "local " + std::to_string(li) + ": " + top;
      }
      result(top == want, "screen " + want + " (open: " + open + ")");
    }
    // where the player stands: "pos": [x,y,z] (a block, "rel" from the anchor) within "within" blocks of its middle;
    // "minY": at least that high (from the anchor with "rel")
    if (st.contains("pos") && p) {
      ivec3 b = at(st, "pos"); vec3 c = vec3(b) + vec3(0.5f, 0, 0.5f);
      float d = glm::distance(vec2(p->pos.x, p->pos.z), vec2(c.x, c.z)) + std::fabs(p->pos.y - c.y), r = st.value("within", 1.f);
      char buf[160]; std::snprintf(buf, sizeof buf, "player near %s (off by %.2f, at %.2f %.2f %.2f)", st["pos"].dump().c_str(), d, p->pos.x, p->pos.y, p->pos.z);
      result(d <= r, buf);
    }
    if (st.contains("minY") && p) {
      float want = st.value("minY", 0.f) + (st.value("rel", false) ? (float)R.anchor.y : 0.f);
      char buf[120]; std::snprintf(buf, sizeof buf, "player at least at y %.1f (got %.2f)", want, p->pos.y);
      result(p->pos.y >= want, buf);
    }
    if (st.contains("rugAt") && s) {   // the rug on the block there: "rug" its carpet ("" none, world/rug.h)
      ivec3 b = at(st, "rugAt"); const BlockId r = rugBlockOf(s->world().getCoat(b.x, b.y, b.z));
      const std::string got = r ? reg().block(r).key : "";
      result(got == st.value("rug", ""), "rug on " + st["rugAt"].dump() + " is '" + st.value("rug", "") + "' (got '" + got + "')");
    }
    if (st.contains("block") && s) {
      ivec3 b = at(st, "block");
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
    if (st.contains("cursor") && p) {
      std::string w = st["cursor"].is_string() ? st["cursor"].get<std::string>() : "";
      bool ok = w.empty() ? p->cursor.empty() : p->cursor.id == w;
      result(ok, "cursor " + (w.empty() ? std::string("empty") : w) + " (got " + (p->cursor.empty() ? std::string("empty") : p->cursor.id + " x" + std::to_string(p->cursor.n)) + ")");
    }
    if (st.contains("count") && p) {
      std::string id = st["count"]; int n = 0;
      for (auto& x : p->inv) if (x.id == id) n += x.n;
      int want = st.value("n", 1);
      result(st.value("atLeast", false) ? n >= want : n == want, "player carries " + std::to_string(want) + " " + id + " (got " + std::to_string(n) + ")");
    }
    if (st.contains("container") && s) {
      ivec3 b = at(st, "container"); int i = st.value("slot", 0);
      json* e = s->world().blockEntity(b.x, b.y, b.z);
      ItemStack got = e && e->contains("items") && (*e)["items"].is_array() && i < (int)(*e)["items"].size() ? ItemStack::fromJson((*e)["items"][i]) : ItemStack();
      stackOk(got, "container " + st["container"].dump() + " slot " + std::to_string(i));
    }
    if (st.contains("blockEntity") && s) {
      ivec3 b = at(st, "blockEntity");
      json* e = s->world().blockEntity(b.x, b.y, b.z);
      std::string dump = e ? e->dump() : "null", want = st.value("has", "");
      result(dump.find(want) != std::string::npos, "block entity " + st["blockEntity"].dump() + " has '" + want + "' (got " + dump.substr(0, 200) + ")");
    }
    if (st.contains("held") && p) result(p->held().id == st["held"].get<std::string>(), "held " + st["held"].get<std::string>() + " (got " + p->held().id + ")");
  } else if (op == "hurt" && s && p) {   // strike the nearest creature of a type as the player would (tool kind, the ray from the eyes)
    std::string t = st.value("entity", ""); Entity* best = nullptr; float bd = st.value("near", 24.f);
    for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->type == t && e->hp > 0 && glm::distance(e->pos, p->pos) < bd) { bd = glm::distance(e->pos, p->pos); best = e.get(); }
    if (!best) { result(false, "hurt: no " + t + " near"); return; }
    DamageSource ds; ds.attacker = p->id; ds.tool = st.value("tool", ""); ds.type = st.value("type", ""); ds.knock = st.value("knock", -1.f); ds.ench = st.value("ench", 0);   // ench: an arrow from a bow of so many enchantment levels
    ds.hasRay = true; ds.rayO = p->pos + vec3(0, p->eye, 0); ds.rayD = glm::normalize(best->pos + vec3(0, best->h * st.value("at", 0.5f), 0) - ds.rayO);
    float dealt = damageEntity(*s->level, *best, st.value("amount", 4.f), ds);
    DQ_LOG("test: hurt %s #%u by %.1f -> dealt %.1f, hp %.1f", t.c_str(), best->id, st.value("amount", 4.f), dealt, best->hp);
  } else if (op == "report") {
    std::string msg = "screens: " + screenStack(a);
    if (s && s->split()) for (int i = 0; i < (int)s->locals.size(); i++) if (!s->locals[i].screens.empty() && !s->entered) msg += " | local " + std::to_string(i) + " menu " + s->locals[i].screens.back()->id();
    if (p) {
      char b[256]; std::snprintf(b, sizeof b, " | pos %.1f %.1f %.1f yaw %.0f pitch %.0f hp %.1f realm %d held %s x%d", p->pos.x, p->pos.y, p->pos.z,
                                 p->yaw * 180 / PI, p->pitch * 180 / PI, p->hp, p->realm, p->held().id.c_str(), p->held().n);
      msg += b;
      std::map<std::string, int> inv;
      for (auto& st : p->inv) if (!st.empty()) inv[st.id] += st.n;
      msg += " | inv:"; for (auto& [k, n] : inv) msg += " " + k + "=" + std::to_string(n);
      if (!p->quiver.empty() && !p->quiver[0].empty()) { int qn = 0; for (auto& q : quiverLoad(p->quiver[0])) qn += q.empty() ? 0 : q.n; msg += " | quiver " + p->quiver[0].id + " " + std::to_string(qn); }
      std::map<std::string, int> near;
      for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->realm == p->realm && glm::distance(e->pos, p->pos) < st.value("near", 48.f)) near[e->type]++;
      if (!knownSpells(*p).empty()) {   // magic: the hands, ranks and mana
        MagicState& m = magicState(*s->level, *p);
        std::string L = offSpell(*p), R0 = activeSpell(*p);
        msg += " | spells R " + spellTitle(*p, R0) + " L " + (L.empty() ? std::string("-") : spellTitle(*p, L)) + " mana " + std::to_string((int)m.mana);
        if (!m.bladeEl.empty()) msg += " blade " + m.bladeEl;
      }
      { const InstrumentState& I = instruments(); char ib[96]; std::snprintf(ib, sizeof ib, " | compass %d death %d clock %d grave %s", I.compassF, I.deathF, I.clockF, p->grave.is_object() ? "yes" : "no"); msg += ib; }
      msg += " | near:";
      for (auto& [k, n] : near) msg += " " + k + "=" + std::to_string(n);
    }
    DQ_LOG("test report: %s", msg.c_str());
    if (s && p && st.contains("ents") && st["ents"].is_string()) {   // report{ents:"villager"}: where each one of that type stands (to lookAt / tp to it)
      const std::string want = st["ents"].get<std::string>();
      for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->realm == p->realm && e->type == want && glm::distance(e->pos, p->pos) < st.value("near", 48.f))
        DQ_LOG("test report ent: %s #%u at %.1f %.1f %.1f job %s", e->type.c_str(), e->id, e->pos.x, e->pos.y, e->pos.z,
               e->data.is_object() && e->data.contains("npc") && e->data["npc"].is_object() ? e->data["npc"].value("j", std::string()).c_str() : "-");
    }
  } else if (op == "shot") {
    if (p) DQ_LOG("test: shot pos %.2f %.2f %.2f yaw %.1f pitch %.1f", p->pos.x, p->pos.y, p->pos.z, p->yaw * 180.f / (float)PI, p->pitch * 180.f / (float)PI);
    a.takeScreenshot(st.value("path", "/tmp/dq_test.png"));
  } else if (op == "drop") {   // a file dragged onto the window
    std::string f = st.value("path", "");
    SDL_Event e{}; e.type = SDL_DROPFILE; e.drop.file = SDL_strdup(f.c_str()); e.drop.windowID = SDL_GetWindowID(a.window);
    SDL_PushEvent(&e);
  } else if (op == "screen") {   // open a screen: players creator skineditor inventory pause ...
    std::string n = st.value("name", "");
    if (n == "creator") a.pushScreen(makeCreatorScreen());
    else if (n == "skineditor") a.pushScreen(makeSkinEditorScreen());
    else if (n == "close") { if (a.topScreen()) a.popScreen(); }
    else if (n == "hud_edit") a.pushScreen(makeHudEditScreen());
    else if (n == "board" && p) a.pushScreen(makeQuestBoardScreen(ivec3(ifloor(p->pos.x), ifloor(p->pos.y), ifloor(p->pos.z))));   // the village's quest board (stand in the village)
    else if (n == "path") a.pushScreen(makePathScreen("knight"));
    else if (n == "brew" && p) a.pushScreen(makeBrewScreen(ivec3(ifloor(p->pos.x), ifloor(p->pos.y), ifloor(p->pos.z)) + ivec3(st.value("dx", 0), st.value("dy", 0), st.value("dz", 0))));
    else if (n == "enchant" && p) a.pushScreen(makeEnchantScreen(ivec3(ifloor(p->pos.x), ifloor(p->pos.y), ifloor(p->pos.z)) + ivec3(st.value("dx", 0), st.value("dy", 0), st.value("dz", 0))));
    else if (n == "waystones") a.pushScreen(makeWaystoneScreen(""));
    else if (n == "places") a.pushScreen(makePlacesScreen());
    else if (n == "travel_journal") a.pushScreen(makeTravelJournalScreen());
    else if (auto sc = makeScreenByName(a, n)) a.pushScreen(std::move(sc));
  } else if (op == "mobdata" && s && p) {   // set data[key] = value on the nearest creature of a type
    std::string t = st.value("entity", ""); Entity* best = nullptr; float bd = st.value("near", 24.f);
    for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->type == t && glm::distance(e->pos, p->pos) < bd) { bd = glm::distance(e->pos, p->pos); best = e.get(); }
    if (best) best->data[st.value("key", "")] = st.contains("value") ? st["value"] : json(); else result(false, "mobdata: no " + t + " near");
  } else if (op == "mob" && s && p) {   // the nearest creature of a type: hp, where, its data (logged); "hp" sets its health
    std::string t = st.value("entity", ""); Entity* best = nullptr; float bd = st.value("near", 64.f);
    for (auto& e : s->level->entities()) if (e && !e->removed && e.get() != p && e->type == t && glm::distance(e->pos, p->pos) < bd) { bd = glm::distance(e->pos, p->pos); best = e.get(); }
    if (!best) { result(false, "mob: no " + t + " near"); return; }
    if (st.contains("hp")) best->hp = st["hp"].get<float>();
    std::string d = best->data.dump(); if (d.size() > 600) d = d.substr(0, 600) + "...";
    DQ_LOG("test: mob %s hp %.1f/%.1f at %.1f,%.1f,%.1f yaw %.2f | %s", t.c_str(), best->hp, best->maxHp, best->pos.x, best->pos.y, best->pos.z, best->yaw, d.c_str());
  } else if (op == "formidi" && s && p) {   // light a Formidibomb (or "key":"super_tnt") at x,y,z as flint and steel would
    const ivec3 b = at(st); primeFormidi(*s->level, s->world(), b.x, b.y, b.z, st.value("key", std::string("formidibomb")));
  } else if (op == "data" && p) {   // set Player::data[key] = value (hideGear, cape ...)
    p->data[st.value("key", "")] = st.contains("value") ? st["value"] : json();
  } else if (op == "explode" && s) {   // a blast there ("power", default 3; "at": "rug" the first rug "find" found)
    ivec3 b = at(st); if (st.value("at", std::string()) == "found" || st.value("at", std::string()) == "rug") b = R.lastFound;
    explode(*s->level, REALM_MAIN, vec3(b) + vec3(0.5f, 0.5f, 0.5f), st.value("power", 3.f));
    DQ_LOG("test: explode at %d %d %d", b.x, b.y, b.z);
  } else if (op == "vdamage" && p && s) {   // the nearest village's damage records, for its builders (count, a few)
    auto vs = villagesNear(*s->level, p->pos.x, p->pos.z, 200);
    if (vs.empty()) { result(false, "vdamage: no village"); return; }
    const json& rec = s->level->info.data.value("villages", json::object());
    const json D = rec.contains(vs[0]->id) && rec[vs[0]->id].contains("damage") ? rec[vs[0]->id]["damage"] : json::array();
    int full = 0, goods = 0; std::string eg;
    if (D.is_array()) for (auto& d : D) { if (d.size() >= 8) { full++; if (d[7].is_object()) { goods++; if (eg.size() < 300) eg += " " + d.dump(); } } }
    DQ_LOG("test: vdamage %d to mend, %d with chiselling/rug/goods, %d with goods:%s", D.is_array() ? (int)D.size() : 0, full, goods, eg.c_str());
    if (st.contains("max")) result(D.is_array() && (int)D.size() <= st.value("max", 0), "village damage mended to <= " + std::to_string(st.value("max", 0)) + " (left " + std::to_string(D.is_array() ? (int)D.size() : 0) + ")");
  } else if (op == "find" && p && s) {   // the first few blocks of a kind within "r" of you (logged): where to look
    const BlockId want = reg().blockId(st.value("key", "stone")); const int r = st.value("r", 48);
    const bool rugs = st.value("key", "") == "rug";   // key "rug": floors with a rug on them (world/rug.h)
    std::string out; int n = 0; World& w = s->world(); ivec3 first(0, -1, 0);
    const int yLo = st.contains("y0") ? st.value("y0", 0) : ifloor(p->pos.y) - 16;   // "y0": no lower than that (the surface's water, not the sea floor's)
    for (int y = std::max(yLo, ifloor(p->pos.y) - 16); y <= ifloor(p->pos.y) + 16 && n < 8; y++) for (int z = ifloor(p->pos.z) - r; z <= ifloor(p->pos.z) + r && n < 8; z++)
      for (int x = ifloor(p->pos.x) - r; x <= ifloor(p->pos.x) + r && n < 8; x++) if (w.isLoaded(x, y, z) && (rugs ? isRugCoat(w.getCoat(x, y, z)) : w.getBlock(x, y, z) == want)) { if (first.y < 0) first = ivec3(x, y, z); out += " " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + "/m" + std::to_string(w.getMeta(x, y, z));
          if (const json* e = w.blockEntity(x, y, z); e && e->contains("items") && (*e)["items"].is_array()) { int k = 0; for (auto& it : (*e)["items"]) k += !it.is_null(); out += "/items" + std::to_string(k); }   // (what it holds)
          n++; }
    DQ_LOG("test: find %s:%s", st.value("key", "stone").c_str(), out.c_str());
    if (first.y >= 0) R.lastFound = first;
    if (st.contains("go") && first.y >= 0) {   // "go": [dx,dy,dz]: stand that far from the first one found, looking at it
      p->pos = vec3(first) + vec3(0.5f, 0, 0.5f) + vec3(st["go"][0].get<float>(), st["go"][1].get<float>(), st["go"][2].get<float>());
      p->vel = vec3(0); p->prevPos = p->pos; p->hasFallStart = false;
      const vec3 d = vec3(first) + vec3(0.5f, 1.f, 0.5f) - (p->pos + vec3(0, 1.62f, 0)); p->yaw = std::atan2(-d.x, -d.z); p->pitch = std::atan2(d.y, std::hypot(d.x, d.z));
    }
  } else if (op == "lanterns" && s) {   // hanging lanterns in a box and what's over them (logged): where a chain falls short
    ivec3 a0 = at(st, "from"), a1 = at(st, "to"); World& w = s->world(); std::map<std::string, int> kinds; std::string eg;
    static const BlockId L = reg().blockId("lantern"), CH = reg().blockId("chain");
    for (int y = std::min(a0.y, a1.y); y <= std::max(a0.y, a1.y); y++) for (int z = std::min(a0.z, a1.z); z <= std::max(a0.z, a1.z); z++)
      for (int x = std::min(a0.x, a1.x); x <= std::max(a0.x, a1.x); x++) {
        if (w.getBlock(x, y, z) != L || !(w.getMeta(x, y, z) & 1)) continue;
        int yy = y + 1, ch = 0; while (w.getBlock(x, yy, z) == CH) { yy++; ch++; }
        const BlockId up = w.getBlock(x, yy, z);
        const std::string k = std::to_string(ch) + " chain, then " + reg().block(up).key + "/m" + std::to_string(w.getMeta(x, yy, z)) + "/c" + std::to_string(w.getCarve(x, yy, z)) + " lmeta " + std::to_string(w.getMeta(x, y, z));
        if (!kinds[k]++) eg += " | " + k + " @" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
      }
    for (auto& [k, n] : kinds) DQ_LOG("lanterns: %d x %s", n, k.c_str());
    DQ_LOG("lanterns eg:%s", eg.c_str());
  } else if (op == "list" && s) {   // the blocks in a box whose key holds one of "keys" (logged): key x,y,z/meta
    ivec3 a0 = at(st, "from"), a1 = at(st, "to"); World& w = s->world(); std::string out; int n = 0;
    std::vector<std::string> keys; if (st.contains("keys")) for (auto& k : st["keys"]) keys.push_back(k.get<std::string>());
    for (int y = std::min(a0.y, a1.y); y <= std::max(a0.y, a1.y); y++) for (int z = std::min(a0.z, a1.z); z <= std::max(a0.z, a1.z); z++)
      for (int x = std::min(a0.x, a1.x); x <= std::max(a0.x, a1.x); x++) {
        const BlockId id = w.getBlock(x, y, z); if (!id) continue;
        const std::string& k = reg().block(id).key; bool hit = false; for (auto& q : keys) if (k.find(q) != std::string::npos) hit = true;
        if (!hit) continue;
        if (st.value("count", false)) { n++; continue; }
        out += " " + k + "@" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + "/" + std::to_string(w.getMeta(x, y, z));
        if (++n % 12 == 0) { DQ_LOG("list:%s", out.c_str()); out.clear(); }
      }
    if (st.value("count", false)) DQ_LOG("count %s: %d", st.value("tag", std::string()).c_str(), n); else DQ_LOG("list:%s", out.c_str());
  } else if (op == "dump" && s) {   // the blocks of a box, layer by layer (logged): "from", "to" [x,y,z]
    ivec3 a0 = at(st, "from"), a1 = at(st, "to"); World& w = s->world();
    std::map<std::string, char> leg; std::string out;
    DQ_LOG("test: dump x %d..%d z %d..%d (west->east, north->south)", std::min(a0.x, a1.x), std::max(a0.x, a1.x), std::min(a0.z, a1.z), std::max(a0.z, a1.z));
    for (int y = std::max(a0.y, a1.y); y >= std::min(a0.y, a1.y); y--) {
      if (!out.empty()) DQ_LOG("%s", out.c_str());
      out = "dump y" + std::to_string(y) + ":";
      for (int z = std::min(a0.z, a1.z); z <= std::max(a0.z, a1.z); z++) {
        out += "\n ";
        for (int x = std::min(a0.x, a1.x); x <= std::max(a0.x, a1.x); x++) {
          const BlockId id = w.getBlock(x, y, z);
          if (!id) { out += '.'; continue; }
          const u8 cv = w.getCarve(x, y, z);   // a chiseled block shows its carve mask: key/meta^carve
          const std::string k = reg().block(id).key + "/" + std::to_string(w.getMeta(x, y, z)) + (cv ? "^" + std::to_string(cv) : "");
          auto it = leg.find(k);
          if (it == leg.end()) { const int n = (int)leg.size(); it = leg.emplace(k, (char)(n < 26 ? 'A' + n : n < 52 ? 'a' + n - 26 : '0' + (n - 52) % 10)).first; }
          out += it->second;
        }
      }
    }
    if (!out.empty()) DQ_LOG("%s", out.c_str());
    std::string lg; for (auto& [k, c] : leg) { lg += std::string(" ") + c + "=" + k; if (lg.size() > 1500) { DQ_LOG("legend:%s", lg.c_str()); lg.clear(); } }
    DQ_LOG("legend:%s", lg.c_str());
  } else if (op == "pathcheck" && p && s) {   // the street blocks within "r": steps of 2+ between neighbours, and ones built over (logged)
    World& w = s->world(); const int r = st.value("r", 60);
    static const std::unordered_set<std::string> PATHS = {"dirt_path", "dirt_path_stairs", "dirt_path_slab"};
    auto topPath = [&](int x, int z, int& y) {
      for (y = ifloor(p->pos.y) + 24; y > ifloor(p->pos.y) - 40; y--) {
        if (!w.isLoaded(x, y, z)) return false;
        const BlockId id = w.getBlock(x, y, z);
        if (!id || !reg().tSolid[id]) continue;
        return PATHS.count(reg().block(id).key) > 0;
      }
      return false;
    };
    int cols = 0, steep = 0, roofed = 0; std::string eg;
    for (int z = ifloor(p->pos.z) - r; z <= ifloor(p->pos.z) + r; z++) for (int x = ifloor(p->pos.x) - r; x <= ifloor(p->pos.x) + r; x++) {
      int y; if (!topPath(x, z, y)) continue;
      cols++;
      for (int d = 0; d < 2; d++) { int y2; const int nx = x + (d ? 1 : 0), nz = z + (d ? 0 : 1); if (topPath(nx, nz, y2) && std::abs(y2 - y) >= 2) { steep++; if (eg.size() < 200) eg += " " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + "~" + std::to_string(y2); } }
      for (int k = 1; k <= 2; k++) { const BlockId u = w.getBlock(x, y + k, z); if (u && reg().tSolid[u] && reg().tShape[u] != ShapeKind::Slab && reg().tShape[u] != ShapeKind::Stairs) { roofed++; break; } }
    }
    DQ_LOG("test: pathcheck %d path columns, %d steps of 2+, %d built over:%s", cols, steep, roofed, eg.c_str());
  } else if (op == "piece" && s) {   // a village piece set down at the anchor, unturned ("pool": "plains/houses", "name"): to look at it
    const VillagePieces* vp = villagePieces();
    const std::vector<VPPiece>* pl = vp ? vp->pool(st.value("pool", "plains/houses")) : nullptr;
    const VPPiece* pc = nullptr; if (pl) for (auto& q : *pl) if (q.name == st.value("name", "")) pc = &q;
    if (!pc) { result(false, "piece: no " + st.value("name", "")); return; }
    const int cut = st.value("cut", 999);   // a cutaway: nothing above this layer (to look down into the rooms)
    int rugs = 0;
    for (const VPBlock& b : pc->blocks) {
      if (b.y > cut) continue;
      const VPState& vs = pc->states[b.s]; s->world().setBlock(R.anchor.x + b.x, R.anchor.y + b.y, R.anchor.z + b.z, vs.id, vs.meta[0]);
      if (vs.rug) { s->world().setCoat(R.anchor.x + b.x, R.anchor.y + b.y, R.anchor.z + b.z, rugCodeOf(vs.rug)); rugs++; }
    }
    DQ_LOG("test: piece %s %dx%dx%d at %d %d %d, %d rugs", pc->name.c_str(), pc->sx, pc->sy, pc->sz, R.anchor.x, R.anchor.y, R.anchor.z, rugs);
  } else if (op == "folk" && p && s) {   // every villager within 200: job@x,y,z (who's where, doing their work or not)
    std::string out;
    for (auto& e : s->level->entities()) if (e && !e->removed && e->type == "villager" && glm::distance(e->pos, p->pos) < 200) {
      const std::string j = e->data.is_object() && e->data.contains("npc") && e->data["npc"].is_object() ? e->data["npc"].value("j", std::string("?")) : std::string("?");
      const bool wet = reg().tLiquid[s->world().getBlock(ifloor(e->pos.x), ifloor(e->pos.y + 0.1f), ifloor(e->pos.z))] != 0;
      char b[96]; snprintf(b, sizeof b, " %s@%.0f,%.0f,%.0f%s", j.c_str(), e->pos.x, e->pos.y, e->pos.z, wet ? "(IN WATER)" : ""); out += b;
    }
    DQ_LOG("test: folk%s", out.c_str());
  } else if (op == "tpvillage" && p && s && s->level->gen) {   // to the nearest village (as /locate finds it), "off" [dx, dy, dz] from its centre
    const StructurePlan* sp = locateStructure(*s->level->gen, DIM_OVERWORLD, "village", ifloor(p->pos.x), ifloor(p->pos.z), 24);
    if (!sp) { result(false, "tpvillage: none"); return; }
    vec3 o(0, 30, 0); if (st.contains("off")) o = vec3(st["off"][0].get<float>(), st["off"][1].get<float>(), st["off"][2].get<float>());
    p->pos = vec3(sp->cx + 0.5f, dimBase(DIM_OVERWORLD) + sp->y, sp->cz + 0.5f) + o; p->vel = vec3(0); p->prevPos = p->pos; p->hasFallStart = false;
    R.anchor = ivec3(sp->cx, dimBase(DIM_OVERWORLD) + sp->y, sp->cz);
    DQ_LOG("test: tpvillage %s (%s) at %d,%d,%d", sp->name.c_str(), sp->data.value("style", std::string("?")).c_str(), sp->cx, dimBase(DIM_OVERWORLD) + sp->y, sp->cz);
  } else if (op == "village" && p && s) {   // the nearest village: its buildings, who works where, what it has in store
    auto vs = villagesNear(*s->level, p->pos.x, p->pos.z, 200);
    if (vs.empty()) { result(false, "village: none near"); return; }
    const Village& v = *vs[0];
    std::string out = v.name + " (" + v.tier + ") at " + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + " |";
    for (auto& b : v.blds) out += " " + b.type + (b.job.empty() ? "" : "[" + b.job + "]") + "@" + std::to_string(b.door.x) + "," + std::to_string(b.door.y) + "," + std::to_string(b.door.z) + " r" + std::to_string(b.r);
    out += " | folk:"; for (auto& f : v.folk) out += " " + f.job;
    out += " | roads " + std::to_string(v.roads.size());
    if (v.hasWall) {
      out += " | wall " + v.wallKind + " " + std::to_string(v.wx0) + "," + std::to_string(v.wz0) + " to " + std::to_string(v.wx1) + "," + std::to_string(v.wz1) + " gates:";
      for (auto& g : v.gates) out += " " + g.name + "@" + std::to_string((int)g.center.x) + "," + std::to_string((int)g.center.y) + "," + std::to_string((int)g.center.z);
    }
    const json& rec = s->level->info.data.value("villages", json::object());
    out += " | stock: " + (rec.contains(v.id) && rec[v.id].contains("stock") ? rec[v.id]["stock"].dump() : std::string("{}"));
    if (Mechanics* mc = mechanicsOf(*s->level, REALM_MAIN)) for (auto& [k, n] : mc->nodes()) {
      if (std::hypot(n.p.x - v.x, n.p.z - v.z) > 80) continue;
      if (n.kind == "windmill") {
        for (int k = -4; k <= 4; k++) { ivec3 q = n.p + (n.axis == 1 ? ivec3(k, 0, 0) : ivec3(0, 0, k)); out += std::string(k == -4 ? " | axle line:" : "") + " " + reg().block(s->world().getBlock(q.x, q.y, q.z)).key + "/" + std::to_string(s->world().getMeta(q.x, q.y, q.z)); }
        out += " | axis " + std::to_string(n.axis);
      }
      if (n.kind == "windmill") { auto sp = mc->sourcePower(n); out += " | sails " + std::to_string(n.p.x) + "," + std::to_string(n.p.y) + "," + std::to_string(n.p.z) + " power " + std::to_string(sp.power) + (sp.blocked ? " BLOCKED" : "") + " speed " + std::to_string(mc->speedAt(n.p.x, n.p.y, n.p.z)); }
      if (n.kind == "millstone") if (const json* be = s->world().blockEntity(n.p.x, n.p.y, n.p.z)) out += " | millstone " + be->dump();
    }
    DQ_LOG("test: village %s", out.c_str());
  } else if (op == "vstock" && p && s) {   // set the nearest village's stores: {"key": item, "value": n}
    auto vs = villagesNear(*s->level, p->pos.x, p->pos.z, 200);
    if (!vs.empty()) { json& r = s->level->info.data["villages"][vs[0]->id]; r["stock"][st.value("key", "wheat")] = st.value("value", 0); }
  } else if (op == "tpbld" && p && s) {   // stand at a building of a kind: its door plus "off" [dx,dy,dz]
    auto vs = villagesNear(*s->level, p->pos.x, p->pos.z, 200);
    const std::string want = st.value("type", "windmill"), wantJob = st.value("job", std::string());   // job: that trade's building
    for (auto& vr : vs) for (auto& b : vr->blds) if ((want.empty() || b.type == want) && (wantJob.empty() || b.job == wantJob)) {
      if (st.value("look", false)) {   // stand "off" from its work spot, looking at it
        vec3 o(0); if (st.contains("off")) o = vec3(st["off"][0].get<float>(), st["off"][1].get<float>(), st["off"][2].get<float>());
        const vec3 at = vec3(b.hasWork ? b.work : b.door) + vec3(0.5f, 0.5f, 0.5f);
        p->pos = at + o; p->vel = vec3(0); p->prevPos = p->pos; p->hasFallStart = false;
        const vec3 d = at - (p->pos + vec3(0, 1.62f, 0)); p->yaw = std::atan2(-d.x, -d.z); p->pitch = std::atan2(d.y, std::hypot(d.x, d.z));
        DQ_LOG("test: tpbld %s/%s work %d,%d,%d", b.type.c_str(), b.job.c_str(), b.work.x, b.work.y, b.work.z);
        return;
      }
      vec3 o(0); if (st.contains("off")) o = vec3(st["off"][0].get<float>(), st["off"][1].get<float>(), st["off"][2].get<float>());
      p->pos = vec3(b.door) + vec3(0.5f, 0, 0.5f) + o; p->vel = vec3(0); p->prevPos = p->pos; p->hasFallStart = false;
      DQ_LOG("test: tpbld %s door %d,%d,%d r%d", want.c_str(), b.door.x, b.door.y, b.door.z, b.r);
      return;
    }
    result(false, "tpbld: no " + want);
  } else if (op == "craft" && p) {   // the crafting matcher on a W x W grid ("grid": item keys, "" empty): the result into the hand
    std::vector<ItemStack> slots; const int W = st.value("w", 3);
    for (auto& k : st["grid"]) { std::string id = k.is_string() ? k.get<std::string>() : ""; slots.push_back(id.empty() ? ItemStack() : ItemStack(id, 1)); }
    slots.resize((size_t)W * W);
    CraftMatch m = matchGrid(slots, W);
    if (!m.ok) { result(false, "craft: nothing"); return; }
    ItemStack out = m.result();
    const ToolInfo t = toolOf(out);
    DQ_LOG("test: craft -> %s (%s) tier %d speed %.2f dmg %.1f dur %d armour %.2f", out.id.c_str(), stackName(out).c_str(), t.tier, t.speed, t.damage, toolMaxDurability(out), armorPointsOf(out));
    if (st.value("wear", false)) { const ItemDef* d = out.def(); int sl = d && d->props.contains("armor") ? d->props["armor"].value("slot", 0) : -1; if (sl >= 0 && sl < (int)p->armor.size()) p->armor[sl] = out; }
    else p->give(out);
  } else if (op == "wear" && p) {   // put an item on: armour slot, or cape/back/hook
    std::string k = st.value("item", ""); const ItemDef* d = reg().item(k);
    if (k == "none") { const std::string g = st.value("slot", "back"); auto* v = g == "back" ? &p->back : g == "quiver" ? &p->quiver : g == "cape" ? &p->cape : nullptr; if (v && !v->empty()) (*v)[0].clear(); }   // take it off
    else if (d && d->props.contains("armor") && d->props["armor"].is_object()) { int sl = d->props["armor"].value("slot", 0); if (sl >= 0 && sl < (int)p->armor.size()) p->armor[sl] = ItemStack(k, 1); }
    else if (d) { std::string g = d->has("cape") ? "cape" : d->has("backpack") ? "back" : (k == "quiver" || k == "golden_quiver") ? "quiver" : st.value("slot", "off");
      if (g == "off") { if (!p->offhand.empty()) p->offhand[0] = ItemStack(k, 1); }
      else if (g == "quiver") { if (p->quiver.empty()) p->quiver.resize(1); p->quiver[0] = ItemStack(k, 1); if (st.contains("arrows")) { ItemStack a("arrow", st["arrows"].get<int>()); quiverStow(*p, a); } }   // arrows: put that many in
      else if (g == "cape" && !p->cape.empty()) p->cape[0] = ItemStack(k, 1);
      else if (g == "back" && !p->back.empty()) p->back[0] = ItemStack(k, 1);
      else p->data[g] = json::array({ItemStack(k, 1).toJson()}); }
  } else if (op == "save" && s) {
    s->save();
  } else if (op == "leave") {
    a.leaveWorld(st.value("toTitle", true));
  } else if (op == "open") {
    a.openWorld(a.savesDir() / st.value("world", "Test"));
  } else if (op == "kill" && s && p) {   // the player (or split-screen "local") dies: the death screen opens in their viewport
    DamageSource ds; p->hp = 0; playerDie(*s->level, *p, ds);
  } else if (op == "unjoin") {   // split screen: the partner "local" (1..3) leaves (the pause screen's Leave button)
    if (s) s->removeLocal(st.value("local", 1));
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
