// sim/firepit.h — the campfire as TerraFirmaCraft's firepit, in immersive worlds (a classic campfire burns on as ever): a fuel store of up to four (logs, planks,
// sticks, charcoal, coal), each fuel burning so long at so hot; the fire's temperature climbs toward its fuel's and falls
// without; out of fuel and cool, it goes out; rain on an open fire puts it out. Food cooks on it as before (once it's hot
// enough); metal, ore and pottery set on it heat up (sim/heat.h). Ticked from sim/camping.cpp's campingTick; its block
// entity is the campfire's own ({items, t} + {fuel, burn, temp, peak}). Lit from three sticks with a firestarter
// (sim/firestarter.h).
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

struct FuelInfo { float peak = 0, secs = 0; };   // °C it burns at, seconds one lasts (0: not a fuel)
FuelInfo fuelOf(const std::string& itemId);
constexpr int FIREPIT_FUEL_MAX = 4;
constexpr float FIREPIT_COOK_MIN = 150;            // °C before food cooks on it
double gameHourSecs(Level& lv);                    // real seconds in an in-game hour (the world's day + night lengths / 24)

bool firepitAddFuel(World& w, int x, int y, int z, ItemStack& held);   // one from held into its fuel store
void firepitTick(Level& lv, World& w, int x, int y, int z, double dt);
// a lit firepit in the cell (x,y,z) from three or more sticks lying there (they're used up); false if not enough
bool firepitFromSticks(Level& lv, World& w, int x, int y, int z);
bool strawLiesAt(Level& lv, int realm, int x, int y, int z);           // straw dropped in the cell (a better chance to light)

} // namespace dq
