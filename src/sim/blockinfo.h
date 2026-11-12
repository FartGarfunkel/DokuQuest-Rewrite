// sim/blockinfo.h — fast per-block gameplay traits (the original read BLOCKS[id].climb etc. directly;
// here the json props are flattened into a table once) and the well-known block ids the rules use
// (the original's B.xxx). Rebuilt automatically when the registry grows (data packs, mods).
#pragma once
#include "core/registry.h"

namespace dq {

namespace BF { enum : u32 {
  Climb = 1u << 0, Scaffold = 1u << 1, Sticky = 1u << 2, Fire = 1u << 3, Hot = 1u << 4, Bouncy = 1u << 5,
  Slippery = 1u << 6, Bed = 1u << 7, Door = 1u << 8, Trapdoor = 1u << 9, Falls = 1u << 10, NeedsSupport = 1u << 11,
  Crop = 1u << 12, Tree = 1u << 13, IsLog = 1u << 14, IsLeaves = 1u << 15, Container = 1u << 16, Furnace = 1u << 17,
  Double = 1u << 18, AxisBlock = 1u << 19, Sign = 1u << 20, Hangs = 1u << 21, AlwaysWet = 1u << 22, Tnt = 1u << 23,
  Carry = 1u << 24, FacingFront = 1u << 25, FacingModel = 1u << 26, Candle = 1u << 27, Attached = 1u << 28,
  StackPlant = 1u << 29, HardensTo = 1u << 30, Flower = 1u << 31,
}; }

struct BlockInfo {
  u32 f = 0;
  float slow = 1;          // soul sand / honey drag
  std::string furnace;     // "", "furnace", "smoker", "blast"
};

const BlockInfo& binfo(BlockId id);
inline bool bhas(BlockId id, u32 flag) { return (binfo(id).f & flag) != 0; }

// Well-known blocks (0 when a data set lacks one). Filled from the registry.
// (X-macro so the id table is filled from the same list)
#define DQ_KNOWN_BLOCKS(X) \
  X(stone) X(cobblestone) X(dirt) X(grass) X(sand) X(red_sand) X(gravel) X(water) \
  X(lava) X(obsidian) X(farmland) X(cactus) X(sugar_cane) X(kelp) X(bamboo) X(fire) \
  X(soul_fire) X(tnt) X(chest) X(barrel) X(furnace) X(lit_furnace) X(item_pile) X(backpack_block) \
  X(lily_pad) X(lantern) X(soul_lantern) X(grave) X(snowy_grass) X(sky_grass) X(podzol) X(coarse_dirt) \
  X(moss_block) X(rooted_dirt) X(mycelium) X(mud) X(dirt_path) X(pumpkin) X(carved_pumpkin) X(berry_bush) \
  X(berry_bush_empty) X(soul_sand) X(soul_soil) X(ash) X(netherrack) X(emberstone) X(magma_block) X(vine) \
  X(seagrass) X(cobweb) X(tall_grass) X(fern) X(tall_grass_double) X(large_fern) X(leaves) X(dark_oak_leaves) \
  X(glow_lichen) X(hanging_roots) X(weeping_vines) X(twisting_vines) X(cave_vines) X(cave_vines_lit) X(nether_sprouts) X(dry_grass) \
  X(marram_grass) X(jungle_grass) X(gilded_blackstone) X(bee_nest) X(beehive) X(cocoa) X(nether_wart) X(sea_pickle) \
  X(pointed_dripstone) X(budding_amethyst) X(small_amethyst_bud) X(medium_amethyst_bud) X(large_amethyst_bud) X(amethyst_cluster) X(dirt_with_dry_grass) X(dry_dirt_with_dry_grass) \
  X(dirt_with_coniferous_litter) X(dirt_with_rainforest_litter) X(dry_dirt) X(peat) X(desert_sand) X(silver_sand) X(terracotta) X(crimson_nylium) \
  X(warped_nylium) X(crimson_roots) X(warped_roots) X(crimson_fungus) X(warped_fungus) X(brown_mushroom) X(red_mushroom) X(loose_stones) \
  X(cattail) X(clay) X(dead_bush) X(jungle_log) X(jungle_wood) X(stripped_jungle_log) X(stripped_jungle_wood) X(bookshelf) \
  X(campfire) X(soul_campfire) X(chain) X(glowstone) X(redstone_ore_lit) X(hay_bale) X(scaffolding) X(snow_layer) \
  X(ice) X(ladder) X(rope)
struct KnownBlocks {
  BlockId air = 0;
#define DQ_KB_FIELD(n) BlockId n = 0;
  DQ_KNOWN_BLOCKS(DQ_KB_FIELD)
#undef DQ_KB_FIELD
};
const KnownBlocks& KB();

} // namespace dq
