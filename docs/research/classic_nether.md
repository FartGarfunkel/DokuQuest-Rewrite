# The classic Nether: generation and look

The classic Nether is the cave Nether of terrain versions 7 and up: a 3D cave sponge over a sea at y 96, seven
climate lands, 47 decorations and its own fog and colour grade. The generator lives in `src/gen/nethergen/`
(standalone, `namespace nethergen`, see its README). `src/gen/nether_exact.cpp` maps its blocks onto ours and feeds
it to the engine. Later terrain versions build on it (for example version 13 puts the overgrown lands of
`gen/nether_bn.h` on top). This page is the reference for what that generator does: numbers, rules and blocks.
Section 4 lists how the older versions 5 and 6 differ and what is still open.

**Height:** the classic Nether is 256 tall like our band, so **band-local y = generator y (scale 1:1)**. Lowest
portal y 63, a ceiling, no aurora. The sea holds lava or water, and the world filler is cobbled netherrack. The
clock starts at 72000, the celestial angle is fixed at 0.5, sky darkening is 0, and the coordinate scale is 1/8.

**[H]** marks numbers measured on generated chunks (seeds 1, 12345 and −987654321 unless stated).

---------------------------------------------------------------------------------------------------------------------
## 1. Generation

### 1.0 Pipeline
1. Biomes per section in 16×2×16 cells (8 blocks tall), altitude `(sectionY*16 + yy)/128`, so every block below y 64
   has altitude < 0.5.
2. Density on a 5×33×5 grid, trilinear lerp over 4×8×4 cells, then the block rule (§1.2).
3. Surface (§1.3).
4. Large features in order: caves (radius 8 chunks), lava floes, rubyglass floes, shelf terraces (own chunk only).
5. Decorations: each one gets its own `Random(cx*A + cz*B ^ worldSeed ^ id.hashCode())` (A, B odd longs from
   `Random(worldSeed)`). The biome mask is **one biome per chunk**, read at `(cx*16, 127, cz*16)`. All x/z picks are
   `cx*16 + rand(16) + 8`. A last pass lays the ash layer.

### 1.1 Biomes
| land | debug colour | top / filler | tags |
|---|---|---|---|
| The Crag | #633E3D | cobbled netherrack / cobbled netherrack | – |
| Volcanic Islands | #FFA333 | brimsand / brimstone | – |
| Sulfur Pools | #A39940 | brimstone / brimstone | sulfur pools |
| Crystal Plains | #AA0000 | cobbled netherrack ×2 | crystal veins |
| Crystal Forest | #DD0000 | cobbled netherrack ×2 | crystal veins |
| The Old World | #636480 | block of ash / block of ash | surface ash |
| Old World Desert | #5C4843 | soul sand / soul sand | surface ash |
| The Shelf | #968A6F | cobbled netherrack ×2 | – |

- Every land spawns only Ghast (weight 10) and Zombie Pigman (weight 10).
- Rain, snow, storm and fog weathers are all blocked.

**Climate.** The noises are seeded by the world seed; all are fractal noise (octave 0 at frequency 1, then
frequency × lacunarity and amplitude × 2 per octave):
```
tN = fractal 3D simplex (seed*9871,   4 octaves), lacunarity 0.25, region scale 1/60
hN = fractal 3D simplex (seed*39811,  4 octaves), lacunarity 1/3,  region scale 1/30
fN = fractal 2D simplex (seed*543321, 4 octaves), lacunarity 1/1.7 (0.5882352941176471), region scale 1/6
```
Each 2D simplex octave's region sampling also does **scale /= 1.5; amplitude *= 0.55**. So the temperature octaves
are 1/90, 1/360, 1/1440 and 1/5760 per block, with amplitudes 0.55, 1.1, 2.2 and 4.4. Observed ranges [H]: tN and
hN ±7.
```
a = fN*1.1 + 0.5
t = (tN*0.15 + 0.7)*0.99 + a*0.01 ;  t = 1 - (1-t)^2 ; clamp 0..1     // folds back down when t > 1 (tN > ~2)
h = (hN*0.15 + 0.5)*0.998 + a*0.002 ; clamp 0..1
altitude = clamp(y/128, 0, 1) ; variety = 0
```
The range lookup (span 50) quantises: `i = (int)(v*50)` and tests the range at `i/50`. The last added matching range
wins. Ranges are (Tmin, Tmax, Hmin, Hmax, Amin, Amax), all upper bounds exclusive. Effective thresholds after
quantisation are in brackets:
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
- Below y 64 everything is Old World Desert. In practice that is **invisible**: y < 63 is solid bedrock.

