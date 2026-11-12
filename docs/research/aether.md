# The Aether (1.19.4-1.5.1-forge) — sky dimension spec for DokuQuest

Source: `/home/admin/Documents/Projects/Aether/` (MANIFEST `Implementation-Version: 1.19.4-1.5.1-forge`, built 2024-11-09).
Everything below was read from the jar's worldgen JSON or from CFR-decompiled classes
(`research_aether/src/com/aetherteam/aether/**`). "y" means Aether y. The dimension is exactly 256 tall with
min_y 0, the same as a DokuQuest band, so **Aether y = DQ band-local y** gives a 1:1 mapping (global y 768 + y).
Helpers written for this report (scratchpad `research_aether/tools/`):
- `AetherDensity.java`: a working port of the terrain density with a statistics pass (`java AetherDensity.java <seed> <size> [top.pgm]`).
- `aether_density.cpp`: the same code in C++, ready to adapt for `skylands.cpp`. It was not compiled because this machine has no C++ compiler.
- `nbt.py` reads the dungeon templates. `png.py` and `cmp2x.py` measure texture colours.
- `top_1.png` is a 1024² top-surface heightmap from the port.

---------------------------------------------------------------------------------------------------------
## 1. Terrain

### 1.1 Dimension and noise settings
| item | value | file |
|---|---|---|
| dimension type | `aether:the_aether`: min_y 0, height 256, logical_height 256, has_skylight, no ceiling, ambient_light 0, bed works, respawn anchor no, raids no, natural, effects `aether:the_aether`, monster light 0–7, block-light limit 0, coordinate_scale 1, fixed_time none | `data/aether/dimension_type/the_aether.json` |
| generator | `minecraft:noise`, settings `aether:skylands`, biome source `multi_noise` (§2) | `data/aether/dimension/the_aether.json` |
| noise cell | min_y 0, **height 128** (no terrain at y ≥ 128), size_horizontal 2 → cells 8 blocks wide, size_vertical 1 → cells 4 blocks tall | `noise_settings/skylands.json` |
| default_block | `aether:holystone` | |
| default_fluid | `minecraft:water`, but **sea_level −64**, so there is no sea. aquifers_enabled false, ore_veins_enabled false | |
| router | continents/erosion/depth/ridges/barrier/lava/veins all 0. Only `temperature` and `vegetation` noises are used (biomes) | |
| carvers | none: every biome has `"carvers": {}`. **No caves.** The only carving is the Gold Dungeon's "gumdrop caves" (§3.9) | biome JSON |

### 1.2 final_density (exact)
```
N  = old_blended_noise{xz_scale .25, y_scale .25, xz_factor 80, y_factor 160, smear_scale_multiplier 8}  (aether:base_3d_noise_aether)
G1 = y_clamped_gradient(y, from_y 8 → to_y 40, 0 → 1)        // 0 at y≤8, 1 at y≥40
G2 = y_clamped_gradient(y, from_y 56 → to_y 128, 1 → 0)      // 1 at y≤56, 0 at y≥128
JSON: -0.05 + (-0.1 + G1*(0.1 + (-0.2 + G2*(0.2 + (-0.13 + N)))))
    = -0.15 + G1*( G2*(N + 0.07) - 0.1 )
final = squeeze( interpolated( blend_density( that ) ) )     // squeeze(x)=x/2-x³/24 after clamp[-1,1]; keeps the sign
solid holystone where final > 0, else air (sea_level -64, so never water)
```
- `interpolated` means density is evaluated only at cell corners every 8 blocks in x/z and every 4 in y, then
  trilinearly interpolated. This gives the smooth, slightly faceted blobs.
- A block is solid when **N > (0.15 + 0.1·G1)/(G1·G2) − 0.07**:

| y | ≤8 | 12 | 16 | 24 | 32 | 40–56 | 72 | 88 | 104 | 112 | ≥118 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| N threshold | never | 1.23 | 0.63 | 0.33 | 0.23 | **0.18** | 0.25 | 0.38 | 0.68 | 1.06 | never |

- **BlendedNoise** is vanilla's old (≤1.17) terrain noise. It has two 16-octave limit noises (lo, hi) and one
  8-octave selector noise:
  - `xzMul = yMul = 684.412·0.25 = 171.103`.
  - selector `n = Σ_{p<8} noise(x·171.1/80·2^-p, y·171.1/160·2^-p, …)·2^p`, then `q = (n/10+1)/2`.
  - limits `l,m = Σ_{r<16} noise(x·171.1·2^-r, …)·2^r`.
  - result `N = clampedLerp(l/512, m/512, q)/128`.
  - Feature sizes: the strongest limit octave has ≈191-block lattice cells at amplitude 0.5, then 96-block at
    0.25, 48-block at 0.125, … The selector's largest octave is ≈60 blocks horizontally and ≈120 vertically.
    Because n/10 is usually far outside ±1, q snaps to 0 or 1, so each place picks one of the two limit noises.
  - Smear: the y-gradient fraction is floored to steps of `171.1·8/160·2^-p` (selector) and `1368.8·2^-r`
    (limits) once those steps are below 1. This flattens the low-frequency octaves into horizontal ledges and
    terraces.
  - Measured with the port: N mean ≈0, **sd 0.18–0.20**, p1 −0.45, p99 +0.45, extremes ±0.8.

### 1.3 Resulting shape (port statistics, 3 seeds, 512²–1024² × 128)
| metric | value |
|---|---|
| columns with any land | **28–33 %** |
| solid fraction by y | 0 % below y 18 · y20 0.2–0.5 % · y24 2 % · y28 5 % · y32 8–12 % · y36 12–15 % · **y40–60 15–20 % (peak)** · y64 11–16 % · y72 7–13 % · y80 5–10 % · y88 1–6 % · y96 0–1.6 % · 0 % above ~y105 |
| land range | lowest solid y ≈18–20, highest ≈93–105 |
| top surface y (highest solid in a land column) | p5 34–45 · **p50 60–73** · p95 85–96 |
| bottom y | p5 23 · p50 36–40 · p95 65–71 |
| slabs per column | 1 slab 22–26 % · **2 stacked 5–7 %** · 3 slabs 0.7–1.2 % · 4+ 0.1 % (of all columns) |
| slab thickness | p25 4–6 · **p50 11–14** · p75 23–29 · p95 43–58 · max 67–83 |
| air gap between stacked slabs | p25 3–4 · p50 6–7 · p95 23–27 |
| connected islands per 1024² | 233–277 (most are 1–50-block crumbs). 39–58 are ≥500 blocks: horizontal extent p25 ≈30, **p50 41–49**, p75 60–157, max 450–550 (merged archipelagos with holes and channels) |

