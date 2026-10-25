// mod/mod_api.cpp — the DQ API's actions (game/mods.js modApi), shared by JS mods and native plugins.
#include "mod/mod_internal.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/commands.h"
#include "net/server_host.h"
#include <atomic>

namespace dq {

void ModRuntime::fault(const std::string& what) {
  if (ModInfo* i = info()) i->error = what;
  if (!warned) {
    warned = true;
    DQ_WARN("mod %s had a problem: %s", name.c_str(), what.c_str());
    if (loader && loader->level && loader->level->net)
      if (auto* h = dynamic_cast<net::ServerHost*>(loader->level->net.get())) h->serverLog("Plugin \"" + name + "\" had a problem: " + what, "warn");
  }
}

namespace modapi {

static Player* findPlayer(ModLoader& L, const std::string& name) {
  if (!L.level) return nullptr;
  for (auto& e : L.level->entities()) if (!e->removed && e->isPlayer()) if (auto* p = dynamic_cast<Player*>(e.get())) if (toLower(p->name) == toLower(name)) return p;
  return nullptr;
}
static json objOf(const Player& p) {
  return {{"name", p.name}, {"x", p.pos.x}, {"y", p.pos.y}, {"z", p.pos.z}, {"dim", jsFromDim(dimOfY((int)p.pos.y))}, {"realm", p.realm}, {"hp", p.hp}};
}
json playerObj(ModLoader& L, const std::string& name) {
  if (Player* p = findPlayer(L, name)) return objOf(*p);
  return {{"name", name}};
}
json players(ModLoader& L) {
  json a = json::array();
  if (L.level) for (auto& e : L.level->entities()) if (!e->removed && e->isPlayer()) if (auto* p = dynamic_cast<Player*>(e.get())) a.push_back(objOf(*p));
  return a;
}
json getBlock(ModLoader& L, double x, double y, double z, int realm) {
  if (!L.level) return json();
  World* w = L.level->worldIfOpen(realm);
  int ix = ifloor(x), iy = ifloor(y), iz = ifloor(z);
  if (!w || !inWorldY(iy) || !w->isLoaded(ix, iy, iz)) return json();
  return reg().block(w->getBlock(ix, iy, iz)).key;
}
bool setBlock(ModLoader& L, double x, double y, double z, const std::string& key, int meta, int realm) {
  if (!L.level) return false;
  BlockId id = reg().blockId(key);
  if (!id && key != "air") return false;
  World* w = L.level->worldIfOpen(realm);
  return w && w->setBlock(ifloor(x), ifloor(y), ifloor(z), id, (u8)clampv(meta, 0, 255), SetFlags::Urgent);
}
bool give(ModLoader& L, const std::string& name, const std::string& item, int n) {
  if (!reg().item(item) || n <= 0) return false;
  Player* p = findPlayer(L, name);
  if (!p) return false;
  p->give(ItemStack(item, std::min(n, 64 * INV_MAIN)));
  return true;
}
u32 spawn(ModLoader& L, const std::string& type, double x, double y, double z) {
  if (!L.level || L.level->isRemote() || !reg().mobs.is_object() || !reg().mobs.contains(type)) return 0;   // host only
  auto e = createEntity(type);
  e->type = type; e->pos = e->prevPos = vec3((float)x, (float)y, (float)z);
  return L.level->spawn(std::move(e))->id;
}
void say(ModLoader& L, const std::string& from, const std::string& text) {
  std::string t = text.substr(0, 200);
  if (L.level) L.level->fx->message(0, "<" + from + "> " + t, "chat");
  else DQ_LOG("<%s> %s", from.c_str(), t.c_str());
}
void toast(ModLoader& L, const std::string& text) { if (L.bus) L.bus->emit("toast", json::array({text, ""})); }
double time(ModLoader& L, double set) {
  if (!L.level) return 0;
  if (set >= 0) L.level->info.time = DAY_LEN * std::fmod(set, 1.0);
  return L.level->info.time / DAY_LEN;
}
void log(ModLoader& L, const std::string& from, const std::string& text) {
  if (L.serving() && L.level && L.level->net)   // a server's log (its console, and the "log" event)
    if (auto* h = dynamic_cast<net::ServerHost*>(L.level->net.get())) { h->serverLog("[" + from + "] " + text, ""); return; }
  DQ_LOG("[%s] %s", from.c_str(), text.c_str());
}

static bool keyOk(const std::string& k, size_t lo) {
  if (k.size() < lo || k.size() > 40) return false;
  for (char c : k) if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_')) return false;
  return true;
}
// Mods register with the same code data packs use (core/registry loadDataPack), one small pack at a time.
static bool loadPack(const std::string& modId, const char* file, const json& arr) {
  static std::atomic<int> n{0};
  fs::path dir = userSub("cache") / "modreg" / strFormat("%s_%d", modId.c_str(), n++);
  std::error_code ec;
  fs::create_directories(dir, ec);
  bool ok = writeJson(dir / file, arr, false) && reg().loadDataPack(dir, modId);
  fs::remove_all(dir, ec);
  return ok;
}
bool registerItem(const std::string& modId, json d) {
  std::string key = d.value("key", "");
  if (!keyOk(key, 2) || reg().item(key)) return false;   // item keys /^[a-z0-9_]{2,40}$/, must be new
  if (!d.contains("name")) d["name"] = key;
  if (d.contains("icon") && d["icon"].is_string() && reg().tile(d["icon"]) < 0 && !reg().item(d["icon"])) d.erase("icon");
  if (d.contains("stack") && !d.contains("maxStack")) d["maxStack"] = d["stack"];
  return loadPack(modId, "items.json", json::array({d})) && reg().item(key);
}
bool registerBlock(const std::string& modId, json d) {
  std::string key = d.value("key", "");
  if (!keyOk(key, 2) || reg().blockId(key)) return false;
  if (!d.contains("name")) d["name"] = key;
  if (!d.contains("tex")) d["tex"] = key;
  if (!loadPack(modId, "blocks.json", json::array({d})) || !reg().blockId(key)) return false;
  if (!reg().item(key) && d.value("item", true)) registerItem(modId, {{"key", key}, {"name", d["name"]}, {"block", key}, {"icon", key}});
  return true;
}
bool addRecipe(const json& d) {
  if (!d.is_object() || !d.contains("out") || !d["out"].is_string() || !reg().item(d["out"])) return false;
  Recipe r; r.props = d;
  r.out = d["out"]; r.n = d.value("n", 1);
  if (d.contains("shape") && d["shape"].is_array()) for (auto& row : d["shape"]) if (row.is_string()) r.shape.push_back(row);
  if (d.contains("key") && d["key"].is_object()) for (auto& [k, v] : d["key"].items()) if (!k.empty() && v.is_string()) r.key[k[0]] = v.get<std::string>();
  if (d.contains("items") && d["items"].is_array()) for (auto& v : d["items"]) if (v.is_string()) r.items.push_back(v);
  if (r.shape.empty() && r.items.empty()) return false;
  r.h = (int)r.shape.size();
  for (auto& row : r.shape) r.w = std::max(r.w, (int)row.size());
  r.group = d.value("group", "");
  reg().addRecipe(std::move(r));
  return true;
}
bool command(ModRuntime& m, const std::string& name, const std::string& desc, std::function<std::string(const json&, const json&)> fn) {
  if (name.empty() || name.size() > 24) return false;
  for (char c : name) if (!isalnum((unsigned char)c) && c != '_') return false;
  ModLoader* L = m.loader;
  std::string modName = m.name;
  registerChatCommand(name, desc + " (" + modName + ")", [fn, L](CommandSource& src, const std::vector<std::string>& args) -> std::string {
    json pl = src.player ? playerObj(*L, src.player->name) : json{{"name", src.name}};
    json a = json::array(); for (auto& s : args) a.push_back(s);
    return fn(pl, a);
  }, false, "", m.id);
  return true;
}
json gameInfo(ModLoader& L) {
  json g = {{"version", DQ_VERSION}, {"serving", L.serving()}};
  if (L.level) {
    auto& I = L.level->info;
    g["name"] = I.name; g["seed"] = I.seed; g["seedText"] = I.seedText; g["mode"] = I.mode; g["difficulty"] = I.difficulty;
    g["day"] = I.day; g["time"] = I.time / DAY_LEN; g["guest"] = L.level->isRemote(); g["rules"] = I.rules;
  }
  return g;
}

} // namespace modapi
} // namespace dq
