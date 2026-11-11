// sim/worktable.h — the crafting table worked in the world (immersive worlds; after Vintage Story's and the
// "craft in the world" mods): its top is a 3 x 3 grid. Look at a square and use with an item: one goes down there. Use
// with an empty hand on a filled square: it comes back. Use with an empty hand on an empty square while the grid makes
// something: it's made (the same recipes, the same result as the crafting screen; leftovers like buckets come back).
// An empty table with an empty hand, or its sides, open the crafting screen as ever. Its block entity
// {type: "worktable", items: [9]}: synced like every block entity, spilled when the table is broken, drawn by
// render/pile_layer.cpp (the pieces on their squares, what they make hovering over the middle).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"
#include <array>

namespace dq {
class Player;
// true: handled (the table's top was used); false: let the table open its screen
bool worktableUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);
// the 3 x 3 grid on a table (empty stacks where nothing lies), and what it makes now ("" if nothing)
std::vector<ItemStack> worktableGrid(const json& be);
int worktableCell(const RayHit& hit);   // 0..8 (row-major from the north-west), -1 if not on the top
// recipe slot (row-major, row 0 the side away from someone looking along yaw) -> the table's cell
std::array<int, 9> worktableOrder(float yaw);
}
