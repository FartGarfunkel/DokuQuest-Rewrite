// mod/modloader.cpp — finding mods, switching them on and off, running them, and passing the game's events on
// (game/mods.js modsLoad / modEmit / modsRender's add & remove).
#include "mod/mod_internal.h"
#include "core/registry.h"
#include <regex>

namespace dq {

struct ModLoader::Impl {
  ModLoader* L = nullptr;
  std::vector<std::unique_ptr<ModRuntime>> scripts;              // JS mods (reloaded when the enabled set changes)
  static std::vector<std::unique_ptr<ModRuntime>>& natives() {   // native plugins: loaded once per process
    static std::vector<std::unique_ptr<ModRuntime>> v; return v;
  }
  std::map<std::string, int> subs;    // event -> EventBus handler id
  bool open = false;

  std::vector<ModRuntime*> all() {
    std::vector<ModRuntime*> v;
    for (auto& s : scripts) v.push_back(s.get());
    for (auto& n : natives()) if (n->loader == L) v.push_back(n.get());
    return v;
  }
  // an event goes to every running mod that listens for it; one that returns false cancels
  bool dispatch(const std::string& ev, const json& args) {
    bool go = true;
    if (ev == "tick") { double dt = args.is_array() && !args.empty() && args[0].is_number() ? args[0].get<double>() : 1.0 / 30; for (auto* m : all()) m->tick(dt); }
    for (auto* m : all()) {
      if (!m->listened.count(ev)) continue;
      if (m->plugin && !L->serving()) continue;   // plugins run while this game hosts
      if (!m->emit(ev, args)) go = false;
    }
    return go;
  }
  void subscribe(const std::string& ev) {
    if (!open || !L->bus || subs.count(ev)) return;
    subs[ev] = L->bus->on(ev, [this, ev](const json& a) { return dispatch(ev, a); });
  }
};

namespace modapi {
void listen(ModRuntime& m, const std::string& ev) {
  m.listened.insert(ev);
  if (m.loader && m.loader->impl) m.loader->impl->subscribe(ev);
}
} // namespace modapi

ModLoader::ModLoader() : impl(std::make_unique<Impl>()) { impl->L = this; }
ModLoader::~ModLoader() {
  onWorldClose();
  impl->scripts.clear();
  for (auto& n : Impl::natives()) if (n->loader == this) n->loader = nullptr;
}

ModInfo* ModLoader::find(const std::string& id) { for (auto& m : mods) if (m.id == id) return &m; return nullptr; }

static std::string readName(const std::string& code, const std::string& def) {
  static const std::regex re(R"(DQ\.name\s*=\s*['"]([^'"]{1,40})['"])");
  std::smatch mt;
  return std::regex_search(code, mt, re) ? mt[1].str() : def;
}
static bool readPlugin(const std::string& code) { static const std::regex re(R"(\bDQ\.plugin\s*=\s*true)"); return std::regex_search(code, re); }
static bool isLib(const fs::path& p) { auto e = p.extension().string(); return e == ".so" || e == ".dll" || e == ".dylib"; }
static fs::path mainJs(const ModInfo& m) {
  if (fs::is_regular_file(m.path)) return m.path;
  auto mj = readJson(m.path / "mod.json");
  std::string main = mj && mj->is_object() ? mj->value("main", "main.js") : "main.js";
  return m.path / main;
}

void ModLoader::scan() {
  mods.clear();
  fs::path dir = userSub("mods");
  json state = readJson(dir / "mods.json").value_or(json::object());
  if (!state.is_object()) state = json::object();
  std::error_code ec;
  std::vector<fs::directory_entry> entries;
  for (auto& e : fs::directory_iterator(dir, ec)) entries.push_back(e);
  std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.path().filename() < b.path().filename(); });
  for (auto& e : entries) {
    ModInfo m;
    m.path = e.path();
    if (e.is_directory()) {
      m.id = e.path().filename().string();
      auto mj = readJson(e.path() / "mod.json");
      bool hasLib = false;
      for (auto& f : fs::directory_iterator(e.path(), ec)) if (isLib(f.path())) hasLib = true;
      std::string kind = hasLib ? "native" : fs::exists(mainJs(m)) ? "js" : "data";
      if (mj && mj->is_object()) {
        m.name = mj->value("name", m.id); m.version = mj->value("version", ""); m.author = mj->value("author", "");
        m.description = mj->value("description", ""); m.kind = mj->value("kind", kind); m.plugin = mj->value("plugin", false);
      } else { m.name = m.id; m.kind = kind; }
      if (m.kind == "js") if (auto code = readFile(mainJs(m))) { m.name = readName(*code, m.name); m.plugin = m.plugin || readPlugin(*code); }
    } else if (e.path().extension() == ".js") {
      m.id = e.path().stem().string(); m.kind = "js";
      auto code = readFile(e.path());
      m.name = readName(code ? *code : "", m.id); m.plugin = code && readPlugin(*code);
    } else continue;
    if (state.contains(m.id) && state[m.id].is_object()) { m.enabled = state[m.id].value("enabled", true); m.plugin = m.plugin || state[m.id].value("plugin", false); }
    mods.push_back(m);
  }
}
void ModLoader::setEnabled(const std::string& id, bool on) { if (ModInfo* m = find(id)) m->enabled = on; }
void ModLoader::saveState() {
  json s = json::object();
  for (auto& m : mods) { s[m.id] = {{"enabled", m.enabled}}; if (m.plugin) s[m.id]["plugin"] = true; }
  writeJson(userSub("mods") / "mods.json", s);
}

