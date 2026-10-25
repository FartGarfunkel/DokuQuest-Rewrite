// sim/endstory.h — the End and its fight, dragons, the Deep Dark's guardian, the players' story (the rift) and the
// watcher. Ports of game/theend.js + endmobs.js (sim/end_fight.cpp), dragons.js (sim/dragons.cpp), deepdark.js +
// deephalls.js gameplay (sim/deepdark.cpp), haunt.js (sim/haunt.cpp), riftarc.js + riftlore.js (sim/rift.cpp).
// One LevelSystem (EndStory) runs them on the host / single player; a remote client's replica skips everything.
//
// Saved state lives where the original kept it (g.meta → Level::info.data):
//   end     {slain, gates, crystalsGone[], dragonHp, met, again, exitY, farGates[]}
//   dragons {<lairId>: {slain}}          eggs [{x, y, z, realm, t, owner, coldSaid}]
//   deepWarn {lvl, t}                    structs {<structureId>: {...}} (deep halls / twin vault records)
//   rift    {stage, frags, village, vname, at[3], yaw, opened, sealed, halls{}, heroHp}
//   flags   watcherSeen, watcherSpoke, watcherTraces, enderDragon
// Player stats: endSeen, dragonsSlain, realmSeen, riftClues{}, riftCluesN, riftCalls{}.
// Player data (read by the HUD layer / controller): darkT (the Deep Dark's darkness), dreadT (the rift's dread),
// frozenT (> 0: time stands still — the controller should ignore movement input).
#pragma once
#include "sim/level.h"

namespace dq {

class Player;
class Creature;

// ---- the story's state (riftarc.js riftOf / riftStability, riftlore.js riftTier) ----
json& riftRec(Level& lv);
int riftStability(Level& lv);          // 100 (quiet) .. 5 (paper thin); the Overworld's sky goes wrong under 15
int riftTier(Level& lv);               // -1 nothing yet · 0 watched · 1 opened · 2 thinning · 3 paper-thin · 4 gateway · 5 sealed
constexpr int RIFT_NEED = 4;
// Opens the rift in a village square (the catalyst; /rift open). at = global position of the tear's foot.
void riftOpenAt(Level& lv, const std::string& villageId, const std::string& villageName, vec3 at, float yaw = 0);
void riftOpenGate(Level& lv);           // the four fragments given: the tear steadies into a gateway (stage 3)
void riftTravel(Level& lv, Player& p, bool toRealm);   // through the gateway / back out of his realm

// ---- villagers (riftlore.js): what the folk make of it. Pure data + the asker's stats; the villagers workstream
// calls these from its talk screen (no UI here). ----
struct RiftLine { std::string text, toast, sound; };          // toast/sound: what the asker gets (may be empty)
RiftLine riftAsk(Level& lv, Player& asker, const std::string& job, const std::vector<std::string>& jobsHere);
std::string riftAskLabel(Level& lv);                            // the button: "Seen anything strange lately?" … ("" = none)
std::string riftGreeting(Level& lv);                            // a greeting that carries it ("" = the usual one)
std::string riftNews(Level& lv);                                // what passes for news ("" = the usual news)
std::string riftElderLine(Level& lv);                           // the elder's "The rift in the square…" answer ("" = none)
std::string riftElderLabel(Level& lv);
std::string riftCorruptSay(Level& lv, const std::string& job);  // a villager's words breaking into his ("" = none)
// The villagers emit this when someone first walks into a village (riftlore.js RIFT_CALL): EventBus
//   villageEnter [playerName, villageId, nearestFolkName]   → the call toast (once per village and tier)

// ---- the End (theend.js / endmobs.js) ----
json& endRec(Level& lv);
vec3 endGatewaySpot(int k);              // the k-th gateway round the main island (global y)
bool chorusTeleport(Level& lv, Player& p);

// ---- dragons (dragons.js) ----
// Hit volumes along a boss dragon's body (0.9.0.76 dragonBoxes): the player's attack ray can use these.
bool isDragon(const Creature& c);
float dragonRayT(const Creature& c, vec3 o, vec3 d);   // distance along the ray, < 0 = missed
bool dragonHas(const Creature& c, vec3 p);

// ---- the Deep Dark (deepdark.js) ----
// A sound the sculk can hear: kind "step" | "land" | "block" (others call it for noises they make).
void vibrate(Level& lv, int realm, vec3 at, u32 sourceId, const std::string& kind);

// ---- the watcher (haunt.js) ----
bool hauntDimOk(Level& lv, const Player& p);
bool hauntForceSighting(Level& lv, Player& p);   // /herobrine
bool hauntLeaveTrace(Level& lv, Player& p);      // /herobrine trace

void registerEndStory();   // entity factories, creature hooks, rig animations, commands, the system (initCore)

} // namespace dq
