// sim/pit_kiln.h — TerraFirmaCraft's pit kiln: unfired pottery set down in a pit (up to four pieces, one to a quarter), eight
// straw over it, then eight logs, lit with a firestarter; walled in on all four sides and solid below it burns for three
// in-game hours (TFC's eight, shortened) and the pottery comes out fired (and hot). A side opened while it burns puts it out
// and the fuel is lost. Block pit_kiln (drawn by render/pile_layer.cpp), entity {type: "kiln", items[4], straw, logs, lit, t}.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

constexpr int KILN_STRAW = 8, KILN_LOGS = 8;
constexpr double KILN_HOURS = 3;
bool isUnfired(const std::string& itemId);                 // something a kiln fires ("fires" in data/items.json)
// sneak + use with unfired pottery into the cell (x,y,z) over a solid block, in quarter q (0..3): a kiln, or one more piece in it
bool kilnPlace(World& w, int x, int y, int z, int q, ItemStack& held);
// use with straw or logs on a kiln: one more (straw first, logs once the straw is in); false + why if not
bool kilnAdd(World& w, int x, int y, int z, ItemStack& held, std::string* why = nullptr);
ItemStack kilnTake(World& w, int x, int y, int z, int q);  // use with an empty hand: a piece back (not while it burns)
bool kilnLight(Level& lv, World& w, int x, int y, int z);  // the firestarter's spark: full and walled in -> lit
bool kilnWalled(World& w, int x, int y, int z);
void pitKilnTick(Level& lv, double dt);                     // the lit ones burn (sim/content.cpp)

} // namespace dq