How it looks (`tools/top_1.png`):
- Irregular, lumpy "continents" 100–500 blocks across, full of bays, holes and arches.
- Plenty of 15–60-block islets.
- Tops are fairly flat and terraced (the smear). Bottoms are rounded blobs, not cones.
- Islands thin out by **density, not by a horizontal mask**:
  - Below y≈32, G1 raises the threshold, so bottoms round off and nothing exists under y≈18.
  - Above y≈60, G2 does the same, so few tops reach 90 and none reach 105.
  - Edges taper over one 8-block cell. That leaves 1–2-block-thick rims, which the quicksoil shelves need (§3.5).

### 1.4 Surface rule (`noise_settings/skylands.json`)
1. Every **floor** gets a soil cap: the top of each slab, every terrace, and the floor under each overhang.
   - Top block (stone_depth floor, offset 0): `aether_grass_block`. The water check (`offset -1`) always passes
     because there is no noise water.
   - Next `surfaceDepth` blocks (stone_depth floor + surface depth): `aether_dirt`.
   - Vanilla `surfaceDepth = int(surfaceNoise·2.75 + 3 + rand·0.25)`, where minecraft:surface has firstOctave −6
     and amps [1,1,1]. That gives about **3 blocks of dirt (range 0–5)**.
2. Everything else stays holystone. Ceilings are plain holystone.

### 1.5 Fluids, sky floor, falling out
- No sea and no aquifers. Water comes only from lakes and springs (§3.6).
- **Nothing generates below y≈18.** Large Aerclouds (§3.10) drift at y 0–31, and the client draws the vanilla
  cloud layer at **y 9.5** (§5).
- Biome tag `aether:fall_to_overworld` covers all 4 biomes: falling out of the bottom puts you in the Overworld.
  DQ's stacked column already does this.

### 1.6 Mapping to DokuQuest
- **1:1 option:** Aether y = DQ band-local y.
  - Islands land at band 18–105 (global 786–873). Land at y < 30 (~2–5 % of solid) sits inside DQ's
    `cloudBank` (band 0–30).
  - Band 128–255 holds only golden aerclouds (≤128) and treetops, as in the Aether.
- Current DQ (`SKY_DY = 120`) puts grass at band ≈183–197 and undersides down to ≈141.
- **Offset option:** `band_y = aether_y + k`. With k ≈ 100 the Aether's 18–105 maps to 118–205.

---------------------------------------------------------------------------------------------------------
## 2. Biomes (`data/aether/worldgen/biome/*.json`)
All four biomes have **identical** effects, temperature, spawns and feature lists. Only the tree-count
feature differs.

| field | value (all 4) |
|---|---|
| ids / names | `aether:skyroot_meadow` "Skyroot Meadow", `aether:skyroot_grove` "Skyroot Grove", `aether:skyroot_woodland` "Skyroot Woodland", `aether:skyroot_forest` "Skyroot Forest" |
| temperature / downfall / precipitation | 0.8 / 0.0 / **none** (never rains or snows; weather only tints colours) |
| sky_color | 12632319 = **#C0C0FF** |
| fog_color | 9671612 = **#9393BC** |
| water_color / water_fog_color | 4159204 = #3F76E4 (vanilla) / 329011 = #050533 (vanilla) |
| grass_color / foliage_color | 11665355 = **#B1FFCB**. Used only for vanilla plants: `AetherColorResolvers` tints minecraft grass, tall_grass, fern and large_fern **#B1FFCB on aether grass** and **#FCE964 on enchanted aether grass**. Aether grass, leaves and flowers are untinted textures. |
| music | `aether:music.aether`, min_delay 12000, max_delay 24000 ticks, replace_current false |
| creature_spawn_probability | 0.25 |

**Feature order**, by generation step:
- 0 `quicksoil_shelf`
- 1 `water_lake`
- 6 ores: `aether_dirt_ore, icestone_ore, ambrosium_ore, zanite_ore, gravitite_ore_buried, gravitite_ore`
- 8 `water_spring`
- 9 vegetation: `<biome>_trees, holiday_tree, grass_patch, tall_grass_patch, white_flower_patch, purple_flower_patch, berry_bush_patch`
- 10 top layer: `crystal_island, cold_aercloud, blue_aercloud, golden_aercloud`

**Trees per biome** (`placed_feature/skyroot_*_trees.json` → `trees_skyroot_and_golden_oak`):
- meadow: rarity 1, i.e. 1 attempt
- grove: count 2 (90 %) or 3 (10 %)
- woodland: count 5 (90 %) or 6 (10 %)
- forest: count 6 (90 %) or 7 (10 %)

Each attempt then uses `improved_layer_placement` (count 0–1 per layer, see §3.0), so the expected trees per
chunk per island layer are **meadow ≈0.5, grove ≈1.05, woodland ≈2.55, forest ≈3.05**. Trees also need
surface_water_depth 0, a sapling that would survive, and no dungeon underneath.

**Biome map**: multi-noise on temperature × humidity only. The other parameters are [-1,1].
- Noises: `aether:temperature` (firstOctave −8, amps [1.5,0,1]) and `aether:vegetation` (firstOctave −7, amps [1,1]).
  Both are shifted_noise with xz_scale 0.25 and y_scale 0, so regions are roughly **1000 × 500 blocks**.

| temperature | humidity → biome |
|---|---|
| −1.0…−0.8 | any → meadow |
| −0.8…0.0 | −1…0 meadow · 0…1 forest |
| 0.0…0.4 | −1…0 grove · 0…0.8 forest · 0.8…1 grove |
| 0.4…0.93 | −1…−0.1 grove · −0.1…1 forest |
| 0.93…0.94 | −1…−0.6 meadow · −0.6…−0.3 grove · −0.3…1 forest |
| 0.94…1.0 | −1…−0.1 meadow · −0.1…0.8 woodland · (0.8…1 unlisted → nearest, woodland/forest) |

