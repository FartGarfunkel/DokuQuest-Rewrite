#include "sim/level.h"
#include "common/noise.h"
#include <map>

namespace dq {

// ---- entities ----
json Entity::save() const {
  return {{"type", type}, {"realm", realm}, {"pos", {pos.x, pos.y, pos.z}}, {"vel", {vel.x, vel.y, vel.z}}, {"yaw", yaw}, {"pitch", pitch},
          {"hp", hp}, {"maxHp", maxHp}, {"age", age}, {"persistent", persistent}, {"data", data}};
}
void Entity::load(const json& j) {
  type = j.value("type", type); realm = j.value("realm", 0);
  if (j.contains("pos")) pos = {j["pos"][0], j["pos"][1], j["pos"][2]};
  if (j.contains("vel")) vel = {j["vel"][0], j["vel"][1], j["vel"][2]};
  yaw = j.value("yaw", 0.0f); pitch = j.value("pitch", 0.0f); hp = j.value("hp", hp); maxHp = j.value("maxHp", maxHp);
  age = j.value("age", 0.0f); persistent = j.value("persistent", false);
  if (j.contains("data")) data = j["data"];
  prevPos = pos;
}
static std::vector<std::pair<std::string, EntityFactory>>& entFactories() { static std::vector<std::pair<std::string, EntityFactory>> f; return f; }
void registerEntityFactory(const std::string& t, EntityFactory f) { entFactories().insert(entFactories().begin(), {t, std::move(f)}); }
std::unique_ptr<Entity> createEntity(const std::string& type) {
  for (auto& [k, f] : entFactories()) if (k == type) if (auto e = f(type)) return e;
  for (auto& [k, f] : entFactories()) if (!k.empty() && k.back() == '*' && startsWith(type, k.substr(0, k.size() - 1))) if (auto e = f(type)) return e;
  for (auto& [k, f] : entFactories()) if (k.empty()) if (auto e = f(type)) return e;
  auto e = std::make_unique<Entity>(); e->type = type; return e;
}

// ---- level info ----
json LevelInfo::toJson() const {
  return {{"name", name}, {"seed", seed}, {"seedText", seedText}, {"mode", mode}, {"difficulty", difficulty}, {"worldType", worldType},
          {"genVersion", genVersion}, {"time", time}, {"day", day}, {"created", created}, {"lastPlayed", lastPlayed},
          {"spawn", {spawn.x, spawn.y, spawn.z}}, {"rules", rules}, {"data", data}, {"format", 1}, {"gameVersion", DQ_VERSION}};
}
void LevelInfo::fromJson(const json& j) {
  name = j.value("name", name); seed = j.value("seed", 0); seedText = j.value("seedText", ""); mode = j.value("mode", mode);
  difficulty = j.value("difficulty", difficulty); worldType = j.value("worldType", worldType); genVersion = j.value("genVersion", 3);
  time = j.value("time", 300.0); day = j.value("day", (i64)0); created = j.value("created", (i64)0); lastPlayed = j.value("lastPlayed", (i64)0);
  if (j.contains("spawn")) spawn = {j["spawn"][0], j["spawn"][1], j["spawn"][2]};
  if (j.contains("rules")) rules = j["rules"];
  if (j.contains("data")) data = j["data"];
}

static std::vector<LevelSystemFactory>& lsFactories() { static std::vector<LevelSystemFactory> f; return f; }
void registerLevelSystem(LevelSystemFactory f) { lsFactories().push_back(std::move(f)); }

Level::Level(LevelInfo i, const fs::path& folder, ThreadPool* pool, EventBus* events)
  : info(std::move(i)), folder(folder), pool(pool), events(events) {
  WorldGenOptions o; o.seed = info.seed; o.genVersion = info.genVersion; o.worldType = info.worldType;
  gen = std::make_shared<WorldGen>(o);
  realms[REALM_MAIN] = std::make_unique<World>(REALM_MAIN, gen, folder, pool);
  if (auto ej = readJson(folder / "entities.json")) for (auto& e : *ej) {
    auto ent = createEntity(e.value("type", "?")); ent->load(e); spawn(std::move(ent));
  }
  for (auto& f : lsFactories()) if (auto s = f(*this)) systems.push_back(s);
}
Level::~Level() { close(); }

World& Level::world(int realm) {
  auto it = realms.find(realm);
  if (it != realms.end()) return *it->second;
  // side realms get their own generator set (their worldType is chosen by the realm's system)
  WorldGenOptions o; o.seed = info.seed + realm * 1013; o.genVersion = info.genVersion;
  o.worldType = realm == REALM_POCKET ? "pocket" : realm == REALM_HIS ? "realm" : info.worldType;
  auto g = std::make_shared<WorldGen>(o);
  auto w = std::make_unique<World>(realm, g, folder / ("realm" + std::to_string(realm)), pool);
  World& ref = *w;
  realms[realm] = std::move(w);
  return ref;
}

std::unique_ptr<Level> Level::open(const fs::path& folder, ThreadPool* pool, EventBus* events) {
  auto j = readJson(folder / "level.json");
  if (!j) return nullptr;
  LevelInfo info; info.fromJson(*j); info.folder = folder.filename().string();
  auto lv = std::make_unique<Level>(info, folder, pool, events);
  return lv;
}
std::unique_ptr<Level> Level::create(const fs::path& savesDir, LevelInfo info, ThreadPool* pool, EventBus* events) {
  std::string base;
  for (char c : info.name) base += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  if (base.empty()) base = "World";
  std::string name = base; int n = 1;
  while (fs::exists(savesDir / name)) name = base + "_" + std::to_string(++n);
  fs::create_directories(savesDir / name);
  if (info.seedText.empty()) info.seed = (i32)(u32)(unixMillis() * 2654435761u);
  else info.seed = seedFromString(info.seedText);
  info.created = info.lastPlayed = unixMillis();
  info.folder = name;
  writeJson(savesDir / name / "level.json", info.toJson());
  return std::make_unique<Level>(info, savesDir / name, pool, events);
}

Entity* Level::spawn(std::unique_ptr<Entity> e) {
  if (!e->id) e->id = nextEntityId++;
  else nextEntityId = std::max(nextEntityId, e->id + 1);
  e->prevPos = e->pos;
  Entity* raw = e.get();
  entityIndex[raw->id] = raw;
  ents.push_back(std::move(e));
  if (events) events->emit("entitySpawn", json::array({raw->id, raw->type}));
  return raw;
}

json Level::loadPlayer(const std::string& name) const {
  auto j = readJson(folder / "players" / (name + ".json"));
  return j ? *j : json();
}
void Level::savePlayer(const std::string& name, const json& j) { writeJson(folder / "players" / (name + ".json"), j); }

bool Level::isNight() const { double f = dayFraction(); return f > 0.5 && f < 0.95; }

void Level::tick(double dt) {
  info.time += dt;
  if (info.time >= DAY_LEN) { info.time -= DAY_LEN; info.day++; }
  for (auto& [r, w] : realms) w->tickSystems(dt);
  for (auto& s : systems) s->tick(*this, dt);
  for (size_t i = 0; i < ents.size(); i++) {   // entities may spawn others while ticking
    Entity* e = ents[i].get();
    if (e->removed) continue;
    e->prevPos = e->pos; e->prevYaw = e->yaw;
    e->age += (float)dt;
    e->tick(*this, dt);
  }
  ents.erase(std::remove_if(ents.begin(), ents.end(), [&](const std::unique_ptr<Entity>& e) {
    if (e->removed) { entityIndex.erase(e->id); return true; } return false; }), ents.end());
  if (events) events->emit("tick", json::array({dt}));
  autosaveTimer += dt;
  if (autosaveTimer > 60) { autosaveTimer = 0; save(false); }
}

void Level::save(bool all) {
  info.lastPlayed = unixMillis();
  for (auto& s : systems) s->save(*this);
  writeJson(folder / "level.json", info.toJson());
  json ej = json::array();
  for (auto& e : ents) if (!e->removed && !e->isPlayer() && (e->persistent || e->type != "item")) ej.push_back(e->save());
  writeJson(folder / "entities.json", ej, false);
  for (auto& [r, w] : realms) w->saveDirty(all);
}

void Level::close() {
  if (realms.empty()) return;
  save(true);
  for (auto& [r, w] : realms) w->unloadAll(true);
  realms.clear();
}

} // namespace dq
