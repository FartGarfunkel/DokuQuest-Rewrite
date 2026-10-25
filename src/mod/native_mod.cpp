// mod/native_mod.cpp — native plugins: a shared library exporting dq_plugin_init(const DqApi*) (mod/dq_plugin.h).
// Loaded once per process and never unloaded (the game may still hold its function pointers).
#include "mod/mod_internal.h"
#include "mod/dq_plugin.h"
#include "sim/level.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace dq {

class NativeMod : public ModRuntime {
public:
  struct H { DqEventFn fn; void* user; };
  std::map<std::string, std::vector<H>> handlers;
  DqApi api{};
  std::string dir, ver;
  void* lib = nullptr;
  bool emit(const std::string& ev, const json& args) override {
    auto it = handlers.find(ev);
    if (it == handlers.end()) return true;
    std::string a = args.dump();
    bool go = true;
    for (auto& h : it->second) if (h.fn(h.user, ev.c_str(), a.c_str()) == 0) go = false;
    return go;
  }
};

static NativeMod* N(void* host) { return static_cast<NativeMod*>(host); }
static int copyOut(const std::string& s, char* out, int cap) {
  if (out && cap > 0) { int n = std::min((int)s.size(), cap - 1); std::memcpy(out, s.data(), n); out[n] = 0; }
  return (int)s.size() + 1;
}
static json parse(const char* s) { if (!s) return json(); json j = json::parse(s, nullptr, false); return j.is_discarded() ? json() : j; }

static void api_log(void* h, const char* text) { modapi::log(*N(h)->loader, N(h)->name, text ? text : ""); }
static int api_on(void* h, const char* ev, DqEventFn fn, void* user) {
  if (!ev || !fn) return 0;
  N(h)->handlers[ev].push_back({fn, user});
  modapi::listen(*N(h), ev);
  return 1;
}
static int api_command(void* h, const char* name, const char* help, DqCommandFn fn, void* user) {
  if (!name || !fn) return 0;
  return modapi::command(*N(h), name, help ? help : "", [fn, user](const json& pl, const json& args) -> std::string {
    std::string p = pl.dump(), a = args.dump();
    const char* r = fn(user, p.c_str(), a.c_str());
    return r ? r : "";
  }) ? 1 : 0;
}
static int api_registerBlock(void* h, const char* def) { json d = parse(def); return d.is_object() && modapi::registerBlock(N(h)->id, d) ? 1 : 0; }
static int api_registerItem(void* h, const char* def) { json d = parse(def); return d.is_object() && modapi::registerItem(N(h)->id, d) ? 1 : 0; }
static int api_addRecipe(void*, const char* r) { return modapi::addRecipe(parse(r)) ? 1 : 0; }
static int api_getBlock(void* h, int x, int y, int z, char* out, int cap) {
  json k = modapi::getBlock(*N(h)->loader, x, y, z, 0);
  if (!k.is_string()) return 0;
  copyOut(k.get<std::string>(), out, cap);
  return 1;
}
static int api_setBlock(void* h, int x, int y, int z, const char* key, int meta) { return key && modapi::setBlock(*N(h)->loader, x, y, z, key, meta, 0) ? 1 : 0; }
static void api_say(void* h, const char* text) { modapi::say(*N(h)->loader, N(h)->name, text ? text : ""); }
static int api_give(void* h, const char* p, const char* item, int n) { return p && item && modapi::give(*N(h)->loader, p, item, n) ? 1 : 0; }
static unsigned api_spawn(void* h, const char* type, double x, double y, double z) { return type ? modapi::spawn(*N(h)->loader, type, x, y, z) : 0; }
static int api_players(void* h, char* out, int cap) { return copyOut(modapi::players(*N(h)->loader).dump(), out, cap); }
static double api_time(void* h, double set) { return modapi::time(*N(h)->loader, set); }
static int api_emit(void* h, const char* ev, const char* args) {
  ModLoader& L = *N(h)->loader;
  if (!L.bus || !ev) return 1;
  json a = parse(args); if (!a.is_array()) a = json::array();
  return L.bus->emit(ev, a) ? 1 : 0;
}
static int api_serving(void* h) { return N(h)->loader->serving() ? 1 : 0; }

std::unique_ptr<ModRuntime> loadNativeMod(ModLoader& L, ModInfo& info, const fs::path& path, std::string& err) {
  auto m = std::make_unique<NativeMod>();
  m->id = info.id; m->name = info.name; m->loader = &L; m->plugin = info.plugin;
#ifdef _WIN32
  HMODULE lib = LoadLibraryW(path.wstring().c_str());
  if (!lib) { err = "could not load " + path.filename().string(); return nullptr; }
  auto init = (DqPluginInitFn)(void*)GetProcAddress(lib, "dq_plugin_init");
#else
  void* lib = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) { const char* e = dlerror(); err = e ? e : "could not load the library"; return nullptr; }
  auto init = (DqPluginInitFn)dlsym(lib, "dq_plugin_init");
#endif
  if (!init) { err = path.filename().string() + " has no dq_plugin_init"; return nullptr; }
  m->lib = (void*)lib;
  m->dir = path.parent_path().string(); m->ver = DQ_VERSION;
  DqApi& a = m->api;
  a.abi = DQ_PLUGIN_ABI; a.size = sizeof(DqApi); a.gameVersion = m->ver.c_str(); a.modId = m->id.c_str(); a.modDir = m->dir.c_str(); a.host = m.get();
  a.log = api_log; a.on = api_on; a.command = api_command; a.registerBlock = api_registerBlock; a.registerItem = api_registerItem;
  a.addRecipe = api_addRecipe; a.getBlock = api_getBlock; a.setBlock = api_setBlock; a.say = api_say; a.give = api_give; a.spawn = api_spawn;
  a.players = api_players; a.time = api_time; a.emit = api_emit; a.serving = api_serving;
  int r = init(&a);
  if (r != 0) { err = strFormat("dq_plugin_init said no (%d)", r); return nullptr; }   // the library stays loaded: it may have kept pointers
  DQ_LOG("mods: native plugin %s (%s)", info.id.c_str(), path.filename().string().c_str());
  return m;
}

} // namespace dq
