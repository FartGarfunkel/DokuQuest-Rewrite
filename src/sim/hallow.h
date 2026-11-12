// sim/hallow.h — the October creatures (sim/hallowmobs.cpp): the pumpkin creature, the scarecrow and the ghost.
// They appear only while halloween() (common/holiday.h) is on, and only on October nights.
#pragma once
#include "sim/creature.h"
#include "sim/physics.h"

namespace dq {

// A creature's small Halloween state (Creature::data["hw"], saved and sent to guests): pumpkin "mode"/"w"/"up"/"tell", ghost "fa" ...
float hwGet(const Creature& m, const char* key, float def = 0);
void registerHalloweenMobs();   // the types + models + rig animation + the spawner; first thing in registerCreatures()
// A move that drifts through leaves (a ghost): like moveBox, but leaf blocks don't stop it.
MoveResult phaseMove(const World& w, vec3& pos, float hw, float h, vec3 delta);

} // namespace dq
