// sim/tactile.h — Tactile Blocks' way of working the stations, in our own first person (docs/TACTILE_PORT.md): no screen,
// what's held laid in the spot you look at, an empty hand takes it back, a long hold pours the whole stack in. The places
// and zones are Tactile's (its block entities and blocks, read from the mod); what they do is our own simulation (our
// furnace's three slots smelt as ever, so hoppers, saves and the classic screen are untouched).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"

namespace dq {
class Player;

// Tactile's facing of a block (0 south, 1 east, 2 north, 3 west, as tools/port/tactile_import.py orders them) from ours
int tactileFacing(World& w, ivec3 at);
float tactileFacingDegrees(int facing);   // Tactile's turn of its renderer about the block's middle (north 0, east -90...)
// a point in the block (0..1) turned back into Tactile's north-facing frame (x across its front, z into it), and back
vec3 tactileNorthLocal(int facing, vec3 inBlock);
vec3 tactileFromNorth(int facing, vec3 north);

// a furnace, smoker or blast furnace used where you look (Tactile's four, fumoir, haut_fourneau): the low part takes fuel,
// the high part what cooks (the smoker's and the blast furnace's door opened first: use the door), an empty hand takes back
// (what's done first, then what cooks; the fuel down low; the blast furnace's ingots from its mould). all: the whole stack.
// false: not one of them (or nothing to do there)
bool tactileFurnaceUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all);
bool tactileDoorOpen(World& w, ivec3 at);   // the smoker's and blast furnace's door (block entity "door")

} // namespace dq
