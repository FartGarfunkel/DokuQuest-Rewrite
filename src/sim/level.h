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
  bool remote = false;         // not saved: this level is a client's replica of a server's world (net/NetClient).
                               // The server simulates it: spawning, fluids, random ticks, AI etc. must skip remote levels.
  json toJson() const;
  void fromJson(const json& j);
};

// Sounds, particles and messages raised by the simulation. The client plugs in audio/particles/HUD,
// the server forwards them to players. Names follow the original game's sfx calls (game/audio.js).
class LevelFx {
public:
  virtual ~LevelFx() = default;
  virtual void sound(const std::string& name, vec3 pos, float range = 28, float volume = 1, float pitch = 1) {}   // sfx.playAt
  virtual void soundUi(u32 playerId, const std::string& name) {}                                                  // sfx.play (that player only)
  virtual void material(const std::string& mat, const std::string& kind, vec3 pos) {}                            // sfx.materialAt
  virtual void voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch = 1) {}
  virtual void particles(const std::string& kind, vec3 pos, int count = 8, const json& extra = nullptr) {}       // burst / smoke / block crumbs...
  virtual void message(u32 playerId, const std::string& text, const std::string& kind = "") {}                   // toast / chat line (0 = everyone)
  virtual void shake(u32 playerId, float strength) {}
};

class Player;

// The network role of an open level (net/): a ServerHost (dedicated server, "Open to LAN") or a NetClient
// (joined someone's world). Gameplay code calls these where a client must ask the server instead of acting.
class LevelNet {
public:
  virtual ~LevelNet() = default;
  virtual bool isClient() const = 0;
  virtual void update(class Level& lv, double dt) {}                                  // every frame (game) / tick (server)
  virtual void streamAnchors(int realm, std::vector<StreamAnchor>& out) {}            // host: keep remote players' chunks loaded
  virtual bool chat(Player& from, const std::string& text) { return false; }          // client: true = sent to the server
  virtual void hitEntity(Player& by, u32 entityId, float amount, const json& src) {}   // client: hurt a server entity (ghost)
  virtual void action(Player& by, const std::string& name, const json& args) {}       // client: anything else the server must do
  virtual void portal(Player& who, int realm, vec3 to) {}                              // a player used a portal (others may follow)
  virtual std::vector<std::string> playerNames() const { return {}; }
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
  LevelFx* fx = &nullFx;            // never null
  LevelFx nullFx;
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
  std::shared_ptr<LevelNet> net;    // set while this level is served or joined over the network (net/)
  bool isRemote() const { return info.remote; }

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
