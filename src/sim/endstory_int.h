// sim/endstory_int.h — what the endstory files share (internal to sim/endstory*.cpp, end_fight, dragons,
// deepdark, haunt, rift). Not an API for other areas: use sim/endstory.h.
#pragma once
#include "sim/endstory.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/content.h"
#include "sim/mob_combat.h"
#include "sim/magic_fx.h"
#include "core/registry.h"
#include "gen/gendefs.h"
#include <deque>

namespace dq::es {

// what a brain remembers between ticks (not saved: the original kept these on the Mob object)
struct Mem {
  // endermen
  float stareT = 0, lookT = 0, tpT = 2, wetCd = 0;
  // dragons
  float stT = 4, orbit = 0, flapT = 0, pbT = 2, breathHitT = 0, stuckT = 0; bool hasOrbit = false, breathed = false, perched = false, hasBreathPt = false;
  vec3 breathPt{0};
  // the Warden
  std::unordered_map<u32, float> anger;
  struct Known { vec3 p; double t; };
  std::unordered_map<u32, Known> known;
  double heardT = 0; bool hasTarget = false, hasAim = false; vec3 target{0}, aim{0};
  float calmT = 0, beatT = 0, boomCd = 3, boomCharge = 0, emergeT = 0, digT = 0, y0 = 0;
  // him
  float hbTp = 3, hbBolt = 2, hbFreeze = 12; int calls = 0;
  bool init = false;
};
struct PlayerMem {
  bool init = false; vec3 last{0}; bool wasGround = false; float lastVy = 0;
  float gateCd = 0, riftCd = 0, stepT = 0, wdT = 0, callT = 0, eyeCd = 0;
  double pearlCd = 0, snowCd = 0;
};

class EndStory : public LevelSystem {
public:
  explicit EndStory(Level& l);
  ~EndStory() override;
  const char* name() const override { return "endstory"; }
  void tick(Level& l, double dt) override;

  Level& lv;
  double now = 0;                       // seconds since the level opened (the original's g.time)
  std::unordered_map<u32, Mem> mems;
  std::unordered_map<u32, PlayerMem> pms;
  Mem& mem(const Entity& e) { return mems[e.id]; }
  PlayerMem& pm(const Player& p);
  void later(double secs, std::function<void(Level&)> f) { timers.push_back({now + secs, std::move(f)}); }

