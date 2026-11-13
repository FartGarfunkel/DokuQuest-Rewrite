# World generation merge (terrain version 14)

Goal (the user's): Soulrend's worlds "look nicer" — merge how they generate with ours, best of both.

## What Soulrend's Overworld is
- Terrain: vanilla Minecraft 1.20's noise terrain (no Tectonic; Nature's Spirit and Lithostitched change no density
  functions). Its look comes from the noise router: continentalness, erosion and ridges noises feed the `offset`,
  `factor` and `jaggedness` splines; `sloped_cheese` = those plus a 3D noise (`base_3d_noise`) gives cliffs and overhangs;
  `final_density` adds cheese/spaghetti/noodle caves and cave entrances.
- Biomes: vanilla's plus Nature's Spirit's 51 (placed by TerraBlender regions, `src/natures_spirit/.../terrablender/*Region.java`).
- Rivers: Climate Rivers (cold / lukewarm / warm river biomes by temperature; cosmetic).
- Underground: Alex's Caves cave biomes.

## What we take, and how
1. **Terrain shape: vanilla's, data-driven.** `tools/port/vanilla_worldgen.py` copies the density functions, noises and
   `noise_settings/overworld.json` from the Minecraft 1.20.1 client jar into `data/vanilla_worldgen/` (git-ignored, like
   the imported structures). `gen/density.{h,cpp}` interprets them (our own Perlin octave noise seeded from the world
   seed: the same kind of land, not the same land as a Minecraft seed).
2. **Height mapping.** Minecraft y = our band y - 1 (its sea 63, ours 64). Our 256-tall band holds Minecraft y -1..254:
   every mountain fits (191 blocks above the sea); only Minecraft's deepest underground (y -64..-2) is left out.
   (A taller world is a separate decision: 118 files assume a 256 band, and every save.)
3. **Everything else stays ours**: biomes and their variants (temperature/humidity from our climate), surfaces, rock
   layers, ores, trees, decoration, structures, villages, the Deep Dark. Landforms come from the terrain: ocean, beach,
   mountains by height and erosion; mushroom islands where continentalness is lowest (vanilla's mushroom fields).
4. **One ground height for everyone.** `column()` (structures, villages, trees, spawning) is the exact surface of
   `sloped_cheese` found per column (2D parts cached per column, a short search in y), so what's planned on the ground is
   on the ground. Blocks are filled from `final_density` (caves too) on Minecraft's 4x8x4 cells, interpolated.
   Structures' levelling (`flatten`) pulls the density to the target height where it applies.
5. Water: air at or below the sea that is above the column's surface is water (seas, lakes, rivers); caves below the
   surface stay dry (no aquifers yet).
6. Old worlds keep their terrain version; new worlds get 14 when the vanilla data is there (else 13).

## Next (after the terrain)
- Nature's Spirit biomes as new lands (surface, trees, plants; their art via the local mod-art pack).
- Climate rivers (river water tint/fish by temperature), Alex's Caves biomes.

# Terrain version 15: Soulrend's world itself

Version 14 above had the terrain right in kind but was wrong about what Soulrend is: its own biomes and surface came from
our climate. Reading its mods and configs (the Prism instance), Soulrend's Overworld is:
- **Expanded Ecosphere** in its DEFAULT mode (its `ee_default` pack): Minecraft's noise router with its own `offset`,
  `factor` and `jaggedness` splines (the shape of the land), its own 4589-entry biome list for the dimension (vanilla
  biomes and 189 `wythers:` ones), and its own surface rule.
- **WWOO** (William Wythers' Overhauled Overworld): its versions of the vanilla biomes (loaded after Expanded Ecosphere's,
  so they win; Nature's Spirit's `modified_*` packs load at the bottom, under both).
- **TerraBlender** (`overworld_region_size` 3): per area, region 0 (weight 10: the dimension's list, i.e. Expanded
  Ecosphere's) or one of **Nature's Spirit**'s five regions (weight 4 each), every Nature's Spirit biome on.
- Surface: TerraBlender's namespaced rules (Nature's Spirit's for its biomes, Minecraft's for `minecraft:`), falling
  back to Expanded Ecosphere's rule. **Climate Rivers** registers rules and rivers but only patches vanilla's biome
  builder, which nothing runs here: it has no effect in Soulrend.
- The biomes' features (`gen/mc_features`): each biome's placed features run as Minecraft runs them (the steps' order, a
  chunk's neighbours' features reaching into it), the placement modifiers and the plant and ground kinds ported:
  random_patch / flower, simple_block, the selectors, block_column, vegetation_patch, disk, seagrass, kelp, sea pickles,
  bamboo, vines, forest_rock, iceberg, blue_ice, freeze_top_layer, Nature's Spirit's leveled patches, ponds, big blocks.
  Left to ours: trees (ours stay, and any feature with a tree in it), ores, lakes, springs, caves, dungeons, fossils,
  geodes, corals. Block tags (`block_tags.json`) come from every jar.
- Nature's Spirit's plants and ground as blocks of ours (`tools/port/soulrend_worldgen/ns_blocks.py`): 66 blocks, their
  built-in pictures our own recoloured, the mod's own in the DokuQuest Base pack.
- The land's extras before its surface (`OverworldGen::surfaceExtras`): eroded badlands spires and frozen-ocean icebergs
  (SurfaceSystem's extensions), Nature's Spirit's sugi and stratified-desert pillars (its SurfaceBuilderMixin).
- Our land kinds (villages, structures, our trees) from Soulrend's biomes by name; snowy exactly where it snows.
- Not yet: Alex's Caves' cave biomes, Quark's underground biomes.

How: `tools/port/soulrend_worldgen/import.py` (needs the instance, Minecraft's jar and mappings, CFR, node) writes
`data/soulrend_worldgen/` (git-ignored). Nature's Spirit's region builders and surface rules are code, so they are
decompiled, converted to JS (`j2js.py`) and run against stand-ins (`regions.js`, `rules.js`) to get their exact tables.
Its ground blocks we lack are matched to our nearest natural ground by look (`block_substitutes.json`).
In the game: `gen/mc_biomes` (TerraBlender's region layers, ported exactly; the nearest climate box as Climate.RTree;
BiomeManager's zoom), `gen/surface_rules` (the rule interpreter and SurfaceSystem.buildSurface's column walk), and
`OverworldGen` v15 (`surfaceV15`). Our seeds are not Minecraft's: the same kind of world, not the same world.
