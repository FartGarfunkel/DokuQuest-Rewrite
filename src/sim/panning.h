// sim/panning.h — gold panning and the sluice (after TerraFirmaCraft): a fired clay pan used on gravel or sand with water
// beside it — two seconds of swirling — now and then gives an ore sample (copper, tin, gold, silver, zinc, bismuth, iron) or,
// rarely, a gem; one try in four uses up the block. A sluice set in the water beside gravel or sand does it on its own, a
// find a minute, until it's worn out (64 finds).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"

namespace dq {

class Player;
constexpr float PAN_HOLD = 2.0f, SLUICE_EVERY = 60;
constexpr int SLUICE_LIFE = 64;
ItemStack panFind(float r, float r2);                      // the table: r < 0.30 a sample, < 0.33 a gem, else nothing
void setPanRoll(float r);                                  // tests: the next rolls are r (< 0: chance again)
bool panUse(Level& lv, Player& p, const RayHit& hit, bool repeat, float step = 0.22f);
ItemStack sluiceTake(World& w, int x, int y, int z);
void sluiceTick(Level& lv, double dt);

} // namespace dq
