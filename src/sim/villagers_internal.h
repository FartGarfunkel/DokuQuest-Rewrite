// sim/villagers_internal.h — shared between the villager files (villagers.cpp: villages, standing, the system;
// villager_ai.cpp: the brain; villager_quests.cpp: quests, shops, talk, paths; villager_life.cpp: gates, raids,
// named foes, the missing, homes, the rift's word).
#pragma once
#include "sim/villagers.h"
#include "sim/content.h"
#include <map>
#include <unordered_map>

namespace dq {

// a step of a townsperson's way (sim/npc_path.cpp): the cell their feet are in, the height they stand at there, and
// whether it's up or down a ladder (a vine, a rope...) rather than on a floor
struct NpcStep { ivec3 at{0}; float floor = 0; bool climb = false; vec2 c{0}; };   // c: where the body's middle goes (a broad one's: a cell corner)
// the way from `from` to `to` for a body bodyW (half width) by bodyH: the steps after the first. *partial: it doesn't get
// there (the steps go as near as the search got). Empty: no step to take at all.
// *snapped: the goal was somewhere nobody stands (over water, in a block) and the way ends at the nearest place by it.
std::vector<NpcStep> npcPath(const World& w, vec3 from, vec3 to, float bodyW, float bodyH, bool* partial = nullptr, int maxNodes = 2400, bool* snapped = nullptr);
// the height a body stands at in cell (x, y, z), or NaN: no floor there (or only a ladder)
float npcStandAt(const World& w, int x, int y, int z, float bodyW = 0.3f, float bodyH = 1.8f);
// the way-finding tallies (tools/test_villagers.cpp's audit reads them): replans, plans that couldn't get there, and the
// times someone was moved to where they were going (stuck for good)
struct NpcNavStats { long plans = 0, partial = 0, failed = 0, rescues = 0, climbs = 0; };
NpcNavStats& npcNavStats();

// a villager's mind (the original kept these on the Mob: goal, holdT, confront, greeted, npcDoors...)
struct NpcMind {
  bool hasGoal = false; vec3 goal{0}; float goalT = 0; int goalSerial = 0, lastSerial = -1;
  float lookT = 0, waveT = 0, holdT = 0; u32 holdBy = 0;
  u32 confront = 0; float confrontT = 0; bool confrontSaid = false; std::string confrontVictim;
  u32 foe = 0; float foeT = 0, scanT = 0;
  std::map<std::string, double> greeted;
  u32 chatMate = 0; float chatT = 0, workT = 0; double mutterT = -1e9; int workN = 0;   // (strokes at a station: the mason's block in, then the cut piece out)
  struct Door { ivec3 at; double t; };
  std::vector<Door> doors;
  // npcGo
  std::vector<NpcStep> path; size_t pathI = 0; bool hasPath = false, pathPartial = false, climbing = false; float navT = 0; bool hasNavGoal = false; vec3 navGoal{0};
  int partialN = 0;   // partial plans in a row toward this goal
  bool wentLast = false;
  bool goalSnapped = false;   // (the way ends by the goal, not on it: it's where nobody can stand)
  bool sent = false; vec3 sentTo{0};   // sent somewhere (tools/test_npc_paths.cpp): they walk there, whatever the hour   // (npcGo walked them last tick: a climber not walked any more is let down)
  float lostT = 0; vec3 lostFrom{0};   // (no way there and not getting anywhere: sim/villager_ai.cpp npcGo's rescue)
  int stuckGo = 0; float stuckT = 0, sideStepT = 0; bool hasNav = false; vec2 navXZ{0};
  int wetN = 0;   // seconds in a row in water (a fountain, a well): climbs out after a few
  float stuckCheckT = 0; int stuckN = 0;   // towns3.js npcStuckCheck
  ivec3 badSeat{INT32_MIN}; double badSeatUntil = 0;   // a seat it couldn't reach: left alone for a while
  // towns3.js: the watch at the gates
  float watchT = 0, watchLen = 0; bool patrol = false;
  // the lookout (sim/villager_lookout.cpp): a guard rides a tower's lift up to the wall walk to keep watch, and back down
  int look = 0; float lookLeft = 0, lookStateT = 0; double nextLookAt = 0, lookClock = 0; ivec3 lookLift{0}; vec3 lookSpot{0};
  // riding a lift (npcRideLift): which, to which floor, how far along (0 to it and on, 1 riding), how long at it
  ivec3 rideS{INT32_MIN}; int rideTo = 0, rideStage = 0; float rideT = 0, rideStageT = 0; double rideSeen = -1e9;
  ivec3 workLift{INT32_MIN}; float workLiftT = 0;   // the lift at their work (the miller's up to the stones), looked for now and then
  // hearth.js: the missing
  std::string lostQ; u32 follow = 0;
  bool pinned = false; std::string sitHeld;
  int pose = 0;
  std::string work; ivec3 workAt{0};   // the trade's work in hand ("hammer", "dip", "brew"...) and its station: the body acts it (render/villager_layer.cpp)
  // the day's chores (sim/villager_chores.cpp): what they carry (an armful of wheat, a sack of flour), the crop they're
  // going to, a pause between jobs
  std::string carry; int carryN = 0; float choreT = 0; bool hasCrop = false; ivec3 crop{0};
  double aboutAt = -1e9;   // when last free and about (sim/villager_social.cpp: only those can be asked into a talk)
  float jobT = 0;   // how long they've been at the job in hand (chopping, mining, fishing...)
  vec3 choreAt{0};  // where they stand to do it
  bool choreIdle = false;   // the day's work has nothing for them just now (they go about the streets, not stand at the door)
  // the trade's round (sim/villager_ai.cpp): which stop they're at, how long left there, and whether they hold its tool
  int taskI = -1; float taskT = 0; bool taskHeld = false;
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
  float npcT = 0, gateT = 0, raidCheckT = 30, eliteT = 0, lostT = 0, homeT = 0, travelT = 20;
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
// the lumberjack fells a tree as a player does (the whole of it falls, its leaves with it); returns the logs for the
// stores (sim/villager_chores.cpp)
int npcFellTree(Level& lv, World& w, ivec3 base, std::string& logKey, vec3 from);
json& villageRec(Level& lv, const std::string& vid);
void later(Level& lv, double secs, const Player& p, const std::string& text);

// villager_ai.cpp
OptMove npcBrain(Level& lv, Creature& e, Entity* p, float dist, float dt);
OptMove npcGo(Level& lv, Creature& e, NpcMind& M, vec3 t, float dt, float speed);   // walk there (its own A*: sim/npc_path.cpp)
void npcUnclimb(Creature& e, NpcMind& M);   // off a ladder (gravity back)
// a guard's turn on lookout, up a stone tower's lift (sim/villager_lookout.cpp): a move while it's on that duty, else none
OptMove lookoutAI(Level& lv, Creature& e, NpcMind& M, const Village& v, float dt);
// a ride on the lift whose slider is `s` from the floor they're on to floor `to` (its "at"): the lift called to them, onto
// the platform, up or down, and there. A move while they're about it; none once they stand at that floor, or when they
// gave it up (*gaveUp: no such lift, or a minute without getting there)
OptMove npcRideLift(Level& lv, Creature& e, NpcMind& M, ivec3 s, int to, float dt, bool* gaveUp = nullptr);
// the floor (a lift's "at" of one of its stops) a body at height y is on, or -1: none near
int npcLiftFloorOf(const World& w, ivec3 s, float y);
// a lift in the box (a windmill, a tower): its slider
bool npcLiftIn(const World& w, int x0, int y0, int z0, int x1, int y1, int z1, ivec3& out);
// the working village (sim/villager_chores.cpp): farmers reap their farms and carry the wheat to the mill, the miller
// feeds the millstone and bags the flour, the baker fetches it and bakes bread. A move while they're about it, or none.
OptMove npcChore(Level& lv, Creature& e, NpcMind& M, const Village& v, const VFolk& f, float ph, float dt);
OptMove npcJobWork(Level& lv, Creature& e, NpcMind& M, const Village& v, const VFolk& f, int idx, float ph, float dt);   // sim/villager_jobs.cpp
json& villageStock(Level& lv, const Village& v);   // {"wheat": n, "flour": n, "bread": n, ...} kept with the village
bool villageMakes(const std::string& item);        // its shops sell only what the village has made of it
bool villageUses(const std::string& item);         // sold to its shopkeepers, it goes into the village's stores
OptMove lostAI(Level& lv, Creature& e, NpcMind& M, float dt);
OptMove travellerAI(Level& lv, Creature& e, NpcMind& M, Entity* p, float dist, float dt);
void npcOffend(Level& lv, Player& who, const std::string& vid, Creature& victim, bool killed);
void npcForgive(Level& lv, Player& who, const std::string& vid);
void npcMutter(Level& lv, Creature& e, const json& lines);
void npcSay(Level& lv, Creature& e, const std::string& line, float vol = 0.5f);   // the babble (one voice per line)
float turnTo(float a, float b, float k);
void npcShutDoors(Level& lv, Creature& e, NpcMind& M, bool all = false);
void npcStuckCheck(Level& lv, Creature& e, NpcMind& M);
// free time together (sim/villager_social.cpp): talks of two to four, what they think of each other, clans, couples
OptMove npcSocial(Level& lv, Creature& e, NpcMind& M, const Village& v, int idx, float dt, bool seated = false);
bool npcInTalk(u32 id);
int npcRelation(Level& lv, const std::string& vid, int a, int b);                  // -100 .. 100 (0: strangers)
void npcRelationAdd(Level& lv, const std::string& vid, int a, int b, int d);
const char* npcRelationWord(int r);                                                 // "friends", "rivals"...
struct NpcClan { std::string name; std::vector<int> members; };
std::vector<NpcClan> villageClans(Level& lv, const Village& v, const std::vector<int>& people);
int npcCouple(Level& lv, const std::string& vid, int i);                            // their partner's place (-1 none): sim/villager_family.cpp
json& villageKids(Level& lv, const std::string& vid);                               // those born in the village
const json* npcKid(Level& lv, const std::string& vid, int i);                       // a child's record (500 + n)
bool npcIsChild(Level& lv, const std::string& vid, int i);
std::vector<int> npcParents(Level& lv, const std::string& vid, int i);
std::vector<int> npcChildren(Level& lv, const std::string& vid, int i);
std::vector<int> villagePeople(Level& lv, const Village& v);
bool npcHome(Level& lv, const Village& v, int i, ivec3& at, int& f, bool& second);   // second: sharing it (sleeps beside)
std::string npcPersonName(Level& lv, const Village& v, int i);
void familyDay(Level& lv, const Village& v);
OptMove childDay(Level& lv, Creature& e, NpcMind& M, const Village& v, int idx, double ph, float dt);
u32 npcSeedOf(const Village& v, int i);

// villager_quests.cpp
void questHudChanged(Level& lv, Player& p);
bool riftAskAvailable(Level& lv);

// villager_life.cpp
void gatesTick(Level& lv, VillagerSystem& S, double dt);
bool gateSet(Level& lv, const VGate& G, bool open, bool sound);   // open or shut a gate (barriers, or a new town's moving gate)
void raidTick(Level& lv, VillagerSystem& S, double dt);
void eliteTick(Level& lv, VillagerSystem& S, double dt);
void lostTick(Level& lv, VillagerSystem& S, double dt);
void travellersTick(Level& lv, VillagerSystem& S, double dt);   // merchants on the roads between villages
void homeTick(Level& lv, VillagerSystem& S, double dt);
void playerLifeTick(Level& lv, VillagerSystem& S, Player& p, double dt);   // welcome, rift calls, path told, lapses
void eliteSlain(Level& lv, Creature& m);
bool homeAsk(Level& lv, Player& p, const Village& v, std::string& say);   // the elder's offer (hearth.js)
const json* homeOf(Player& p, const std::string& vid);
std::vector<std::pair<std::string, std::string>> houseUpOffers(Level& lv, Player& p, const Village& v);   // the elder's house improvements: [label, key]
bool houseUpBuy(Level& lv, Player& p, const Village& v, const std::string& up, std::string& sayText);
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
// money: villagers pay and charge gold coins; an emerald still pays, worth EMERALD_COINS (change comes back in coins)
constexpr const char* COIN = "gold_coin";
constexpr int EMERALD_COINS = 4;
int purseOf(Player& p);                                              // coins + emeralds at their worth
bool payCoins(Level& lv, Player& p, int price);                      // false (nothing taken) when the purse is short

} // namespace dq
