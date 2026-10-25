# Terrain generation (src/gen)

Port of the original's terrain for the four stacked bands and the two side realms.

| C++ | Original | What |
|---|---|---|
| `gen/overworld.cpp` | world/worldgen.js `column`, `generateOverworld`, `caveField/isCave`, `placeOres`, `odditiesFor/placeOddities`, `goodSpawn`; game/worldupdate.js (rivers, `riverFlow`, Ember Crust); game/metals.js (`placeOres`); game/deepdark.js (`deepZone`, `carveDeep`); game/distant.js (`distTop`, forest cover) | the Overworld |
| `gen/trees.cpp` | worldgen.js `treesForChunk`, `treeParts`, `writeTree` (+ structures.js tree filter) | all 13 tree kinds |
| `gen/decorate.cpp` | worldgen.js `decorate`, `decorate2`, `decorateCaves` | plants, flowers, sea life, caves, geodes |
| `gen/depths.cpp` | worldgen.js `generateDepths`, `depthsSolid`, `fungusTreesFor`; theend.js void crust | the Ember Depths |
| `gen/skylands.cpp` | worldgen.js `generateSkylands` | the Skylands |
| `gen/theend.cpp` | theend.js `endColumn`, `endPillars`, `endFountainY`, `chorusAt`, End `generate` | The End |
| `gen/realms.cpp` | worldgen.js `generatePocket`; riftarc.js `realmIslands`, `generateRealm`; registration | side realms, world types |
| `gen/structures.*` | structures.js `structAt`, `structsNearKind`, `sPut` (the machinery only) | the structure hook |
| `gen/gendefs.*` | core/util.js helpers, shapes.js `DIRX/DIRZ/WLOG` | vertical mapping, block ids, buffers |

nether2.js, aether.js and seamless.js have no terrain code (mobs, fortresses, the cloud-bank *fog*); the
fortress is a structure (later). World types: the original's create-world screen offers none, so there is
`default` (all four bands), plus `pocket` and `realm` for the side realms (`Level::world()` picks them), and
the old `debug` fallback.

## The vertical mapping (`gen/gendefs.h`, namespace `vmap`)
All noise, seeds and decisions are the original's, evaluated in the original's 128-tall units; only where
things end up in the 256-tall band changes. `o` = original y, `y` = band-local y.

**Overworld surface** (`owSurface`, inverse `owSurfaceInv`), d = o − 48:
- d ≥ 0: `y = 64 + d + 0.0145·d²` — plains barely change (o 56 → 72.9), mountains grow (o 72 → 96.4, snow line o 96 → 145, peak o 120 → 211).
- −4 ≤ d < 0: `y = 64 + d` — shores, river beds (1–4 deep) and shallows keep their exact depths.
- d < −4: `y = o · 60/44` — ocean floors deeper (o 30 → 40.9).
- The column's height is `floor(owSurface(h + frac))`, frac = the original's fractional height (0.5 when rivers or levelling changed it). The mapped height of every column lies in `[owSurface(h), owSurface(h+1)]`.

**Overworld underground** (`owUnderO`, `owUnder`, `owStretchLo/Hi`): uniform 4/3 stretch (o = 0.75·y), so
caves, ores, the slate layer, geodes and the Deep Dark (top o 34 → 45) fill the extra room. Caves use
`owCaveO(y, H, h)`: the uniform stretch deeper than 32 rows below the surface, blending to "the surface block
is the original's surface row" at the top (faded out where the two mappings differ by more than 8–16 rows,
high mountains), so caves open onto the land where they did in the original. Ores and rock blobs are walked in
original rows with the original random stream, each row written onto the 1–2 band rows it covers (veins 4/3
taller, same count, same places). Bedrock rows 0–3 are unscaled; row 0 is the Ember Crust (terrain v3+).
Surface layers (top/filler depths, plant heights, tree sizes) are block counts, not scaled. All elevation tests
(`h > 82` stone, `h > 96` snow, `h <= SEA+1` shores...) use the original height, so every column chooses
exactly what the original chose.

