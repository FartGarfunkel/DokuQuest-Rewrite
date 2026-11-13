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
