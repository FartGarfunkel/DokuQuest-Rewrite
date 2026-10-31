// sim/blockfx.h — blocks with a life of their own (port of game/blockfx.js + Game.onBlockChanged):
// sand/gravel/powder that fall ("falling_block" entities), concrete powder setting in water, TNT
// ("tnt" entities) and explosions, sleeping through the night, and what happens around a changed
// block (plants and torches losing their support, things hanging from it, the other half of doors).
#pragma once
#include "sim/level.h"

namespace dq {

void checkFall(Level& lv, World& w, int x, int y, int z);
void checkHarden(Level& lv, World& w, int x, int y, int z);
void primeTNT(Level& lv, World& w, int x, int y, int z, float fuse);
// Blow a round hole, knock everyone back, set off nearby TNT. noBlocks: hurt only (mobGriefing off).
void explode(Level& lv, int realm, vec3 at, float power, bool noBlocks = false, u32 source = 0);
// Everyone in bed: skip to morning (called every tick by the gameplay system).
void tickSleep(Level& lv, double dt);
// Game.onBlockChanged: supports, hanging things, attached torches/ladders/signs, falling, hardening.
void onBlockChangedRules(Level& lv, World& w, int x, int y, int z, BlockId oldId, BlockId newId);
// After a chisel changes a block's octants: things on a face that is no longer whole drop off.
void onCarveChangedRules(Level& lv, World& w, int x, int y, int z);

void registerBlockFxEntities();

} // namespace dq