**Shares, sizes and mean climate** [H] (16384² blocks):

| land | share | mean T | mean H | typical patch width | largest connected |
|---|---|---|---|---|---|
| Volcanic Islands | 31–37 % | 0.987 | 0.50 | ~200 blocks | one network spanning thousands |
| Crag | 30 % | 0.884 | 0.42 | ~130 | |
| Crystal Forest | 11–14 % | 0.595 | 0.56 | ~115 | |
| Old World | 10 % | 0.258 | 0.50 | ~145 | |
| Shelf | 5–6 % | 0.731 | 0.50 | ~40 (thin bands between Crag and Crystal) | |
| Sulfur Pools | 5–6 % | 0.884 | 0.96 | ~80 | |
| Crystal Plains | 1.8 % | 0.593 | 0.025 | ~65 | |

The edges are frayed by `a`: ±0.15 on temperature through the 0.01 weight, which works out to a few blocks of dither.

### 1.2 Terrain density
```
minLimit = fractal 3D improved Perlin (seed, 16 octaves, skip 0)
maxLimit = fractal 3D improved Perlin (seed, 16 octaves, skip 16)   // "skip" = that many octaves' RNG skipped
main     = fractal 3D improved Perlin (seed,  8 octaves, skip 32)
```
Lacunarity 0.5 and persistence 2. Grid: xSize 5, zSize 5, origin `cx*4, 0, cz*4` in cell units. Terrain height 256,
so `ySize = 33`, `quarterYSize = 256/4/8+1 = 9`, `threeQuarterYSize = 192/8+1 = 25`.

The field is computed for dy = 0..24 and stored at rows `dy + 8` (rows 8..32 are y 64..256). Rows 0..7 stay 0.0.
Sampling scales:
```
main  region(..., 8.55515, 34.2206, 8.55515)      // 684.412/80, 2053.236/60
limit region(..., 684.412, 2053.236, 684.412)
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

**Block rule**, in order (the rand is seeded per chunk with `cx*341873128712 + cz*132897987541`; calls follow the lerp
loop order blobX, blobZ, blobY, yInBlob, xInBlob, zInBlob):
```
y < 63                         -> BEDROCK
y >= 255 - rand.nextInt(10)    -> BEDROCK      (y 255 100%, 254 90% … 246 10%)
y <= 63 + rand.nextInt(10)     -> BEDROCK      (y 63 100%, 64 90% … 72 10%)
density > 0                    -> COBBLE_NETHERRACK (world filler)
y < 96 (sea) -> fluid by land at (x, 96, z):
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
- In effect it is a 3D cave sponge from about 84 to 238, flooded to 95.

### 1.3 Surface
Per column, using `rand = Random(cx*341873128712 + cz*132897987541)`:
```
sand  = beachNoise.region(x,z,0; 16,16,1; 1/32,1/32,1)   + rand.nextDouble()*0.2 > 0   // beach = sand || gravel
grav  = beachNoise.region(x,109.0134,z; 16,1,16; 1/32,1,1/32) + rand.nextDouble()*0.2 > 0
soil  = (int)(soilNoise.region(x,z,0; 1/16 ...)/3 + 3 + rand.nextDouble()*0.25)       // usually 2..4
```
Noise seeds: beach improved Perlin (seed, 4, skip 40), soil (seed, 4, skip 44). A main noise (seed, 8, skip 32) is
also created but unused.