**Ember Depths**: `y = 2.5·o` (`NETHER_K`): the 100 rows fill the band (lava sea o 28 → 70, cavern floors
~o 34 → 85, roofs ~o 80 → 200). Noise is sampled at the original rows and linearly interpolated between them
(nCave1 and the rock-region noise on a 4-block grid; exact at the grid points and at the rows). Bedrock: rows
0 and 255, random at 1–2 and 253–254. Row 0 is the void crust (bedrock at 1–2 turns blackstone, as theend.js).

**Skylands**: `y = o + 120` (`SKY_DY`): islands keep their shapes (original 20..110 → 140..230, tops 180–200),
leaving the Overworld's sky open. The cloud bank between the Overworld and the Skylands is the original's
fog (seamless.js `cloudAt`), `vmap::cloudBank(dim, y)`: band 0..30 of the Skylands and 234..255 of the
Overworld; there are no cloud blocks in the original.

**The End**: `y = o + 64` (`END_DY`): main island top 61 → 125, pillars up to 167 (+cage), the stronghold
platform 48 → 112 (`EndGen::PLAT_Y`, `spawnPoint()`), exit portal bowl `fountainY()`.

**Side realms** keep the original y (pocket rooms 63..100, his realm 40..83) in the Overworld band.

## Fidelity (`tools/port/gen_compare.py`, `dq-gentool dump`)
`python3 tools/port/gen_compare.py <js-root> build <work-dir> hello dokuquest` runs the original headless,
picks one chunk per biome from a coarse biome map, and compares. Result for both seeds:
- heights, biomes, climate bytes: 100% (12 chunks, and a 60×60 grid spanning 3600 blocks with rivers, coasts
  and mountains, minus the ~1.5% of points the original's villages level, which is structure work); every
  mapped height inside its interval; tree lists (position, kind, size roll, bare, bees) identical.
  The script ends with `OVERALL OK`.
- top block and the block above it: 100% except a few columns in high mountains (an ore or cave there in the
  original, stretched differently here) and a mangrove case (below).
- Skylands island tops/bottoms 100% (+120) apart from the original's Skylands places (structures); End island
  shapes 100% (+64), pillars identical; Ember Depths solidity at the original rows 799/800 (grid interpolation).
- block histograms: stone/slate/ores/lava ≈ 1.3–1.5× the original (the stretched underground).

## Differences from the original (deliberate)
- **Trees across chunk borders**: the original checks the soil under a tree only when the tree's base is in
  the chunk being generated, so a tree on bad ground (or whose ground an earlier tree's root replaced) leaves
  half a crown in the neighbour. Here every chunk asks the column itself (top block + cave test), so both
  sides always agree. Trees are generated inside `generate()` from the 3×3 neighbourhood (no `populate()`).
- The JS quirk where dark oak leaf balls (half-block centres) lose a row at chunk borders is kept (same |0
  truncation), so canopies match the original.
- Ember Depths noise interpolated between original rows (2.5× stretch), weeping/twisting vine lengths and
  floor plants per band row; ores 10 veins walked in original rows and stretched; fungus trees found in band rows.
- Overworld ore/blob/geode positions follow the stretch; the geode's sphere and the emerald are placed at the
  mapped row (their random streams can diverge from the original after the first stretched blob).
- Structures run before the Deep Dark carve, like the original's wrapper order; the Ember Crust before both.
- `Level::world()` seeds side realms with `seed + realm*1013`; the original used the world seed (only his
  realm's floating islands and crystals depend on it).

## Performance (`dq-gentool bench hello 36`, thread CPU time, one core)
Overworld ~1.2 ms/chunk, Ember Depths ~4 ms, Skylands ~0.2 ms, End ~0.1 ms. `sample()`: Overworld ~0.7 µs/column,
Depths ~2 µs, Skylands/End ~0.2 µs.

## APIs for other areas
- `DimGenerator::sample(x, z, ColumnSample&)`: height, water/fluid, top/under, biome, `temp/humid` climate
  bytes (tint = `climateTint(kind, temp, humid)` on the client; `tint` is left 0xFFFFFFFF), forest cover as
  `tree`/`treeTop`/`leaves` (distant.js far tiles), `floatBottom` for islands (Skylands, End) and the Depths' roof.
- `DimGenerator::trees(cx, cz, out)`: the real trees of a chunk, column by column (distant.js near tiles).
- `DimGenerator::terrain()` → `TerrainQuery`: raw `terrainColumn(x,z)` (band y, water, biome, climate, the
  original's height, river), `solidAt`, `finalSurfaceY` (with levelling).
- `OverworldGen` (cast `wg.forDim(DIM_OVERWORLD)`): `column`, `biomeAt`, `goodSpawn` (spawn search),
  `riverFlow` (the river current, worldupdate.js `riverPush`), `deepZone` (warden/sculk systems),
  `surface`, `treesForChunk`, `treeGroundOk`. Saplings: `treeParts(B, seed, TreeSpec{..., ySeed = y}, parts)` +
  `writeTreeParts` or write the parts through World.
- `DepthsGen::solidAt/densityColumn` (fortress placement, ghast spawning), `SkylandsGen::islandTop` (skyLanding
  in seamless travel), `EndGen::island/pillars/fountainY/spawnPoint/PLAT_*` (dragon fight, End portal),
  `PocketGen::center(slot)`.
- `vmap::*`: converting the original's y constants (owUnder for dungeon/stronghold depths, owSurface for
  surface heights, netherY, SKY_DY, END_DY), `vmap::cloudBank(dim, y)` for the sky/fog renderer and slow-fall.

## The structure hook (`gen/structures.h`)
```cpp
StructureType t; t.key = "nether_fortress"; t.name = "a Nether Fortress";
t.region = 520; t.chance = 0.75; t.margin = 60; t.dims = 1u << DIM_NETHER; t.flags = 0;   // SF_SURFACE, SF_NO_CARVE
t.plan = [](const TerrainQuery& q, int x, int z, Mulberry32& rnd, StructurePlan& s) {   // raw terrain only
  s.x0 = x - 40; s.x1 = x + 40; s.z0 = z - 40; s.z1 = z + 40; s.y = vmap::netherY(34); s.cx = x; s.cz = z; return true; };
t.build = [](ChunkWriter& P, const StructurePlan& s) { P.fill(...); P.put(x, y, z, "nether_bricks"); P.blockEntity(...); };
t.flatten = ...; t.flattenPad = 12;          // optional: villages on level ground (weight 0..1 toward a band y)
registerStructure(t);                        // from an explicit registerXxx() called by registerWorldTypes()
```
Placement = structures.js `structAt`: region `(rx, rz)` of `region` blocks, `Mulberry32(lairSeed(seed, rx, rz,
9100 + type*37 + jsDim))`, `rnd() < chance`, candidate `region*rx + margin + floor(rnd()*(region-2*margin))`,
then `plan()`; plans overlapping an earlier-registered type (within its `clearance` + radius) are dropped.
`s.id`/`s.seed` match the original (`s<k>_<jsDim>_<rx>_<rz>`, `lairSeed(.., 9200 + k)`). Each band generator owns a
`StructureIndex` (`structures`): `at`, `near`, `flagged`, `flatten`; `build()` is called during `generate()`
on the worker thread, so plan/build/flatten must be thread-safe and deterministic. `ChunkWriter` = the
original `sPut` (`in/put/get/fill/h/pick/ground/blockEntity`), band-local y. Surface structures
(`SF_SURFACE`) keep trees 3 blocks off; `SF_NO_CARVE` keeps the Deep Dark out (ancient cities).

## Left for later
- Structures: done, see docs/port/structures.md (the played-land books, structBook etc., are not kept).
- genVersion < 3 worlds (worldgen_v1.js) are not ported; every C++ world uses the 0.9 (v5) terrain.
- Spawn search could use `OverworldGen::goodSpawn` (client/session.cpp uses `sample()` water < 0).
