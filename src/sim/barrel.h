// sim/barrel.h — TerraFirmaCraft's barrel (our "sealed_barrel"; the storage barrel stays as it was): up to ten buckets of a
// liquid and four stacks of things; some mixes happen at once (lime in water: limewater), the rest only sealed, over
// in-game hours (data/barrel.json: hides soaked in limewater, logs steeped into tannin, scraped hides tanned). The leather
// chain: raw hide (animals drop it in an immersive world) -> soaked -> scraped with a knife -> prepared -> leather.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

constexpr int BARREL_BUCKETS = 10, BARREL_SLOTS = 4;
bool barrelPour(World& w, int x, int y, int z, ItemStack& bucket);     // a water bucket in (it comes back empty), an empty one out
bool barrelPut(World& w, int x, int y, int z, ItemStack& held);        // a stack in (not sealed)
ItemStack barrelTake(World& w, int x, int y, int z);                   // the last stack out (not sealed)
bool barrelSeal(Level& lv, World& w, int x, int y, int z, bool seal);
std::string barrelLiquid(World& w, int x, int y, int z);
ItemStack barrelItem(World& w, int x, int y, int z, int slot);
void barrelTick(Level& lv, double dt);
bool scrapeHide(ItemStack& hide, ItemStack& knife);                    // soaked -> scraped, the knife worn
std::string hideFor(const std::string& drop, bool immersiveWorld);     // what an animal drops for "leather"

} // namespace dq
