// sim/farming.h — the kitchen garden (game/farming.js, 0.9.0.27) and the drops game/obtain.js added.
// The crops, produce, dishes and recipes are data (data/*.json); crop growth is gameplay's random tick.
// Here: the watering can (fill it at water, pour it to bring crops on), rice wanting water beside its
// farmland, and the wild seeds long grass gives up now and then (for gameplay's block drops).
#pragma once
#include "sim/content.h"

namespace dq {

constexpr int WATERING_CAN_MAX = 24;
std::string wildSeed();                       // farming.js wildSeed(): a garden seed for cut long grass
// obtain.js: what else breaking this block gives (wild carrots/potatoes in the grass, nether wart among the
// Depths' roots), for gameplay's dropBlockItems (blockDrops rule on, not washed away)
std::vector<ItemStack> obtainExtraDrops(const std::string& blockKey, const std::string& toolType);
bool riceSpotWet(World& w, int x, int y, int z);   // farmland under (x,y,z) has water beside it
int canWater(const ItemStack& s);

} // namespace dq