**Mob spawns** (identical in all biomes; weight, group min–max):

| category | cap | entries |
|---|---|---|
| creature | vanilla 10 | phyg 10 (3–4) · sheepuff 12 (3–4) · flying_cow 12 (2–5) · aerbunny 11 (3) · moa 8 (1–3) |
| aether_surface_monster | 15 | aechor_plant 7 · blue_swet 6 · golden_swet 6 · whirlwind 3 · evil_whirlwind 1 (all 1) |
| aether_sky_monster | 4 | zephyr 20 (1) |
| aether_darkness_monster | 5 | cockatrice 8 (1) |
| aether_aerwhale | 1 (friendly) | aerwhale 10 (1) |

Spawn-cost charges are 0.4–0.6 with budgets 0.10–0.16.

Spawn rules:
- Animals need `#aether_animals_spawnable_on` below and light > 8.
- Zephyr: hitbox sees the sky, not Peaceful, 1/11 roll.
- Aerwhale: in air, light > 8, sees the sky, 1/40 roll.
- Cockatrice: dark (sky light ≤ rand 32), 1/3 roll.
- Aechor plant: 1/10 roll, none within 10 of a flower.
- Swets: none within 40 of a swet banner or cape.
- Dungeon structures override every category with "no spawns".

---------------------------------------------------------------------------------------------------------
## 3. Features and structures
### 3.0 Placement helpers
- `aether:improved_layer_placement {heightmap, count 0–1, verticalBounds 4}`:
  - For each layer i = 0, 1, 2…, pick a random column in the chunk and walk down from the heightmap.
  - Take the i-th spot that has **air, a solid non-water/lava/bedrock block below, and 4 clear blocks above**.
  - It stops at the first layer with no hit. So features land on **every stacked island top**, at most 1 per
    count roll per layer, with probability 0.5 each.
- `aether:dungeon_blacklist_filter`: skip if inside a bronze, silver or gold dungeon.
- `aether:holiday_filter`: config "always" (default false), or "seasonally" (default true) **and** the month is
  December or January.
- `aether:config_filter "Generate Tall Grass in the Aether"`: default **true**.

### 3.1 Skyroot tree (`configured_feature/skyroot_tree.json`): the vanilla oak shape exactly
- Trunk: straight_trunk_placer base 4 + rand(0..2), so **4–6 logs** of `skyroot_log[axis=y]`.
- Leaves: blob_foliage_placer radius 2, offset 0, height 3, block `skyroot_leaves`. H = trunk height, logs at 0..H−1.
  - y H: radius 1, corners always cut → plus of 5
  - y H−1: 3×3, each corner 50 %
  - y H−2 and H−3: 5×5, each corner 50 %
- 99 %: skyroot tree. **1 %: golden oak** (random_selector 0.01).

### 3.2 Golden oak (`golden_oak_tree.json`, `GoldenOakTrunkPlacer`, `GoldenOakFoliagePlacer`)
- Trunk: **10 logs** `golden_oak_log` (y 0..9).
- Branches: at trunk heights i = 5, 6, 7, 8, each with chance 2/3.
  - Branch length is 1 log, or 2 logs with 50 %.
  - Each log steps (dx, dz) ∈ {−1,0,1}² (the same step every time) and rises slant = i/4−1 (0 for i 5–7, +1 for i 8).
- Leaves `golden_oak_leaves`, rows at y 11 down to y 4 (attachment y10, offset +1, foliage height 7).
  - Kept where `x² + (row−10+2)² + z² ≤ 12 + rand(0..4)` with |x|,|z| ≤ 4.
  - That is a **ball of radius ≈3.5–4 centred on y 8** (2 below the trunk top), with a ragged edge.
- About 12 tall and 7–9 wide.
- Where it grows:
  - 1 % of tree attempts.
  - 66 % on the Gold-dungeon island (otherwise a dandelion or poppy).
  - 1/64 per column on gold stubs.

### 3.3 Crystal tree and Crystal Island (`crystal_tree.json`, `crystal_island`, `CrystalIslandFeature`)
- Placement: rarity **1/50 chunks**, in_square, y **uniform 32–96**, not in dungeons. It floats in mid-air.
- The tree goes at pos+1:
  - Trunk: 7 `skyroot_log` (y 0..6), plus side logs at (±1,0) and (0,±1) on y 2 and y 5.
  - Leaves: `crystal_leaves` (weight 4) or `crystal_fruit_leaves` (weight 1), corners of each square cut:

    | y | 7 | 6 | 5 | 4 | 3 | 2 |
    |---|---|---|---|---|---|---|
    | shape | 1 leaf | plus (5) | diamond \|dx\|+\|dz\|≤2 (13) | plus | diamond | 5×5 minus corners (21) |

- Only if the tree fits, an islet is placed under it:
  - y 0: `aether_grass_block` diamond \|dx\|+\|dz\|≤2 (13 blocks)
  - y −1: holystone, same diamond (13)
  - y −2: holystone plus (5)

### 3.4 Holiday tree (`holiday_tree.json`, seasonal)
- Placement: rarity **1/75 chunks**, top layer only (OCEAN_FLOOR heightmap), on dry ground, skyroot sapling must
  survive, holiday filter.
- Trunk: straight, **9** `skyroot_log`.
- Foliage: `holiday_leaves` (4) or `decorated_holiday_leaves` (1), a tiered fir:

  | y | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 |
  |---|---|---|---|---|---|---|---|---|
  | shape | 1 | plus | 3×3 | diamond r2 | 5×5 + tips at ±3 | 7×7-ish octagon | 7×7 minus corners + tips at ±4 | 9×9-ish octagon |

- Decorator:
  - In a disc of radius ≈9 around the base, about 80–100 % of ground spots get a layer:
    `minecraft:snow[layers=1]` (10) or `aether:present` (1).
  - Leaves get a snow layer on top.

