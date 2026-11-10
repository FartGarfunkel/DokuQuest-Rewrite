# BTA v8.0.1 Nether — exact spec for DokuQuest

Sources: CFR decompiles of `/home/admin/Documents/Projects/BTA/jarmods/bta-v8.0.1/` (copies in `research_nether/src/`,
`research_nether/vis/src/`) and `/tmp/bta-nether/`. **[H]** = measured with Java harnesses that call BTA's own noise
classes with identical arguments (`research_nether/harness/*.java`, run `java -cp <bta jar dir> X.java <seed> ...`).
Biome map, seed 1, 1 px = 16 blocks, 16384² blocks: `research_nether/biome_map_seed1_16k.ppm`.
`research_nether/` = `/tmp/claude-1000/-home-admin/74cc0717-0b56-4036-a95b-6d49f35e7e9b/scratchpad/research_nether/`.

**Height correction:** the brief assumed BTA's Nether is 128 tall. That holds only for `minecraft:nether.beta_173`
(bounds 0..127, sea 32). The default `minecraft:nether.default` (`WorldTypes.NETHER_DEFAULT`) is `bounds(0, 255, 96)`,
`portalBounds(63, 255)`, `withCeiling()`, `withNoAurora()`, ocean blocks `{lava still, water still}`, filler
`COBBLE_NETHERRACK`: 256 tall like our band, so **band-local y = BTA y (scale 1:1)**. Clock set to 72000 at creation,
celestial angle fixed 0.5, sky darken 0, coordinate scale 1/8 (`Dimension.NETHER`). (beta_173 would scale ×2.)

---------------------------------------------------------------------------------------------------------------------
## 1. Generation

### 1.0 Pipeline (`ChunkGeneratorPerlin.doBlockGeneration` + `ChunkGenerator.generate/decorate`)
1. Biomes per section in 16×2×16 cells (8 blocks tall): `getBiomes(..., cx*16, sectionY*16, cz*16, 16, 2, 16)`, altitude
   `(sectionY*16 + yy)/128` → every block below y 64 has altitude < 0.5.
2. Density 5×33×5 (`DensityGeneratorNether`) → trilinear lerp over 4×8×4 cells (`TerrainGeneratorLerp`) →
   `TerrainGeneratorNether.getBlockAt`.
3. Surface: `SurfaceGeneratorNether`.
4. Large features in order: `CavesLargeFeature(64,256)` (radius 8 chunks), `LavaFloeLargeFeature`,
   `RubyglassFloeLargeFeature`, `ShelfLargeFeature` (radius 0, own chunk only).
5. `ChunkDecoratorNether`: each decoration gets its own `Random(cx*A + cz*B ^ worldSeed ^ id.hashCode())` (A, B odd
   longs from `Random(worldSeed)`). Biome mask = **one biome per chunk**, read at `(cx*16, 127, cz*16)`. All x/z picks
   are `cx*16 + rand(16) + 8`. Then `postDecorate` (ash layer).

### 1.1 Biomes (`Biomes.java`, `BiomeProviderNether`)
| key (`minecraft:nether.*`) | lang name | debug colour | top / filler (SurfaceProperties) | tags | class |
|---|---|---|---|---|---|
| `crag` | The Crag | #633E3D | cobble netherrack / cobble netherrack | – | BiomeNether |
| `volcanic_islands` | Volcanic Islands | #FFA333 | brimsand / brimstone | – | BiomeVolcanoIslands |
| `sulfur_pools` | Sulfur Pools | #A39940 | brimstone / brimstone | HAS_SULFUR_POOLS | BiomeSulfurPools |
| `crystal_plains` | Crystal Plains | #AA0000 | cobble netherrack ×2 | HAS_CRYSTAL_VEINS | BiomeCrystalForest |
| `crystal_forest` | Crystal Forest | #DD0000 | cobble netherrack ×2 | HAS_CRYSTAL_VEINS | BiomeCrystalForest |
| `old_world` | The Old World | #636480 | block of ash / block of ash | HAS_SURFACE_ASH | BiomeNether |
| `old_world.desert` | (key "nether.old_world") | #5C4843 | soul sand / soul sand | HAS_SURFACE_ASH | BiomeNether |
| `shelf` | The Shelf | #968A6F | cobble netherrack ×2 | – | BiomeNether |
| `legacy.hell` | Beta Hell | — | cobble netherrack ×2 | – | (beta_173 type only) |

- Every Nether biome spawns only Ghast (weight 10) and Zombie Pigman (weight 10).
- Rain, snow, storm and fog weathers are all blocked.

**Climate** (`getBiomes`). Noises are seeded by world seed; all are `FractalNoise` (octave 0 at frequency 1, then
frequency × lacunarity and amplitude × 2 per octave):
```
tN = FractalNoise3D(SimplexNoise.genOctaves(seed*9871, 4)), lacunarity 0.25, region scale 1/60
hN = FractalNoise3D(SimplexNoise.genOctaves(seed*39811,4)), lacunarity 1/3,  region scale 1/30
fN = FractalNoise2D(SimplexNoise.genOctaves(seed*543321,4)), lacunarity 1/1.7 (0.5882352941176471), region scale 1/6
```
Each 2D simplex octave's `addRegion` itself does **scale /= 1.5; amplitude *= 0.55**. So the temperature octaves are
1/90, 1/360, 1/1440 and 1/5760 per block, with amplitudes 0.55, 1.1, 2.2 and 4.4. Observed ranges [H]: tN and hN ±7.
```
a = fN*1.1 + 0.5
t = (tN*0.15 + 0.7)*0.99 + a*0.01 ;  t = 1 - (1-t)^2 ; clamp 0..1     // folds back down when t > 1 (tN > ~2)
h = (hN*0.15 + 0.5)*0.998 + a*0.002 ; clamp 0..1
altitude = clamp(y/128, 0, 1) ; variety = 0
```
The lookup `BiomeRangeMap` → `BiomeRangeLookup(span 50)` quantises: `i = (int)(v*50)` and tests the range at `i/50`.
The last added matching range wins. Ranges are (Tmin, Tmax, Hmin, Hmax, Amin, Amax), all upper bounds exclusive.
Effective thresholds after quantisation are in brackets:
```
VOLCANIC_ISLANDS  T 0.95–1.0 [T>=0.96]            alt>=0.5
CRAG              T 0.75–0.95 [0.76–0.94], H<0.85 [H<=0.84]
SULFUR_POOLS      T 0.75–0.95,             H>=0.85 [H>=0.86]
SHELF             T 0.70–0.75 [0.70–0.74]
CRYSTAL_FOREST    T 0.45–0.70 [0.46–0.68], H>=0.10
CRYSTAL_PLAINS    T 0.45–0.70,             H<0.10
OLD_WORLD         T <0.45 [<=0.44]
OLD_WORLD_DESERT  any T/H, altitude <0.5 (y<64)   (added last → always wins below 64)
```
- `NETHER_BIOME_PATCH_SIZE_FRACTION`, `BIOME_STRETCH_FACTOR` and `TEMP_SCALE` are declared but unused.
- Below y 64 everything is Old World Desert. In practice that is **invisible**: y < 63 is solid bedrock.

