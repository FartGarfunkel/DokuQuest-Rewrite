// sim/villagers_internal.h — shared between the villager files (villagers.cpp: villages, standing, the system;
// villager_ai.cpp: the brain; villager_quests.cpp: quests, shops, talk, paths; villager_life.cpp: gates, raids,
// named foes, the missing, homes, the rift's word).
#pragma once
#include "sim/villagers.h"
#include "sim/content.h"
#include <map>
#include <unordered_map>

namespace dq {

// a villager's mind (the original kept these on the Mob: goal, holdT, confront, greeted, npcDoors...)
struct NpcMind {
  bool hasGoal = false; vec3 goal{0}; float goalT = 0; int goalSerial = 0, lastSerial = -1;
  float lookT = 0, waveT = 0, holdT = 0; u32 holdBy = 0;
  u32 confront = 0; float confrontT = 0; bool confrontSaid = false; std::string confrontVictim;
  u32 foe = 0; float foeT = 0, scanT = 0;
  std::map<std::string, double> greeted;
  u32 chatMate = 0; float chatT = 0, workT = 0; double mutterT = -1e9;
  struct Door { ivec3 at; double t; };
  std::vector<Door> doors;
  // npcGo
  std::vector<ivec3> path; size_t pathI = 0; bool hasPath = false; float navT = 0; bool hasNavGoal = false; vec3 navGoal{0};
  int stuckGo = 0; float stuckT = 0, sideStepT = 0; bool hasNav = false; vec2 navXZ{0};
  float stuckCheckT = 0; int stuckN = 0;   // towns3.js npcStuckCheck
  // towns3.js: the watch at the gates
  float watchT = 0, watchLen = 0; bool patrol = false;
  // hearth.js: the missing
  std::string lostQ; u32 follow = 0;
  bool pinned = false; std::string sitHeld;
  int pose = 0;
};

struct Offence { int strikes = 0; double warnT = -1e9; bool hostile = false, forgiven = false; std::string victim; bool victimGuard = false; };

class VillagerSystem : public LevelSystem {
public:
  const char* name() const override { return "villagers"; }
  void tick(Level& lv, double dt) override;
  double now = 0;                                                // the original's g.time (seconds of play)
  std::unordered_map<u32, NpcMind> minds;
  struct Active { VillageRef v; std::vector<u32> ents; };
  std::map<std::string, Active> active;                          // village id -> who's out
  std::map<std::string, VillageRef> manual;                      // addVillage
  std::unordered_map<const void*, VillageRef> parsed;            // structure plan -> village
  std::map<std::string, Offence> offence;                        // player|village
  float npcT = 0, gateT = 0, raidCheckT = 30, eliteT = 0, lostT = 0, homeT = 0;
  bool cleaned = false;
  // towns3.js gates
  std::map<std::string, int> gateSeen; std::map<std::string, float> gateHeld, gateManual; std::map<std::string, int> gatePending;
  int gateNightWas = -1; std::string lastDusk;
  // livingworld.js
  struct Raid { std::string vid, name; std::vector<u32> mobs; double t = 0; int n = 0; bool on = false; } raid;
  // hearth.js
  struct Pending { std::string vid, player; int tries = 0; };
  std::vector<Pending> homePending;
  std::map<std::string, float> playerT;                          // per-player timers (welcome, rift call, path, lapse)
  std::vector<std::pair<double, std::pair<std::string, std::string>>> later;   // [when, player, toast] (the original's setTimeout)
  NpcMind& mind(u32 id) { return minds[id]; }
};
VillagerSystem* vsys(Level& lv);

// villagers.cpp
Village* mutableVillage(const VillageRef& v);
Creature* npcSpawn(Level& lv, const Village& v, int i, const std::string& kind);
void npcTickVillages(Level& lv, VillagerSystem& S, double dt);
Player* playerByName(Level& lv, const std::string& name);
Player* playerById(Level& lv, u32 id);
std::string dirWord(double ang);                                 // 'east' ... 'north-east' (Math.atan2(dz, dx))
int roundTo(double v, int step);
int riftStability(Level& lv);      // sim/endstory.cpp
int riftTier(Level& lv);
json& villageRec(Level& lv, const std::string& vid);
void later(Level& lv, double secs, const Player& p, const std::string& text);

// villager_ai.cpp
OptMove npcBrain(Level& lv, Creature& e, Entity* p, float dist, float dt);
OptMove lostAI(Level& lv, Creature& e, NpcMind& M, float dt);
void npcOffend(Level& lv, Player& who, const std::string& vid, Creature& victim, bool killed);
void npcForgive(Level& lv, Player& who, const std::string& vid);
void npcMutter(Level& lv, Creature& e, const json& lines);
void npcSay(Level& lv, Creature& e, const std::string& line, float vol = 0.5f);   // the babble (one voice per line)
float turnTo(float a, float b, float k);
void npcShutDoors(Level& lv, Creature& e, NpcMind& M, bool all = false);
void npcStuckCheck(Level& lv, Creature& e, NpcMind& M);

// villager_quests.cpp
void questHudChanged(Level& lv, Player& p);
bool riftAskAvailable(Level& lv);

// villager_life.cpp
void gatesTick(Level& lv, VillagerSystem& S, double dt);
void raidTick(Level& lv, VillagerSystem& S, double dt);
void eliteTick(Level& lv, VillagerSystem& S, double dt);
void lostTick(Level& lv, VillagerSystem& S, double dt);
void homeTick(Level& lv, VillagerSystem& S, double dt);
void playerLifeTick(Level& lv, VillagerSystem& S, Player& p, double dt);   // welcome, rift calls, path told, lapses
void eliteSlain(Level& lv, Creature& m);
bool homeAsk(Level& lv, Player& p, const Village& v, std::string& say);   // the elder's offer (hearth.js)
const json* homeOf(Player& p, const std::string& vid);
bool raidStart(Level& lv, const Village& v, double st, bool sealed);
const std::vector<VGate>& gatesOf(Level& lv, const Village& v);

// shared tiny helpers
inline const json& jarr(const json& j, const char* k) { static const json E = json::array(); return j.is_object() && j.contains(k) && j[k].is_array() ? j[k] : E; }
inline std::string pickStr(const json& list, double r) { return list.is_array() && !list.empty() ? list[(size_t)std::floor(r * list.size())].get<std::string>() : std::string(); }
constexpr int DXL[4] = {0, 1, 0, -1}, DZL[4] = {-1, 0, 1, 0};
std::string itemName(const std::string& id);
int countOf(Player& p, const std::string& id);                   // 'log' counts every #log
void removeOf(Player& p, const std::string& id, int n);
void vGive(Level& lv, Player& p, const ItemStack& s);              // lp.give(), the rest dropped at your feet

} // namespace dq
