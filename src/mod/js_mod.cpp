// mod/js_mod.cpp — JS mods on QuickJS with the original DQ API (game/mods.js modApi). Each mod has its own
// runtime + context; the code runs as the body of `function (DQ) { "use strict"; ... }`. Every call into a mod
// has a time budget (an interrupt handler stops it), so a bad mod can't freeze the game: one that keeps running
// over is switched off. Faults are logged and kept in ModInfo::error, like the original's modFault.
#include "mod/mod_internal.h"
#include "sim/level.h"
#include "sim/commands.h"
#include <quickjs.h>

namespace dq {

static std::string jsStr(JSContext* ctx, JSValueConst v) {
  size_t n = 0;
  const char* c = JS_ToCStringLen(ctx, &n, v);
  if (!c) { JS_FreeValue(ctx, JS_GetException(ctx)); return ""; }
  std::string s(c, n);
  JS_FreeCString(ctx, c);
  return s;
}
static double jsNum(JSContext* ctx, JSValueConst v, double def = 0) { double d; return JS_IsUndefined(v) || JS_ToFloat64(ctx, &d, v) ? def : d; }

static JSValue toJs(JSContext* ctx, const json& j) {
  switch (j.type()) {
    case json::value_t::null: return JS_NULL;
    case json::value_t::boolean: return JS_NewBool(ctx, j.get<bool>());
    case json::value_t::number_integer: case json::value_t::number_unsigned: {
      i64 v = j.get<i64>();
      return v >= INT32_MIN && v <= INT32_MAX ? JS_NewInt32(ctx, (int32_t)v) : JS_NewFloat64(ctx, (double)v);
    }
    case json::value_t::number_float: return JS_NewFloat64(ctx, j.get<double>());
    case json::value_t::string: { auto& s = j.get_ref<const std::string&>(); return JS_NewStringLen(ctx, s.data(), s.size()); }
    case json::value_t::array: {
      JSValue a = JS_NewArray(ctx);
      for (u32 i = 0; i < j.size(); i++) JS_SetPropertyUint32(ctx, a, i, toJs(ctx, j[i]));
      return a;
    }
    case json::value_t::object: {
      JSValue o = JS_NewObject(ctx);
      for (auto& [k, v] : j.items()) JS_SetPropertyStr(ctx, o, k.c_str(), toJs(ctx, v));
      return o;
    }
    default: return JS_UNDEFINED;
  }
}
static json fromJs(JSContext* ctx, JSValueConst v) {
  if (JS_IsUndefined(v)) return json();
  JSValue s = JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED);
  if (JS_IsException(s)) { JS_FreeValue(ctx, JS_GetException(ctx)); return json(); }
  json j;
  if (JS_IsString(s)) j = json::parse(jsStr(ctx, s), nullptr, false);
  JS_FreeValue(ctx, s);
  return j.is_discarded() ? json() : j;
}
static std::string excText(JSContext* ctx) {
  JSValue e = JS_GetException(ctx);
  std::string msg = jsStr(ctx, e);
  if (JS_IsError(ctx, e)) {
    JSValue st = JS_GetPropertyStr(ctx, e, "stack");
    if (JS_IsString(st)) { std::string s = jsStr(ctx, st); auto nl = s.find('\n'); std::string at = s.substr(0, nl); while (!at.empty() && at[0] == ' ') at.erase(0, 1); if (!at.empty()) msg += " (" + at + ")"; }
    JS_FreeValue(ctx, st);
  }
  JS_FreeValue(ctx, e);
  return msg;
}

class JsMod : public ModRuntime {
public:
  JSRuntime* rt = nullptr;
  JSContext* ctx = nullptr;
  std::map<std::string, std::vector<JSValue>> handlers;
  std::vector<JSValue> keep;
  struct Timer { int id; double at, every; JSValue fn; std::vector<JSValue> args; bool dead = false; };
  std::vector<Timer> timers;
  int nextTimer = 1;
  double clock = 0, deadline = 0;
  int timeouts = 0;
  bool stopped = false, loading = false;

  ~JsMod() override {
    unregisterChatCommands(id);
    if (!ctx) return;
    for (auto& [k, v] : handlers) for (auto& f : v) JS_FreeValue(ctx, f);
    for (auto& f : keep) JS_FreeValue(ctx, f);
    for (auto& t : timers) { JS_FreeValue(ctx, t.fn); for (auto& a : t.args) JS_FreeValue(ctx, a); }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
  }

