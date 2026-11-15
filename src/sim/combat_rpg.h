// sim/combat_rpg.h — stamina, dodge, block/parry, posture, the six stats, mob levels, second chance, boss cap.
// The bars, roll and the lock-on marker are drawn elsewhere. These fields are what that code should read.
#pragma once
#include "sim/combat.h"

namespace dq {

class Level;
class Entity;
class Player;
struct PlayerInput;

// Player: stamina, maxStamina, dodgeT, dodgeYaw, blockT, parryFlash, guardFlash,
//         statCon, statStr, statAgi, statInt, statLck, statFoc, statPoints, secondChanceCd
// Entity: posture, maxPosture, staggerT, mobLevel
// A boss's per-second damage window stays in entity data (bossDmg, bossDmgT). It is not a HUD field.

void combatRpgTick(Level& lv, Player& p, float dt);
void combatRpgInput(Level& lv, Player& p, const PlayerInput& in, float dt);
float combatRpgIncoming(Level& lv, Player& p, float amount, DamageSource& src);
bool combatRpgSecondChance(Level& lv, Player& p);
float combatRpgOutgoing(Level& lv, Player& p, Entity& e, float dmg, bool& finishing);
float combatRpgBossCap(Level& lv, Entity& e, float dmg);
float combatRpgMobScale(const Entity& attacker, float amount);
int playerCombatLevel(const Player& p);         // the level creatures measure you by (all your skills' levels / 8)
void mobLevelUpdate(Level& lv, Entity& e);      // its level once (from the party near it), its health with the party about it
float mobXpScale(const Entity& e);              // experience for killing it: more for higher levels, double for an elite
// an arrow's step from->to. true: it struck a shield and this tick is finished (absorbed, or sent back).
bool arrowStrikeShield(Level& lv, Entity& arrow, const vec3& from, const vec3& to);

} // namespace dq
