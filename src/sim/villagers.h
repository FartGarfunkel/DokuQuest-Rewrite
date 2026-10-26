// sim/villagers.h — the people of the villages and towns (game/villagers.js, towns3.js, townlife.js people parts,
// livingworld.js, hearth.js homes + the missing, paths.js path choice + Knight/Ranger talents, riftlore.js, the quest
// board and together.js shared jobs). Runs where the world is simulated (no GL); the client draws villagers as voxel
// people (render/villager_layer) and opens the screens (ui/villager_screens).
//
// Where villages come from: the structures index (gen/structures.h, type "village"; StructurePlan::data in the
// original plan's shape, y band-local) — converted once into `Village` (y global). Tests and dev tools can add a
// village by hand (addVillage).
//
// Villagers are Creatures (types villager / village_guard / iron_golem) with Entity::data["npc"] =
// {v: village id, i: index (folk 0.., guards 100.., golems 200.., the missing 300), n: name, j: job, s: look seed,
// p: pose bits (NPC_P), sh: what they hold when seated}. They aren't saved: they come when someone comes near the
// village and go when everyone has left. What lasts is in LevelInfo::data (villages: who died, pens filled; raids,
// elites, lost, homeBuilt, homeSites) and in Player::stats (vrep, quests, qdone, vstock, vcap, vseen, homes,
// riftClues, riftCalls, path, pathTold, pathPts, questsDone).
#pragma once
#include "sim/creature.h"
#include "sim/player.h"
#include <memory>

