# src/gen/be — Better End 3.2.5 End terrain and biome layout (standalone)

A C++20 port of Better End 3.2.5's End generator (github.com/quiqueck/BetterEnd, MIT): the floating-island density
(`TerrainGenerator`, `IslandLayer`, `LayerOptions`, `GeneratorOptions`), the land/void decision
(`EndLandBiomeDecider`), and the BCLib End biome source it runs on (`BCLibEndBiomeSource` with the HEX map).
There are no engine dependencies: nothing here includes engine headers or touches `World`. Spec:
`docs/research/better_end.md` §2, §3 and §3.8.

| file | what |
|---|---|
| `be_noise.h/.cpp` | `JavaRandom` (java.util.Random / LegacyRandomSource), `OpenSimplexNoise` (BE's class, 2D + 3D), `beHash` |
| `be_biomemap.h/.cpp` | biome table (`kBiomes`), `BiomePicker`, `HexBiomeMap` (BCLib reconstruction) |
| `be_terrain.h/.cpp` | `BETerrain`: island layers, density, `chunkDensity`, `isLand`, `land`, terrain-height averaging |

## API (`namespace dq::be`)

```cpp
explicit BETerrain(int64_t worldSeed, const BETerrainOptions& opt = {});
void  chunkDensity(int cx, int cz, float* out) const;          // 16*256*16, out[(y<<8)|(z<<4)|x], OUR y; > 0 solid
void  cornerColumn(int cellX, int cellZ, float out[33]) const; // fillTerrainDensity of one 8-block corner column (BE y = 4*i)
bool  isLand(int x, int z) const;                              // TerrainGenerator.isLand for the 8x8 column (block coords)
int   land(int x, int z, int* vanillaKind = nullptr) const;    // 0..18 our land index, -1 vanilla, -2 the_end
void  chunkLand(int cx, int cz, int8_t out[256], int8_t* vanillaKind = nullptr) const;  // land() for a chunk
int   biome(int x, int z) const;                               // BiomeId 0..23 (0..18 = land index)
int   biomeQuart(int qx, int qz) const;                        // BCLibEndBiomeSource.getNoiseBiome(qx, 0, qz)
static double terrainHeight(int land);                         // -1/-2 -> 0.1 (vanilla default)
float avgTerrainHeight(int qx, int qz) const;                  // getAverageDepth (29 quart offsets)
void  warp(double cellX, double cellZ, double& px, double& pz) const;   // the ±35-block domain warp (cell units in, blocks out)
float islandDensity(double px, double py, double pz, float h) const;    // max of the 3 layers' -SDF at a warped point (BE y)
float density(double px, double py, double pz, float h) const;          // + 3D roughness + top fade
int   ourY(int beY) const; int beY(int ourY) const;
// tools/tests: islandsNear(), islandOwnDensity(), layerStats(), layerDistance()
```
`vanillaKind`: `V_HIGHLANDS 0, V_MIDLANDS 1, V_SMALL_ISLANDS 2, V_BARRENS 3, V_THE_END 4` (`V_NONE -1` for BE biomes).

Biomes are per 4×4 quart, as in Minecraft. Each quart is sampled at its min corner and there is no fuzzy zoom.
`h` (the island-top noise strength) is `avgTerrainHeight(cellX*2, cellZ*2) * 0.5`.

**Threads:** every query is const and safe to call from any number of threads at once. The isLand cache
(16 KiB sections of 128×128 cells, cleared above 512 sections) and the hex-map chunk cache (1 KiB per ~1.2×1 km,
never evicted) are behind `shared_mutex`es. A 4-thread run on a fresh instance gives the same bytes as a 1-thread run.
Build one `BETerrain` per world seed and share it. Construction is about 0.2 ms.

## Y mapping
`ourY = beY + yOffset` (default 56, from §3.8). BE's noise column is y 0..127, so ours is 56..183. `chunkDensity`
writes -1 everywhere outside that range. The top fade starts at BE y 100 (our 156) and reaches -1 at BE y 128.
Island roots that would reach below BE y 0 are cut flat at our 56, as BE's are at the world bottom.

## Tables

Layers (`LayerOptions`; derived exactly as BE does: `minY/maxY = avg ± var` of 128, `threshold = 0.9999 − 2·coverage`,
`centerDist = floor(voidRing/distance)`):

| layer | distance | scale | averageHeight | heightVariation | centre BE y | coverage → threshold | central island |
|---|---|---|---|---|---|---|---|
| big | 300 | 200 | 0.546875 | 0.078125 | 60–80 | 0.5 → −0.0001 | no |
| medium | 150 | 100 | 0.546875 | 0.15625 | 50–90 | 0.5 → −0.0001 | yes, (0,64,0), s = 1.3 |
| small | 60 | 50 | 0.546875 | 0.234375 | 40–100 | 0.5 → −0.0001 | no |

Biomes (`kBiomes`; BE values come from `data/betterend/worldgen/betterx/biome/*.json`; every `edgeSize` is 0):

| idx | id | pool | genChance | terrainHeight | parent |
|---|---|---|---|---|---|
| 0 | chorus_forest | land | 1.0 | 0.1 | |
| 1 | blossoming_spires | land | 1.0 | 0.1 | |
| 2 | foggy_mushroomland | land | 1.0 | 0.1 | |
| 3 | glowing_grasslands | land | 1.0 | 0.1 | |
| 4 | crystal_mountains | land | 1.0 | 0.1 | |
| 5 | sulphur_springs | land | 1.0 | **0.0** | |
| 6 | umbrella_jungle | land | 1.0 | 0.1 | |
| 7 | shadow_forest | land | 1.0 | 0.1 | |
| 8 | ice_starfield | **void** | **0.25** | 0.1 | |
| 9 | amber_land | land | 1.0 | 0.1 | |
| 10 | megalake | land | 1.0 | **0.0** | |
| 11 | lantern_woods | land | 1.0 | 0.1 | |
| 12 | painted_mountains | sub | 1.0 | 0.1 | dust_wastelands |
| 13 | neon_oasis | sub | **0.5** | 0.1 | dust_wastelands |
| 14 | dragon_graveyards | land | **0.1** | 0.1 | |
| 15 | dry_shrubland | land | 1.0 | 0.1 | |
| 16 | dust_wastelands | land | 1.0 | **1.5** | |
| 17 | umbra_valley | land | 1.0 | 0.1 | |
| 18 | megalake_grove | sub | 1.0 | **0.0** | megalake |
| 19 | minecraft:the_end | centre | 0.5* | 0.1* | |
| 20 | minecraft:end_highlands | land | 0.5* | 0.1* | edge end_midlands 8* |
| 21 | minecraft:end_midlands | land | 0.5* | 0.1* | |
| 22 | minecraft:small_end_islands | void | 0.5* | 0.1* | |
| 23 | minecraft:end_barrens | barrens | 0.5* | 0.1* | |

`*` marks BCLib wrapper values recalled from BCLib's source, not read from the jar. The terrainHeight effect:
- megalake, megalake_grove and sulphur_springs give 0, so their tops are flat (no radial noise).
- dust_wastelands gives `h` up to 0.75: large mounds within ~0.29 units of the island centres (dunes), flattened by the
  top fade where they rise past BE y 100.
- Everything else gives `h` = 0.05: gentle bumps.

## What is exact and what is reconstructed

**Ported from the jar, Java-exact (float vs double as in the bytecode):**
- **Noise and RNG:** OpenSimplexNoise 2D/3D, java.util.Random, the seed order (`nextInt()` for big, medium, small,
  noise1, noise2) and `beHash`.
- **Island placement:** `updatePositions`, i.e. the 3×3 grid cells, the centre skip `px²+pz² <= centerDist`, the
  per-cell LCG draws, the coverage noise keep test, the void-ring removal (`x²+z² < voidRing²` when
  `|ix|,|iz| < voidRing>>3`) and the central island. Also BCLib's `MHelper.floor`, which is one too low at exact
  negative integers, as BE uses it.
- **Per-island values:** the scale `nextFloat()+0.5` reseeded by the island position, and the radial-noise offset
  `x & 32767`.
- **Density:** `fillTerrainDensity`, i.e. the warp, the 33 corners at y = 4i, the `dist > 1` layer short-circuit, the
  3-octave 3D roughness and the top fade, plus vanilla NoiseChunk's trilinear interpolation (y, then x, then z).
- **isLand:** warp at the cell centre (cell + 0.5), y 0..124, threshold −0.01.
- **getAverageDepth:** the 29 offsets, `|o|/3` weights normalised in float, and the `< 0.1` centre check.
- **Biome decision:** the decider, i.e. land gives LAND (CENTER stays CENTER) and void gives VOID (CENTER becomes
  BARRENS), with centre = block `x²+z² <= voidRing²`.
- **Checked against Java 25 running the jar's own class:** 36,000 random 2D/3D noise evals over 9 seeds plus lattice
  points and Random draws are bit-identical. All 9,198 island candidates (positions, heights and scale bits) of the
  three layers in an 80×80-cell window match a Java transcription of `updatePositions` that uses the jar's noise, as
  do the warp values.

**Reconstructed (BCLib 2.3 is not in the jar; written from the spec and from BCLib 2.x as remembered):**
1. **`SDFCappedCone`:** Inigo Quilez's exact capped cone.
2. **`SDFSmoothUnion`:** `h = clamp(.5+.5(b−a)/r,0,1)`, `lerp(h,b,a) − r·h(1−h)`.
3. **`SDFScale`, `SDFTranslate`, `SDFDisplacement`:** `f(p/s)·s`, `f(p−t)` and `f(p)+g(p)`.
4. **`SDFRadialNoiseMap`:**
   - `d = |xz|/radius`; the result is 0 if `d > 1`, else `(1−d)·N(rot0.5(xz/radius)·0.75 + offset)·intensity`.
   - `N = n + .5·n(3p+(1000,0)) + .2·n(9p+(1000,0))`.
   - Assumptions: BCLib's OpenSimplex is the same algorithm as BE's, seeded with the layer seed; sin/cos(0.5) come
     from Minecraft's `Mth` table.
5. **The radial-noise strength `isLand` sees:** BE reuses whatever the last fill left in the shared SDF object, which
   is non-deterministic. This port uses a fixed `isLandHeight` = 0.05, the common value.
6. **The pools and their order:**
   - Land: the 15 BE parents plus end_highlands and end_midlands. Void: ice_starfield and small_end_islands.
   - Centre and barrens: the_end and end_barrens.
   - Order: registry order, assumed to be the resource path sorted.
   - Vanilla wrapper weights: genChance 0.5, with end_highlands edged by end_midlands 8 blocks wide.
7. **`BiomePicker` / `WeighTree` / `WeightedList`:** one `nextFloat()·total` per pick. A sub-biome list is
   `[self 1.0, children by genChance]`.
8. **`HexBiomeMap` / `HexBiomeChunk`:**
   - Cells are `size/8` blocks (32 for size 256). Map chunks are 32×32 cells seeded with `beHash(seed, cx, cz)`.
   - Each chunk starts from 16 jittered seeds, one per 8×8 cell block, each painting a 7-cell hexagon. Every pass,
     each painted cell grows into one random neighbour until the chunk is full. The rim then copies the cells two in,
     and each cell has a 1-in-4 chance to paint a 7-cell sub-biome hexagon.
   - The lookup point is scaled (columns √3/2), rotated 0.4 rad, warped by `min(ceil(log2 cell),5)` fractal octaves
     (amplitude `hex.warp`) and assigned to the nearest hex centre.
   - Land and void maps share the world seed.
   - Edge biomes are tested on 8 points of a circle of radius `edgeSize`.
9. **Not BCLib:** `hex.seamWarp` adds one extra octave at 1/4 cycle per cell to bend the straight map-chunk seams this
   reconstruction otherwise shows every ~1 km. Set it to 0 to drop it.

Data points that confirm the reconstruction behaves as it should:
- Over a 48 km square, every genChance-1 land biome takes 6.5–6.9 % of land (1/15.1 = 6.6 %) and
  dragon_graveyards 0.58 % (0.66 %).
- Each parent family totals about 6.6 %: dust + painted + neon 6.8 %, megalake + grove 6.5 %, highlands + midlands
  6.7 %.
- ice_starfield is 33.7 % of the void (expected 1/3).
- Biome regions are about 200–350 blocks across with ragged edges.

## Tuning knobs (`BETerrainOptions`)
| option | default | meaning |
|---|---|---|
| `big/medium/small` | table above | `distance`, `scale`, `averageHeight`, `heightVariation`, `coverage`, `hasCentralIsland` (BE `generator.json`) |
| `voidRing` | 1000 | central empty ring (BE `voidRingSize` and BCLib's inner void). BE hardcodes `centerDist` to 1000; here it follows `voidRing` |
| `centralIsland` | true | `generateCentralIsland` |
| `yOffset` | 56 | ourY = beY + yOffset |
| `landBiomeSize` / `voidBiomeSize` | 256 / 256 | BCLib hex map sizes |
| `vanillaBiomes` | true | keep end_highlands / end_midlands / small_end_islands in the pools |
| `isLandHeight` | 0.05 | radial-noise strength in isLand (see 5 above) |
| `hex.warp`, `hex.seamWarp`, `hex.subOneIn` | 0.5, 1.5, 4 | hex map warp, seam bending, sub-biome rate |
| `pruneIslands` | true | skip islands whose bounding-cylinder lower bound cannot lower the min SDF. The output is identical (checked bitwise on 1,858 chunks); false runs BE's plain loop |

## Validation (seed 1234567890123; tools live in the session scratch dir, not in the repo)

**Island candidates:**
- 49.9 %, 50.0 % and 49.6 % of grid cells keep their island (big, medium, small).

**Island shapes (each island's own SDF, h = 0.05):**

| layer | radius (blocks) | root depth | dome above centre |
|---|---|---|---|
| big | 49–150, median 101 | 30–90 | ≤ 26 |
| medium | 24–74 | 14–44 | |
| small | 12–37 | 8–22 | |

Real terrain adds about 0.035 units of roughness (+7 blocks on big islands) and moves islands by the ±35-block warp.

**Void ring:**
- Inside r < 850 the only solid is the central island (max r 81).
- Islands whose centres lie outside 1000 reach into the ring by up to their radius, as in BE.

**Coverage and tops:**
- 50.6–51.4 % of columns outside the ring are solid. isLand says land for 53–54 % of 8×8 cells; at 9 of 16,456 and
  25 of 65,536 solid cell centres it says void.
- Top surface (our y): p5 107, p25 126–130, median 138, p75 146, p95 155, i.e. BE y 51/70–74/82/90/99.
- These sit a little above the spec's "60–80": the formula gives centre + dome + 0.035·scale of roughness, and the
  top-down view picks the highest of the stacked islands.

**Connected land masses:** median equivalent radius 30, p90 70, max 230 (merged clusters).

**Shapes:**
- Flat tops with rounded rims and inverted-cone undersides.
- Megalake and sulphur tops are flat.
- Dust wastelands have the roughest tops, with raised mounds near island centres.

**Performance:**

| measurement | cost |
|---|---|
| `chunkDensity`, one thread, other builds loading the 2 cores | 0.7–1.6 ms per chunk |
| — of which the 9 corner columns | most |
| — of which the 121 quart biome lookups | about 0.1 ms |
| `land()` | about 1 µs |
| `chunkLand()` | about 13 µs per chunk |
| `isLand` on a cold cell | about 50 µs, then cached |

A corner-column cache shared between neighbouring chunks would save about another 50 %, if ever needed.
