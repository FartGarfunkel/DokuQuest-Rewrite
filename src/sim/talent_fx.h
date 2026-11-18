// sim/talent_fx.h — what the skill trees' notables and keystones do to a foe you strike (sim/balance.h TALENTS):
// bleeding, poison (the Assassin's venom on the undead too, or venom that kills), slowness, fire, and life drawn back.
// Called for a melee blow (sim/combat.cpp attackEntity) and an arrow (sim/controller.cpp), host side, once it landed.
#pragma once
#include "sim/level.h"
#include "sim/player.h"

namespace dq {

// `roll` < 0: each chance rolled on its own; 0..1: that roll for all of them (tests: 0 = every chance there comes up)
void talentOnHit(Level& lv, Player& p, Entity& foe, float dealt, double roll = -1);
// a creature's bleeding (Entity::data "bleedT"): ticks through armour while it lasts (sim/creature.cpp)
float bleedLeft(const Entity& e);

} // namespace dq
