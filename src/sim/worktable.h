// sim/worktable.h — the crafting table worked in the world (normal and immersive worlds; after Vintage Story's and the
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
// the same, one at a time (the crafting table scene, ui/cinematic_table.cpp): a piece of what's held onto a square, the
// piece on a square back into the bag, and the grid made (read from where p stands) — false if nothing happened
bool worktablePut(Level& lv, Player& p, World& w, ivec3 at, int cell, ItemStack& held);
bool worktableTake(Level& lv, Player& p, World& w, ivec3 at, int cell);
bool worktableCraft(Level& lv, Player& p, World& w, ivec3 at);
// a recipe laid out on the table from your bag (the crafting guide's "Lay it out"): what's on the grid back to you first,
// then one of each piece onto its square as you'd read the recipe standing where you are. "" done, else why not (toasted)
std::string worktableLayout(Level& lv, Player& p, World& w, ivec3 at, int recipe);
struct CraftMatch;
CraftMatch worktableMatch(World& w, ivec3 at, float yaw, bool immersiveWorld);   // what the grid makes, read from that facing
// the 3 x 3 grid on a table (empty stacks where nothing lies), and what it makes now ("" if nothing)
std::vector<ItemStack> worktableGrid(const json& be);
// where the 3 x 3 is drawn on the table's top (x0, z0, x1, z1 of the block; the client measures it from the texture:
// render/pile_layer.cpp), each square's middle on the top, and the square a point on the top falls in
void setWorktableGrid(vec4 rect);
vec4 worktableGridRect();
vec3 worktableCellCenter(ivec3 at, int cell);
int worktableCell(const RayHit& hit);   // 0..8 (row-major from the north-west), -1 if not on the top
// recipe slot (row-major, row 0 the side away from someone looking along yaw) -> the table's cell
std::array<int, 9> worktableOrder(float yaw);
}
