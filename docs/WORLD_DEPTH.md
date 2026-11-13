# The deep world (layout 2): the Underworld and the open sky

The user's chart: Aether 768..1024, the Overworld 0..512, the Underworld -1..-1024 (cave biomes, tombs and ruins, a
depth chosen per world), the Nether below it, the End below that. Minecraft's own depth (y -64..-1, deepslate,
diamonds at -58) is the top of the Underworld.

## Design: every dimension stays ONE 256-tall band
The engine everywhere assumes "a Chunk is one dimension's band": ChunkKey.dim is the dim, local y = y - dimBase(dim),
generators fill one band. So new depth is added as NEW one-band dimensions, never as extra bands of an old one:

| dim id | name            | layout 1 position | layout 2 position (bottom up) |
|--------|-----------------|-------------------|-------------------------------|
| 0      | The End         | 0                 | 0                             |
| 1      | Ember Depths    | 1                 | 1                             |
| 4..7   | Underworld 1..4 | -                 | 2..5 (4 deepest at 2; 1 under the Overworld at 5) |
| 2      | Overworld       | 2                 | 6                             |
| 8, 9   | Sky 1, 2        | -                 | 7, 8 (open air)               |
| 3      | Skylands        | 3                 | 9                             |

- A world's layout is chosen when it is made (LevelInfo "layout"; old saves: 1 = today exactly). One world is open
  per process (host and guests agree: the host sends it). `dims.h`: the tables posOfDim / dimAtPos, worldH(),
  dimOfY(y) = dimAtPos[y >> 8], dimBase(d) = posOfDim[d] * 256; inWorldY uses worldH(). A layout-1 world: every
  answer identical to before (the tests prove it).
- The Underworld's depth (1..4 bands, default 4) is a world option; fewer bands = fewer positions.
- Display y (F3, coordinates, commands' read-outs): layout 2 shows Minecraft's numbers: y - (dimBase(Overworld) + 1)
  (sea level 63, diamonds at -58, the Underworld down to -1025, the Skylands from 767). Layout 1: as before.
- Region files: region/d<dim id>/ (new dims, new folders; nothing old moves). LOD cache tag bumped for layout 2.
- `landOf(d)`: the Underworld and the Sky are the Overworld's for what is the Overworld's (its sky, weather, day,
  music, the Overworld's mobs underground...). Gameplay checks that mean "the surface" keep DIM_OVERWORLD.

## Generation (terrain version 19 = layout 2)
- The Overworld band: no bedrock floor, no Ember Crust row (its land goes on down).
- Underworld 1 (Minecraft y -257..-2): Minecraft's land continued (final_density, its caves) to y -64 in deepslate
  (the surface rule's deepslate gradient), its ores at their Minecraft heights (diamonds -64..16, deepslate ores),
  the Deep Dark (ancient cities) in the deep; below -64: our deep caverns.
- Underworld 2..4: deep caverns (cave biomes), lava, tombs and ruins; bedrock floor at the very bottom over the Nether.
- Sky 1, 2: air.

## Progress ledger (one line per step; newest last)

## Also to do with the depth work (the user's reports, 2026-11-13)
- The Nether (Ember Depths): the same striped banding as the Overworld had; not every biome appears; trees stacked
  one on another (the tall many-tiered crimson trees); some plants drawn wrong (the mushroom-like ones).
- The End: a lantern shown as a full block (hanging from a chain); the purple banner/cloth hangings, the chorus-like
  pillars' tops and other things drawn as plain blocks or wrong shapes.
- Step 1 (layout tables): dims.h/dims.cpp layouts, dimOfY/dimBase by table, WORLD_H per world, dimAbove/dimBelow, light posKey 12-bit y, sky seeding off in the Underworld; layout 1 identical (12/12 tests).
- Step 2 (the Underworld): gen/underworld.cpp, terrain version 19, new worlds layout 2; seams (light, water, the crust shaft, sky blend, clouds, weather, spawning, ropes, villagers), display y, /tp y; tools/test_deep (21 checks) + 12/12.