  // deepdark.js: the vibrations the Warden hears (g._vibes) and the sculk's cool-downs
  struct Vibe { vec3 p; u32 src; double t; std::string kind; int realm; };
  std::deque<Vibe> vibes;
  std::unordered_map<std::string, double> sculkCd;
  std::unordered_map<u64, std::vector<ivec4>> sculkCells;   // per chunk: x, y, z, id (+ meta in the high bits of id? no: w = id | meta << 16)
  // timers of the modules
  float endT = 0, dragT = 0, eggT = 0, deepT = 0, spawnT = 0, riftT = 1, shrT = 0;
  double riftEventT = 120, frozenT = 0, dreadT = 0;
  std::vector<u32> frozenCalm;          // creatures held still while time stands still
  std::string tearSig;                  // the rift tears last handed to the portal system
  // the watcher (haunt.js Haunt), per player on this machine: one state for the level (the original kept one per game)
  struct Haunt { double next = 600, traceT = 900; u32 active = 0; bool noticed = false, force = false, init = false; } haunt;
  std::vector<int> handlers;

private:
  struct Timer { double at; std::function<void(Level&)> fn; };
  std::vector<Timer> timers;
};
EndStory* endStory(Level& lv);

// ---- module entry points (each file) ----
void endFightTick(Level& lv, EndStory& S, double dt);           // end_fight.cpp
void endOnDeath(Level& lv, EndStory& S, Creature& c, Entity* killer);
bool endItemUse(Level& lv, EndStory& S, Player& p, const std::string& item, ivec3 hit, int face);
void endBlockChanged(Level& lv, World& w, int x, int y, int z, BlockId o, BlockId n);
void registerEndCreatures();
void dragonsTick(Level& lv, EndStory& S, double dt);            // dragons.cpp
void dragonsOnDeath(Level& lv, EndStory& S, Creature& c, Entity* killer);
void dragonsBlockChanged(Level& lv, World& w, int x, int y, int z, BlockId o, BlockId n);
void registerDragons();
void deepTick(Level& lv, EndStory& S, double dt);               // deepdark.cpp
void deepOnDeath(Level& lv, EndStory& S, Creature& c, Entity* killer);
bool deepItemUse(Level& lv, EndStory& S, Player& p, const std::string& item, ivec3 hit, int face);
void deepBlockChanged(Level& lv, World& w, int x, int y, int z, BlockId o, BlockId n);
void registerDeepDark();
void hauntTick(Level& lv, EndStory& S, double dt);              // haunt.cpp
void registerHaunt();
void riftTick(Level& lv, EndStory& S, double dt);               // rift.cpp
void riftOnDeath(Level& lv, EndStory& S, Creature& c, Entity* killer);
bool riftItemUse(Level& lv, EndStory& S, Player& p, const std::string& item, ivec3 hit, int face);
void riftVillageEnter(Level& lv, Player& p, const std::string& villageId, const std::string& folkName);
void registerRift();

// the watcher (haunt.cpp): a figure that stands and stares (modes far | behind | cave | peek); riftarc's doings use it too
Entity* spawnWatcher(Level& lv, Player& p, vec3 at, const std::string& mode, float stare = 0, bool riftDoor = false);
void watcherVanish(Level& lv, Entity& w);
bool hauntBossFight(Level& lv);
std::vector<ivec3> hauntPortals(Level& lv, int realm);

// ---- helpers ----
inline BlockId B(const char* k) { return reg().blockId(k); }
inline bool solid(BlockId id) { return reg().tSolid[id]; }
inline bool liquid(BlockId id) { return reg().tLiquid[id]; }
inline const BlockDef& def(BlockId id) { return reg().block(id); }
inline bool isLeaves(BlockId id) { return id && def(id).has("isLeaves"); }
inline bool isLog(BlockId id) { return id && def(id).has("isLog"); }
inline bool truthy(const json& j, const char* k) { return j.is_object() && j.contains(k) && !j[k].is_null() && j[k] != false && j[k] != 0 && j[k] != ""; }
json& sub(json& j, const char* k, bool array = false);   // j[k] as an object (or array), made if missing
double num(const json& j, const char* k, double d = 0);
int jint(const json& j, const char* k, int d = 0);
vec3 jvec(const json& j, vec3 d = vec3(0));
inline json vj(vec3 v) { return json::array({v.x, v.y, v.z}); }
float rnd();                                   // Math.random()
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float turnTo(float a, float b, float k) { float d = std::remainder(b - a, TAU); return a + clampf(d, -k, k); }
inline float headingTo(vec3 from, float tx, float tz) { return std::atan2(-(tx - from.x), -(tz - from.z)); }
// toasts: to one player (id) or every player near a place (r <= 0: everyone in the level)
void say(Level& lv, u32 id, const std::string& text, const std::string& kind = "info");
void sayNear(Level& lv, int realm, vec3 at, float r, const std::string& text, const std::string& kind = "info");
void sayAll(Level& lv, const std::string& text, const std::string& kind = "info");
std::vector<Player*> livePlayers(Level& lv, int realm = -1);   // in the game (not dead); realm -1 = any
inline int dimHere(const Entity& e) { return e.realm == REALM_MAIN ? dimOfY(ifloor(e.pos.y)) : -1; }
inline bool inEnd(const Entity& e) { return e.realm == REALM_MAIN && dimOfY(ifloor(e.pos.y)) == DIM_END; }
inline bool inOverworld(const Entity& e) { return e.realm == REALM_MAIN && dimOfY(ifloor(e.pos.y)) == DIM_OVERWORLD; }
Creature* spawnMob(Level& lv, int realm, const std::string& type, vec3 at);
void teleport(Level& lv, Entity& e, vec3 to);   // pos, no velocity, no fall; portals forget the old place
void particles(Level& lv, const std::string& tile, vec3 at, int n, float speed, float grav = 14);
bool lineOfSight(Level& lv, int realm, vec3 eye, vec3 to, float d);   // haunt.js lineOfSight (leaves don't block)
int standY(World& w, float x, float z, float py);                  // riftarc.js riftStandY (-1 none)
bool hurtPawn(Level& lv, Entity& who, float amount, Entity* by, const std::string& type, float knock = -1, const std::string& element = "");
// structures (plans from the band generator's StructureIndex; data in the original's shape)
struct Plan { std::string id, name; int dim = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0, y = 0, cx = 0, cz = 0; json data; };
std::vector<Plan> plansNear(Level& lv, int dim, const std::string& key, int x, int z, int pad);
const json& mobType(const std::string& key);
std::string capLabel(const std::string& label);   // "the Ender Dragon" -> "The Ender Dragon"

} // namespace dq::es
