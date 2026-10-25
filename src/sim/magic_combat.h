// sim/magic_combat.h — the thin adapter between magic / enchanting / brewing / skills and the game's combat.
// The gameplay engineer's sim/combat.h (damage, knockback, death, xp) is not in this tree yet: every hurt,
// heal, knock, status and experience grant of the magic code goes through these few functions, so at merge
// only this file's .cpp needs re-pointing. `src` is the original's damage source object as JSON:
//   { type: "magic"|"burn"|"poison"|..., element: "fire"|"frost"|"lightning"|"death"|"nature", knock: n,
//     attacker: <entity id>, attackerName, bypass, bypassArmor, weapon, tool }
#pragma once
#include "sim/level.h"
#include "sim/player.h"

namespace dq {

// ---- what a creature is (MOB_TYPES flags) ----
const json& mobTemplate(const Entity& e);          // reg().mobs[type] (null json if none)
bool isMob(const Entity& e);                       // a creature (not a player, item, bolt...)
bool isAlive(const Entity& e);
bool isFoe(const Entity& e);                       // spellcraft.js hostile(): alive, not a friendly NPC, not owned/tamed
bool isSpellTarget(const Entity& e);               // magic.js: any live creature but a friendly NPC
bool isUndead(const Entity& e);
bool isArthropod(const Entity& e);
bool isFireImmune(const Entity& e);

// ---- damage / healing ----
// Hurt any entity. Players go through magicIncomingDamage (ward, stoneskin, potions, enchantments, Defence)
// then lose health; creatures lose health (dying = removed + "mobDeath"). A ghost (net replica) is hit through
// lv.net->hitEntity on behalf of `by`. Returns the damage dealt here (0 if none or sent to the server).
float combatHurt(Level& lv, Entity& target, float amount, const json& src, Player* by = nullptr);
float combatHeal(Level& lv, Entity& target, float amount);      // returns the health gained
void combatKnock(Level& lv, Entity& target, vec3 from, float strength);

// ---- creature statuses (kept in Entity::data: burnT, slowT, poisonT, rootT) ----
void setMobStatus(Level& lv, Entity& e, const char* key, float secs, Player* by = nullptr);   // max(old, secs)
float mobStatus(const Entity& e, const char* key);

// ---- experience (game/player.js) ----
inline int xpForLevel(int level) { return 12 + level * 6; }
int addXP(Player& p, int n);                                     // returns levels gained
void grantXP(Level& lv, Player& p, int n, const vec3* at = nullptr);   // with Insight and Mending (enchanting.js)
void spendLevels(Player& p, int n);

// ---- other things the spells ask of the world ----
Player* playerNamed(Level& lv, const std::string& name);
Entity* spawnCreature(Level& lv, const std::string& type, int realm, vec3 pos);   // via createEntity (creatures' factory)
void dropStack(Level& lv, int realm, ItemStack s, vec3 pos);   // a dropped item entity ("item", data.stack)
void giveOrDrop(Level& lv, Player& p, ItemStack s);

} // namespace dq