  // Call a mod function within a time budget. false = it threw or ran over (the fault is recorded).
  bool call(JSValueConst fn, int argc, JSValue* argv, JSValue* out, double budget) {
    if (stopped || !JS_IsFunction(ctx, fn)) return false;
    JS_UpdateStackTop(rt);   // the world may run on another thread than the one that loaded the mod
    deadline = nowSeconds() + budget;
    JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
    bool over = nowSeconds() > deadline;
    bool ok = !JS_IsException(r);
    if (!ok) {
      std::string e = excText(ctx);
      if (over) {
        timeouts++;
        if (timeouts >= 3) { stopped = true; fault("switched off: it kept freezing the game (" + e + ")"); }
        else fault(strFormat("took longer than %.0f ms and was stopped", budget * 1000));
      } else fault(e);
    }
    JSContext* jc;
    for (int n = 0; n < 1000 && JS_ExecutePendingJob(rt, &jc) > 0; n++) {}   // promises it started
    deadline = 0;
    if (ok && out) *out = r; else JS_FreeValue(ctx, r);
    return ok;
  }

  bool emit(const std::string& ev, const json& args) override {
    auto it = handlers.find(ev);
    if (stopped || it == handlers.end() || it->second.empty()) return true;
    std::vector<JSValue> argv;
    auto num = [&](size_t i) { return i < args.size() ? toJs(ctx, args[i]) : JS_UNDEFINED; };
    // the original's argument shapes: a player object for block events, (player, key) for blockPlace
    if ((ev == "blockBreak" || ev == "blockPlace") && args.is_array() && args.size() >= 5 && args[0].is_string()) {
      JSValue pl = toJs(ctx, modapi::playerObj(*loader, args[0].get<std::string>()));
      if (ev == "blockBreak") argv = {pl, num(1), num(2), num(3), num(4)};
      else argv = {pl, num(4), num(1), num(2), num(3)};
    } else if (args.is_array()) for (auto& a : args) argv.push_back(toJs(ctx, a));
    std::vector<JSValue> fns;
    for (auto& f : it->second) fns.push_back(JS_DupValue(ctx, f));
    bool go = true;
    for (auto& f : fns) {
      JSValue r;
      if (call(f, (int)argv.size(), argv.data(), &r, ev == "tick" ? 0.05 : 0.25)) {
        if (JS_IsBool(r) && !JS_ToBool(ctx, r)) go = false;   // `return false` cancels (where that means something)
        JS_FreeValue(ctx, r);
      }
      JS_FreeValue(ctx, f);
    }
    for (auto& a : argv) JS_FreeValue(ctx, a);
    return go;
  }

  void tick(double dt) override {
    if (stopped || timers.empty()) return;
    clock += dt;
    size_t n = timers.size();
    for (size_t i = 0; i < n && i < timers.size(); i++) {
      if (timers[i].dead || clock < timers[i].at) continue;
      JSValue fn = JS_DupValue(ctx, timers[i].fn);
      std::vector<JSValue> a; for (auto& v : timers[i].args) a.push_back(JS_DupValue(ctx, v));
      if (timers[i].every > 0) timers[i].at = clock + timers[i].every; else timers[i].dead = true;
      call(fn, (int)a.size(), a.data(), nullptr, 0.1);
      JS_FreeValue(ctx, fn); for (auto& v : a) JS_FreeValue(ctx, v);
    }
    for (size_t i = 0; i < timers.size();) {
      if (timers[i].dead) { JS_FreeValue(ctx, timers[i].fn); for (auto& v : timers[i].args) JS_FreeValue(ctx, v); timers.erase(timers.begin() + i); }
      else i++;
    }
  }
};

static JsMod* M(JSContext* ctx) { return static_cast<JsMod*>(JS_GetContextOpaque(ctx)); }
#define ARG(i) (argc > (i) ? argv[i] : JS_UNDEFINED)