namespace dq {

// ---- a village (villages.js / towns.js plan) ----
struct VFolk { ivec3 home{0}; int homeF = 0; std::string job = "none"; bool hasWork = false; ivec3 work{0}; int workF = 0; };
struct VSeatSpot { ivec3 at{0}; int f = 0; bool walk = false; int ax = 0, az = 0; std::string held; };
struct VRoad { int d = 0, L = 0, hw = 0; std::vector<ivec3> cells; };   // cells: x, global y of the ground, z (may be empty)
struct VGate {
  std::string key, name; int d = 0; bool stone = false;
  std::vector<ivec3> cells; vec3 center{0}; int h0 = 0, inX = 0, inZ = 0, hw = 0, span = 0, cx = 0, cz = 0;
  vec3 posts[2]{}; ivec3 lever{0};
};
struct VBld { std::string type, job, animals; int r = 0, ox = 0, oz = 0, W = 0, D = 0, H = 0, y = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0; bool hasMid = false; ivec3 mid{0}, door{0}; };
struct Village {
  std::string id, name, style, tier;   // tier: village | town | city
  int rx = 0, rz = 0;                  // the region (id "t<rx>_<rz>")
  int x = 0, y = 0, z = 0;             // the square (y = its ground, global)
  int P = 7, hw = 1, fr = 0, r = 24, guards = 0, golems = 0;
  u32 seed = 0;
  ivec3 board{0}, bell{0};
  std::vector<VFolk> folk;
  std::vector<VSeatSpot> seats;
  std::vector<VRoad> roads;
  std::vector<VGate> gates;
  std::vector<VBld> blds;
  bool hasWall = false; std::string wallKind; int wx0 = 0, wz0 = 0, wx1 = 0, wz1 = 0, wallH = 0;
  bool tierN() const { return tier != "village"; }   // the original's truthy v.tier (towns and cities)
};
using VillageRef = std::shared_ptr<const Village>;
Village parseVillage(const json& planData);         // StructurePlan::data (kind "village", y band-local)
void addVillage(Level& lv, const json& planData);   // a village by hand (tests, dev): band-local y like the plans
std::vector<VillageRef> villagesNear(Level& lv, float x, float z, float pad);   // the Overworld's villages within pad
VillageRef villageById(Level& lv, const std::string& id);
VillageRef villageAtPos(Level& lv, int realm, vec3 p, float pad);              // only in the Overworld band
VillageRef villageNeighbour(Level& lv, const Village& v, float* dist = nullptr); // the nearest other village (news, word quests)

// ---- who they are (data/villagers.json, extracted from the original) ----
const json& VD();                                     // NPC_JOBS, NPC_SHOPS, NPC_ERRANDS, NPC_TALK, RIFT_LORE ...
std::string npcName(u32 seed);
std::string jobTitle(const std::string& job);
u32 npcSeedOf(const Village& v, int i);
struct NpcVoice { float pitch = 200, formant = 1; bool triangle = false; int seed = 0; };
NpcVoice npcVoice(u32 seed);
// their skins (villagers.js npcSkinCanvas, the same pixels): 64x64 RGBA; the face for the conversation window is (8,8)-(16,16) + the hat layer (40,8)
void npcSkinPixels(u32 seed, const std::string& job, std::vector<u8>& px);
enum NpcPose : int { NP_SLEEP = 1, NP_SIT = 2, NP_WAVE = 4, NP_TALK = 8, NP_WORK = 16, NP_READ = 32, NP_FOLD = 64, NP_HOE = 128 };
bool isVillager(const Entity& e);                     // a creature with data.npc (golems included)
const json* npcOf(const Entity& e);                   // data["npc"] or null
double npcPhase(const Level& lv);                     // time of day 0..1 (the original's meta.time / DAY_LEN)
bool npcNight(const Level& lv);
i64 npcDay(const Level& lv);

// ---- standing (each player their own) ----
struct Standing { int k = 0; std::string name; int tier = 0; bool hasNext = false; double next = 0; };
Standing standingOf(double rep);
json& npcStats(Player& p);                            // p.stats with vrep / quests / vstock / qdone made
double repOf(Player& p, const std::string& vid);
void npcRep(Level& lv, Player& p, const std::string& vid, double n, const std::string& why = "");

// ---- quests (villagers.js + livingworld.js elites, hearth.js the missing, dungeons.js, rpg.js scout/word) ----
json npcQuestFor(Level& lv, const Village& v, int i);   // today's errand from one villager (null = none)
json boardQuests(Level& lv, const Village& v);          // today's notes on the board
int questHave(Level& lv, Player& p, json& q);
bool questReady(Level& lv, Player& p, json& q);
bool questTake(Level& lv, Player& p, const json& q, bool shared = false);
bool questFinish(Level& lv, Player& p, const std::string& qid);
void questDrop(Level& lv, Player& p, const std::string& qid);
void questKill(Level& lv, Player& p, const std::string& type, bool party = false);
void questLapse(Level& lv, Player& p);
json* findQuest(Player& p, const std::string& qid);
json boardJob(Level& lv, const std::string& id, const std::string& kind);   // the host's copy of a board job (elite / lost)

// ---- shops ----
struct ShopLine { int i = 0; std::string id, key; int n = 0, price = 0, tier = 0; bool locked = false; int left = 0; bool can = false; };
std::vector<ShopLine> shopLines(Level& lv, Player& p, const Village& v, int npcIndex, const std::string& job, const std::string& tab);
// returns what the shopkeeper says ("" = it went through), and whether it did
bool shopTrade(Level& lv, Player& p, const Village& v, int npcIndex, const std::string& job, const std::string& tab, int line, std::string& say);
bool hasShop(const std::string& job);

// ---- the conversation (villagers.js NpcTalk + its additions: hearth.js, paths.js, riftlore.js, riftarc.js, rpg.js)
// The dialogue is worked out here; the screen shows `text` (typed out) and the options, and calls choose().
struct TalkOpt { std::string label, icon, act; json arg; };   // icon "" = its number
struct Talk {
  u32 npc = 0; std::string vid, job, name, title; int idx = 0; u32 seed = 0;
  std::string text, mode = "home";      // mode: home | shop | confront
  std::vector<TalkOpt> opts;
  std::string tab = "buy";             // the shop's page
  bool closeNow = false;               // the screen should close
  std::string openScreen;              // "path" (the path window), "" = none
  std::string pathHint;
  bool greetLine = false;
  int voiceLines = 0;                  // bumps each time a line is said (the text types out again)
  std::string spoken; float spokenVol = 0.5f; int spokenN = 0;   // what the villager says aloud, and how loud: the screen babbles it (spokenN bumps each time)
};
bool talkOpen(Level& lv, Player& p, Creature& npc, Talk& t, bool confront = false, const std::string& victim = "");
void talkChoose(Level& lv, Player& p, Talk& t, int option);
void talkShopTrade(Level& lv, Player& p, Talk& t, int line);   // in shop mode
void talkShopBack(Level& lv, Player& p, Talk& t);
void talkHold(Level& lv, Player& p, Talk& t);                   // each frame: they keep standing and facing you
bool talkStillOk(Level& lv, Player& p, const Talk& t);           // false = they're gone / too far: close it
void talkClosed(Level& lv, Player& p, Talk& t);

// ---- paths.js: Knight, Ranger, Mage chosen at a village guard, fletcher, librarian or elder ----
int combatLevel(const Player& p);
int pathPoints(const Player& p);
int pathSpent(const Player& p);
bool pathPick(Level& lv, Player& p, const std::string& k);     // walk a path (combat level 20)
bool pathLearn(Level& lv, Player& p, const std::string& talent);
bool pathForget(Level& lv, Player& p);                          // 20 emeralds
int pathMaxHealthBonus(const Player& p);                        // the Knight's two hearts (+4): Player::recalcMax adds it
// the Knight's and Ranger's talents for the combat code (Mage ones are in sim/magic.cpp)
float pathMeleeFactor(const Player& p);                         // Heavy Blows
float pathArrowFactor(const Player& p, float distance);         // Ranger base, Eagle Eye, Deadeye
bool pathArrowPoisons(const Player& p);                         // Poisoned Tips (4 s on what isn't undead)
bool pathQuiverSaves(const Player& p);                          // Quiver Master: this arrow isn't used up (30 %)
float pathSpeedFactor(const Player& p);                         // Fleet Foot
bool pathCheatDeath(Level& lv, Player& p);                      // Unbreakable: true = stood on half a heart instead
void pathCleave(Level& lv, Player& p, Creature& hit, float dealt);   // Cleaving Arc (call after a weapon blow)
float pathPetFactor(Level& lv, const Creature& victim, const Entity* attacker);   // Beast Friend (both ways)

// ---- the quest board block, gates, levers (itemUse) ----
VillageRef boardVillage(Level& lv, int realm, ivec3 at);         // null: "this board belongs to no village"
bool gateUse(Level& lv, Player& p, ivec3 at, bool lever);       // a gate or its lever (towns3.js)
bool gateShutAt(Level& lv, const VGate& g);

// ---- hearth.js: a house for a village's Hero (the builders) ----
// The house itself is raised by a hook (the structures workstream's village house template, VTEMPLATES.house_m);
// the default builds a plain house of the village's planks with a bed and a chest of gifts. Returns the bed and door.
struct HeroHouse { bool ok = false; ivec3 bed{0}, door{0}; std::string name; };
using HouseBuilder = std::function<HeroHouse(Level& lv, const Village& v, int r, int ox, int oz, int y, const json& gifts)>;
void setHouseBuilder(HouseBuilder f);
bool isHero(Player& p, const std::string& vid);

// ---- the missing (hearth.js) and named foes (livingworld.js) ----
bool lostTake(Level& lv, Creature& e, Player& p, std::string* said = nullptr);

// ---- events the client listens to ----
// "villagerScreen" [playerName, "talk"|"board"|"path", {npc, confront, victim} | {vid}]
// "questHud" [playerName] when the list of jobs changed; "villagerScreen" [name, "close"] closes the talk window
void openVillagerScreen(Level& lv, const Player& p, const std::string& id, const json& args);

// dev: villagers in front of a player without a village (DQ_VILLAGERS=farmer,guard,... / the /villagers command)
std::vector<Creature*> spawnDevVillagers(Level& lv, Player& p, const std::vector<std::string>& jobs);

void registerVillagers();   // called once from initCore(): the VillagerSystem, creature hooks, chat commands

} // namespace dq
