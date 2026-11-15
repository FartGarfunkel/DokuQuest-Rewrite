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
// Fire striking the world at `at` (a firebolt, a fireball, lightning, a flaming arrow), travelling along dir: TNT there
// is lit, a campfire / candle / brazier lights, else a fire takes on the open side of what it struck (on its top, or
// against its side). Only where the world is simulated, never in water. false: nothing caught
class Player;
bool igniteAt(Level& lv, int realm, vec3 at, vec3 dir, Player* by = nullptr);   // (a guest's: asked of the host, by)
// Blow a round hole, knock everyone back, set off nearby TNT. noBlocks: hurt only (mobGriefing off).
void explode(Level& lv, int realm, vec3 at, float power, bool noBlocks = false, u32 source = 0);
// Everyone in bed: skip to morning (called every tick by the gameplay system).
void tickSleep(Level& lv, double dt);
// Put the sleeper back beside the bed (the spot they clicked from) when they get up.
class Player;
void riseFromBed(Player& p);
// Game.onBlockChanged: supports, hanging things, attached torches/ladders/signs, falling, hardening.
void onBlockChangedRules(Level& lv, World& w, int x, int y, int z, BlockId oldId, BlockId newId);
// After a chisel changes a block's octants: things on a face that is no longer whole drop off.
void onCarveChangedRules(Level& lv, World& w, int x, int y, int z);

void registerBlockFxEntities();

// a block falling (sim/cavein.cpp: rubble from a roof): at = the bottom middle of where it starts
void spawnFallingBlock(Level& lv, int realm, vec3 at, const std::string& key, u8 meta = 0);

} // namespace dq