**Shares, sizes and mean climate** [H] (seeds 1, 12345, −987654321; 16384² blocks):

| biome | share | mean T | mean H | typical patch width | largest connected |
|---|---|---|---|---|---|
| Volcanic Islands | 31–37 % | 0.987 | 0.50 | ~200 blocks | one network spanning thousands |
| Crag | 30 % | 0.884 | 0.42 | ~130 | |
| Crystal Forest | 11–14 % | 0.595 | 0.56 | ~115 | |
| Old World | 10 % | 0.258 | 0.50 | ~145 | |
| Shelf | 5–6 % | 0.731 | 0.50 | ~40 (thin bands between Crag and Crystal) | |
| Sulfur Pools | 5–6 % | 0.884 | 0.96 | ~80 | |
| Crystal Plains | 1.8 % | 0.593 | 0.025 | ~65 | |

The edges are frayed by `a`: ±0.15 on temperature through the 0.01 weight, which works out to a few blocks of dither.

### 1.2 Terrain density (`DensityGeneratorNether`)
```
minLimit = FractalNoise3D(ImprovedPerlinNoise.genOctaves(seed,16, pre 0))
maxLimit = FractalNoise3D(ImprovedPerlinNoise.genOctaves(seed,16, pre 16))   // "pre" = skip that many octaves' RNG
main     = FractalNoise3D(ImprovedPerlinNoise.genOctaves(seed, 8, pre 32))
```
Lacunarity 0.5 and persistence 2 (beta-style). Grid: xSize 5, zSize 5, origin `cx*4, 0, cz*4` in cell units.
`terrainHeight = 256`, so `ySize = 33`, `quarterYSize = 256/4/8+1 = 9`, `threeQuarterYSize = 192/8+1 = 25`.

The field is computed for dy = 0..24 and stored at rows `dy + 8` (rows 8..32 are y 64..256). Rows 0..7 stay 0.0.
Sampling scales:
```
main  getRegion(..., 8.55515, 34.2206, 8.55515)      // 684.412/80, 2053.236/60
limit getRegion(..., 684.412, 2053.236, 684.412)
off[dy]  = cos(dy*PI*6/25)*2
edge     = dy>12 ? 24-dy : dy ;  if edge<4: off[dy] -= (4-edge)^3 * 10
lo = minLimit/512, hi = maxLimit/512, m = (main/10 + 1)/2
d  = m<0 ? lo : m>1 ? hi : lo + (hi-lo)*m ;  d -= off[dy]
if dy > 21: k=(dy-21)/3 ; d = d*(1-k) - 10*k
```

**Offsets per row** (subtract these, so negative means solid). The cos term:

| dy (y) | 0 (64) | 1 (72) | 2 (80) | 3 (88) | 4 (96) | 5 (104) | 6 (112) | 7 (120) | 8 (128) | 9 (136) | 10 (144) | 11 (152) | 12 (160) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cos term | 2.0 | 1.46 | 0.13 | −1.28 | −1.98 | −1.62 | −0.38 | 1.07 | 1.94 | 1.75 | 0.62 | −0.85 | −1.86 |

The rows mirror about dy 12 (13 = −1.86, 14 = −0.85, … 24 = 1.46). Edge walls: dy 0/24 −640, 1/23 −270, 2/22 −80,
3/21 −10. A negative cos term means more air at that height: bands of open space near y 96, 160 and 224.

**`getBlockAt`**, in order (the rand is seeded per chunk with `cx*341873128712 + cz*132897987541`; calls follow the lerp
loop order blobX, blobZ, blobY, yInBlob, xInBlob, zInBlob):
```
y < 63                         -> BEDROCK
y >= 255 - rand.nextInt(10)    -> BEDROCK      (y 255 100%, 254 90% … 246 10%)
y <= 63 + rand.nextInt(10)     -> BEDROCK      (y 63 100%, 64 90% … 72 10%)
density > 0                    -> COBBLE_NETHERRACK (world filler)
y < 96 (ocean) -> fluid by biome at (x, 96, z):
    Volcanic/Sulfur/Crag -> LAVA still
    Crystal Forest/Plains -> WATER still, except y==95 -> RUBYGLASS_CRYSTAL (a crust over the water)
    Shelf -> NETHERRACK or COBBLE_NETHERRACK 50/50 (solid "sea")
    Old World -> OBSIDIAN (solid "sea");  Desert/other -> AIR
else AIR
```

**Resulting shape** [H] (24×24 chunks, two seeds):
- Solid share by height:

  | y | 63–83 | 84–87 | 88–91 | 92–95 | 96–131 | 132–171 | 172–203 | 204–231 | 232–235 | 236–239 | ≥240 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | solid % | 100 | 95–98 | 68–83 | 59–71 | 48–55 | 54–58 | 47–53 | 54–74 | 83–89 | 95–99.6 | 100 |

- The cave floor is pinned at about **y 84–90** by the cubic edge, so the open sea (lava/water/rubyglass) is only
  **6–12 deep**.
- Exposed sea surface at 95/96 in **37–45 % of columns**.
- Vertical open-run lengths: 1–5 = 15 %, 6–15 = 30 %, 16–31 = 25 %, 32–63 = 16 %, ≥64 = 13 % (median ≈ 16).
- Air share at y 100–254 is 0.476. P(air with solid directly above) = 0.016. P(air with non-air below) = 0.013.
- In effect it is a 3D beta-Nether sponge from about 84 to 238, flooded to 95. Pictures: `harness/slice_<seed>.ppm`
  (x–y cross-sections).

### 1.3 Surface (`SurfaceGeneratorNether.generateSurface`)
Per column, using `rand = Random(cx*341873128712 + cz*132897987541)`:
```
sand  = beachNoise.getRegion(x,z,0; 16,16,1; 1/32,1/32,1)   + rand.nextDouble()*0.2 > 0   // beach = sand || gravel
grav  = beachNoise.getRegion(x,109.0134,z; 16,1,16; 1/32,1,1/32) + rand.nextDouble()*0.2 > 0
soil  = (int)(soilNoise.getRegion(x,z,0; 1/16 ...)/3 + 3 + rand.nextDouble()*0.25)       // usually 2..4
```
Noise seeds: beach `IPN(seed,4,pre 40)`, soil `IPN(seed,4,pre 44)`. A `main` IPN(seed,8,pre 32) is also created but
unused.

