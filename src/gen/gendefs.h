// gen/gendefs.h — shared bits of the terrain generators: JS-exact math helpers, the vertical mapping
// from the original 128-tall world into the 256-tall bands, biomes, and the block ids the generators use.
#pragma once
#include "world/dirs.h"
#include "world/chunk.h"
#include "common/noise.h"
#include <cstring>

namespace dq {

// ---- JS-exact helpers (core/util.js) ----
inline double jsClamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double jsLerp(double a, double b, double t) { return a + (b - a) * t; }
inline double jsSmooth(double a, double b, double x) { double t = jsClamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
inline int jsFloor(double v) { return (int)std::floor(v); }
inline int jsRound(double v) { return (int)std::floor(v + 0.5); }            // Math.round
inline int jsTrunc(double v) { return (int)v; }                               // x | 0, x << n
// lairSeed (game/dragons.js): per-region seeds for structures
inline u32 lairSeed(i32 seed, i32 rx, i32 rz, i32 k) {
  u32 h = (u32)(seed ^ k) * 0x9E3779B1u ^ (u32)rx * 0x85EBCA6Bu ^ (u32)rz * 0xC2B2AE35u;
  h = (h ^ (h >> 16)) * 0x7FEB352Du; h = (h ^ (h >> 15)) * 0x846CA68Bu;
  return h ^ (h >> 16);
}
// DIRX / DIRZ: world/dirs.h
#ifndef DQ_WLOG_DEFINED
#define DQ_WLOG_DEFINED
constexpr u8 WLOG = 0x80;   // waterlogged meta bit
#endif

// ---- the vertical mapping (see docs/port/gen.md) ----
// Every band is 256 tall; the original world was 128 tall (sea 48). "o" = original-world y, "y" = band-local y.
namespace vmap {
constexpr int JS_SEA = 48;
// Overworld surface: identity within 4 blocks of the sea (shores, rivers, shallows keep their exact
// depths), a 60/44 stretch below that (ocean floors deeper), and y = 64 + d + 0.0145 d^2 above the sea
// (d = o - 48): lowlands nearly unchanged, mountains grow to ~211 (original max 120).
constexpr double OW_UP_Q = 0.0145;
inline double owSurface(double o) {
  double d = o - JS_SEA;
  if (d >= 0) return SEA_LEVEL + d + OW_UP_Q * d * d;
  if (d >= -4) return SEA_LEVEL + d;
  return o * (SEA_LEVEL - 4) / (JS_SEA - 4.0);
}
inline double owSurfaceInv(double y) {
  if (y >= SEA_LEVEL) { double e = y - SEA_LEVEL; return JS_SEA + (-1 + std::sqrt(1 + 4 * OW_UP_Q * e)) / (2 * OW_UP_Q); }
  if (y >= SEA_LEVEL - 4) return JS_SEA + (y - SEA_LEVEL);
  return y * (JS_SEA - 4.0) / (SEA_LEVEL - 4);
}
// Overworld underground (caves, ores, geodes, the deep dark, dungeon depths): a uniform 4/3 stretch.
constexpr double OW_UNDER = 4.0 / 3.0;
inline double owUnderO(int y) { return y * 0.75; }                 // band y -> original y (fractional)
inline int owUnder(double o) { return jsFloor(o * OW_UNDER); }     // original y -> band y
inline int owStretchLo(int o) { return (int)std::ceil(o * OW_UNDER - 1e-9); }   // the band rows one original row covers: [lo, hi)
inline int owStretchHi(int o) { return owStretchLo(o + 1); }
// Caves (and the slate layer) of one column: the uniform stretch deep down, blending over the top 32 rows to
// "the surface block is the original's surface row", so caves open onto lowland and hills where they did in
// the original. Where the two mappings differ a lot (high mountains) the correction fades out instead of
// stretching caves. H = band height of the column's top block, h = its original height. Monotonic.
inline double owCaveO(int y, int H, int h) {
  double u = y * 0.75, off = h - H * 0.75, depth = H - y;
  off *= std::clamp((16 - std::fabs(off)) / 8, 0.0, 1.0);
  return depth >= 32 ? u : u + off * (depth <= 0 ? 1 : 1 - depth / 32);
}
// Ember Depths: the 100-tall original stretched 2.5x (lava sea 28 -> 70); bedrock floor and roof.
constexpr double NETHER_K = 2.5;
inline int netherY(double o) { return jsFloor(o * NETHER_K); }
// Skylands: islands keep their shapes, lifted 120 (original 20..110 -> 140..230); the cloud bank is band 0..30.
constexpr int SKY_DY = 120;
// The End: shapes kept, lifted 64 (main island top 61 -> 125, the platform 48 -> 112).
constexpr int END_DY = 64;
// Side realms (pocket dimension, his realm) keep the original y.

// How deep in the cloud bank between the Overworld and the Skylands a band-local y is (game/seamless.js
// cloudAt): the bottom 30 of the Skylands band and the top 22 of the Overworld band. 0 = clear, <= 0.93.
inline float cloudBank(int dim, double y) {
  double k = 0;
  if (dim == DIM_AETHER) k = jsSmooth(30, 0, y);
  else if (dim == DIM_OVERWORLD) k = jsSmooth(DIM_H - 22, DIM_H, y);
  return k > 0 ? (float)(jsSmooth(0.15, 0.85, k) * 0.93) : 0.f;
}
} // namespace vmap

enum Biome : u8 { BIOME_OCEAN = 0, BIOME_BEACH, BIOME_PLAINS, BIOME_FOREST, BIOME_DESERT, BIOME_TAIGA, BIOME_MOUNTAINS, BIOME_COUNT };
const char* biomeName(int b);

// Block ids the generators place (the original's B.xxx), resolved from the registry once per generator.
#define DQ_GEN_BLOCKS(X) \
  X(stone) X(dirt) X(grass) X(water) X(sand) X(sandstone) X(snowy_grass) X(clay) X(gravel) X(snow_block) X(mud) X(podzol) \
  X(coarse_dirt) X(red_sand) X(terracotta) X(packed_ice) X(blue_ice) X(dirt_with_dry_grass) X(dirt_with_coniferous_litter) X(dirt_with_rainforest_litter) X(desert_sand) \
  X(desert_stone) X(desert_sandstone) X(silver_sand) X(peat) X(bedrock) X(slate) X(lava) X(ice) X(old_torch) X(tall_grass) \
  X(coal_ore) X(iron_ore) X(copper_ore) X(tin_ore) X(gold_ore) X(lapis_ore) X(diamond_ore) X(mese_ore) X(redstone_ore) \
  X(granite) X(diorite) X(andesite) X(tuff) X(marble) X(limestone) X(calcite) X(dripstone_block) X(emerald_ore) \
  X(silver_ore) X(mithril_ore) X(adamantite_ore) X(ruby_ore) X(sapphire_ore) \
  X(spruce_log) X(spruce_leaves) X(jungle_log) X(jungle_leaves) X(cacao_leaves) X(seasonal_leaves) X(vine) X(cocoa) X(pine_log) X(pine_needles) X(aspen_log) \
  X(aspen_leaves) X(log) X(leaves) X(birch_log) X(birch_leaves) X(flowering_azalea_leaves) X(azalea_leaves) X(acacia_log) \
  X(acacia_leaves) X(dark_oak_log) X(dark_oak_leaves) X(cherry_log) X(cherry_leaves) X(mangrove_log) X(mangrove_leaves) \
  X(crimson_stem) X(warped_stem) X(nether_wart_block) X(warped_wart_block) X(shroomlight) X(weeping_vines) X(bee_nest) \
  X(fern) X(flower_yellow) X(flower_red) X(azure_bluet) X(oxeye_daisy) X(cornflower) X(red_tulip) X(orange_tulip) \
  X(white_tulip) X(pink_tulip) X(lily_of_the_valley) X(allium) X(lilac) X(rose_bush) X(peony) X(tube_coral) X(brain_coral) \
  X(bubble_coral) X(fire_coral) X(horn_coral) X(tube_coral_block) X(brain_coral_block) X(bubble_coral_block) \
  X(fire_coral_block) X(horn_coral_block) X(seagrass) X(kelp) X(lily_pad) X(tall_grass_double) X(sunflower) X(pumpkin) \
  X(berry_bush) X(brown_mushroom) X(red_mushroom) X(melon) X(lavender) X(heather) X(sugar_cane) X(large_fern) X(snow_layer) \
  X(blue_orchid) X(dead_bush) X(cactus) X(amethyst_cluster) X(sea_pickle) X(tube_coral_fan) X(brain_coral_fan) \
  X(bubble_coral_fan) X(fire_coral_fan) X(horn_coral_fan) X(cattail) X(marram_grass) X(loose_stones) X(loose_flint) X(twigs) X(chalk) X(shale) X(claystone) X(conglomerate) X(gneiss) X(schist) X(quartzite) X(gabbro) X(rhyolite) X(dacite) X(wintergreen_bush) X(blueberry_bush) X(raspberry_bush) X(blackberry_bush) X(bunchberry_bush) X(cranberry_bush) X(snowberry_bush) X(elderberry_bush) X(gooseberry_bush) X(cloudberry_bush) X(banana_leaves) X(lemon_leaves) X(olive_leaves) X(orange_leaves) X(peach_leaves) X(plum_leaves) X(zinc_ore) X(bismuth_ore) X(copper_sample) X(tin_sample) X(zinc_sample) X(bismuth_sample) X(iron_sample) X(gold_sample) X(silver_sample) X(dry_grass) X(bamboo) \
  X(jungle_grass) X(pink_petals) X(torchflower) X(pitcher_plant) X(moss_block) X(moss_carpet) X(azalea) X(cave_vines_lit) \
  X(cave_vines) X(spore_blossom) X(pointed_dripstone) X(hanging_roots) X(glow_lichen) X(smooth_basalt) X(budding_amethyst) \
  X(amethyst_block) X(large_amethyst_bud) X(small_amethyst_bud) X(glowstone) X(obsidian) X(blackstone) X(basalt) \
  X(netherrack) X(emberstone) X(magma_block) X(gilded_blackstone) X(soul_sand) X(soul_soil) X(bone_block) X(soul_fire) \
  X(crimson_nylium) X(crimson_fungus) X(crimson_roots) X(warped_nylium) X(twisting_vines) X(warped_fungus) X(warped_roots) \
  X(nether_sprouts) X(ash) X(pumice) X(cinder_sand) X(emberglass_cluster) X(emberglass_block) X(sulphur_block) X(steam_vent) X(igneous_emberstone) X(thorn_leaves) X(thorn_log) X(shrub_leaves) X(flowering_cherry_leaves) X(null_stone) X(end_moss) X(cloud_puff) X(bounce_cloud) X(sun_cloud) X(glimmersand) X(sunwood_log) X(sunwood_leaves) X(palm_log) X(palm_leaves) X(oak_branch) X(birch_branch) X(dark_oak_branch) X(jungle_branch) X(cherry_branch) X(acacia_branch) X(mangrove_branch) X(sky_bloom) X(bluebells) X(marigold) X(spinifex) X(scorched_dirt) X(rich_scorched_dirt) X(perma_ice) X(permafrost) X(ember_coal_ore) X(mossy_dark_oak_log) X(mossy_mangrove_log) X(algae) X(soul_schist) X(bone_shale) X(ashen_trunk) X(gum_log) X(gum_leaves) X(cinderite_ore) X(quartz_ore) X(nether_gold_ore) X(ancient_debris) X(skystone) X(sky_grass) \
  X(aether_crystal_ore) X(driftstone) X(cloud_leaves) X(pocket_wall) X(ember_crust) X(void_crust) X(end_stone) X(chorus_plant) \
  X(chorus_flower) X(iron_bars) X(rift_wall) X(rift_glass) X(rift_crystal) X(sculk) X(sculk_vein) X(sculk_sensor) \
  X(sculk_shrieker) X(sculk_catalyst) X(cobbled_slate) \
  X(orange_terracotta) X(yellow_terracotta) X(white_terracotta) X(brown_terracotta) X(cyan_terracotta) X(purple_terracotta) X(magenta_terracotta) X(light_blue_terracotta) \
  X(purpur_block) X(purpur_pillar) X(end_rod) X(gold_block) X(sea_lantern) X(medium_amethyst_bud) \
  X(holystone) X(quicksoil) X(skyroot_log) X(skyroot_leaves) X(ambrosium_ore) X(zanite_ore) X(gravitite_ore) \
  X(golden_oak_log) X(golden_oak_leaves) X(cold_aercloud) X(blue_aercloud) X(golden_aercloud) \
  X(gloomstone) X(nether_coal_ore) X(basalt_coal_ore) X(gloom_coal_ore) X(granite_gold_ore) X(limestone_coal_ore) \
  X(dragon_log) X(dragon_leaves) X(umbrella_cap) X(glowshroom_stem) X(glowshroom_cap) X(amber_block) X(neon_moss) X(shadow_soil) X(lantern_log) \
  X(chorus_turf) X(glow_turf) X(umbrella_turf) X(lantern_turf) X(crystal_turf) X(blossom_cap) \
  X(fire) X(acid) X(red_crystal) X(charged_crystal) X(red_crystal_cluster) X(smouldering_ash) X(scorched_bricks) X(cracked_scorched_bricks) \
  /* Better End (gen/theend_be.cpp) */ \
  X(endstone_dust) X(chorus_nylium) X(end_mycelium) X(crystal_moss) X(shadow_grass) X(pink_moss) X(amber_moss) X(jungle_moss) X(sangnum) X(rutiscus) \
  X(cave_moss) X(pallidium_full) X(pallidium_heavy) X(pallidium_thin) X(pallidium_tiny) X(flavolite) X(violecite) X(sulphuric_rock) X(brimstone) X(umbralith) \
  X(virid_jadestone) X(azure_jadestone) X(sandy_jadestone) X(dragon_bone_block) X(dense_snow) X(emerald_ice) X(dense_emerald_ice) X(ancient_emerald_ice) X(amber_ore) X(ender_ore) \
  X(thallasium_ore) X(aurora_crystal) X(smaragdant_crystal) X(smaragdant_crystal_shard) X(mossy_glowshroom_log) X(mossy_glowshroom_cap) X(mossy_glowshroom_hymenophore) X(mossy_glowshroom_fur) X(pythadendron_log) X(pythadendron_leaves) \
  X(lacugrove_log) X(lacugrove_leaves) X(dragon_tree_log) X(dragon_tree_leaves) X(tenanea_log) X(tenanea_leaves) X(tenanea_flowers) X(helix_tree_log) X(helix_tree_leaves) X(umbrella_tree_log) \
  X(umbrella_tree_membrane) X(umbrella_tree_cluster) X(jellyshroom_log) X(jellyshroom_cap_purple) X(lucernia_log) X(lucernia_leaves) X(filalux) X(amaranita_hyphae) X(amaranita_cap) X(amaranita_lantern) \
  X(amaranita_fur) X(umbrella_moss) X(twisted_umbrella_moss) X(creeping_moss) X(chorus_grass) X(crystal_grass) X(shadow_plant) X(bushy_grass) X(amber_grass) X(jungle_grass_be) \
  X(blooming_cooksonia) X(salteago) X(vaiolush_fern) X(fracturn) X(clawfern) X(globulagus) X(orango) X(aeridium) X(lutebus) X(lamellarium) \
  X(inflexia) X(needlegrass) X(murkweed) X(cave_grass) X(charnia_cyan) X(charnia_purple) X(charnia_red) X(charnia_orange) X(charnia_green) X(end_lily) \
  X(glowing_pillar_luminophor) X(dense_vine) X(twisted_vine) X(jungle_vine) X(blue_vine_lantern) X(blue_vine) X(neon_cactus) X(tail_moss) X(cyan_moss) X(twisted_moss) \
  X(end_stone_stalactite) X(red_mushroom_block) \
  /* BTA 8.0's Nether (gen/nether_bta.cpp, terrain version 7) */ \
  X(brimsand) X(nether_brimstone) X(brimthaw) X(cobbled_netherrack) X(crystalline_netherrack) X(cobbled_gloomstone) X(ember) X(ash_layer) \
  X(rubyglass_column) X(rubyglass_node) X(rubyglass_crystal) X(rubyglass_growth) X(magmatic_boulder) X(sulfuric_boulder) X(bone_pile) X(soul_catcher)

struct GenBlocks {
#define DQ_GB_FIELD(k) BlockId k = 0;
  DQ_GEN_BLOCKS(DQ_GB_FIELD)
#undef DQ_GB_FIELD
  BlockId air = AIR;
  void resolve();                       // from reg(); call after the registry is loaded
  // registry tables (read-only while generating)
  const u8* solid = nullptr; const u8* liquid = nullptr; const u8* wlog = nullptr;
  std::vector<u8> axis, dbl;            // axisBlock, double (two-tall plant)
};

// A fast fixed-size block buffer for one band chunk, laid out like the JS chunk (colIdx), copied into
// the Chunk's sections at the end of generate().
struct GenBuf {
  BlockId blocks[DIM_H * 256];
  u8 meta[DIM_H * 256];
  u8 coat[DIM_H * 256];   // (a rug laid on a floor: world/rug.h)
  void clear() { std::memset(blocks, 0, sizeof blocks); std::memset(meta, 0, sizeof meta); std::memset(coat, 0, sizeof coat); }
  BlockId at(int x, int y, int z) const { return (x < 0 || x > 15 || z < 0 || z > 15 || y < 0 || y >= DIM_H) ? (BlockId)0xFFFF : blocks[colIdx(x, y, z)]; }
  void store(Chunk& c) const;           // copy into the chunk (sections allocated as needed)
  static GenBuf& local();               // this thread's buffer, cleared
};

} // namespace dq
