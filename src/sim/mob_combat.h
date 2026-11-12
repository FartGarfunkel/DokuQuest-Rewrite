// sim/mob_combat.h — the creatures' thin combat adapter. Every blow that creatures deal or take goes
// through here, so the gameplay workstream's sim/combat.h (armour, hurt time, death, the player's attack)
// can take over by re-pointing these few functions at merge. Until then: a simple implementation
// (creatures: Creature::hurt; players: half-second hurt time, no armour, effects from the attacker's onHit).
#pragma once
#include "sim/creature.h"

namespace dq {

// Hurt any entity. Returns the damage dealt (0 = immune / invulnerable / not hurtable).
// cause: "mob", "projectile", "arrow", "explosion", "lava", "burn", "drown", "poison", "magic"...
float damageEntity(Level& lv, Entity& target, float amount, Entity* attacker, const std::string& cause, const DamageSrc& extra = {});
inline float damageEntity(Level& lv, Entity& target, float amount, Entity* attacker, const DamageSrc& src) { return damageEntity(lv, target, amount, attacker, src.type, src); }
// Push an entity (the original pawn.knock(vx, vy, vz)): adds horizontal velocity, lifts it.
void knockback(Entity& e, vec3 v);
// Set something alight for t seconds (players: data.fx.burn; creatures: burn).
void igniteEntity(Entity& e, float t);
// Status effects on players (monsters.js): poison, slow, chill, burn — kept in player.data["fx"].
float playerEffect(const Entity& p, const char* name);
void setPlayerEffect(Entity& p, const char* name, float t);
// Ticks the effects (poison never takes the last half heart, chill keeps you slowed, burn hurts). Called
// by the creature system for every player each tick.
void tickPlayerEffects(Level& lv, Player& p, float dt);
// XP for a kill (game: grantXP): adds to player.xp and emits nothing else.
void grantXP(Level& lv, Entity& who, int xp, vec3 at);

} // namespace dq
