// sim/knapping.h — knapping (after TerraFirmaCraft's ContainerSpecialCrafting): stones, flint or clay worked on a 5x5 grid,
// chipping cells away until what's left is a shape from data/knapping.json (a tool head, a vessel, a mold). The screen is
// ui/knapping_screen.cpp; the controller opens it (use with 2+ pebbles or flint, or 5 clay).
#pragma once
#include "common/common.h"
#include <array>

namespace dq {

struct KnapPattern { std::string out; int n = 1; std::vector<std::string> rows; bool defOn = false; };   // '#' kept, ' ' chipped; defOn (TFC default_on): the cells outside a smaller pattern stay kept instead of chipped
const std::vector<KnapPattern>& knapPatterns(const std::string& material);          // "stone" | "flint" | "clay"
std::string knapMaterialOf(const std::string& itemId);                               // pebble: stone; flint: flint; clay_ball: clay
int knapCost(const std::string& material);                                           // items it takes: stone/flint 1 (the first chip), clay 5 (a shape made)
int knapNeeds(const std::string& material);                                          // in hand to start: stone/flint 2, clay 5
struct KnapGrid { std::array<bool, 25> kept{}; KnapGrid() { kept.fill(true); } };    // row-major, y down
// the pattern the grid is now (any offset, mirrored too; the cells outside it all chipped, or all kept for a defOn one), or null
const KnapPattern* knapMatch(const std::string& material, const KnapGrid& g);

} // namespace dq
