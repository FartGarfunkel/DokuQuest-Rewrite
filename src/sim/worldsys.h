// sim/worldsys.h — the world's own systems (port of world/world.js fluids + random ticks, the
// growth parts of game/blocks2.js, Game.tickFurnaces/tickFurnace + itemfix.js, game/crowd.js).
//   FluidSystem (WorldSystem): water/lava flow with the original levels and rates (water max 7 every
//     5 ticks of 1/20 s, lava max 3 every 28 — max 6 every 10 in the Ember Depths), sources from two
//     neighbours, waterlogged blocks as still sources, water+lava -> obsidian/cobblestone/stone,
//     plants washed away (with their drops), newly loaded chunks waking their liquids.
//   BlockRules (WorldSystem): Game.onBlockChanged reactions (sim/blockfx).
//   randomTicks: once a second around each player — saplings, crops, cane/cactus/kelp/bamboo, vines,
//     berry bushes, fire burning out, copper weathering, amethyst, hives, grass spreading/dying.
//   tickFurnaces: furnaces/smokers/blast furnaces smelt in loaded chunks (block entity JSON in the
//     original's format: {type:'furnace', items:[input, fuel, output], burn, burnMax, cook}).
#pragma once
#include "sim/level.h"

namespace dq {

constexpr float SMELT_TIME = 5;   // seconds per item
void attachWorldSystems(Level& lv, World& w);
void randomTicks(Level& lv, World& w, float px, float pz);
bool growTree(Level& lv, World& w, int x, int y, int z, double r);
void tickFurnaces(Level& lv, double dt);
bool tickFurnace(json& e, double dt, const std::string& kind);   // true when something visible changed
float fuelValue(const std::string& itemKey);
std::string smeltResult(const std::string& input, const std::string& kind);   // "" if it doesn't smelt there
void crowdPush(Level& lv, float dt);
int fluidPending(World& w);   // queued fluid updates (tests/debug)

} // namespace dq
