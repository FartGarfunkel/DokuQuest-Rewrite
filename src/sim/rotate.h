// sim/rotate.h — a block's facing turned with it: a quarter turn about the upright (the /rotate command, a hinge facing
// up or down) or about a sideways axis (a hinge facing sideways: drawbridges, hatches).
#pragma once
#include "core/registry.h"

namespace dq {

// clockwise seen from above, `quarters` times (any count; negative turns the other way)
u8 rotateMetaY(BlockId id, u8 meta, int quarters);
// about a sideways axis (1: east-west, 2: north-south), `quarters` times: a positive quarter about east-west takes up to
// south, about north-south takes up to east. Logs and rods swap their axis; six-way facings turn; the rest keep theirs.
u8 rotateMetaAbout(BlockId id, u8 meta, int axis, int quarters);
// a chiseled block's cut corners (bit (x<<2)|(y<<1)|z) turned the same ways
u8 rotateCarveY(u8 carve, int quarters);
u8 rotateCarveAbout(u8 carve, int axis, int quarters);

} // namespace dq