// ---- DQ ----
static JSValue js_getName(JSContext* ctx, JSValueConst) { auto s = M(ctx)->name; return JS_NewStringLen(ctx, s.data(), s.size()); }
static JSValue js_setName(JSContext* ctx, JSValueConst, JSValueConst v) {
  JsMod* m = M(ctx); m->name = jsStr(ctx, v).substr(0, 40);
  if (ModInfo* i = m->info()) i->name = m->name;
  return JS_UNDEFINED;
}
static JSValue js_getPlugin(JSContext* ctx, JSValueConst) { return JS_NewBool(ctx, M(ctx)->plugin); }
static JSValue js_setPlugin(JSContext* ctx, JSValueConst, JSValueConst v) {
  JsMod* m = M(ctx); m->plugin = JS_ToBool(ctx, v) > 0;
  if (ModInfo* i = m->info()) i->plugin = m->plugin;
  return JS_UNDEFINED;
}
static JSValue js_getGame(JSContext* ctx, JSValueConst) { return toJs(ctx, modapi::gameInfo(*M(ctx)->loader)); }

static JSValue js_on(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  JsMod* m = M(ctx);
  if (!JS_IsFunction(ctx, ARG(1))) return JS_UNDEFINED;
  std::string ev = jsStr(ctx, ARG(0));
  m->handlers[ev].push_back(JS_DupValue(ctx, ARG(1)));
  modapi::listen(*m, ev);
  return JS_UNDEFINED;
}
static JSValue js_command(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  JsMod* m = M(ctx);
  if (!JS_IsFunction(ctx, ARG(2))) return JS_FALSE;
  JSValue fn = JS_DupValue(ctx, ARG(2));
  m->keep.push_back(fn);
  bool ok = modapi::command(*m, jsStr(ctx, ARG(0)), jsStr(ctx, ARG(1)), [m, fn](const json& pl, const json& args) -> std::string {
    JSValue a[2] = {toJs(m->ctx, pl), toJs(m->ctx, args)};
    JSValue r;
    std::string out;
    if (m->call(fn, 2, a, &r, 0.5)) { if (!JS_IsUndefined(r)) out = jsStr(m->ctx, r); JS_FreeValue(m->ctx, r); }
    else { ModInfo* i = m->info(); out = "That went wrong: " + (i ? i->error : std::string("?")); }
    JS_FreeValue(m->ctx, a[0]); JS_FreeValue(m->ctx, a[1]);
    return out;
  });
  return JS_NewBool(ctx, ok);
}
static JSValue js_item(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  JsMod* m = M(ctx);
  json def = fromJs(ctx, ARG(1)); if (!def.is_object()) def = json::object();
  def["key"] = jsStr(ctx, ARG(0));
  return JS_NewBool(ctx, modapi::registerItem(m->id, def));
}
static JSValue js_block(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  JsMod* m = M(ctx);
  json def = fromJs(ctx, ARG(1)); if (!def.is_object()) def = json::object();
  def["key"] = jsStr(ctx, ARG(0));
  return JS_NewBool(ctx, modapi::registerBlock(m->id, def));
}
static JSValue js_recipe(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return JS_NewBool(ctx, modapi::addRecipe(fromJs(ctx, ARG(0)))); }
static JSValue js_players(JSContext* ctx, JSValueConst, int, JSValueConst*) { return toJs(ctx, modapi::players(*M(ctx)->loader)); }
static JSValue js_give(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  return JS_NewBool(ctx, modapi::give(*M(ctx)->loader, jsStr(ctx, ARG(0)), jsStr(ctx, ARG(1)), (int)jsNum(ctx, ARG(2), 1)));
}
static JSValue js_spawn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  JsMod* m = M(ctx);
  std::string type = jsStr(ctx, ARG(0));
  double x = jsNum(ctx, ARG(1)), y = jsNum(ctx, ARG(2)), z = jsNum(ctx, ARG(3));
  u32 id = modapi::spawn(*m->loader, type, x, y, z);
  if (!id) return JS_NULL;
  return toJs(ctx, {{"id", id}, {"type", type}, {"x", x}, {"y", y}, {"z", z}});
}
static JSValue js_say(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { JsMod* m = M(ctx); modapi::say(*m->loader, m->name, jsStr(ctx, ARG(0))); return JS_UNDEFINED; }
static JSValue js_toast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { modapi::toast(*M(ctx)->loader, jsStr(ctx, ARG(0)).substr(0, 200)); return JS_UNDEFINED; }
static JSValue js_time(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  double v = JS_IsUndefined(ARG(0)) ? -1 : jsNum(ctx, ARG(0), -1);
  return JS_NewFloat64(ctx, modapi::time(*M(ctx)->loader, v));
}
static std::string joinArgs(JSContext* ctx, int argc, JSValueConst* argv) {
  std::string s;
  for (int i = 0; i < argc; i++) { if (i) s += ' '; s += JS_IsObject(argv[i]) && !JS_IsFunction(ctx, argv[i]) ? fromJs(ctx, argv[i]).dump() : jsStr(ctx, argv[i]); }
  return s;
}
static JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { JsMod* m = M(ctx); modapi::log(*m->loader, m->name, joinArgs(ctx, argc, argv)); return JS_UNDEFINED; }
static JSValue js_emit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  JsMod* m = M(ctx);
  if (!m->loader->bus) return JS_TRUE;
  json a = json::array(); for (int i = 1; i < argc; i++) a.push_back(fromJs(ctx, argv[i]));
  return JS_NewBool(ctx, m->loader->bus->emit(jsStr(ctx, ARG(0)), a));
}
// DQ.world
static int realmArg(JSContext* ctx, int argc, JSValueConst* argv, int i) { return (int)jsNum(ctx, ARG(i), 0); }
static JSValue js_getBlock(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  return toJs(ctx, modapi::getBlock(*M(ctx)->loader, jsNum(ctx, ARG(0)), jsNum(ctx, ARG(1)), jsNum(ctx, ARG(2)), realmArg(ctx, argc, argv, 3)));
}
static JSValue js_setBlock(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  return JS_NewBool(ctx, modapi::setBlock(*M(ctx)->loader, jsNum(ctx, ARG(0)), jsNum(ctx, ARG(1)), jsNum(ctx, ARG(2)), jsStr(ctx, ARG(3)),
                                          (int)jsNum(ctx, ARG(4), 0), realmArg(ctx, argc, argv, 5)));
}
// timers and console, as a browser page had them
static JSValue addTimer(JSContext* ctx, int argc, JSValueConst* argv, bool repeat) {
  JsMod* m = M(ctx);
  if (!JS_IsFunction(ctx, ARG(0))) return JS_NewInt32(ctx, 0);
  JsMod::Timer t; t.id = m->nextTimer++;
  double ms = std::max(0.0, jsNum(ctx, ARG(1), 0));
  t.every = repeat ? std::max(0.01, ms / 1000) : 0; t.at = m->clock + ms / 1000;
  t.fn = JS_DupValue(ctx, ARG(0));
  for (int i = 2; i < argc; i++) t.args.push_back(JS_DupValue(ctx, argv[i]));
  m->timers.push_back(std::move(t));
  modapi::listen(*m, "tick");
  return JS_NewInt32(ctx, m->timers.back().id);
}
static JSValue js_setTimeout(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return addTimer(ctx, argc, argv, false); }
static JSValue js_setInterval(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return addTimer(ctx, argc, argv, true); }
static JSValue js_clearTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  int id = (int)jsNum(ctx, ARG(0), 0);
  for (auto& t : M(ctx)->timers) if (t.id == id) t.dead = true;
  return JS_UNDEFINED;
}
static JSValue js_console(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic) {
  JsMod* m = M(ctx);
  std::string s = joinArgs(ctx, argc, argv);
  if (magic == 1) DQ_WARN("[%s] %s", m->name.c_str(), s.c_str()); else if (magic == 2) DQ_ERR("[%s] %s", m->name.c_str(), s.c_str());
  else DQ_LOG("[%s] %s", m->name.c_str(), s.c_str());
  return JS_UNDEFINED;
}