### 3.5 Quicksoil shelf (`quicksoil_shelf.json`, `ShelfFeature`)
- Placement: rarity **1/5 chunks**, not in dungeons.
- The feature scans the chunk's 16×16 columns for y in **0..47**. A column qualifies at the first y where:
  - y is air,
  - y+1 is `aether_grass_block`,
  - y+2 is air,
  i.e. a **1-block-thick grass rim**.
- Each qualifying column places a flat disk of `quicksoil` at that y, **radius √12 ≈ 3.46** (x²+z² ≤ 12, 37
  blocks, air only), hanging under the rim.
- The result is **thin quicksoil aprons under low island edges** (y < 48).

### 3.6 Water
**Lake** (`aether:lake`, `AetherLakeFeature`; a vanilla lake with an Aether-grass rim):
- Placement: rarity **1/15 chunks**, at the WORLD_SURFACE_WG height of the chunk's corner column (no in_square).
- Shape:
  - Origin moves down 4.
  - 4–7 ellipsoids in a 16×8×16 box, each with axes 3–9 × 2–6 × 3–9.
  - Water at local y < 4, air at y 4–7.
- Aborts if the shell is open at the water level or touches liquid above it.
- Dirt left exposed under the cleared part of the basin (local y 3–6, with sky light) becomes `aether_grass_block`.
- Result: ponds up to ~14×4×14.

**Spring** (`spring_feature`, like vanilla):
- **30 per chunk**, y uniform 8–128.
- Needs rock_count 4 (holystone or aether dirt neighbours), hole_count 1, and a block below.
- Places a flowing water source in a cliff face, giving **waterfalls pouring off island sides into the void**.
- Not in dungeons.

### 3.7 Ores (`minecraft:ore`, target tag `aether:holystone` = holystone + mossy_holystone)
| feature | block | vein size | count/chunk | y range | air-exposure discard |
|---|---|---|---|---|---|
| aether_dirt_ore | aether_dirt | 33 | 20 | uniform 0–128 | 0 |
| icestone_ore | icestone | 32 | 10 | uniform 0–128 | 0 |
| ambrosium_ore | ambrosium_ore | 16 | 20 | uniform 0–128 | 0 |
| zanite_ore | zanite_ore | 5 | 14 | uniform 0–75 | 0.5 |
| gravitite_ore_buried | gravitite_ore | 3 | 5 | uniform 0–74 | 0.5 |
| gravitite_ore | gravitite_ore | 4 | 7 | **trapezoid −58…74** (peak ≈ y 8, so most attempts are low and miss land) | 0 |

- "size" is vanilla OreFeature: a blob of about `size` blocks along a short segment.
  - 33 / 32 ≈ vanilla dirt / granite blobs, 16 ≈ coal, 3–5 ≈ diamond-sized.
- Many tries land in air because land only exists at y ≈ 18–105.

### 3.8 Ground cover
All are on MOTION_BLOCKING layers via improved_layer_placement and need air at the target.
`random_patch` tries offset (rand8−rand8, rand4−rand4, rand8−rand8): xz spread 7, y spread 3.