Scan y = 255 → 0 (land from the chunk's 8-tall cells):
- **Air** sets `depth = -1`. top/filler are taken from the land at the first air block, once per column.
- **Any non-air block that is not the filler** (lava, water, bedrock, netherrack, the rubyglass crust) is skipped
  without touching `depth`.
- **First filler under open space** (`depth == -1`):
  - `soil <= 0`: top = AIR, and filler by land: Volcanic **BRIMSAND**, Sulfur **SULFUR**, Old World and Desert
    **GLOOMSTONE / COBBLE_GLOOMSTONE 50/50**, otherwise **NETHERRACK**.
  - else if `92 <= y <= 97`: land top/filler, but if beach then **SOUL SAND / SOUL SAND**.
  - else if `y <= 96` and the land has sulfur pools: top **BRIMSAND**, filler **BRIMSTONE**.
  - If `y < 96` and top == AIR: top = LAVA.
  - Then `depth = soil + 2`. Place **top if y >= 95, otherwise filler**: floors below the sea line get the filler.
- `depth > 0`: place filler, `depth--`. In the Old World, when an ash filler runs out, `depth = rand(8)+2` and
  filler = **SLATE**.
- `depth == 0` (deep rock) by land:
  - Crag and Shelf: **NETHERRACK / COBBLE_NETHERRACK 50/50**
  - Crystal: **COBBLE_NETHERRACK only**
  - Volcanic: **BRIMSAND**
  - Sulfur: **BRIMSTONE**
  - Old World and Desert: **GLOOMSTONE / COBBLE_GLOOMSTONE 50/50**
- Stone variant mix (top and filler only): in Crag, Shelf and Crystal, cobbled netherrack becomes netherrack or
  cobble 50/50; in the Old World, gloomstone stays a gloomstone/cobbled 50/50 mix.

Resulting columns, top → down (top block ~1, filler soil+2 ≈ 4–6, then deep rock):

| land | top | filler | below | deep |
|---|---|---|---|---|
| Crag, Shelf | rack/cobble speckle | rack/cobble speckle | – | rack/cobble |
| Crystal | rack/cobble speckle | rack/cobble speckle | – | cobble (crystalline veins added later) |
| Volcanic | brimsand | brimstone | – | brimsand |
| Sulfur | brimstone (brimsand at/below the sea) | brimstone | – | brimstone (+ sulfur pockets) |
| Old World | ash | ash | slate 2–9 | gloomstone/cobbled gloomstone |

Shores at y 92–97 are about 75 % soul sand. Caves later re-top cut floors: if the block under a carved cell equals the
land's filler, it becomes the land's top.

### 1.4 Large features
- **Caves** (the classic tunnel-and-room carver, y 64..256):
  - 1 chunk in 15 starts `rand(rand(rand(40)+1)+1)` tunnels at `y = 64 + rand(rand(184)+8)`.
  - 1/4 chance of a hub room (radius 1–7, height ×0.5) with +rand(4) extra branches.
  - Tunnel radius `rand*2 + rand`, length 112 − rand(28), branching at a quarter to three quarters of the length.
  - Clamped to y 65..247.
  - A segment that would touch lava or water (the sea block list) is skipped.
  - Cuts only these blocks: netherrack, cobbled netherrack, basalt, block of ash, soul sand, rubyglass crystal,
    gloomstone, cobbled gloomstone, ember, brimsand, brimstone, sulfur. **Not** slate or crystalline.
- **Lava floes** (Volcanic Islands only, land at y 96):
  - Worley cells of **1 chunk** (Worley noise from `Random(seed)`, sampled at `cx + x/16`).
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

Land abbreviations: VI Volcanic, CR Crag, SP Sulfur, SH Shelf, CF Crystal Forest, CP Crystal Plains, OW Old World,
OD Desert.

| # | decoration id | feature (params) | lands | y | per chunk |
|---|---|---|---|---|---|
| 1 | rubyglass_crystal_ceiling | RubyglassCrystalline(ceiling, min 20, mod 15) | CF | Upper | `max(0,(int)((N(x/2,z/2)+24)/6)+4)`; N = fractal 2D improved Perlin (seed, 8, skip 74) at the chunk corner; mean **~8** [H] |
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
| 15 | lava_lake | Lake(lava still) (16×8×16 blob lake, edges → filler) | VI CR CF CP SH SP | Upper | 1/4 |
| 16 | lava_pool_terrace | Terrace(lava still, brimstone, r 4+rand 4) | VI | MinY | 1/3 |
| 17 | boulder_magma (1st) | same Terrace(lava, brimstone, 4, 4) — duplicate id, but both run | VI | MinY | 1/3 |
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
| 41 | scatter_ember_ash | ScatterEmberAsh | no mask | FromOcean | 20 |
| 42 | scatter_bone_pile | ScatterBonePile | no mask | FromOcean | 8 |
| 43 | scatter_boulder_magmatic | ScatterBoulderMagmatic | CR SH VI | FromOcean | 8 |
| 44 | scatter_boulder_magmatic_sparce | ScatterBoulderMagmatic | CF CP | FromOcean | 4 |
| 45 | scatter_boulder_sulfuric | ScatterBoulderSulfuric | SP | FromOcean | 24 |
| 46 | patch_soul_catcher | SoulCatcherPatch | no mask | FromOcean | 8 |
| 47 | pebbles | 16 × Pebbles(16, vary) at y 64..191 | no mask | — | **no-op**: pebbles only go on grass/sand/dirt/sandstone/scorched dirt |
| post | ash pass | **LAYER_ASH** on every air block whose block below is solid (blocks motion), y 255 → 97, in surface-ash lands (OW, OD); not on obsidian, soul sand or rubyglass node/column/growth | OW | — | every column |

**Feature details**
- **Ore** (a vein of `n` blocks along a random line ±n/8; replaces only the variant map's keys). Nether coal:
  COBBLE_NETHERRACK/COBBLE_BASALT/COBBLE_GLOOMSTONE → ORE_NETHERCOAL_{NETHERRACK,BASALT,GLOOMSTONE} (**cobbled
  variants only**). Node: RUBYGLASS_COLUMN → NODE. Sulfur: BRIMSTONE, BRIMSAND → SULFUR. Soul sand: GLOOMSTONE,
  COBBLE_GLOOMSTONE, BLOCK_ASH, SLATE → SOULSAND.
- **Glowstone**: the start must be air with a **solid block directly above**, else it fails; then 1500 tries at ±7
  x/z, 0..11 down, setting air with exactly 1 glowstone neighbour. Success per try [H] ≈ 0.625 × 0.016 ≈ **1 %** →
  **~0.12 clusters/chunk** (≈1 per 8 chunks), ~0.33 in Sulfur, 0 in the Old World. The classic Nether is lit by lava,
  not glowstone.
- **NetherSpring**: fails if the block above is nether surface, or the block itself is a non-air nether surface;
  needs exactly 4 of {4 sides + below} nether surface and 1 air; places the flowing fluid and ticks it once →
  lavafalls and waterfalls out of walls. Nether surface: netherrack, cobbled netherrack, cobbled basalt, rubyglass
  crystal, gloomstone, ember, brimsand, brimstone, brimstone bricks, sulfur.
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
- **Terrace**: circle r at +rand(8); in every column scan y 255 → 101: a nether surface block with air above becomes
  the **wall** (rim); inside r−1, one that is surface with air above and non-air on 4 sides + below becomes **fluid**
  → 1-deep pools cut into **every** floor inside the cylinder.
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
- **Fire**: 32 tries ±7/±3 on air. Default: FIRE over infinite-burn blocks (netherrack, cobble, crystalline, cobble
  slab), FIRE_SULFURIC over sulfuric infinite-burn blocks (glowstone, brimsand, brimstone family, brimthaw, sulfur).
  Volcanic variant: FIRE on any surface block and the ground → NETHERRACK.
- **Scatter** (shared): scan down ≤96 to ground; never on rubyglass node/column/growth; air above; the block below
  must render solid. BonePile: never on obsidian, 1/8 on ash. Boulders: magmatic anywhere, sulfuric only on brimsand,
  brimstone or brimthaw. EmberAsh: 3/4 of tries proceed; a BLOCK_ASH with air above in OW/OD and ash on all 4 sides
  → **EMBER** with LAYER_ASH on top (a hidden mine).
- **SoulCatcherPatch**: ground on soul sand gets 28 tries, else 6 tries at 1/5; ±2 x/z, ±1 y; not on obsidian.
- **Structures: none** in the generator itself. Our fortresses and other Nether structures come from
  `structures.build` on top.

---------------------------------------------------------------------------------------------------------------------
## 2. Blocks placed by the classic Nether

Light values: a float emission f gives `(int)(15f)`; an int gives the value & 15. Each generator block maps to one
of ours in `src/gen/nether_exact.cpp` (`nbMap`, with fallbacks where a block is missing). "avg" is the mean opaque
colour our art should aim for. Emissive masks: those pixels render full-bright, but the block gives no light unless it
has emission.

| our key | name | render | light | hard. | avg colour | behaviour |
|---|---|---|---|---|---|---|
| netherrack | Netherrack | cube | 0 | 0.32 | #6b3535 | infinite burn; drops cobbled netherrack |
| cobbled_netherrack | Cobbled Netherrack | cube | 0 | 0.4 | #6e3533 | **world filler**; infinite burn; grows rubyglass |
| crystalline_netherrack | Crystalline Netherrack | cube | 0 | 0.4 | #7b2a2e + emissive | glowing red veins (emissive only) |
| gloomstone | Gloomstone | cube | 0 | 2.0 | #332741 | Old World deep rock; drops cobbled |
| cobbled_gloomstone | Cobbled Gloomstone | cube | 0 | 2.0 | #312742 | |
| brimsand | Brimsand | cube | 0 | 0.7 | #2e2b24, near-black | sand material but **does not fall**; sulfuric infinite burn |
| nether_brimstone | Brimstone | top/side/bottom | 0 | 1.0 | #2f2e2c | dark volcanic rock; drops itself (brimsand ×1–2 when crushed by a piston) |
| brimthaw | Brimthaw | cube | **10** | 1.25 | #605c1f + emissive | like magma for acid: melts into acid next to acid; sulfuric fire hurt; collision −1/8 |
| sulphur_block | Block of Sulfur | cube | 0 | 0.5 | #ab8e1c | drops 4 sulfur; shovel |
| ash | Block of Ash | cube | 0 | 0.2 | #454046 | shovel |
| ash_layer | Ash Layer | 1/8-tall layer | 0 | 0.1 | (ash) | stepping on it over an EMBER sets the ember off |
| ember | Ember | cube (animated, 4 frames, interpolated, frametime 3) | **5** | 1.0 | #3a271a + emissive | touched by a player or wolf: 8 blast damage, fire for 100 ticks, fling 0.65 up, block removed, explosion 3.0 with fire. Particles: flame + large smoke. Collision −1/64 |
| slate | Slate | column | 0 | 1.5 | #757380 | not cut by caves |
| soul_sand | Soul Sand | cube | 0 | 0.5 | #544033 | slows to ×0.4; collision −1/8 |
| obsidian | Obsidian | cube | 0 | 10 | #14121d | Old World "sea" |
| bedrock | Bedrock | cube | 0 | ∞ | | |
| glowstone | Block of Glowstone | lit cube | 15 | 0.3 | #897040 | |
| fire / sulfuric_fire | Fire / Sulfuric Fire | fire | 15 / 15 | 0 | sulfuric #e0d620, **yellow** | the sulfuric one burns forever on sulfuric infinite-burn blocks |
| lava | Lava | fluid | 15 | – | | |
| water | Water | fluid | 0 (blocks 3) | – | | tinted (255,50,32) in the Nether |
| acid | Acid | fluid | 0 | – | #cccb09 + emissive (animated, 4 frames, interpolated, ft 30) | only appears via the terraces |
| rubyglass_crystal | Rubyglass Crystal | translucent cube, 4 variants by meta&3 | 0 | 1.5 | #740000 + emissive | ice material, friction 0.98 (slippery). **Breaks when stood on**: data 0→3 every 5 ticks while a mob is on it, then shatters; recovers when empty; no drop. Random crackle particles |
| rubyglass_column | Rubyglass Column | cube | 0 | 2.0 | #86091a + emissive | the giant crystals |
| rubyglass_node | Rubyglass Node | tinted cube (2-frame animation) | 0 | 3.0 | #a21234 + emissive | |
| rubyglass_growth | Rubyglass Growth | custom (bud, bottom, top) | 0 | 1.0 | #8a091a + emissive | oriented by support side; 1 or 2 tall; ticking |
| molten_pumice | Molten Pumice | lava base + pumice overlay | **9** (0.625) | 1.0 | | drips lava particles |
| magma_block | Magma | cube | **10** | 1.25 | #8a3e28 + emissive | melts into flowing lava when next to lava (and no water or acid); fire hurt; collision −1/8 |
| nether_coal_ore / basalt_coal_ore / gloom_coal_ore | … Nether Coal Ore | cube | 0 | 3.0 | #7f3a29 + emissive | |
| nether_coal_block | Block of Nether Coal | cube | 0 | 3.0 | #71220f + emissive | |
| basalt | Basalt | cube | 0 | 1.5 | #3f3f3f | vents and spires |
| thermal_vent (falls back to steam_vent) | Thermal Vent | top/side/bottom | 0 | 1.5 | #353535 | **active only with a vent below** (spires have 2 stacked). Builds up on 3/4 of ticks (+2 per tick over magma); at state 4 it erupts: 40 smoke and flame particles, flings entities in 3×7×3 by +2.0 y, alerts players within 200 |
| magmatic_boulder | Magmatic Boulder | cross, box 0.1–0.9 × 0.8 | 0 | 1.0 | #774229 + emissive | sets the toucher on fire (100 ticks); drops pebbles and 1/4 nether coal |
| sulfuric_boulder | Sulfuric Boulder | cross | 0 | 1.0 | #787722 + emissive | same, with sulfuric fire; 1/4 sulfur |
| bone_pile | Bone Pile | cross | 0 | 1.0 | #938a80 | broken by fluids |
| soul_catcher | Soul Catcher | cross | 0 | 1.0 | #46362c | |

Related family (not placed by generation): polished basalt, cobbled basalt, polished and carved netherrack,
netherrack bricks, polished/carved gloomstone, gloomstone bricks, brimstone bricks, rubyglass block and bricks, soul
schist, dry pumice.

---------------------------------------------------------------------------------------------------------------------
## 3. Visuals
Our renderer for this: `src/render/nether_grade_layer.cpp` and `assets/shaders/nether_grade.glsl` (grade and heat
haze), plus the Nether fog in `src/render/sky.cpp`. The target values:

**Fog colour**
- A 256-wide ramp sampled at `x = (int)((1-temp)*255)`, with temp = the block temperature at the camera. It is
  continuous and blends smoothly; land identity is not used.

  | temp | 1.0 | 0.9 | 0.8 | 0.72 | 0.6 | 0.5 | 0.35 | 0.25 | 0.1 | 0.0 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | RGB | 214,121,0 | 193,103,0 | 163,79,0 | 134,57,0 | 95,26,0 | 69,5,0 | 44,0,2 | 33,6,6 | 22,14,13 | 16,16,15 |

  The **mean temperatures per land from §1.1**: Volcanic 0.99, Crag/Sulfur 0.88, Shelf 0.73, Crystal 0.59, Old World
  0.26.
- The sky is black, so `fog = ramp * (1 - rds)`, with `rds = 1 - (1/(40-rd))^0.25`. That factor is 0.435 at render
  distance 12, 0.42 at 8, 0.452 at 16.
- Then multiplied by a fog brightness that eases toward `ramp[blockLight@camera]*(1-m)+m`, with `m = (32-rd)/32`.
  At rd 12 that is 0.68 to 1.0.
- The background is the fog colour, flat.

**Sky:** none. No sky, sun, stars, clouds or void plane, and no aurora.

**Fog distance**
- Linear, on Euclidean distance. **Start 0**, end `min(rd*16, 512)`.
- In the Old World the fog eases (+0.0005 per fog setup) to start 0.15·far and end 0.45·far: 28.8 → 86.4 blocks at
  rd 12.
- Inside lava: EXP density 2.0, colour (0.6, 0.1, 0) × brightness. Inside acid: EXP 1.2, colour (0.2, 0.2, 0.1).
  Inside water: EXP 0.1, colour (0.5, 0.098, 0.063).

**Colour grade:** parametric (no lookup table), on the world picture only.
- Option: Nether Color Correction, default 0.5; used in the Nether.
- Per tick in a hot world: `scale = 1` (or `2·temp` when temp < 0.5, which also does `saturation -= 0.5(1-2t)`);
  `rMod += 1.2·scale`, `gMod += 0.3·scale`, `saturation += 0.125·scale`, `exposure -= 0.125`, `contrast -= 0.05`,
  `brightness -= 0.05`.
- The slider multiplies brightness, contrast, exposure and saturation; each colour mod becomes `1-(1-mod)·slider`.
  Values ease at 5 % per tick.
- **Defaults (temp ≥ 0.5): brightness −0.025, contrast −0.025, exposure −0.0625, saturation +0.0625, R×1.6, G×1.15,
  B×1.0.**
- Order: `c += brightness; c = 0.5 + (1+contrast)(c-0.5); c *= 1+exposure;
  c = mix(luma(Rec709), c, 1+sat); c *= rgbMod`. Mid-grey 128 comes out (183, 131, 114).

**Heat haze**: option `heatHaze` (default 0.5), whole screen in the Nether, `amount = scale·slider·3` (1.5 default).
UV warp: `s = clamp((pow(depth,500)-0.16)*2,0,1)*amount`;
`uv += vec2(sin(uv.y*65-10t)+sin(uv.y*250-14t), 2 sin(uv.x*50+5t)) * s * 1e-4`. Starts ~13 blocks, full ~52 (rd 12);
under 1 px, a faint distant shimmer.

**Particles**
- Ambient per tick around the player: Old World 3 ash motes in ±12; Volcanic 3 ash motes + 2 flames in ±24; other
  lands none. Ash mote: grey 0.2–0.5, falls `vy - (0.02..0.07)`/tick constant, horizontal ±0.05 damped ×0.9,
  `size = rand²·6+1` (×0.1), lives 66–322 ticks, smoke-puff sprite.
- Block display ticks: lava pop 1/100 on open tops, drip 1/4 under a hanging block; acid boiling, drips, fizz; fire 3
  large smoke; ember smoke every tick + flame at 1/3; molten pumice lava drips; vent smoke (distance throttled);
  portal. Music: our own Nether tracks; no Nether-only ambient loop is needed by the design.

**Light**
- No sky light anywhere (ceiling).
- Lightmap = `ramp[blockLight]`, with `ramp[i] = (1-f)/(3f+1)*0.85 + 0.15`, `f = 1 - i/15`:

  | level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | ramp | .150 | .165 | .182 | .200 | .221 | .244 | .271 | .303 | .339 | .382 | .433 | .496 | .575 | .676 | .811 | 1.0 |

  The 0.15 floor is additive. Gamma and brightness are no-ops at their defaults.
- Emissive masks are full-bright.
- The vignette is strong because the scene is dark.

---------------------------------------------------------------------------------------------------------------------
## 4. The older Nether versions, and what is left

Terrain version 7 and later run the classic generator (`gen/nethergen`, mapped by `gen/nether_exact.cpp`), so
sections 1–3 describe what new worlds get. Version 5 (`generateLands`, a hand-made heightfield of 5 lands) and version
6 (`DepthsGen::generateCave` in `src/gen/nether_lands.cpp`, a looser cave Nether) are kept only for old saves. Heights
need **no scaling** (all are 256 tall, sea 96). The list below is how version 6 differs from the classic Nether. It
is a record, not a to-do: version 6 stays as it is so old saves do not change. The open items for the current
Nether are at the end.

**Version 6 vs the classic Nether, the whole picture**
1. **Grade and fog**: version 6 used per-land colours (yellow Sulfur, purple Crystal, blue-grey Shelf), a sky dome
   and no tint; its Volcanic colour (0x2a0a06) is among the darkest, where the classic Volcanic is the **brightest
   orange**. Fog started at 0.14·viewFar. The classic look (§3) is now drawn by `nether_grade_layer.cpp`.
2. **Climate noise scale**: `NetherClimate::f2` (`nether_lands.cpp`) leaves out the 2D simplex octave rule
   (`scale/1.5`, `amplitude*0.55`), so the shares come out wrong:

   | | Old World | Volcanic | Crag | Crystal Forest | Sulfur | Shelf | Crystal Plains |
   |---|---|---|---|---|---|---|---|
   | version 6 | 29 % | 23 % | 20.5 % | 10 % | 8.8 % | 5 % | 4 % |
   | classic | 10 % | 34 % | 30 % | 13 % | 5.4 % | 6 % | 1.8 % |

   Its patches are also about half as wide, and T and H are not quantised to `(int)(v*50)/50`.
3. **Surface materials** (`skinFor`, `deepOf`, `mixPlace` in `nether_lands.cpp`):
   - Shelf: permafrost over packed ice, where the classic one is rack/cobble with brimsand terraces.
   - Volcanic: cinder_sand over yellow sulphur_block, where the classic one is brimsand over near-black brimstone.
   - Sulfur Pools: all yellow sulphur_block, where the classic land is black brimstone with 40 sulfur pockets and
     1000 glowing brimthaw clumps per chunk.
   - Crag / Crystal deep rock adds granite, limestone and gloomstone; Old World deep rock mixes gloomstone,
     limestone, slate and blackstone.
   - cinder_sand falls where brimsand does not, so overhangs collapse.
4. **Large features missing**: lava floes, rubyglass floes, shelf terraces, and caves (`generateCave` never calls
   `carveCaves`).
5. **Density main noise**: `fieldDensity` uses the same `skip = 16-octaves` for all three noises, so with 8 octaves
   the main noise starts 256× too low in frequency and 256× too loud; the blend practically always saturates to min
   or max. Version 6 also uses simplex 3D where the classic one uses improved Perlin.
6. **Glowstone**: `DepthsGen::glowstone` (`depths.cpp`) makes 5–10 clusters per chunk with an upward search, where the
   classic Nether has ~0.12 per chunk, lit by lava.

**Version 6, signature features**
7. Giant crystals are straight 3–16-tall `red_crystal` columns (glass, light 9) instead of tilted 20–50-long rubyglass
   prisms with nodes, crystalline beds and growths.
8. The crust at y 95 is `red_crystal` (light 9) and holds; the classic crust gives no light and breaks underfoot.
9. Old World ash: a full ash block on 22 % of columns (8 % smouldering) instead of an ash layer on every exposed top,
   ember mines, 40 soul sand pockets per chunk, slate pillars and roof spires.
10. Pillars: one 1-wide column on 1/8 of 32-cells instead of thick hourglass columns.
11. No springs, lava lakes, lava or acid terraces, or glowstone mounds in acid; only 3×3 acid dimples.
12. Vents: basalt+magma stacks with a lava cap and steam_vent, instead of floor vents, spire vents, magma boulders
    and roof spires.
13. Fire: plain fire, 18 tries, no sulfuric fire.
14. Scatter: 2-block `bone_block` piles in the Old World only; no boulders or soul catchers.
15. Ores: version 6 adds quartz, nether gold, cinderite, gold, ancient debris, ember/limestone/granite ores, soul
    schist and bone shale. These are our own items and stay.

**Version 6, mostly hidden or deliberate**
16. Bottom of the band: bedrock 0–3, then a fluid layer at y 5–55 under a rock slab at 56–~85 (keeps the stacked-world
    shaft open), where the classic one is bedrock 0–62, ragged 63–72.
17. Land resolution: per-column climate instead of one land per chunk for decorations.
18. Old World Desert is unreachable in both (below the y 64 floor).
19. Structures from `structures.build` (our fortresses and others) run on every version. Deliberate.
20. Visual details: version 6's light curve is `l/(3.2-2.2l)` with a max-floor of 0.16 (level 8 ≈ 0.26 vs 0.339);
    lava fog (1, .35, .04) linear over 22 blocks; blue water; many extra emitters (eruptions, glints, sulfur fumes,
    shelf flakes) and tiny ash (0.03); acid light 4; magma light 3; molten pumice light 12.

**Still open for the current Nether**
- Blocks with no exact match yet fall back in `nbMap` (`nether_exact.cpp`): for example `thermal_vent` falls back to
  `steam_vent` / magma. Any block that resolves to its fallback still needs its own entry, art and behaviour.
- Global-random details the generator cannot reproduce from the seed alone (fire age, the state of a stacked vent)
  use a per-chunk stand-in; see the "Not exact" list in `src/gen/nethergen/README.md`.
- Art: all textures are our own (`tools/port/classic_blocks.py`). Use the average colours in §2 as targets when
  painting; nothing is copied in.