static const JSCFunctionListEntry dqFuncs[] = {
  JS_CGETSET_DEF("name", js_getName, js_setName),
  JS_CGETSET_DEF("plugin", js_getPlugin, js_setPlugin),
  JS_CGETSET_DEF("game", js_getGame, nullptr),
  JS_CFUNC_DEF("on", 2, js_on),
  JS_CFUNC_DEF("command", 3, js_command),
  JS_CFUNC_DEF("item", 2, js_item),
  JS_CFUNC_DEF("block", 2, js_block),
  JS_CFUNC_DEF("recipe", 1, js_recipe),
  JS_CFUNC_DEF("players", 0, js_players),
  JS_CFUNC_DEF("give", 3, js_give),
  JS_CFUNC_DEF("spawn", 4, js_spawn),
  JS_CFUNC_DEF("say", 1, js_say),
  JS_CFUNC_DEF("toast", 1, js_toast),
  JS_CFUNC_DEF("time", 1, js_time),
  JS_CFUNC_DEF("log", 1, js_log),
  JS_CFUNC_DEF("emit", 1, js_emit),
};
static const JSCFunctionListEntry worldFuncs[] = {
  JS_CFUNC_DEF("getBlock", 3, js_getBlock),
  JS_CFUNC_DEF("setBlock", 4, js_setBlock),
};
static const JSCFunctionListEntry globalFuncs[] = {
  JS_CFUNC_DEF("setTimeout", 2, js_setTimeout),
  JS_CFUNC_DEF("setInterval", 2, js_setInterval),
  JS_CFUNC_DEF("clearTimeout", 1, js_clearTimer),
  JS_CFUNC_DEF("clearInterval", 1, js_clearTimer),
};
static const JSCFunctionListEntry consoleFuncs[] = {
  JS_CFUNC_MAGIC_DEF("log", 1, js_console, 0),
  JS_CFUNC_MAGIC_DEF("info", 1, js_console, 0),
  JS_CFUNC_MAGIC_DEF("warn", 1, js_console, 1),
  JS_CFUNC_MAGIC_DEF("error", 1, js_console, 2),
};