| feature | block | per chunk | patch |
|---|---|---|---|
| grass_patch | `minecraft:grass` (short, tinted #B1FFCB) | noise_threshold_count: 10 if biome-info noise > −0.8, else 5 | 32 tries → **dense short grass** |
| tall_grass_patch | `minecraft:tall_grass` (lower) | 7 or 0, then rarity 1/32 | 96 tries (rare clumps) |
| white_flower_patch | `aether:white_flower` | rarity 1/8 | flower patch, 64 tries |
| purple_flower_patch | `aether:purple_flower` | rarity 1/16 | 64 tries |
| berry_bush_patch | `aether:berry_bush` | rarity 1/8 | 32 tries |

### 3.9 Dungeons (structure sets; templates measured with `tools/nbt.py`)
| | Bronze | Silver | Gold |
|---|---|---|---|
| set | `bronze_dungeon`: random_spread spacing **6** chunks, separation 5, triangular, salt 32146754, exclusion 4 chunks from silver/gold | `silver_and_gold_dungeons`: spacing **36**, separation 24, salt 4325806, weights silver 3 : gold 1 | same set as silver |
| where | **inside** a thick island: needs a ≥16-block solid run at all 4 corners of chunk ±1, between y 32 and 232. Otherwise it tries the 8 neighbour chunks. Room centred in the thickest run | **in the air**. 60 %: y = (surface−2)+18 + rand(128−that). 40 %: y = max(surface−2, 35+rand 70) | y = max(surface−20, 40+rand 60) |
| layout | Boss room 16×14×16 (locked carved stone, ~5 % locked sentry stone). Square tunnels 6×6×6 (holystone, 20 % mossy). Chest rooms 12×8×12 (carved stone, 10 % sentry stone, 13 % trapped stone on floors; chests 60 % chest / 40 % mimic). Lobby 12×12×12. Up to 8 rooms within 3 chunks of the start. End corridor 6×8×5 dug outward until it reaches air (≤100 blocks). Surface ruins: ragged holystone, mossy holystone and slab columns plus a purple/white flower patch | Cloud bed: 100 walks × 10 cold-aercloud blobs over a 50×77 area at y−1. Rear template 32×30×31 (locked angelic stone, holystone, pillars). Exterior "skeleton" 32×27×26. 3×3×3 grid of 7-block rooms (chest rooms and staircases; 11.7 % trapped angelic stone, 5 % light). Boss room 22×17×26 (angelic stone, glowstone, ambrosium torches, water, 2 golden-oak saplings) | Island template **38×43×38** (holystone, 1084 aether dirt, 1084 grass). 8–12 stub islands (15×15×15) at radius 16.8–19.8, dropped 0–7. 18 "gumdrop caves" (≈7.5-block tubes, radius ≤2.4, carved through holystone/dirt within ±24 of the centre). Boss room 23×9×28 of locked hellfire stone (10 % light). Tunnel 7×5×16. Golden oaks on top |
| boss | Slider | Valkyrie Queen (+ valkyries) | Sun Spirit |

### 3.10 Aerclouds
- `aether:aercloud` feature (`AercloudFeature`):
  - Start at the origin + (−rand 8, 0, ±(0..8)), with direction d = random bool.
  - Repeat `bounds` times:
    - Step the cursor: x += rand(0..1), y += (coin ? rand(−1..1) : 0), z += ±rand(0..1).
    - Fill a box 3–4 (x) × **2** (y) × 3–4 (z) where the cell is air and |dx|+|dy|+|dz| < 4 + rand(0..1).
  - The result is a diagonal chain of rounded 2-tall puffs.

| feature | block | bounds (blobs) | size | y | rarity |
|---|---|---|---|---|---|
| cold_aercloud | cold_aercloud | 16 | ≈10–13 × 2–4 × 10–13 | uniform **32–96** | **1/7** chunks |
| blue_aercloud | blue_aercloud | 8 | ≈6–9 across | uniform 32–96 | **1/24** |
| golden_aercloud | golden_aercloud | 4 | ≈4–6 across | uniform **96–128** | **1/75** |

- **Large Aercloud** structure (`LargeAercloudStructure`):
  - Set spacing **6** chunks, separation 3, salt 15536586. That is about one per 96×96 blocks.
  - Altitude: **y = 0 + rand(32)**, i.e. under all islands.
  - Shape: 64 steps.
    - y drifts ±1 with chance 1/10.
    - x/z drift by rand(−1..1) + tendency, where the tendency is in {−1,0,1} per axis.
    - Each step fills 9–12 × 2 × 9–12 where Manhattan distance < 12 + rand(0..1).
  - Result: **long cold-aercloud ribbons ≈70–130 blocks long and 9–12 wide**, forming a patchy cloud floor.

### 3.11 Ruined portals
Overworld only (`structures/ruined_portal/*`, glowstone portals). Not in the Aether.

---------------------------------------------------------------------------------------------------------
## 4. Blocks
- Texture root: `/home/admin/Documents/Projects/Aether/assets/aether/textures/block/`.
- All textures are **16×16** PNG unless noted, and every path below exists.
- "avg" is the mean RGB of the opaque pixels. "Clear" counts the 0-alpha pixels out of 256.
- Worldgen places these blocks with `double_drops=true`. That only affects drops.

| id | name (en_us) | render | light | behaviour | texture(s) |
|---|---|---|---|---|---|
| holystone | Holystone | cube | 0 | stone, strength 0.5 | natural/holystone.png (avg #A7A7A7, neutral grey) |
| mossy_holystone | Mossy Holystone | cube | 0 | dungeon/ruin variant | natural/mossy_holystone.png (#95A5A2) |
| aether_grass_block | Aether Grass Block | cube_bottom_top, **untinted** | 0 | random ticks, spreads | top natural/aether_grass_block_top.png (#72A785) · side …_side.png (#667E7E: grey-blue dirt with a mint fringe) · bottom aether_dirt · snowy side aether_grass_block_snow.png |
| enchanted_aether_grass_block | Enchanted Aether Grass Block | cube_bottom_top | 0 | from ambrosium bonemeal; not worldgen | enchanted_…_top.png (#D4C560) / _side.png |
| aether_dirt | Aether Dirt | cube | 0 | dirt | natural/aether_dirt.png (**#63777F slate blue-grey**) |
| quicksoil | Quicksoil | cube | 0 | **friction 1.1** (ice 0.98), so you speed up while sliding; capped to 0.99 once \|vx\| or \|vz\| > 1 | natural/quicksoil.png (#D8CE82 pale sand) |
| icestone | Icestone | cube | 0 | random ticks; freezes nearby water to ice and lava to obsidian (block entity) | natural/icestone.png (#BCBCBC, RGBA) |
| ambrosium_ore | Ambrosium Ore | cube | 0 | strength 3, 0–2 xp | natural/ambrosium_ore.png (#ADAB93) |
| zanite_ore | Zanite Ore | cube | 0 | strength 3, 3–5 xp | natural/zanite_ore.png (#9386A5) |
| gravitite_ore | Gravitite Ore | cube | **0** | `FloatingBlock`: after a block update, if the block above is air, fire, liquid or replaceable, it becomes a floating entity that **rises** (falling sand in reverse). Worldgen does not trigger it | natural/gravitite_ore.png (#A69AA5) |
| cold_aercloud | Cold Aercloud | **translucent**, 6 inward+outward quads, texture alpha 225/255 | 0 | Collision is a 0.01 sheet at the block bottom; full cube if another aercloud is above; 0.9 tall if falling > 2.5. Inside: fall distance resets and **vy ×0.005 while moving down** (slow sink, no damage). Shade 0.25, no occlusion, never suffocates | natural/cold_aercloud.png (#F2F2F2) |
| blue_aercloud | Blue Aercloud | same | 0 | **no collision**. Sets **vy = 2.0 blocks/tick**, launching you ≈20 blocks in 21 ticks. 50 splash particles and a wobble sound. Sneaking (or a ridden mount) cancels the launch | natural/blue_aercloud.png (#C2F2F2 pale cyan) |
| golden_aercloud | Golden Aercloud | same | 0 | same as cold in this version (soft sink); gold map colour | natural/golden_aercloud.png (#F2D57B) |
| skyroot_log | Skyroot Log | log column (axis) | 0 | as oak log | natural/skyroot_log.png (#57553E dark olive-grey) · natural/skyroot_log_top.png (#65664B) |
| golden_oak_log | Golden Oak Log | log column | 0 | — | side natural/golden_oak_log.png (#665B3B) · end **skyroot_log_top.png** (there is no golden_oak top texture) |
| skyroot_leaves | Skyroot Leaves | leaves, alpha holes (72/256 clear) | 0 | decays | natural/skyroot_leaves.png (#89A961) |
| golden_oak_leaves | Golden Oak Leaves | leaves (84 clear) | 0 | **particles** (see §5) | natural/golden_oak_leaves.png (#CCAF38) |
| crystal_leaves / crystal_fruit_leaves | Crystal Leaves / Crystal Fruit Leaves | leaves (55 / 40 clear) | 0 | particles; the fruit variant drops white apples | natural/crystal_leaves.png (#5AA8C1) · natural/crystal_fruit_leaves.png (#609DB2) |
| holiday_leaves / decorated_holiday_leaves | Holiday Leaves / Decorated Holiday Leaves | leaves | 0 | particles (white) | natural/holiday_leaves.png (#797AA2) · natural/decorated_holiday_leaves.png (#7A7596) |
| white_flower | White Flower | cross, cutout | 0 | small flower (stew effect, 4 s) | natural/white_flower.png (#B8BFBB) |
| purple_flower | Purple Flower | cross, cutout | 0 | flower (inebriation, 12 s) | natural/purple_flower.png (#696795) |
| berry_bush | Berry Bush | **full leafy cube** (cutout) + inner X of stem | 0 | harvest → blue berries; leaves a `berry_bush_stem` | natural/berry_bush.png (#75945F, 82 clear) + natural/berry_bush_stem.png |
| berry_bush_stem | Bush Stem | cross | 0 | regrows | natural/berry_bush_stem.png |
| skyroot_sapling / golden_oak_sapling | Skyroot / Golden Oak Sapling | cross | 0 | grows the §3.1 / §3.2 trees | natural/skyroot_sapling.png · natural/golden_oak_sapling.png |
| present | Present | cube_bottom_top | 0 | holiday decoration | miscellaneous/present_side.png · present_top.png |
| carved_stone / sentry_stone | Carved Stone / Sentry Stone | cube | 0 / **11** | bronze dungeon | dungeon/carved_stone.png (#797979) · dungeon/sentry_stone.png (#74797F) |
| angelic_stone / light_angelic_stone | Angelic Stone / Light Angelic Stone | cube | 0 / 11 | silver dungeon | dungeon/angelic_stone.png (#B7A075) · light_angelic_stone.png |
| hellfire_stone / light_hellfire_stone | Hellfire Stone / Light Hellfire Stone | cube | 0 / 11 | gold dungeon | dungeon/hellfire_stone.png (#8D4E46) · light_hellfire_stone.png |
| pillar / pillar_top | Pillar / Pillar Top | log column | 0 | silver dungeon | dungeon/pillar_side.png, pillar_top.png, pillar_carved.png (#E9CDC0) |
| locked_* / trapped_* / boss_doorway_* / treasure_doorway_* | — | cube with overlay | as base | locked = unbreakable (−1, 3.6e6); trapped = spawns a sentry, valkyrie or fire minion when stepped on; overlay particles lock.png, exclamation.png, door.png, treasure.png | dungeon/*.png |
| ambrosium_torch | Ambrosium Torch | torch | 14 (vanilla torch copy) | | utility/ambrosium_torch.png |
| aether_portal | Aether Portal | translucent pane | 11 (nether-portal copy) | | miscellaneous/aether_portal.png **16×512, 32 frames, frametime 2** |
| vanilla used | `minecraft:grass`, `tall_grass` (tinted #B1FFCB), `water`, `snow[layers=1]`, `dandelion`, `poppy` (gold island) | | | | vanilla |

Animated block textures are only the portal, quicksoil_glass and the skyroot bookshelf. No worldgen block is animated.

---------------------------------------------------------------------------------------------------------
## 5. Visuals (client: `AetherSkyRenderEffects`, `DimensionClientHooks`, `AetherMusicManager`)
- **Effects constructor** `(cloudLevel 9.5, hasGround true, SkyType NORMAL, forceBrightLightmap false, constantAmbientLight false)`.
  - The vanilla cloud texture is drawn as a layer at **y 9.5**, under every island, scaled 12×1×12 and drifting
    +x at 0.03 blocks/tick.
  - Cloud colour is white × (0.9·f + 0.1) for R and G and (0.85·f + 0.15) for B, with f = clamp(cos(2π·t)·2 + 0.5, 0, 1).
- **Sky dome**:
  - Colour: biome sky colour **#C0C0FF** × the same f, with vanilla rain and thunder greying.
  - The vanilla lower dark-void disc is **not drawn**, so the sky below the horizon stays sky-coloured.
- **Sun and moon**:
  - Vanilla sun.png (quad half-size 30) and moon_phases.png (half-size 20).
  - Their opacity follows Aether time (**72000-tick day**, 3× vanilla):
    - Sun opaque while dayTime%72000 < 38400; it fades out over 600 ticks.
    - Moon until 71400, then fades back to the sun.
- **Eternal day**:
  - Until the Sun Spirit is beaten, `AetherTime.tickTime` holds dayTime at **18000** (noon), moving at most
    10 ticks per tick toward it.
  - Afterwards time advances normally on the 72000-tick cycle.
  - Stars are vanilla (night only).
- **Sunrise / sunset fan**: for cos(2πt) in ±0.4, with f3 = cos/0.4·0.5 + 0.5:
  - Default: RGB = (0.65 + 0.3·f3, 0.25 + 0.7·f3², 0.4), alpha = (1 − (1 − sin(π·f3))·0.99)². A pinker,
    bluer sunset than vanilla (0.7 + 0.3f, 0.2 + 0.7f², 0.2).
  - Config `green_sunset`: (0.5·f3, 0.3 + 0.3·f3², 0.3 + 0.5·f3²).
- **Fog**:
  - Biome fog colour **#9393BC** through vanilla day/weather mixing.
  - Weather can only lighten it: rain ×(1+0.8r) on RG and ×(1+0.56r) on B, thunder ×(1+0.66) and ×(1+0.76).
    Each channel is capped at the sky colour.
  - **Terrain fog starts at farDistance/2** (vanilla starts near far − far/10).
  - Near the bottom of the world (camY·clearColorScale < 1, i.e. y ≲ 32 with the vanilla 1/32 scale), fog RGB is
    **divided** by d². It turns brighter and whiter as you sink, instead of vanilla's darkening.
- **Lightmap**: vanilla warm lightmap by default. Config `colder_lightmap` gives a blue-tinted night.
- **Particles**:
  - No biome ambient particles.
  - Golden oak, crystal and holiday leaves (`LeavesWithParticlesBlock`): each animateTick has a 1/10 chance to
    spawn **15** portal-style swirling particles within ±4 blocks, velocity ±0.25.
    - Tints: golden oak **#F9BE00** (0.976, 0.745, 0), crystal **#00A5BE** (0, 0.645, 0.745), holiday white.
  - Blue aercloud: 50 splash particles per bounce.
  - Whirlwinds have their own cloud particles.
- **Music**:
  - `music.aether` = aether1…5.ogg, delay 12000–24000 ticks (10–20 min), only in `#aether_music` biomes.
  - Boss tracks: slider_fight, valkyrie_queen_fight, sun_spirit_fight.
  - Records: aether_tune, ascending_dawn, chinchilla, high, klepto, sliders_wrath.
  - Menu: menu.ogg.
- **No weather visuals**: has_precipitation false, so rain or thunder only greys the sky.

---------------------------------------------------------------------------------------------------------
## 6. Gap list: DokuQuest `src/gen/skylands.cpp` (working tree incl. the uncommitted ~220 lines) vs the Aether
Ordered by how visible the difference is.

1. **Island shape and altitude (highest impact).**
   - DQ:
     - One heightmap slab per column.
     - Mask = fbm2(x/180, z/180, 3) > 0.05, top = 70 + 10·fbm(x/60) + 120, giving grass at band ≈183–197.
     - The slab hangs down `(mask−0.05)^1.35·105·(0.6 + 0.4·noise3)` to a point ("upturned hill").
     - Extra islets at a fixed band 216 and crystal islets at 228.
   - Aether:
     - 3D BlendedNoise density (§1.2) with the y gradients 8–40 and 56–128.
     - Land in **y 18–105, peak 40–60**, on 28–33 % of columns.
     - **6–7 % of columns have 2+ stacked slabs** (gap p50 7). Slabs p50 11–14 thick.
     - Holes, arches, terraces, rounded undersides.
   - Fix: port `tools/aether_density.cpp` (BlendedNoise + cell interpolation) into `SkylandsGen::generate`.
     Then make `sample()`, `islandTop()`, `trees()` and the structure probes (`skyTop`, dragon lair,
     `islandMask`) use the topmost solid y. They currently assume one heightmap.
2. **Soil colour.**
   - DQ uses `sky_grass` (top #99C2B9) over **vanilla brown `dirt`** (#7E5A3F), 2 deep.
   - Aether uses `aether_grass_block` (#72A785, grey-blue side #667E7E) over **`aether_dirt` #63777F**, ≈3
     deep (0–5), on every floor, holystone below.
   - The brown dirt band on every island side is the most "un-Aether" thing on screen.
   - Missing blocks: **aether_grass_block, aether_dirt** (stand-ins `sky_grass`, `dirt`).
3. **Cloud layers.**
   - DQ:
     - Three flat noise-masked sheets at fixed altitudes: cold at band 158, **blue_aercloud as the "white middle"
       sheet** at 176, gold at 206. They are 1–3 thick.
     - All three are flagged `"bouncy"` in data/blocks.json, i.e. a slime bounce: vy = −vy·0.82 (sim/player.cpp:209).
   - Aether:
     - Scattered 2–4-tall puff chains: cold 1/7 chunks at y32–96, blue 1/24 (small), golden 1/75 at y 96–128.
     - Cold-aercloud ribbons 70–130 long at y 0–31 every ~96 blocks.
     - The vanilla cloud plane at y 9.5.
   - Behaviour gaps:
     - Cold and golden should **sink-catch**: no bounce, vy ×0.005, no fall damage.
     - Only blue should launch, with vy 2.0 (≈20 blocks).
     - A whole sheet of blue aercloud would fling players constantly.
   - DQ fallbacks `bounce_cloud`, `cloud_puff`, `sun_cloud` (sun_cloud gives slowfall and light 6) are not
     Aether behaviour either.
4. **Trees.**
   - DQ's own canopy: layers H−2…H+1, radius 2/1, height 4–6 (+2 in forest).
   - Golden oak at **8 % (40 % in woodland)** instead of 1 %, and golden oaks use the generic shape.
   - `sunwood` trees (gold leaves, light 4) are a DQ invention with no Aether equivalent.
   - Density: DQ gives 0–2 per chunk + 2 extra in forest / 1 in woodland. Aether gives ≈3 / 2.55 / 1.05 / 0.5
     per chunk **per island layer** for forest / woodland / grove / meadow.
   - Fix:
     - Skyroot = vanilla oak (§3.1).
     - Golden oak = 10-log trunk + branches + r≈3.7 ball (§3.2).
     - Fallbacks `birch_log` and `cloud_leaves` should go.
5. **Ground cover.**
   - DQ: `tall_grass` 8–42 % of tops, plus `sky_bloom`, dandelion, `bluebells`, `marigold`, `azure_bluet`, and
     berry bushes on 3 % of meadow.
   - Aether:
     - Dense **short vanilla grass tinted #B1FFCB** (5–10 patches × 32 tries per layer).
     - Rare double tall grass.
     - Only two flowers: **aether:white_flower** (1/8 chunks) and **aether:purple_flower** (1/16), in patches of 64 tries.
     - **berry_bush** patches 1/8.
   - Missing blocks: white_flower, purple_flower. The DQ flower set is all stand-ins.
   - DQ's berry_bush is a cross plant (render 3). The Aether's is a full leafy cube with a stem.
6. **Water.**
   - DQ: one water block replacing grass in meadow/grove dips.
   - Aether: lakes (1/15 chunks, up to ≈14×14×4, grass rim) and **30 springs/chunk**, so many **waterfalls off
     island sides**. That is a signature Aether sight and is missing.
7. **Sky colour and fog.**
   - DQ (`render/sky.cpp` DIM_AETHER): zenith #6EB6FF, horizon #FFF6E4, fog #E7F4FF, fogStart 0.55.
   - Aether: sky **#C0C0FF**, fog **#9393BC** (lavender-grey), fog start at half render distance, a brighter
     haze toward the bottom, pink sunset fan, 72000-tick day held at noon (eternal day).
   - DQ's single sun direction (0.45, 0.82, 0.22) is a fixed noon, which roughly matches eternal day.
   - DQ's cloud layer (`CLOUD_BASE_Y` 724, overworld band) plus the `cloudBank` band 0–30 already resemble the
     Aether's y 9.5 cloud floor.
8. **Ores.**
   - DQ: per-block hash every 3rd y between 44 and 110. That is single blocks at gravitite 3.5 % (<72),
     zanite ≈4.5 %, ambrosium ≈7 % of eligible stone, far denser than the Aether and never in veins.
     Plus `aether_crystal_ore` blobs (DQ's own) and `driftstone` (DQ's own, floats up, a gravitite look-alike).
   - Aether veins (§3.7): ambrosium 20×16, zanite 14×5, gravitite 7×4 + 5×3, **icestone 10×32**, **aether-dirt
     blobs 20×33** inside holystone.
   - Missing: **icestone**.
   - Data mismatch: DQ `gravitite_ore` has light 6; the Aether's has 0.
9. **Quicksoil.**
   - DQ: quicksoil as the *top* block where the mask is < 0.1, plus a 2-block lip on mask 0–0.05 at the top height.
   - Aether: flat 1-thick disks (r ≈ 3.46) **under** thin grass rims at y < 48, 1/5 chunks. No quicksoil on top.
   - Fallback `glimmersand` (slippery: accel 2 vs 14) is not Aether friction 1.1.
10. **Crystal islets.**
    - DQ: calcite and amethyst islets at band 228 with amethyst buds.
    - Aether: 13+13+5-block grass/holystone islet with a **crystal tree** (skyroot log, crystal leaves, crystal
      fruit leaves), 1/50 chunks, y 32–96.
    - Missing: crystal_leaves, crystal_fruit_leaves.
11. **Structures.**
    - DQ has its own sky_temple (quartz, Valkyrie Queen), sky_shrine, skyship, moa_nest, sky_sanctum and
      storm-dragon lairs. None are Aether.
    - Missing: Bronze (in-island, carved and sentry stone, Slider), Silver (sky temple on a cloud bed, angelic
      stone and pillars, Valkyrie Queen), Gold (floating 38×43×38 island with stubs and golden oaks,
      hellfire-stone room, Sun Spirit), Large Aercloud ribbons.
    - Missing blocks: carved/sentry/angelic/light-angelic/hellfire/light-hellfire stone, pillar, pillar_top,
      mossy_holystone, holystone_bricks, ambrosium_torch, treasure chest.
12. **Holiday tree, presents, snow.** Seasonal only. Missing holiday_leaves, decorated_holiday_leaves, present.
13. **Particles.**
    - DQ invents meadow "white motes" and woodland falling gold leaves (`render/env_layers.cpp:319`).
    - Aether: no ambient particles. Only portal-style swirls from golden oak (#F9BE00), crystal (#00A5BE) and
      holiday leaves.
14. **Mobs.**
    - DQ spawns whirlwind, aerbunny, moa, phyg, flying_cow, sheepuff, aerwhale, zephyr, valkyries.
    - Missing: cockatrice (dark), aechor plant, blue and golden swet, evil whirlwind, bosses Slider and Sun Spirit,
      sentry, mimic, fire minion.
15. **Texture colours of existing stand-ins** (DQ 32 px avg vs Aether 16 px avg):

    | block | DQ | Aether |
    |---|---|---|
    | holystone | #C8D3E2 (bluish) | #A7A7A7 (neutral) |
    | quicksoil | #E7B838 (orange) | #D8CE82 (pale sand) |
    | blue_aercloud | #458BD0 (deep blue) | **#C2F2F2 (pale cyan)** |
    | skyroot_leaves | #4A9176 | #89A961 (yellow-green) |
    | skyroot_log side | #C5AA7E (tan) | **#57553E (dark olive)** |
    | golden_oak_log | #A06D27 | #665B3B |
    | gravitite_ore | #A2B4CE | #A69AA5 (pink-purple) |
    | zanite_ore | #AEB0CC | #9386A5 |
    | ambrosium_ore | #C3C9CC | #ADAB93 |

    cold_aercloud (#DDE8F2) and golden_aercloud (#EDC443) are close.
    DQ's 32 px textures are **not** copies of the Aether's (mean pixel difference vs a 2× upscale is 12–88).

Block stand-in summary (DQ → real Aether block):
- `sky_grass` → aether_grass_block
- `dirt` → aether_dirt
- `skystone` → holystone
- `glimmersand` → quicksoil
- `birch_log` / `cloud_leaves` → skyroot_log / skyroot_leaves
- `sunwood_*` → (none; golden_oak_*)
- `tall_grass` → minecraft:grass (short, #B1FFCB)
- `sky_bloom`, `flower_yellow`, `bluebells`, `marigold`, `azure_bluet` → white_flower / purple_flower
- `bounce_cloud` → blue_aercloud
- `cloud_puff` → cold_aercloud
- `sun_cloud` → golden_aercloud
- `aether_crystal_ore` → ambrosium_ore
- `driftstone` → gravitite_ore (behaviour)
- `calcite` / `amethyst_*` → crystal island + crystal leaves

---------------------------------------------------------------------------------------------------------
## 7. Licence
- `META-INF/mods.toml`: **`license = "Assets: All Rights Reserved; Code: LGPL-3.0"`**.
- No LICENSE file ships in the jar.
- Credits: "The Aether Team, former Aether Team Collaborators…, Modding Legacy (Aether Legacy)…, C4 for Curios API".
- Authors: AlphaMode, bconlon, Blodhgarm, Burning Cactus, Drullkus, Hugo Payn, Jaryt, Oscar Payn, quek, Raptor,
  reetam, RENREN, sunsette.
- Bundled jar-in-jars (`META-INF/jarjar`): cumulus_menus, curios, mixinextras, nitrogen_internals, each under its
  own licence.

What this means for DokuQuest:
- **Art, sounds, music, structure NBTs and lang text are All Rights Reserved.** Do not ship, convert, recolour or
  trace them.
  - That covers `assets/aether/textures/**`, `sounds/**`, `data/aether/structures/*.nbt`, and the
    music/records (aether1–5, boss tracks).
  - Paths and average colours in this report are for reference only. DQ's own 32 px textures are original and
    not derived.
- **Code is LGPL-3.0**:
  - Algorithms and numbers (density formula, feature parameters, tree shapes) may be reimplemented.
  - Copying actual source would put those parts under LGPL obligations. A clean reimplementation from this spec
    is the safe path.
  - Worldgen JSON is data in `data/`. It is not explicitly carved out, so treat it like the code.
