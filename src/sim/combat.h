// sim/combat.h — damage, knockback, armour, hurt time, death and respawn (port of the hurt/die/
// respawn parts of game/local.js + game/player.js, Mob.hurt/die from game/entities.js, and the
// fairplay.js rule that armour never stops more than four fifths of a blow).
//
// Creatures (a later workstream) use this API:
//   damageEntity(lv, target, amount, src)     — any entity; players go through damagePlayer
//   registerHurtHandler("zombie", fn)        — own hurt logic for a type ("prefix*" or "" = all mobs);
//                                              the default (Mob.hurt) knocks back, sets hurtTime and
//                                              calls mobDeath() + removes the entity at 0 hp
//   mobDeath(lv, mob, src)                    — drops (MOB_TYPES drops), XP to the killer, mobDeath event
//   knockback(e, from, k, up)
#pragma once
#include "sim/player.h"
#include "sim/level.h"

namespace dq {

struct DamageSource {
  std::string type;          // "", fall, drown, lava, burn, void, cactus, explosion, arrow, projectile, magic...
  std::string element;       // fire | frost
  std::string tool, weapon;  // tool type and item id of the blow
  u32 attacker = 0;          // entity id of whoever struck (0 = none)
  float knock = -1;          // knockback; -1 = default (5), 0 = none
  bool bypass = false;       // ignores the invulnerable moment after a hit
  bool bypassArmor = false;
  float raw = 0;             // the blow before anything softened it (set by damagePlayer)
  bool hasRay = false; vec3 rayO{0}, rayD{0};   // where the blow came from (creatures: which limb it lands on)
};

// LocalPlayer.hurt + Player.hurt: rules (fall/drown/fire damage), creative, invulnerability,
// armour (3.2% a point, at most 80%; at least a fifth of the raw blow), fire resistance, armour
// wear, knockback, catching fire, death. Returns the damage dealt (half hearts).
float damagePlayer(Level& lv, Player& p, float amount, DamageSource src);
float damageEntity(Level& lv, Entity& e, float amount, const DamageSource& src);
using HurtHandler = std::function<float(Level&, Entity&, float, const DamageSource&)>;
void registerHurtHandler(const std::string& typeOrPrefix, HurtHandler h);
void mobDeath(Level& lv, Entity& mob, const DamageSource& src);
void knockback(Entity& e, vec3 from, float k, float up);

// player helpers
const ToolInfo* heldTool(const Player& p);           // the main-hand tool (null for a bare hand)
std::string heldToolElement(const Player& p);          // tool.element ("fire", "frost") or ""
bool wearHeld(Level& lv, Player& p, int n = 1);            // tool durability
void wearArmor(Player& p);                             // each worn piece takes one wear
float attackEntity(Level& lv, Player& p, Entity& target);   // a melee blow (crits when falling); returns damage dealt
void playerDie(Level& lv, Player& p, const DamageSource& src);
void respawnPlayer(Level& lv, Player& p);              // to the bed if it's still there, else the world spawn
void placeAtSpawn(Level& lv, Player& p, bool initial);
void dropCarried(Level& lv, Player& p);

void registerCombat();

} // namespace dq