Scan y = 255 → 0 (biome from the chunk's 8-tall cells):
- **Air** sets `depth = -1`. top/filler are taken from the biome at the first air block, once per column.
- **Any non-air block that is not the filler** (lava, water, bedrock, netherrack, the rubyglass crust) is skipped
  without touching `depth`.
- **First filler under open space** (`depth == -1`):
  - `soil <= 0`: top = AIR, and filler by biome: Volcanic **BRIMSAND**, Sulfur **SULFUR**, Old World and Desert
    **GLOOMSTONE / COBBLE_GLOOMSTONE 50/50**, otherwise **NETHERRACK**.
  - else if `92 <= y <= 97`: biome top/filler, but if beach then **SOUL SAND / SOUL SAND**.
  - else if `y <= 96` and the biome has HAS_SULFUR_POOLS: top **BRIMSAND**, filler **BRIMSTONE**.
  - If `y < 96` and top == AIR: top = LAVA.
  - Then `depth = soil + 2`. Place **top if y >= 95, otherwise filler**: floors below the sea line get the filler.
- `depth > 0`: place filler, `depth--`. In the Old World, when an ash filler runs out, `depth = rand(8)+2` and
  filler = **SLATE**.
- `depth == 0` (deep rock) by biome:
  - Crag and Shelf: **NETHERRACK / COBBLE_NETHERRACK 50/50**
  - Crystal: **COBBLE_NETHERRACK only**
  - Volcanic: **BRIMSAND**
  - Sulfur: **BRIMSTONE**
  - Old World and Desert: **GLOOMSTONE / COBBLE_GLOOMSTONE 50/50**
- `mixStoneVariant` (applies to top and filler only): in Crag, Shelf and Crystal, cobble netherrack becomes
  netherrack or cobble 50/50; in the Old World, gloomstone stays a gloomstone/cobbled 50/50 mix.

Resulting columns, top → down (top block ~1, filler soil+2 ≈ 4–6, then deep rock):

| biome | top | filler | below | deep |
|---|---|---|---|---|
| Crag, Shelf | rack/cobble speckle | rack/cobble speckle | – | rack/cobble |
| Crystal | rack/cobble speckle | rack/cobble speckle | – | cobble (crystalline veins added later) |
| Volcanic | brimsand | brimstone | – | brimsand |
| Sulfur | brimstone (brimsand at/below the sea) | brimstone | – | brimstone (+ sulfur pockets) |
| Old World | ash | ash | slate 2–9 | gloomstone/cobbled gloomstone |

Shores at y 92–97 are about 75 % soul sand. Caves later re-top cut floors: if the block under a carved cell equals the
biome filler, it becomes the biome top.

### 1.4 Large features
- **Caves** (`CavesLargeFeature(64,256)`, the classic beta overworld carver, not MapGenCavesHell):
  - 1 chunk in 15 starts `rand(rand(rand(40)+1)+1)` tunnels at `y = 64 + rand(rand(184)+8)`.
  - 1/4 chance of a hub room (radius 1–7, height ×0.5) with +rand(4) extra branches.
  - Tunnel radius `rand*2 + rand`, length 112 − rand(28), branching at a quarter to three quarters of the length.
  - Clamped to y 65..247.
  - A segment that would touch lava or water (the ocean block list) is skipped.
  - Cuts only blocks tagged `CAVES_CUT_THROUGH`: netherrack, cobble netherrack, basalt, block of ash, soul sand,
    rubyglass crystal, gloomstone, cobbled gloomstone, ember, brimsand, brimstone, sulfur. **Not** slate or crystalline.
- **Lava floes** (Volcanic Islands only, biome at y 96):
  - Worley cells of **1 chunk** (`WorleyNoise(Random(seed))`, sampled at `cx + x/16`).
  - `w.distance` = 1 − (distance to the cell border, in chunk units). A column is skipped if `cell.index % 15 == 0`
    (~5 % of cells empty) or `w.distance > 0.9 + gap*0.01`, `gap = IPN4(x/5, z/5)/2 + 0.5`: **lava cracks ~3 wide**.
  - `heightMod = Random(cell.index).nextInt(3) + 1` and `height = (IPN4(x/5, z/5)/2 + 0.5) * heightMod`.
  - y 93 .. 96+height−2: NETHERRACK / COBBLE_NETHERRACK 50/50, with 1/20 **PUMICE_WET**.
  - Then up to 96+height+1: **BRIMSAND**.
  - Only replaces lava or air. The result is flat chunk-sized islands paving the lava sea.
- **Rubyglass floes** (Crystal Forest/Plains):
  - Same Worley cells. Kept where `w.distance <= 0.8 + IPN4(x/15, z/15)*0.025 + 0.025`: gaps of **~5–6 blocks** of
    crust-covered water.
  - `depth = (int)(Normalize(IPN2(localX/10, localZ/10))*6)` uses chunk-local x and z, so the pattern repeats each
    chunk.
  - y 96−(depth+2) .. 95: **COBBLE_NETHERRACK_CRYSTALLINE**. y 96 .. 96+depth−1: rack/cobble.
  - Only replaces crust, water or air.
- **Shelf** (Shelf only):
  - Per Worley cell, `h = Random(index).nextInt(5) + 1`. **BRIMSAND** fills air or water from y 76 to 96+h−1.
  - Because the shelf "sea" is solid rack, this makes **flat brimsand terraces 1–5 high** with steps at the
    chunk-scale cell borders.

### 1.5 Decorations (registration order = run order). y selectors with minY 0, maxY 255, range 256:
- `Uniform` 0..255
- `FromOcean` 100..347 (37 % of picks fall above the world and are wasted)
- `Upper` 132..379 (50 % wasted)
- `BiasedTop` `rand(rand(240)+16)`, biased low
- `MinY` y = 0 (the feature scans for itself)
- `Pillar` = the Worley(32-block cells) feature point of the cell containing a random point of the chunk, at y 200

Biome abbreviations: VI Volcanic, CR Crag, SP Sulfur, SH Shelf, CF Crystal Forest, CP Crystal Plains, OW Old World,
OD Desert.

| # | id (`decoration/nether/default/…`) | feature (params) | biomes | y | per chunk |
|---|---|---|---|---|---|
| 1 | rubyglass_crystal_ceiling | RubyglassCrystalline(ceiling, min 20, mod 15) | CF | Upper | `max(0,(int)((N(x/2,z/2)+24)/6)+4)`; N = FractalNoise2D(IPN(seed,8,pre 74)) at the chunk corner; mean **~8** [H] |
| 2 | rubyglass_crystal_floor | (floor, 20, 30) | CF | FromOcean | same formula |
| 3 | rubyglass_crystal_floor_2 | (floor, 15, 25) | CF CP | FromOcean | 4 |
| 4 | rubyglass_node | Ore(column→node, 5) | CF CP | Uniform | 10 |
| 5 | rubyglass_bud_patch | GrowthPatch(tall 0) | CF CP | FromOcean | 100 |
| 6 | rubyglass_growth_patch | GrowthPatch(tall 0.6) | CF CP | FromOcean | 100 |
| 7 | scatter_crystalline | PerlinVein(crystalline ← cobble, scale 0.06, \|n\|≤0.045) | CP CF | MinY | 1 (whole 16×255×16) |
| 8 | clump_crystalline | WallClump(crystalline ← exposed cobble, 1) | CP CF | Uniform | 500 |
| 9 | clump_brimthaw | WallClump(brimthaw ← exposed brimstone, 1) | SP | Uniform | 1000 |
| 10 | sulfur_pocket | Ore(brimstone/brimsand → SULFUR, 24) | SP | Uniform | 20×256/128 = **40** |
| 11 | soulsand_pocket | Ore(gloomstone/cobbled gloom/ash/slate → SOULSAND, 32) | OW OD | Uniform | **40** |
| 12 | water_spring | NetherSpring(water flowing) | CF CP | BiasedTop | 16 |
| 13 | lava_spring | NetherSpring(lava flowing) | VI CR SH | Uniform | 16 |
| 14 | lava_spring_2 | NetherSpring(lava flowing) | VI | Uniform | 16 |
| 15 | lava_lake | Lake(lava still) (beta WorldGenLakes, 16×8×16, edges → filler) | VI CR CF CP SH SP | Upper | 1/4 |
| 16 | lava_pool_terrace | Terrace(lava still, brimstone, r 4+rand 4) | VI | MinY | 1/3 |
| 17 | boulder_magma (1st) | same Terrace(lava, brimstone, 4, 4) — duplicate id, but both run (the map is keyed by object) | VI | MinY | 1/3 |
| 18 | acid_spring | NetherSpring(acid) → **never places acid**: 1/50 → GLOWSTONE + FIRE_SULFURIC above | SP | Uniform | 16 |
| 19 | pillar_netherrack | Pillar(RUBYGLASS_COLUMN, no lava) | CP CF | Pillar | 1/8 |
| 20 | pillar_netherrack_2 | Pillar(COBBLE_NETHERRACK, each block 50 % netherrack) | CP CF CR SH | Pillar | 1/4 |
| 21 | pillar_cobble_basalt | Pillar(BRIMSTONE, lava) | VI SP | Pillar | 4 |
| 22 | pillar_slate | Pillar(SLATE, lava) | OW | Pillar | 4 |
| 23 | boulder_magma (2nd) | BoulderMagma | VI | FromOcean | 2 |
| 24 | sulfur_pool_terrace | Terrace(ACID flowing, BRIMSAND, r 6+rand 6) | SP | MinY | 3 |
| 25 | floor_vent | FloorVent(r 1+rand 2) | VI SP | FromOcean | 12 |
| 26 | volcanic_islands_spire | SpireVent | VI | FromOcean | 8 |
| 27 | sulfur_pool_boulder | BoulderGlowstone(GLOWSTONE, FIRE_SULFURIC, r 1) | SP | FromOcean | 12 |
| 28 | nether_roof_spire | RoofSpire(cobble/rack 50/50) | CR SH | FromOcean | 2 |
| 29 | volcano_roof_spire | RoofSpire(BRIMSTONE) | VI | FromOcean | 2 |
| 30 | sulfur_roof_spire | RoofSpire(SULFUR) | SP | FromOcean | 2 |
| 31 | oldworld_roof_spire | RoofSpire(SLATE) | OW | FromOcean | 2 |
| 32 | nether_coal_ore | Ore(nethercoal variant, 12) | all (no mask) | Uniform | 10 |
| 33 | nether_coal_ore_2 | Ore(…, 12) | VI | Uniform | 20 |
| 34 | nether_coal_ore_3 | Ore(…, 16) | CR SH | Uniform | 10 |
| 35 | glowstone_1 | Glowstone | all but OW OD | FromOcean | `rand(rand(10)+1)` (mean 2.25) |
| 36 | glowstone_2 | Glowstone | all but OW OD | FromOcean | 10 |
| 37 | glowstone_sulfur | Glowstone | SP | FromOcean | 20 |
| 38 | patch_fire | Fire() | CR SH | FromOcean | 16 |
| 39 | patch_fire_volcanic | Fire(FIRE, ground → NETHERRACK) | VI | FromOcean | 24 |
| 40 | patch_fire_sulfuric | Fire() | SP | FromOcean | 32 |
| 41 | scatter_ember_ash | NetherScatterEmberAsh | no mask | FromOcean | 20 |
| 42 | scatter_bone_pile | NetherScatterBonePile | no mask | FromOcean | 8 |
| 43 | scatter_boulder_magmatic | ScatterBoulderMagmatic | CR SH VI | FromOcean | 8 |
| 44 | scatter_boulder_magmatic_sparce | ScatterBoulderMagmatic | CF CP | FromOcean | 4 |
| 45 | scatter_boulder_sulfuric | ScatterBoulderSulfuric | SP | FromOcean | 24 |
| 46 | patch_soul_catcher | SoulCatcherPatch | no mask | FromOcean | 8 |
| 47 | pebbles | 16 × Pebbles(16, vary) at y 64..191 | no mask | — | **no-op**: pebbles only go on grass/sand/dirt/sandstone/scorched dirt |
| post | postDecorate | **LAYER_ASH** on every air block whose block below is solid (blocksMotion), y 255 → 97, in HAS_SURFACE_ASH biomes (OW, OD); not on obsidian, soul sand or rubyglass node/column/growth | OW | — | every column |

**Feature details**
- **Ore** (beta WorldGenMinable; a vein of `n` along a random line ±n/8; replaces only the variant map's keys). Nether
  coal: COBBLE_NETHERRACK/COBBLE_BASALT/COBBLE_GLOOMSTONE → ORE_NETHERCOAL_{NETHERRACK,BASALT,GLOOMSTONE} (**cobbled
  variants only**). Node: RUBYGLASS_COLUMN → NODE. Sulfur: BRIMSTONE, BRIMSAND → SULFUR. Soul sand: GLOOMSTONE,
  COBBLE_GLOOMSTONE, BLOCK_ASH, SLATE → SOULSAND.
- **Glowstone** (≈ beta GlowStone1): the start must be air with a **solid block directly above**, else it fails; then
  1500 tries at ±7 x/z, 0..11 down, setting air with exactly 1 glowstone neighbour. Success per try [H] ≈ 0.625 ×
  0.016 ≈ **1 %** → **~0.12 clusters/chunk** (≈1 per 8 chunks), ~0.33 in Sulfur, 0 in the Old World. BTA's Nether is
  lit by lava, not glowstone.
- **NetherSpring**: fails if the block above is NETHER_SURFACE, or the block itself is a non-air NETHER_SURFACE; needs
  exactly 4 of {4 sides + below} NETHER_SURFACE and 1 air; places the flowing fluid and ticks it once → lavafalls and
  waterfalls out of walls. NETHER_SURFACE tag: netherrack, cobble netherrack, cobble basalt, rubyglass crystal,
  gloomstone, ember, brimsand, brimstone, brimstone bricks, sulfur.
- **RubyglassCrystalline** (giant crystals): ±1 jitter; scan down (up for ceiling) ≤64 to the first COBBLE_NETHERRACK
  and need ≥6 consecutive cobble beyond it (in practice the cobble-only deep rock). `length = min + floor(r^4*mod)`
  (ceiling 20–35, floor 20–50, floor_2 15–40), `base = length/4`. Convex prism: base square `base` + rand(length/16)
  per side, height `length`, pyramid tip +`base*2*(0.5..1)`; yaw random, pitch ±36° (`(rand-0.5)*π/2*0.8`), ceiling
  ones flipped. Voxels (never bedrock): inside (>0.5 from a side) COLUMN with 1 % NODE; shell NODE when
  `rand + 0.5*sin(3π·d/maxBase) > 0.5`, else COLUMN; lower half → COBBLE_NETHERRACK_CRYSTALLINE when
  `(d/0.5) < rand*0.75`, a crystalline "bed" fading up the shaft. Skipped if a column already exists in the 2·base box.
- **GrowthPatch**: 20 tries in ±3; air next to a supporting full cube (column, node, crystalline, or cobble with a
  crystalline in its 3³) → RUBYGLASS_GROWTH pointing away from the support; with `tall` a 2-block growth (meta 8|dir,
  0x10|dir).
- **Terrace**: circle r at +rand(8); in every column scan y 255 → 101: a NETHER_SURFACE block with air above becomes the
  **wall** (rim); inside r−1, one that is surface with air above and non-air on 4 sides + below becomes **fluid** →
  1-deep pools cut into **every** floor inside the cylinder.
- **Pillar**: floor = scan down from (x ± 8, 200); roof = scan up from (x − 1..2, 200); each end +5 into rock; length
  6..127. A sheared cylinder, `r = (int)(8 − (rand*4+1))` = 3..7, profile `r·((cos 2πp/2+0.5)·0.75+0.5)` floored to
  1/6, min `rand·r·0.9` (hourglass: 1.25× at the ends, 0.5× mid). 1/8 top half only, 1/8 bottom half only; both base
  discs must be solid. `doLava`: 8–27 flowing lava where the block is COBBLE_NETHERRACK. If y 200 is inside rock it is
  a hidden stub (a material swap only).
- **BoulderMagma**: needs a 4³ box of air/surface; MAGMA discs at y, y+1 (r ~1) and y+2; ORE_NETHERCOAL_NETHERRACK at
  1/8 on any non-air in 4×8×4; BLOCK_NETHER_COAL at the centre of y+1.
- **FloorVent**: air over the centre and 4 neighbours; disc r 1–2, 4 deep, surface blocks only: centre top
  THERMAL_VENT, rest 3/4 BASALT (stray top vent 1/80).
- **SpireVent**: 2×2×4 air needed; 2×2 BASALT columns 4–9 tall, lower third 50 % ORE_NETHERCOAL_BASALT; 2
  THERMAL_VENT on top of the tallest.
- **RoofSpire**: scan up to the roof (<245); the 6×6×6 region 3–8 below must be air/surface; 2×2 stalactite, columns
  4–9 long + 2 below the longest; bedrock kept.
- **BoulderGlowstone**: only on a surface block **under acid** (an acid pool floor): diamond mound of GLOWSTONE
  (centre 3–4, edges 2–3, corners 1–2 high), FIRE_SULFURIC on top at 1/2.
- **Fire**: 32 tries ±7/±3 on air. Default: FIRE over INFINITE_BURN (netherrack, cobble, crystalline, cobble slab),
  FIRE_SULFURIC over INFINITE_BURN_SULFURIC (glowstone, brimsand, brimstone family, brimthaw, sulfur). Volcanic
  variant: FIRE on any surface block and the ground → NETHERRACK.
- **Scatter** (shared): scan down ≤96 to ground; never on rubyglass node/column/growth; air above; `canStay` (solid
  render below). BonePile: never on obsidian, 1/8 on ash. Boulders: magmatic anywhere, sulfuric only on brimsand,
  brimstone or brimthaw. EmberAsh: 3/4 of tries proceed; a BLOCK_ASH with air above in OW/OD and ash on all 4 sides
  → **EMBER** with LAYER_ASH on top (a hidden mine).
- **SoulCatcherPatch**: ground on soul sand gets 28 tries, else 6 tries at 1/5; ±2 x/z, ±1 y; not on obsidian.
- **Structures: none.** There is no fortress or ruin in 8.0.1's Nether generator.

---------------------------------------------------------------------------------------------------------------------
## 2. Blocks placed by Nether generation

Light values: `withLightEmission(float f)` gives `(int)(15f)`; the int form gives the value & 15.
- Every texture is an individual 16×16 PNG (animated ones are vertical strips with `.mcmeta`) under
  `assets/minecraft/textures/block/`. **No atlas cutting is needed.** `terrain.tiles` and `items.tiles` at the jar
  root are only legacy name→(row,col) indexes for a 32×32 grid, with no image and no entries for the new Nether
  blocks.
- `*.emiss.png` is a same-size **emissive mask**: those pixels render full-bright, but the block gives no light
  unless it has emission.
- Models are in `assets/minecraft/models/block/<model>.json`; the dispatch is in
  `client/render/block/model/BlockModelDispatcher`.
- All texture paths below were verified to exist. "avg" is the mean opaque colour, computed by us.

| BTA key (id) | name | render | light | hard. | texture(s) (avg) | behaviour |
|---|---|---|---|---|---|---|
| netherrack (803) | Netherrack | cube | 0 | 0.32 | netherrack.png (#6b3535) | infinite burn; drops cobble netherrack |
| cobble.netherrack (800) | Cobbled Netherrack | cube | 0 | 0.4 | cobbled_netherrack/normal.png (#6e3533) | **world filler**; infinite burn; grows rubyglass |
| cobble.netherrack.crystalline (802) | Crystalline Netherrack | cube | 0 | 0.4 | cobbled_netherrack/crystalline.png (#7b2a2e) + .emiss | glowing red veins (emissive only) |
| gloomstone (1032) | Gloomstone | cube | 0 | 2.0 | gloomstone.png (#332741) | Old World deep rock; drops cobbled |
| cobble.gloomstone (1033) | Cobbled Gloomstone | cube | 0 | 2.0 | cobbled_gloomstone.png (#312742) | |
| brimsand (1085) | Brimsand | cube | 0 | 0.7 | brimsand.png (#2e2b24, near-black) | Material SAND but plain BlockLogic, **does not fall**; sulfuric infinite burn |
| brimstone (1086) | Brimstone | cube_bottom_top | 0 | 1.0 | brimstone/{top,side,bottom}.png (#2f2e2c) | dark volcanic rock; drops itself (brimsand ×1–2 when crushed by a piston) |
| brimthaw (1092) | Brimthaw | cube | **10** | 1.25 | brimthaw.png (#605c1f) + .emiss | like magma for acid: melts into acid next to acid; sulfuric fire hurt; collision −1/8 |
| sulfur (1098) | Block of Sulfur | cube | 0 | 0.5 | sulfur.png (#ab8e1c) | drops 4 sulfur; shovel |
| block.ash (741) | Block of Ash | cube | 0 | 0.2 | block_ash.png (#454046) | shovel |
| layer.ash (723) | Ash Layer | 1/8-tall layer (BlockModelGenericLayerAsh, uses block_ash) | 0 | 0.1 | block_ash.png | stepping on it over an EMBER sets the ember off |
| ember (1065) | Ember | cube (animated 16×64, random interpolated, frametime 3) | **5** | 1.0 | ember.png + ember.emiss.png (#3a271a) | touched by a player or wolf: 8 blast damage, fire for 100 ticks, fling 0.65 up, block removed, explosion 3.0 with fire. Particles: flame + large smoke. Collision −1/64 |
| slate (6) | Slate | cube_column | 0 | 1.5 | slate_top.png / slate_side.png (#757380) | not cut by caves |
| soulsand (810) | Soul Sand | cube | 0 | 0.5 | soulsand.png (#544033) | slows to ×0.4; collision −1/8 |
| obsidian (180) | Obsidian | cube | 0 | 10 | obsidian.png (#14121d) | Old World "sea" |
| bedrock (260) | Bedrock | cube | 0 | ∞ | bedrock.png | |
| glowstone (820) | Block of Glowstone | cube_lit | 15 | 0.3 | glowstone.png (#897040) | |
| fire (630) / fire_sulfuric (634) | Fire / (sulfuric) | fire model | 15 / 15 | 0 | fire.png / fire_sulfuric.png (#e0d620, **yellow**) | the sulfuric one burns forever on INFINITE_BURN_SULFURIC blocks |
| fluid.lava.still/flowing (273/272) | Lava | fluid | 15 | – | fluid/lava/still.png, flowing.png (32×32) | |
| fluid.water.still (271) | Water | fluid, layer 1 | 0 (blocks 3) | – | fluid/water/still.png | tinted (255,50,32) in the Nether |
| fluid.acid.still/flowing (1063/1062) | Acid | fluid, layer 1 | 0 | – | fluid/acid/still.png (16×64, interpolated, ft 30) + .emiss (#cccb09) | only appears via the terraces |
| rubyglass.crystal (823) | Rubyglass Crystal | translucent cube, 4 variants by meta&3 | 0 | 1.5 | rubyglass/crystal/{0..3}.png + .emiss (#740000) | Material ICE, friction 0.98 (slippery). **Breaks when stood on**: data 0→3 every 5 ticks while a mob is on it, then shatters; recovers when empty; no drop. Random "rubyglassLightning" particles |
| rubyglass.column (821) | Rubyglass Column | cube | 0 | 2.0 | rubyglass/column.png + .emiss (#86091a) | the giant crystals |
| rubyglass.node (826) | Rubyglass Node | tinted cube | 0 | 3.0 | rubyglass/node.png (16×32 anim) + .emiss (#a21234) | |
| rubyglass.growth (825) | Rubyglass Growth | custom (BlockModelGenericRubyglassGrowth) | 0 | 1.0 | rubyglass/growth/{bud,bottom,top}.png + .emiss (#8a091a) | oriented by support side; 1 or 2 tall; ticking |
| pumice.wet (233) | Molten Pumice | lava base + pumice_overlay | **9** (0.625) | 1.0 | fluid/lava/still + pumice_overlay.png | drips lava particles |
| magma (801) | Magma | cube | **10** | 1.25 | magma.png + .emiss (#8a3e28) | melts into flowing lava when next to lava (and no water or acid); fire hurt; collision −1/8 |
| ore.nethercoal.netherrack/basalt/gloomstone (420/421/422) | … Nether Coal Ore | cube | 0 | 3.0 | ore/nethercoal/{netherrack,basalt,gloomstone}.png + .emiss (#7f3a29) | |
| block.nethercoal (436) | Block of Nether Coal | cube | 0 | 3.0 | block_nethercoal.png + .emiss (#71220f) | |
| basalt (2) | Basalt | cube | 0 | 1.5 | basalt.png (#3f3f3f) | vents and spires |
| vent (1025) | Thermal Vent | cube_bottom_top | 0 | 1.5 | thermal_vent/{top,side,bottom}.png (#353535) | **active only with a vent below** (spires have 2 stacked). Builds up on 3/4 of ticks (+2 per tick over magma); at state 4 it erupts: 40 smoke and flame particles, flings entities in 3×7×3 by +2.0 y, alerts players within 200 |
| boulder.magmatic (1060) | Magmatic Boulder | cross (render3D false), box 0.1–0.9 × 0.8 | 0 | 1.0 | boulder_magmatic.png + .emiss (#774229) | sets the toucher on fire (100 ticks); drops pebbles and 1/4 nether coal |
| boulder.sulfuric (1061) | Sulfuric Boulder | cross | 0 | 1.0 | boulder_sulfuric.png + .emiss (#787722) | same, with sulfuric fire; 1/4 sulfur |
| bone_pile (1030) | Bone Pile | cross | 0 | 1.0 | bone_pile.png (#938a80) | broken by fluids |
| soul_catcher (1031) | Soul Catcher | cross | 0 | 1.0 | soul_catcher.png (#46362c) | |

Related family (not placed by generation): basalt.polished, cobble.basalt, netherrack.polished, netherrack.carved,
brick.netherrack, gloomstone.polished/.carved, brick.gloomstone, brick.brimstone, block/brick of rubyglass,
soulschist (811), pumice.dry (232).

---------------------------------------------------------------------------------------------------------------------
## 3. Visuals
These are from a delegated decompile of `client/render/{FogManager,PostProcessingManager,worldtype/WorldTypeFXNether}`.
Spot-checked: `shaders/post/post.fsh` and `colormap/fog/nether.png` exist, `rMod += 1.2` is present, and the
uniforms `exposure`/`saturation`/`rMod`/`heatHaze` are present.

**Fog colour**
- Source: `textures/colormap/fog/nether.png` (256×1), sampled at `x = (int)((1-temp)*255)`, with
  temp = the block temperature at the camera. It is continuous and blends smoothly; biome identity is not used.

  | temp | 1.0 | 0.9 | 0.8 | 0.72 | 0.6 | 0.5 | 0.35 | 0.25 | 0.1 | 0.0 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | RGB | 214,121,0 | 193,103,0 | 163,79,0 | 134,57,0 | 95,26,0 | 69,5,0 | 44,0,2 | 33,6,6 | 22,14,13 | 16,16,15 |

  Use the **mean temperatures per biome from §1.1** (Volcanic 0.99, Crag/Sulfur 0.88, Shelf 0.73, Crystal 0.59,
  Old World 0.26).
- The sky is black, so `fog = ramp * (1 - rds)`, with `rds = 1 - (1/(40-rd))^0.25`. That factor is 0.435 at render
  distance 12, 0.42 at 8, 0.452 at 16.
- Then multiplied by `fogBrightness`, which eases toward `ramp[blockLight@camera]*(1-m)+m`, with `m = (32-rd)/32`.
  At rd 12 that is 0.68 to 1.0.
- The background is `glClearColor(fog)`, flat.

**Sky:** none. The Nether has no sky, sun, stars, clouds or void plane, and no aurora.

**Fog distance**
- Linear, on Euclidean distance. **Start 0**, end `min(rd*16, 512)`.
- In the Old World, `oldWorldFogTransition` (+0.0005 per `setupFog` call) eases the fog to start 0.15·far and end
  0.45·far: 28.8 → 86.4 blocks at rd 12.
- Inside lava: EXP density 2.0, colour (0.6, 0.1, 0) × brightness. Inside acid: EXP 1.2, colour (0.2, 0.2, 0.1).
  Inside water: EXP 0.1, colour (0.5, 0.098, 0.063).

**Colour grade: there is no LUT file.** It is a parametric grade in `shaders/post/post.fsh` on the world framebuffer
only.
- Option: `colorCorrectionNether` (`GameSettings.COLOR_CORRECTION_NETHER`), default 0.5; picked when the world type
  has the NETHER tag.
- Per tick for a HOT world: `scale = 1` (or `2·temp` when temp < 0.5, which also does `saturation -= 0.5(1-2t)`);
  `rMod += 1.2·scale`, `gMod += 0.3·scale`, `saturation += 0.125·scale`, `exposure -= 0.125`, `contrast -= 0.05`,
  `brightness -= 0.05`.
- The slider multiplies brightness, contrast, exposure and saturation; each colour mod becomes `1-(1-mod)·slider`.
  Values ease at 5 % per tick.
- **Defaults (temp ≥ 0.5): brightness −0.025, contrast −0.025, exposure −0.0625, saturation +0.0625, R×1.6, G×1.15,
  B×1.0.**
- Shader order: `c += brightness; c = 0.5 + (1+contrast)(c-0.5); c *= 1+exposure;
  c = mix(luma(Rec709), c, 1+sat); c *= rgbMod`. Mid-grey 128 comes out (183, 131, 114).

**Heat haze**: option `heatHaze` (default 0.5); active on HOT world types (the Nether), whole screen,
`amount = scale·slider·3` (1.5 default). UV warp in post.fsh: `s = clamp((pow(depth,500)-0.16)*2,0,1)*amount`;
`uv += vec2(sin(uv.y*65-10t)+sin(uv.y*250-14t), 2 sin(uv.x*50+5t)) * s * 1e-4`. Starts ~13 blocks, full ~52 (rd 12);
under 1 px, a faint distant shimmer.

**Particles**
- Ambient per tick around the player: Old World 3 `ashmote` in ±12; Volcanic 3 ashmote + 2 `flame` in ±24; other
  biomes none. Ash mote: grey 0.2–0.5, falls `vy - (0.02..0.07)`/tick constant, horizontal ±0.05 damped ×0.9,
  `size = rand²·6+1` (×0.1), lives 66–322 ticks, smoke-puff sprite.
- Block display ticks: lava pop 1/100 on open tops, drip 1/4 under a hanging block; acid boiling, drips, fizz; fire 3
  large smoke; ember smoke every tick + flame at 1/3; molten pumice lava drips; vents `ventsmoke` (distance throttled);
  portal. Music `music.bta.nether` (nether1–7.ogg); no Nether-only ambient loop.

**Light**
- No sky light anywhere (`hasCeiling`).
- Lightmap = `ramp[blockLight]`, with `ramp[i] = (1-f)/(3f+1)*0.85 + 0.15`, `f = 1 - i/15`:

  | level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | ramp | .150 | .165 | .182 | .200 | .221 | .244 | .271 | .303 | .339 | .382 | .433 | .496 | .575 | .676 | .811 | 1.0 |

  The 0.15 floor is additive. Gamma and brightness are no-ops at their defaults.
- Emissive masks (`.emiss`) are full-bright.
- The vignette is strong because the scene is dark.

---------------------------------------------------------------------------------------------------------------------
## 4. Gaps: our port vs BTA

The default is `genVersion = 6` (`src/gen/generator.h:51`), which runs `DepthsGen::generateCave` in
`src/gen/nether_bta.cpp:645`. Version 5 (`generateBTA`, a hand-made heightfield of 5 lands) is not BTA's algorithm at
all; it is kept only for old saves. Heights need **no scaling** (both are 256 tall, sea 96). Most visible first.

**P1 — the whole picture**
1. **Grade and fog** (`src/render/sky.cpp:25` `btaNetherEnv`, `post_layer.cpp`, `assets/shaders/post.glsl`).
   - BTA: flat orange→red fog/background from the temperature ramp (§3), and a warm grade (R×1.6, G×1.15) on every
     Nether frame.
   - Ours: per-land colours including yellow (Sulfur), purple (Crystal) and blue-grey (Shelf), a sky dome with
     zenith/horizon gradients, and no tint. Our Volcanic colour (0x2a0a06) is among the darkest; in BTA, Volcanic is
     the **brightest orange**.
   - Fog: BTA is linear from **0** to rd·16, with the Old World closing to 0.15–0.45·far. Ours starts at 0.14·viewFar
     and ends near viewFar, and LOD can push it much further out.
2. **Climate noise scale is wrong.** `NetherClimate::f2` (`nether_bta.cpp:468`) leaves out the 2D simplex octave
   rule (`scale/1.5`, `amplitude*0.55`). Effect [H, same formula through BTA noise]:

   | | Old World | Volcanic | Crag | Crystal Forest | Sulfur | Shelf | Crystal Plains |
   |---|---|---|---|---|---|---|---|
   | ours | 29 % | 23 % | 20.5 % | 10 % | 8.8 % | 5 % | 4 % |
   | BTA | 10 % | 34 % | 30 % | 13 % | 5.4 % | 6 % | 1.8 % |

   Our patches are also about half as wide. Fix: multiply each octave's frequency by 1/1.5 and its amplitude by 0.55.
   Then quantise T and H with `(int)(v*50)/50` before the range tests, which gives the effective thresholds
   0.96 / 0.76 / 0.46 / 0.86.
3. **Surface materials and stand-ins** (`skinFor`, `deepOf` and `mixPlace` at `nether_bta.cpp:585–723`):
   - **Shelf**: ours is permafrost over packed ice (invented). BTA: rack/cobble speckle, plus **brimsand terraces 1–5
     high** on the solid rack "sea" (§1.4).
   - **Volcanic**: ours is cinder_sand over **sulphur_block (yellow #d5cb77)**, with deep basalt/blackstone/cinder.
     BTA: brimsand over **brimstone (near-black #2f2e2c)**, deep brimsand. Brimstone should be a dark stone
     (basalt #3d3d42 is the nearest we have), not sulphur.
   - **Sulfur Pools**: ours is all sulphur_block (yellow). BTA: black brimstone (brimsand at and below the sea), and the
     yellow comes from **40 sulfur pockets/chunk (size 24)**, bare SULFUR where soil ≤ 0, and **1000 brimthaw clumps**
     (glowing olive, light 10) on exposed walls. Today the biome is the wrong colour.
   - **Crag / Crystal deep rock**: ours adds granite 10 %, limestone 10 % and gloomstone 14–34 % (invented). BTA is
     rack/cobble only in the Crag and cobble only in Crystal.
   - **Old World deep rock**: ours mixes gloomstone 22 %, limestone 16 % and slate/blackstone. BTA: gloomstone /
     cobbled gloomstone 50/50 (we have no cobbled_gloomstone).
   - Below the sea in Sulfur: ours is cinder_sand over sulphur; BTA is brimsand over brimstone. For `soil<=0` ours
     uses `deepOf`; BTA uses Volcanic brimsand, Sulfur SULFUR, Old World gloomstone, otherwise netherrack.
   - Stand-ins that are fine: emberstone (#662a22) for cobbled netherrack (#6e3533); cinder_sand (#3f3c40) for
     brimsand (#2e2b24) — **but ours falls and BTA's does not**, so generated overhangs will collapse; ash for block
     of ash; gloomstone; slate (ours #41454f is darker than BTA #757380); soul_sand; obsidian.
4. **Missing large features**:
   - **Lava floes**: the chunk-sized brimsand-topped islands with 3-block lava cracks are what make the Volcanic
     Islands "islands".
   - **Rubyglass floes**: crystalline slabs in the crystal water.
   - **Shelf terraces.**
   - **Caves**: `CavesLargeFeature(64,256)`. `generateCave` never calls `carveCaves`.
5. **Density main-noise bug.** `fieldDensity` uses the same `skip = 16-octaves` for all three noises. BTA's main
   noise has only 8 octaves starting at frequency 1 / amplitude 1. With `octaves = 8`, ours starts at 2⁻⁸ / 2⁸: 256×
   too low a frequency and 256× too loud.
   - Result: `w` practically always saturates to min or max (BTA's blends part of the time), losing BTA's ~60-block (x) / ~30-block (y)
     min/max switching.
   - Fix: the main noise uses its own loop, `freq = 1, amp = 1` for 8 octaves. Dropping the 8 finest limit octaves is
     an acceptable LOD shortcut.
   - We use simplex 3D where BTA uses ImprovedPerlin. Same range, a different look; acceptable.
6. **Glowstone is far too common.** `DepthsGen::glowstone` (`depths.cpp:236`) makes 5–10 clusters per chunk with an
   upward search (and runs before the repaint). BTA makes **~0.12 per chunk**, none in the Old World and ~0.33 in
   Sulfur, because it only seeds at random points that touch a ceiling. Our Nether is much brighter and more yellow
   than BTA's, which is lit by lava.

**P2 — signature features**

7. **Giant rubyglass crystals.**
   - Ours (`nether_bta.cpp:813`): straight 3–16-tall columns of `red_crystal`, which is glass with **light 9**.
   - BTA: 20–50-long prisms tilted up to 36°, about 8 floor + 8 ceiling + 4 per chunk in the Forest. Made of
     **rubyglass column** (opaque, emissive texture, light 0), with 1 % nodes, crystalline cobble at the base, and
     buds/growths on them (200 tries).
   - BTA also has crystalline veins through all the cobble (PerlinVein + 500 clumps).
   - Missing blocks: rubyglass column, node, growth, crystalline netherrack. Possible stand-ins:
     red_crystal_cluster for growth, red_crystal for column (but remove its light).
8. **Rubyglass crust.** We put `red_crystal` (light 9) at y 95 over the water. BTA's crust gives **no light** and is
   translucent, slippery, and **cracks and breaks under anything standing on it**. Ours glows and holds.
9. **Old World ash.**
   - BTA covers **every** exposed solid top above y 96 with LAYER_ASH (1/8 block).
   - Ours (`nether_bta.cpp:899`) puts a **full ash block** on 22 % of columns, 8 % of them smouldering.
   - Missing pieces: a layer-ash block (our snow_layer shape could carry it); embers as hidden mines (light 5, blast
     3.0 and fire on touch) rather than glowing smouldering_ash; **40 soul sand pockets per chunk (size 32)**; slate
     pillars and slate roof spires.
10. **Pillars.**
    - BTA: thick hourglass columns (r 3–7) from floor to roof at 32-block Worley centres. 4 tries per chunk in
      Volcanic/Sulfur (brimstone + lava) and the Old World (slate); 1/4 rack in Crag/Shelf/Crystal; 1/8 rubyglass in
      Crystal.
    - Ours: one 1-wide column, on 1/8 of 32-cells.
11. **Springs and fluid features.**
    - BTA: 16–32 lava springs per chunk (lavafalls out of walls) in Volcanic/Crag/Shelf, 16 water springs in Crystal,
      lava lakes 1/4, lava terraces (r 4–7, ringed in brimstone) and **acid terraces** (r 6–11, ringed in brimsand)
      cut into every floor inside the cylinder, and glowstone mounds rising out of acid with yellow fire.
    - Ours has none of these in v6, only 3×3 acid dimples (`:838`).
12. **Vents and volcanic dressing.**
    - BTA: 12 floor vents (a basalt disc with a thermal vent), 8 spire vents (2×2 basalt and coal ore, 2 stacked
      vents that erupt and fling), magma boulders (a magma mound, coal ore and a nether coal block), and roof spires
      in 4 materials.
    - Ours: basalt+magma stacks with a lava cap, and steam_vent (closest to the thermal vent, but a different
      mechanic).
13. **Fire.** BTA uses **yellow sulfuric fire** in Sulfur Pools (32 patches) and on glowstone, and in Volcanic sets the
    ground under each fire to netherrack. Ours: plain fire on netherrack/emberstone, 18 tries. FIRE_SULFURIC is
    missing.
14. **Scatter.** BTA has bone piles (everywhere), magmatic and sulfuric boulders (cross-model, they burn) and soul
    catchers. Ours: 2-block `bone_block` piles in the Old World only; no boulders or soul catchers.
15. **Ores.** BTA has only nether coal (in **cobbled** netherrack, basalt and gloomstone), rubyglass nodes, and the
    sulfur and soul sand pockets. Ours (`depths.cpp:266`) adds quartz, nether gold, cinderite, gold, ancient debris,
    ember/limestone/granite ores, soul schist and bone shale. **Probably deliberate** (DQ-original items).
    nether_coal_ore, basalt_coal_ore and gloom_coal_ore already match BTA's three variants.

**P3 — mostly hidden or deliberate**

16. **Bottom of the band.**
    - BTA: solid bedrock at y 0–62, ragged 63–72; the roof is ragged at 246–255.
    - Ours: bedrock 0–3 (55 % at 4), then **a fluid layer at y 5–55** under a rock slab at 56–~85; roof bedrock
      ≥ 252.
    - This is deliberate: the comment at `nether_bta.cpp:439` keeps the stacked-world shaft open. It is only seen
      when digging, but it puts a lava ocean under every Volcanic/Crag/Sulfur floor.
    - A compromise: keep the shaft, but fill 5–62 with bedrock-like rock or obsidian.
17. **Biome resolution.** BTA masks decorations by one biome per chunk (corner, y 127) and stores surface biomes in
    8-block cells. Ours uses per-column climate. This only matters at edges.
18. **Old World Desert** is unreachable in both (below the y 64 floor). Our `oceanKind` for it says lava; BTA says
    air. Harmless.
19. **Structures.** `structures.build` (DQ fortresses and others) has no BTA counterpart. Deliberate DQ content.
20. **Visual details** (agent report, §3):
    - Ours has no heat haze.
    - The light curve: ours is `l/(3.2-2.2l)` with a max-floor of 0.16. BTA is `0.15 + 0.85·l/(4-3l)` with an
      **additive** floor. Mid levels are darker in ours (level 8: ≈0.26 vs 0.339).
    - Lava fog: ours is (1, .35, .04) linear over 22 blocks; BTA is EXP 2.0 (0.6, 0.1, 0).
    - Water in the Nether is blue in ours; BTA tints it red.
    - Particles: BTA has only large ash motes (Old World, Volcanic) and loose flames (Volcanic). Ours has many
      invented emitters (eruptions, glints, sulfur fumes, shelf flakes) and tiny ash (0.03).
    - Our acid gives light 4; BTA's gives none, but has an emissive mask.
    - Magma: ours light 3, BTA 10. Molten pumice: ours 12, BTA 9.

**Licensing note:** BTA art is third-party. `tools/port/bta_blocks.py` states "nothing is taken from BTA". Use the
texture paths and average colours above as **references for recolouring our own art**; do not copy the PNGs into the
repo.
