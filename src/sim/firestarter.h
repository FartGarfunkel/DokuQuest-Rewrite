// sim/firestarter.h — TerraFirmaCraft's firestarter (two sticks): hold use on something for about a second; then a 30%
// chance (70% with straw lying there) to light it: an out firepit, three sticks lying on the ground (a new firepit), a full
// pit kiln (sim/pit_kiln.h), a log pile (a charcoal pit, sim/charcoal_pit.h). Each try wears it (8 tries).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"

namespace dq {

class Player;
constexpr float FIRESTARTER_HOLD = 1.0f;   // seconds of rubbing before the spark is tried
// called for every use (repeat = held down; the controller calls it every 0.22 s): true if it took the click
bool firestarterUse(Level& lv, Player& p, const RayHit* hit, bool repeat, float step = 0.22f);
// what lighting things can light besides firepits (sim/pit_kiln, sim/charcoal_pit register themselves)
using LightFn = bool (*)(Level& lv, World& w, int x, int y, int z);   // true: it was its thing and it's lit
void registerLightable(LightFn f);
void setFirestarterRoll(float r);          // tests: the next tries roll r (0..1); < 0 back to chance

} // namespace dq