bool ModLoader::checkSyntax(const std::string& code, std::string* err) { return jsCheckSyntax(code, err); }

bool ModLoader::addMod(const fs::path& file, bool asPlugin, std::string* err) {
  fs::path dir = userSub("mods");
  std::error_code ec;
  if (fs::is_directory(file)) {
    fs::path to = dir / file.filename();
    if (fs::exists(to)) { if (err) *err = "A mod with that name is already there"; return false; }
    fs::copy(file, to, fs::copy_options::recursive, ec);
    if (ec) { if (err) *err = ec.message(); return false; }
  } else {
    auto code = readFile(file);
    if (!code) { if (err) *err = "That file couldn't be read"; return false; }
    std::string e;
    if (!checkSyntax(*code, &e)) { if (err) *err = "That file has an error in it: " + e; return false; }
    std::string base = file.stem().string(), id = base; int n = 1;
    while (fs::exists(dir / (id + ".js")) || fs::exists(dir / id)) id = base + "_" + std::to_string(++n);
    if (!writeFileAtomic(dir / (id + ".js"), *code)) { if (err) *err = "There was no room to keep it"; return false; }
    scan();
    if (ModInfo* m = find(id)) { m->plugin = m->plugin || asPlugin; m->enabled = true; }
    saveState();
    return true;
  }
  scan(); saveState();
  return true;
}
bool ModLoader::removeMod(const std::string& id) {
  ModInfo* m = find(id);
  if (!m) return false;
  std::error_code ec;
  fs::remove_all(m->path, ec);
  if (ec) return false;
  scan(); saveState();
  return true;
}

std::string ModLoader::exampleMod() {
  return R"(// An example DokuQuest mod/plugin. Put it in the mods folder (or add it in Options -> Mods & plugins).
DQ.name = 'Welcome & Diamonds';
// a command anyone can use: /roll
DQ.command('roll', 'Roll a die', (player, args) => player.name + ' rolls a ' + (1 + Math.floor(Math.random() * (+args[0] || 6))));
// tell everyone when someone finds diamonds
DQ.on('blockBreak', (player, x, y, z, key) => { if (key === 'diamond_ore') DQ.say(player.name + ' found diamonds!'); });
// greet people joining a server (plugins hear joins)
DQ.on('join', (name) => DQ.say('Welcome, ' + name + '! Type /roll to try your luck.'));
// a new item and its recipe
DQ.item('lucky_charm', { name: 'Lucky Charm', icon: 'emerald', desc: 'It does nothing. Probably.' });
DQ.recipe({ out: 'lucky_charm', n: 1, items: ['emerald', 'gold_nugget', 'string'] });
)";
}

