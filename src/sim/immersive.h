// sim/immersive.h — the immersive progression (after TerraFirmaCraft; docs/superpowers/specs/2026-11-10-immersive-progression-
// design.md): a world rule, "immersive", off unless chosen in Create World (experimental). Here its gates: logs need an axe, leaves
// don't hold you up, leaves drop sticks; and the recipes it turns off (sim/crafting.cpp asks recipeAllowed).
#pragma once
#include "sim/level.h"

namespace dq {

bool immersive(Level& lv);                          // the rule
bool needsAxe(Level& lv, BlockId id);               // a log, in an immersive world: no breaking it without an axe
constexpr float LEAF_STICK_CHANCE = 0.25f;          // a stick from broken leaves (TFC's ~28%)
bool recipeAllowed(Level& lv, const json& recipe);  // false: a classic-only recipe in an immersive world, or an immersive-only one in a classic world

} // namespace dq
