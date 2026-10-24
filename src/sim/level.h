// sim/level.h — one open world (save): its realms (the stacked main world + side realms),
// entities, players, time of day, weather, rules and saved global state. Runs the same way in
// single player (inside the game), on the launcher's dedicated server and headless.
#pragma once
#include "world/world.h"
#include "sim/entity.h"
#include "sim/events.h"
#include <map>

namespace dq {

constexpr double DAY_LEN = 1200;   // seconds per full day (game/game.js)
constexpr double TICK_DT = 1.0 / 30.0;

struct LevelInfo {
  std::string name = "New World";
  std::string folder;          // folder name under saves/
  i32 seed = 0;
  std::string seedText;
  std::string mode = "survival";       // survival | creative | adventure | spectator
  std::string difficulty = "normal";   // peaceful | easy | normal | hard
  std::string worldType = "default";
  int genVersion = 3;
  double time = 300;           // seconds into the day (0 = sunrise in the original's clock)
  i64 day = 0;
  i64 created = 0, lastPlayed = 0;
  vec3 spawn{0, -1, 0};        // y < 0 = not chosen yet
  json rules = json::object(); // game rules (rules.js): keepInventory, mobGriefing, ...
  json data = json::object();  // global state for systems: portal links, maps, quests, towns, weather...
  json toJson() const;
  void fromJson(const json& j);
};

class Level {
public:
  Level(LevelInfo info, const fs::path& folder, ThreadPool* pool, EventBus* events);
  ~Level();
  static std::unique_ptr<Level> open(const fs::path& folder, ThreadPool* pool, EventBus* events);   // nullptr if not a world
  static std::unique_ptr<Level> create(const fs::path& savesDir, LevelInfo info, ThreadPool* pool, EventBus* events);

  LevelInfo info;
  fs::path folder;
  ThreadPool* pool;
  EventBus* events;
  std::shared_ptr<WorldGen> gen;

  World& world(int realm = REALM_MAIN);
  World* worldIfOpen(int realm) const { auto it = realms.find(realm); return it == realms.end() ? nullptr : it->second.get(); }
  const std::map<int, std::unique_ptr<World>>& allRealms() const { return realms; }

  // entities
  Entity* spawn(std::unique_ptr<Entity> e);
  Entity* entity(u32 id) const { auto it = entityIndex.find(id); return it == entityIndex.end() ? nullptr : it->second; }
  std::vector<std::unique_ptr<Entity>>& entities() { return ents; }
  template <class F> void forEntitiesNear(int realm, vec3 p, float r, F f) {
    for (auto& e : ents) if (!e->removed && e->realm == realm && glm::distance(e->pos, p) <= r) f(*e);
  }

  // per-player saved data (inventory, position...) keyed by player name
  json loadPlayer(const std::string& name) const;
  void savePlayer(const std::string& name, const json& j);

  // simulation
  void tick(double dt);                // fixed-step: call with TICK_DT
  double dayFraction() const { return std::fmod(info.time, DAY_LEN) / DAY_LEN; }
  bool isNight() const;
  std::vector<std::shared_ptr<class LevelSystem>> systems;   // weather, spawning, villages... (added by subsystems)
  template <class T> T* system() { for (auto& s : systems) if (auto p = dynamic_cast<T*>(s.get())) return p; return nullptr; }

  void save(bool all = true);
  void close();
  double autosaveTimer = 0;

private:
  std::map<int, std::unique_ptr<World>> realms;
  std::vector<std::unique_ptr<Entity>> ents;
  std::unordered_map<u32, Entity*> entityIndex;
  u32 nextEntityId = 1;
};

class LevelSystem {
public:
  virtual ~LevelSystem() = default;
  virtual const char* name() const = 0;
  virtual void tick(Level& lv, double dt) {}
  virtual void save(Level& lv) {}
};

using LevelSystemFactory = std::function<std::shared_ptr<LevelSystem>(Level&)>;
void registerLevelSystem(LevelSystemFactory f);   // added to every level when it opens

} // namespace dq
