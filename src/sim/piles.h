// sim/piles.h — ingot piles and log piles (after TerraFirmaCraft's BlockIngotPile / TELogPile), in every world: sneak + use
// with an ingot or a log on the ground sets one down, the pile grows one at a time, use takes one back. An ingot pile holds
// 64 (8 to a layer as two rows of four, each layer across the one under it, like brickwork), a log pile 16 (a crib of four
// courses). One metal (one makeup) or one wood to a pile; a full pile takes a new one on top, and adding to a pile tops up
// the one below it first. The block's meta is its height (layers / courses), which its collision follows (world/shapes.cpp);
// render/pile_layer.cpp draws the slots. Block entity {type: "ingots"|"logs", item: stack}.
#pragma once
#include "core/itemstack.h"
#include "world/world.h"

namespace dq {

enum class PileKind : u8 { None, Ingots, Logs };
constexpr int INGOT_PILE_MAX = 64, LOG_PILE_MAX = 16;

PileKind pileKindOfItem(const std::string& itemId);   // "*_ingot": ingots; a log (tag #log): logs
PileKind pileKindOfBlock(BlockId id);
int pileMax(PileKind k);
u8 pileMeta(PileKind k, int n);                        // ingots: layers of 8 (1..8); logs: courses of 4 (1..4)

// sneak + use with `held` into the free cell (x,y,z) over a solid block (or a full pile): tops up a not-full pile of the same
// thing straight below first, else starts a pile there. Moves one from held. false (held untouched, why set) if it can't.
bool pilePlace(World& w, int x, int y, int z, ItemStack& held, std::string* why = nullptr);
// sneak + use with `held` on the pile at (x,y,z): one more onto the lowest pile of its column with room (so a pile below is
// topped up first); all full: a new pile on top
bool pileAdd(World& w, int x, int y, int z, ItemStack& held, std::string* why = nullptr);
// use on a pile: one off it (n = 1; empty if none); the block goes when it's empty
ItemStack pileTake(World& w, int x, int y, int z);

// where the i-th one (in placing order) lies: block units from the block's corner, and which way it points (yaw)
struct PileSlot { vec3 pos; float yaw; };
PileSlot ingotSlot(int i);
PileSlot logSlot(int i);

} // namespace dq
