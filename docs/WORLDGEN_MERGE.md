# The Overworld's terrain (terrain versions 14–21)

Goal (the user's): worlds that "look nicer" — a data-driven terrain shape merged with our own world, best of both.

## How the land is shaped
- The land comes from a noise router: continentalness, erosion and ridges noises feed the `offset`, `factor` and
  `jaggedness` splines; `sloped_cheese` = those plus a 3D noise (`base_3d_noise`) gives cliffs and overhangs;
  `final_density` adds cheese/spaghetti/noodle caves and cave entrances.

## Terrain version 14: the shape, data-driven
1. **Terrain shape from data.** `tools/port/worldgen_base.py` writes the density functions, noises and
   `noise_settings/overworld.json` into `data/worldgen_base/` (git-ignored, like the imported structures).
   `gen/density.{h,cpp}` interprets them (our own Perlin octave noise seeded from the world seed).
2. **Height mapping.** The imported data's y = our band y - 1 (its sea 63, ours 64). Our 256-tall band holds its
   y -1..254: every mountain fits (191 blocks above the sea); only the deepest underground (y -64..-2) is left out.
   (A taller world is a separate decision: 118 files assume a 256 band, and every save. See WORLD_DEPTH.md.)
3. **Everything else stays ours**: biomes and their variants (temperature/humidity from our climate), surfaces, rock
   layers, ores, trees, decoration, structures, villages, the Deep Dark. Landforms come from the terrain: ocean, beach,
   mountains by height and erosion; mushroom islands where continentalness is lowest.
4. **One ground height for everyone.** `column()` (structures, villages, trees, spawning) is the exact surface of
   `sloped_cheese` found per column (2D parts cached per column, a short search in y), so what's planned on the ground is
   on the ground. Blocks are filled from `final_density` (caves too) on 4x8x4 cells, interpolated.
   Structures' levelling (`flatten`) pulls the density to the target height where it applies.
5. Water: air at or below the sea that is above the column's surface is water (seas, lakes, rivers); caves below the
   surface stay dry (no aquifers yet).
6. Old worlds keep their terrain version; new worlds get 14 when the terrain data is there (else 13).

# Terrain version 15: the land biomes and their surfaces
Version 14 had the terrain right in kind, but its biomes and surface still came from our climate. Version 15 adds:
- **The land biomes:** a large data-driven biome list (about 4600 climate entries), placed in regions: per area,
  region 0 (weight 10: the main list) or one of five extra regions (weight 4 each), every biome on.
- **Surfaces:** each biome's namespaced surface rules, falling back to the main surface rule.
- **The biomes' features** (`gen/biome_features`): each biome's placed features run in order (the steps' order, a
  chunk's neighbours' features reaching into it), the placement modifiers and the plant and ground kinds:
  random_patch / flower, simple_block, the selectors, block_column, vegetation_patch, disk, seagrass, kelp, sea pickles,
  bamboo, vines, forest_rock, iceberg, blue_ice, freeze_top_layer, leveled patches, ponds, big blocks.
  Left to ours: trees (ours stay, and any feature with a tree in it), ores, lakes, springs, caves, dungeons, fossils,
  geodes, corals. Block tags come from `block_tags.json`.
- **New plants and ground as blocks of ours** (`tools/port/worldgen/ns_blocks.py`): 66 blocks, their built-in pictures
  our own recoloured, the rest from the DokuQuest Base pack.
- **The land's extras before its surface** (`OverworldGen::surfaceExtras`): eroded badlands spires, frozen-ocean
  icebergs, sugi and stratified-desert pillars.
- Our land kinds (villages, structures, our trees) from the land biomes by name; snowy exactly where it snows.

How: `tools/port/worldgen/import.py` writes `data/worldgen/` (git-ignored); region builders and surface rules are
converted (`j2js.py`) and run against stand-ins (`regions.js`, `rules.js`) to get their exact tables. Ground blocks we
lack are matched to our nearest natural ground by look (`block_substitutes.json`).
In the game: `gen/land_biomes` (the region layers; the nearest climate box as Climate.RTree; the biome zoom),
`gen/surface_rules` (the rule interpreter and the column walk), and `OverworldGen` v15 (`surfaceV15`).

Not yet: cave biomes underground, underground biomes; river water tint and fish by temperature.

# Terrain version 18: no stripes
The original game's position hash (`hash32`: a weighted sum, then one xorshift-multiply) barely mixes its third input
(its weight is 2^31-1, so z and z+2 come out almost the same): bedrock came out in straight rows, villages two regions
apart got the same name, and the snow-line and frozen-sea noise (one murmur round) drew ruler-straight ice edges.
Version 18 uses `posHashF` (splitmix rounds) for the bedrock floor, icebergs, deep slate and rock, cave plants, and a
whole murmur mix for the snow line; village names come from a per-tile bijection (no two alike in 24 x 24 regions).
The "thermal" taiga (white, orange and yellow terracotta round its hot springs) is meant to look that way and stays.

# Terrain version 21: the land's whole top, and plain stone
Checked by tools, not by eye (`dq-gentool cmpsave / topcheck / profile / relief / xsect`):
- Generation is deterministic: 2180 saved chunks of a v20 world regenerate the same (only snow and plants differ).
- The density interpreter is complete: ImprovedNoise, PerlinNoise, NormalNoise, BlendedNoise, CubicSpline and every
  density type the data uses (all 28 types handled; every named function resolves).
- **Found 1: the land's top cut off flat.** `v14Corner` searched for the ground from ~20 blocks over its 2D shape, down.
  Where the 3D noise lifts the land higher (low `factor`: crags, windswept hills) the search began in an air gap and the
  land over it was never built (the fill places stone only up to that height): a flat top and a sheer wall. Version 21
  finds the top on the density lattice itself (density every 8 blocks up, blended between), from the sky down: against
  that lattice v20 had 32 corners in 16384 8+ blocks too low (worst 119), v21 none.
- **Found 2: our rock layers on every cliff.** Version 12's strata (a 200-block region of sandstone, claystone,
  quartzite... under the soil, its layer tops following the ground) showed on every cut as flat slabs and straight
  seams. Version 21: `strata` off, plain stone with dirt, gravel, granite, diorite, andesite and tuff blobs (the
  biomes' `ore_*` features; ours in `placeOres`); the metal ores (tin, zinc, bismuth, silver) keep their depths in any
  stone.
- Meant to be, not bugs: `crimson_patch` (crimson nylium in windswept gravelly hills, by its basalt columns); sugi
  pillars (sugi forest reaches down to the coast, continentalness -0.19, and its pillar fills through water: sea
  stacks); 10+ block cliffs where the density drops.

## Terrain 21 underground: the depths (deep worlds)
Underground places sit at display y: mineshafts -55..30 (0.3% of chunks), dungeons -50..50, ancient city -27 in the
Deep Dark, ancient temple -46..-36, stronghold 15, catacombs -40..20. Ours were all squeezed into the Overworld band by
owUnder (y 16..60). In a deep world (layout 2, terrain 21) a plan's y may run below the Overworld's floor and Underworld 1
builds the same plans a band lower (StructureIndex::build yShift, SF_DEEP kinds only), so shafts cross the seam:
mineshafts -55..30 on their own 256-block grid (mineshaft_deep, also under the sea; ~70 in 3000x3000, target ~100),
dungeons -50..50, the Deep Dark 60 rows lower (y -45..-11) with its cities, the Deep Halls and the Twin Vault.
Strongholds stay at y 15. The watcher's signs are no longer generated (the haunting leaves them).
