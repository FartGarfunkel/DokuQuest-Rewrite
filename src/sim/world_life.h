// sim/world_life.h — small ways the world lives on its own (all host-side; guests see the results through the normal
// block and creature sync):
//  - lightning changes creatures: a bolt by a fuseling charges it (a crackling aura, render/creature_layer.cpp; its blast
//    twice as big), a pig becomes a zombie pigman and a villager a witch (name and place kept);
//  - the enderman now and then lifts a natural block ("carriable" in data/blocks.json, tools/port/world_life.py) it stands
//    by, carries it in its arms (data "carry"/"carryMeta") and later sets it down somewhere it fits; it drops it if killed.
//    Never a block a player put down (sim/skills.h skillWasPlaced), and only while the "mobGriefing" rule is on;
//  - small mushrooms spread slowly in the dark (at most a few of one kind in a 9x3x9 area) and pop off in bright light
//    unless on mycelium or podzol; bone meal on one grows a huge mushroom (gen/huge_mushroom.h);
//  - the Overworld's vine creeps along walls, up a little and hangs further down (a cap per area); bone meal lengthens it.
#pragma once
#include "sim/level.h"

namespace dq {

class Creature;

// ---- lightning ----
void lightningChangesCreatures(Level& lv, int realm, int x, int y, int z);   // a bolt came down at (x, y, z)
bool isFuseling(const std::string& type);
bool fuselingCharged(const Entity& e);
float chargedBlastScale(const Entity& e);   // 2 for a charged fuseling, else 1

// ---- the enderman's carried block ----
bool blockCarriable(BlockId id);
bool blockNatural(BlockId id);                         // data "natural" (tools/port/world_life.py), carriable, or air
bool nearBuilt(World& w, int x, int y, int z);          // anything not natural within two blocks
void enderCarryTick(Level& lv, Creature& m, float dt);   // from its brain while it isn't angry
bool enderTryLift(Level& lv, Creature& m);               // one try at lifting a block near it (true = it took one)
bool enderTrySetDown(Level& lv, Creature& m);            // one try at putting its block down nearby
void enderDropCarried(Level& lv, Creature& m);           // killed: what it held falls as an item
std::string carriedBlock(const Entity& e);               // "" = empty-handed

// ---- mushrooms and vines (random ticks, sim/worldsys.cpp; `force` skips the odds, for tests) ----
bool lifeRandomTick(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta);   // true = it was one of ours
void mushroomTick(Level& lv, World& w, int x, int y, int z, BlockId id, bool force = false);
void vineTick(Level& lv, World& w, int x, int y, int z, u8 meta, bool force = false);
int countNear(World& w, int x, int y, int z, BlockId id, int rxz, int ry);
// bone meal on a small mushroom (a huge one, with a chance, room and the right ground) or a vine (it grows down a few)
// true = the bone meal was used
bool lifeBonemeal(Level& lv, World& w, int x, int y, int z, BlockId id, bool force = false);
bool growHugeMushroom(World& w, int x, int y, int z, BlockId small);   // false = no room or the wrong ground

} // namespace dq