static int interruptHandler(JSRuntime*, void* opaque) {
  auto* m = static_cast<JsMod*>(opaque);
  return m->deadline > 0 && nowSeconds() > m->deadline ? 1 : 0;
}

bool jsCheckSyntax(const std::string& code, std::string* err) {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);
  std::string src = "(function (DQ) {\"use strict\"; " + code + "\n})";
  JSValue r = JS_Eval(ctx, src.c_str(), src.size(), "mod.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  bool ok = !JS_IsException(r);
  if (!ok && err) *err = excText(ctx);
  JS_FreeValue(ctx, r);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return ok;
}

std::unique_ptr<ModRuntime> loadJsMod(ModLoader& L, ModInfo& info, const std::string& code, std::string& err) {
  auto m = std::make_unique<JsMod>();
  m->id = info.id; m->name = info.name; m->loader = &L; m->plugin = info.plugin;
  m->rt = JS_NewRuntime();
  if (!m->rt) { err = "could not start a JS runtime"; return nullptr; }
  JS_SetMemoryLimit(m->rt, 256u << 20);
  JS_SetMaxStackSize(m->rt, 1u << 20);
  JS_SetInterruptHandler(m->rt, interruptHandler, m.get());
  m->ctx = JS_NewContext(m->rt);
  JS_SetContextOpaque(m->ctx, m.get());
  JSContext* ctx = m->ctx;
  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyFunctionList(ctx, global, globalFuncs, sizeof globalFuncs / sizeof globalFuncs[0]);
  JSValue con = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, con, consoleFuncs, sizeof consoleFuncs / sizeof consoleFuncs[0]);
  JS_SetPropertyStr(ctx, global, "console", con);
  JS_FreeValue(ctx, global);
  JSValue dq = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, dq, dqFuncs, sizeof dqFuncs / sizeof dqFuncs[0]);
  JSValue world = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, world, worldFuncs, sizeof worldFuncs / sizeof worldFuncs[0]);
  JS_SetPropertyStr(ctx, dq, "world", world);
  JS_SetPropertyStr(ctx, dq, "version", JS_NewString(ctx, DQ_VERSION));
  // the file is the body of a function given the API as DQ (same line, so error line numbers match the file)
  std::string src = "(function (DQ) {\"use strict\"; " + code + "\n})";
  std::string file = info.path.filename().string();
  JS_UpdateStackTop(m->rt);
  m->deadline = nowSeconds() + 5;
  JSValue fn = JS_Eval(ctx, src.c_str(), src.size(), file.c_str(), JS_EVAL_TYPE_GLOBAL);
  m->deadline = 0;
  if (JS_IsException(fn)) { err = excText(ctx); JS_FreeValue(ctx, dq); return nullptr; }
  JSValue arg[1] = {dq};
  bool ok = m->call(fn, 1, arg, nullptr, 2.0);
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, dq);
  if (!ok) { err = info.error.empty() ? "it stopped while starting" : info.error; return nullptr; }
  info.plugin = m->plugin;
  info.name = m->name;
  return m;
}

} // namespace dq
