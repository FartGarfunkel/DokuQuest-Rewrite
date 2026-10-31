// sim/stations.h — the work stations' rules (game/blocks2.js stationResult + enchanting.js's additions,
// core/items.js stonecutterOptions): anvil, grindstone, smithing table, loom, fletching table and the
// stonecutter. The panels themselves are modes of the inventory screen (ui/inventory.cpp, sim/inv_ops).
#pragma once
#include "sim/enchanting.h"
#include "sim/crafting.h"   // recolorKey (the loom)

namespace dq {

// What a station makes from its two input slots (StationResult::ok false = nothing).
StationResult stationResult(const std::string& kind, const ItemStack& a, const ItemStack& b);
// The stonecutter: one block of stone into its stairs, slabs (two), walls and bricks: {item, count}.
std::vector<std::pair<std::string, int>> stonecutterOptions(const std::string& id);
// The Tool Forge (a stonecutter-like list): the heads a material makes, and what one costs (tools/port/forging.py).
std::vector<std::pair<std::string, int>> forgeOptions(const std::string& id);
int forgeCost(const std::string& head);
inline std::vector<std::pair<std::string, int>> cutterOptions(const std::string& kind, const std::string& id) { return kind == "forge" ? forgeOptions(id) : stonecutterOptions(id); }
std::string repairMaterial(const std::string& key);                         // the anvil (item key or #tag)
int maxDurabilityOf(const ItemDef* d);                                      // tools and armour (0 = none)
const char* stationHelp(const std::string& kind);                           // the line under an empty station
std::pair<const char*, const char*> stationPlaceholders(const std::string& kind);   // faint icons in the slots

} // namespace dq