static std::string enabledSig(const std::vector<ModInfo>& mods) {
  std::string s;
  for (auto& m : mods) if (m.enabled && (m.kind == "js" || m.kind == "native")) s += m.id + (m.plugin ? "+p" : "") + ";";
  return s;
}

static void runScripts(ModLoader& L, std::vector<ModInfo>& mods, ModLoader::Impl& I) {
  I.scripts.clear();   // their commands go with them
  for (auto& m : mods) {
    if (!m.enabled) continue;
    if (m.kind == "js") {
      auto code = readFile(mainJs(m));
      if (!code) { m.error = "its code couldn't be read"; continue; }
      std::string err;
      m.error.clear();
      auto rt = loadJsMod(L, m, *code, err);
      if (rt) { m.loaded = true; DQ_LOG("mods: %s%s", m.name.c_str(), m.plugin ? " (plugin)" : ""); I.scripts.push_back(std::move(rt)); }
      else { m.loaded = false; m.error = err; DQ_WARN("mods: %s didn't start: %s", m.name.c_str(), err.c_str()); }
    } else if (m.kind == "native" && fs::is_directory(m.path)) {
      bool already = false;
      for (auto& n : ModLoader::Impl::natives()) if (n->id == m.id) { already = true; if (!n->loader) n->loader = &L; }
      if (already) continue;
      std::error_code ec;
      for (auto& f : fs::directory_iterator(m.path, ec)) {
        if (!isLib(f.path())) continue;
#ifdef _WIN32
        if (f.path().extension() != ".dll") continue;
#elif defined(__APPLE__)
        if (f.path().extension() != ".dylib") continue;
#else
        if (f.path().extension() != ".so") continue;
#endif
        std::string err;
        auto rt = loadNativeMod(L, m, f.path(), err);
        if (rt) { m.loaded = true; ModLoader::Impl::natives().push_back(std::move(rt)); }
        else { m.error = err; DQ_WARN("mods: %s didn't load: %s", m.id.c_str(), err.c_str()); }
      }
    }
  }
}

void ModLoader::loadData() {
  for (auto& m : mods) if (m.enabled && fs::is_directory(m.path)) {
    bool any = reg().loadDataPack(m.path / "data", m.id);
    any = reg().loadDataPack(m.path, m.id) || any;
    if (any) { m.loaded = true; DQ_LOG("mod data: %s", m.id.c_str()); }
  }
  runScripts(*this, mods, *impl);
  loadedSet = enabledSig(mods);
  reg().finalize();
}

void ModLoader::onWorldOpen(Level& lv, EventBus& events, bool serving) {
  onWorldClose();
  level = &lv; bus = &events; servingNow = serving;
  // switched on or off since: registrations apply on this world open
  if (enabledSig(mods) != loadedSet) { runScripts(*this, mods, *impl); loadedSet = enabledSig(mods); reg().finalize(); }
  impl->open = true;
  impl->subscribe("tick");   // timers
  for (auto* m : impl->all()) for (auto& ev : m->listened) impl->subscribe(ev);
}
void ModLoader::onWorldClose() {
  if (bus) for (auto& [ev, id] : impl->subs) bus->off(id);
  impl->subs.clear();
  impl->open = false;
  level = nullptr; bus = nullptr;
}
void ModLoader::setServing(bool on) { servingNow = on; }

std::vector<std::pair<std::string, fs::path>> modTextures() {
  std::vector<std::pair<std::string, fs::path>> out;
  fs::path dir = userSub("mods");
  json state = readJson(dir / "mods.json").value_or(json::object());
  std::error_code ec;
  std::vector<fs::path> dirs;
  for (auto& e : fs::directory_iterator(dir, ec)) if (e.is_directory()) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());
  for (auto& d : dirs) {
    std::string id = d.filename().string();
    if (state.is_object() && state.contains(id) && state[id].is_object() && !state[id].value("enabled", true)) continue;
    for (fs::path t : {d / "textures", d / "data" / "textures"}) {
      std::vector<fs::path> files;
      for (auto& f : fs::directory_iterator(t, ec)) if (f.path().extension() == ".png") files.push_back(f.path());
      std::sort(files.begin(), files.end());
      for (auto& f : files) out.push_back({f.stem().string(), f});
    }
  }
  return out;
}

} // namespace dq
