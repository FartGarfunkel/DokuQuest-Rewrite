# The Skylands: target design and gap list

Design notes for the Skylands band (global y 768–1023). They describe the island shape, biomes, features,
blocks and sky we are aiming for, with exact numbers, and list what `src/gen/skylands.cpp` (plus
`skylands_isles.cpp`, `structures_skylands.cpp`) still does differently.
"y" below means **band-local y** (0–255; global y = 768 + y). Terrain only lives in the lower half of the band.

There is no density prototype in the repo yet; §1.2 has everything needed to write it straight into
`SkylandsGen::generate`.

---------------------------------------------------------------------------------------------------------
## 1. Terrain

### 1.1 Band settings
| item | target |
|---|---|
| band | 256 tall, sky light, no ceiling, ambient light 0, beds work, mobs spawn at light 0–7 |
| terrain height | density is only evaluated for **y < 128**; nothing solid at y ≥ 128 |
| noise cells | 8 blocks wide, 4 blocks tall, trilinearly interpolated |
| base block | holystone |
| sea | none (no sea level, no aquifers, no ore veins from the noise) |
| biome inputs | only a temperature and a humidity noise (§2) |
| caves | **none**. The only carving is the gold dungeon's tunnels (§3.9) |

### 1.2 Density (exact)
```
N  = blended 3D noise (see below), horizontal scale 0.25, vertical scale 0.25, xz factor 80, y factor 160, smear 8
G1 = clamp((y - 8) / 32, 0, 1)          // 0 at y<=8, 1 at y>=40
G2 = clamp((128 - y) / 72, 0, 1)        // 1 at y<=56, 0 at y>=128
d  = -0.15 + G1*( G2*(N + 0.07) - 0.1 )
final = squash( interpolate( d ) )      // squash(x) = x/2 - x^3/24 after clamping to [-1,1]; keeps the sign
solid holystone where final > 0, else air
```
- `interpolate` means the density is sampled only at cell corners (every 8 blocks in x/z, every 4 in y) and
  trilinearly interpolated in between. This gives smooth, slightly faceted blobs.
- A block is solid when **N > (0.15 + 0.1·G1)/(G1·G2) − 0.07**:

| y | ≤8 | 12 | 16 | 24 | 32 | 40–56 | 72 | 88 | 104 | 112 | ≥118 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| N threshold | never | 1.23 | 0.63 | 0.33 | 0.23 | **0.18** | 0.25 | 0.38 | 0.68 | 1.06 | never |

- **The blended noise** is two 16-octave "limit" noises (lo, hi) mixed by one 8-octave selector noise:
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
  - Measured: N mean ≈0, **sd 0.18–0.20**, p1 −0.45, p99 +0.45, extremes ±0.8.

### 1.3 Resulting shape (statistics, 3 seeds, 512²–1024² × 128)
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

How it looks from above:
- Irregular, lumpy "continents" 100–500 blocks across, full of bays, holes and arches.
- Plenty of 15–60-block islets.
- Tops are fairly flat and terraced (the smear). Bottoms are rounded blobs, not cones.
- Islands thin out by **density, not by a horizontal mask**:
  - Below y≈32, G1 raises the threshold, so bottoms round off and nothing exists under y≈18.
  - Above y≈60, G2 does the same, so few tops reach 90 and none reach 105.
  - Edges taper over one 8-block cell. That leaves 1–2-block-thick rims, which the quicksoil shelves need (§3.5).

### 1.4 Surface rule
1. Every **floor** gets a soil cap: the top of each slab, every terrace, and the floor under each overhang.
   - Top block: sky grass.
   - Below it, `surfaceDepth` blocks of sky dirt, with `surfaceDepth = int(surfaceNoise·2.75 + 3 + rand·0.25)`
     (surface noise: first octave −6, amplitudes [1,1,1]). That gives about **3 blocks of dirt (range 0–5)**.
2. Everything else stays holystone. Ceilings are plain holystone.

### 1.5 Fluids, sky floor, falling out
- No sea. Water comes only from lakes and springs (§3.6).
- **Nothing generates below y≈18.** Large cloud ribbons (§3.10) drift at y 0–31, and a flat cloud layer sits
  at **y 9.5** (§5).
- Falling out of the bottom drops you into the Overworld. The stacked column already does this.

### 1.6 Placing it in the band
- **1:1 option:** the density's y is the band-local y.
  - Islands land at band 18–105 (global 786–873). Land at y < 30 (~2–5 % of solid) sits inside the
    `cloudBank` (band 0–30).
  - Band 128–255 holds only golden clouds (≤128) and treetops.
- Current gen (`SKY_DY = 120`) puts grass at band ≈183–197 and undersides down to ≈141.
- **Offset option:** `band_y = density_y + k`. With k ≈ 100, 18–105 maps to 118–205.

---------------------------------------------------------------------------------------------------------
## 2. Biomes
All four biomes share effects, temperature, spawns and feature lists. Only the tree count differs.

| field | value (all 4) |
|---|---|
| biomes | Skyroot Meadow, Skyroot Grove, Skyroot Woodland, Skyroot Forest |
| temperature / downfall / precipitation | 0.8 / 0.0 / **none** (never rains or snows; weather only tints colours) |
| sky colour | **#C0C0FF** |
| fog colour | **#9393BC** |
| water colour / water fog | #3F76E4 / #050533 (same as the Overworld) |
| grass / foliage tint | **#B1FFCB**, used only for the ordinary grass and ferns growing here: **#B1FFCB on sky grass**, **#FCE964 on enchanted sky grass**. Sky grass, skyroot leaves and the sky flowers are untinted textures |
| music | Skylands music, delay 12000–24000 ticks, does not cut the current track |
| creature spawn chance | 0.25 |

**Feature order**, by generation step:
- 0 quicksoil shelf
- 1 water lake
- 6 ores: sky dirt blobs, icestone, ambrosium, zanite, buried gravitite, gravitite
- 8 water springs
- 9 vegetation: trees, holiday tree, grass patch, tall grass patch, white flower patch, purple flower patch, berry bush patch
- 10 top layer: crystal island, cold cloud, blue cloud, golden cloud

**Trees per biome** (skyroot with a golden-oak chance):
- meadow: 1 attempt
- grove: 2 (90 %) or 3 (10 %)
- woodland: 5 (90 %) or 6 (10 %)
- forest: 6 (90 %) or 7 (10 %)

Each attempt then uses layer placement (count 0–1 per layer, §3.0), so the expected trees per chunk per island
layer are **meadow ≈0.5, grove ≈1.05, woodland ≈2.55, forest ≈3.05**. A tree also needs dry ground, ground a
sapling would survive on, and no dungeon underneath.

**Biome map**: temperature × humidity only.
- Noises: temperature (first octave −8, amps [1.5,0,1]) and humidity (first octave −7, amps [1,1]), both shifted,
  horizontal scale 0.25, no y dependence, so regions are roughly **1000 × 500 blocks**.

| temperature | humidity → biome |
|---|---|
| −1.0…−0.8 | any → meadow |
| −0.8…0.0 | −1…0 meadow · 0…1 forest |
| 0.0…0.4 | −1…0 grove · 0…0.8 forest · 0.8…1 grove |
| 0.4…0.93 | −1…−0.1 grove · −0.1…1 forest |
| 0.93…0.94 | −1…−0.6 meadow · −0.6…−0.3 grove · −0.3…1 forest |
| 0.94…1.0 | −1…−0.1 meadow · −0.1…0.8 woodland · (0.8…1 → nearest, woodland/forest) |

**Mob spawns** (same in all biomes; weight, group min–max):

| category | cap | entries |
|---|---|---|
| creature | 10 | phyg 10 (3–4) · sheepuff 12 (3–4) · flying_cow 12 (2–5) · aerbunny 11 (3) · moa 8 (1–3) |
| surface monster | 15 | aechor_plant 7 · blue_swet 6 · golden_swet 6 · whirlwind 3 · evil_whirlwind 1 (all 1) |
| sky monster | 4 | zephyr 20 (1) |
| darkness monster | 5 | cockatrice 8 (1) |
| aerwhale | 1 (friendly) | aerwhale 10 (1) |

Spawn-cost charges are 0.4–0.6 with budgets 0.10–0.16.

Spawn rules:
- Animals need sky grass (or another animal-spawnable block) below and light > 8.
- Zephyr: hitbox sees the sky, not Peaceful, 1/11 roll.
- Aerwhale: in air, light > 8, sees the sky, 1/40 roll.
- Cockatrice: dark (sky light ≤ rand 32), 1/3 roll.
- Aechor plant: 1/10 roll, none within 10 of a flower.
- Swets: none within 40 of a swet banner or cape.
- Dungeons override every category with "no spawns".

---------------------------------------------------------------------------------------------------------
## 3. Features and structures
### 3.0 Placement helpers
- **Layer placement** `{heightmap, count 0–1, verticalBounds 4}`:
  - For each layer i = 0, 1, 2…, pick a random column in the chunk and walk down from the heightmap.
  - Take the i-th spot that has **air, a solid non-water/lava/bedrock block below, and 4 clear blocks above**.
  - Stop at the first layer with no hit. So features land on **every stacked island top**, at most 1 per
    count roll per layer, with probability 0.5 each.
- **Dungeon filter**: skip if inside a bronze, silver or gold dungeon.
- **Holiday filter**: setting "always" (default off), or "seasonally" (default on) **and** the month is
  December or January.
- **Tall grass** setting: default on.

### 3.1 Skyroot tree: a plain oak shape
- Trunk: straight, 4 + rand(0..2), so **4–6 logs** of `skyroot_log` (vertical).
- Leaves: blob radius 2, height 3, `skyroot_leaves`. H = trunk height, logs at 0..H−1.
  - y H: radius 1, corners always cut → plus of 5
  - y H−1: 3×3, each corner 50 %
  - y H−2 and H−3: 5×5, each corner 50 %
- 99 %: skyroot tree. **1 %: golden oak**.

### 3.2 Golden oak
- Trunk: **10 logs** `golden_oak_log` (y 0..9).
- Branches: at trunk heights i = 5, 6, 7, 8, each with chance 2/3.
  - Branch length is 1 log, or 2 logs with 50 %.
  - Each log steps (dx, dz) ∈ {−1,0,1}² (the same step every time) and rises slant = i/4−1 (0 for i 5–7, +1 for i 8).
- Leaves `golden_oak_leaves`, rows at y 11 down to y 4.
  - Kept where `x² + (row−10+2)² + z² ≤ 12 + rand(0..4)` with |x|,|z| ≤ 4.
  - That is a **ball of radius ≈3.5–4 centred on y 8** (2 below the trunk top), with a ragged edge.
- About 12 tall and 7–9 wide.
- Where it grows:
  - 1 % of tree attempts.
  - 66 % on the gold-dungeon island (otherwise a dandelion or poppy).
  - 1/64 per column on the gold dungeon's stub islands.

### 3.3 Crystal tree and crystal islet
- Placement: **1/50 chunks**, anywhere in the chunk, y **uniform 32–96**, not in dungeons. It floats in mid-air.
- The tree goes at pos+1:
  - Trunk: 7 `skyroot_log` (y 0..6), plus side logs at (±1,0) and (0,±1) on y 2 and y 5.
  - Leaves: `crystal_leaves` (weight 4) or `crystal_fruit_leaves` (weight 1), corners of each square cut:

    | y | 7 | 6 | 5 | 4 | 3 | 2 |
    |---|---|---|---|---|---|---|
    | shape | 1 leaf | plus (5) | diamond \|dx\|+\|dz\|≤2 (13) | plus | diamond | 5×5 minus corners (21) |

- Only if the tree fits, an islet is placed under it:
  - y 0: sky grass diamond \|dx\|+\|dz\|≤2 (13 blocks)
  - y −1: holystone, same diamond (13)
  - y −2: holystone plus (5)

### 3.4 Holiday tree (seasonal)
- Placement: **1/75 chunks**, top layer only, on dry ground where a skyroot sapling would survive, holiday filter.
- Trunk: straight, **9** `skyroot_log`.
- Foliage: holiday leaves (4) or decorated holiday leaves (1), a tiered fir:

  | y | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 |
  |---|---|---|---|---|---|---|---|---|
  | shape | 1 | plus | 3×3 | diamond r2 | 5×5 + tips at ±3 | 7×7-ish octagon | 7×7 minus corners + tips at ±4 | 9×9-ish octagon |

- Decoration:
  - In a disc of radius ≈9 around the base, about 80–100 % of ground spots get a snow layer (10) or a present (1).
  - Leaves get a snow layer on top.

### 3.5 Quicksoil shelf
- Placement: **1/5 chunks**, not in dungeons.
- Scan the chunk's 16×16 columns for y in **0..47**. A column qualifies at the first y where:
  - y is air,
  - y+1 is sky grass,
  - y+2 is air,
  i.e. a **1-block-thick grass rim**.
- Each qualifying column places a flat disk of `quicksoil` at that y, **radius √12 ≈ 3.46** (x²+z² ≤ 12, 37
  blocks, air only), hanging under the rim.
- The result is **thin quicksoil aprons under low island edges** (y < 48).

### 3.6 Water
**Lake** (an ordinary lake with a sky-grass rim):
- Placement: **1/15 chunks**, at the surface height of the chunk's corner column.
- Shape:
  - Origin moves down 4.
  - 4–7 ellipsoids in a 16×8×16 box, each with axes 3–9 × 2–6 × 3–9.
  - Water at local y < 4, air at y 4–7.
- Abort if the shell is open at the water level or touches liquid above it.
- Dirt left exposed under the cleared part of the basin (local y 3–6, with sky light) becomes sky grass.
- Result: ponds up to ~14×4×14.

**Spring**:
- **30 per chunk**, y uniform 8–128.
- Needs 4 rock neighbours (holystone or sky dirt), exactly 1 open side, and a block below.
- Places a flowing water source in a cliff face, giving **waterfalls pouring off island sides into the void**.
- Not in dungeons.

### 3.7 Ores (replace holystone and mossy holystone)
| ore | block | vein size | count/chunk | y range | discard if air-exposed |
|---|---|---|---|---|---|
| sky dirt blobs | sky dirt | 33 | 20 | uniform 0–128 | 0 |
| icestone | `icestone` | 32 | 10 | uniform 0–128 | 0 |
| ambrosium | `ambrosium_ore` | 16 | 20 | uniform 0–128 | 0 |
| zanite | `zanite_ore` | 5 | 14 | uniform 0–75 | 0.5 |
| gravitite (buried) | `gravitite_ore` | 3 | 5 | uniform 0–74 | 0.5 |
| gravitite | `gravitite_ore` | 4 | 7 | **trapezoid −58…74** (peak ≈ y 8, so most attempts are low and miss land) | 0 |

- "Vein size" is a blob of about that many blocks along a short segment: 33/32 are big dirt-sized blobs,
  16 is coal-sized, 3–5 diamond-sized.
- Many tries land in air because land only exists at y ≈ 18–105.

### 3.8 Ground cover
All use layer placement on the motion-blocking heightmap and need air at the target.
A patch tries offsets (rand8−rand8, rand4−rand4, rand8−rand8): xz spread 7, y spread 3.

| patch | block | per chunk | tries |
|---|---|---|---|
| grass | short grass, tinted #B1FFCB | 10 where the biome noise > −0.8, else 5 | 32 → **dense short grass** |
| tall grass | double tall grass | 7 or 0, then 1/32 | 96 (rare clumps) |
| white flower | `white_flower` | 1/8 | 64 |
| purple flower | `purple_flower` | 1/16 | 64 |
| berry bush | `berry_bush` | 1/8 | 32 |

### 3.9 Dungeons
| | Bronze | Silver | Gold |
|---|---|---|---|
| spacing | every **6** chunks, separation 5, triangular spread, kept 4 chunks from silver/gold | **36** chunks, separation 24, shared with gold, weights silver 3 : gold 1 | shares the silver grid |
| where | **inside** a thick island: needs a ≥16-block solid run at all 4 corners of chunk ±1, between y 32 and 232. Otherwise it tries the 8 neighbour chunks. Room centred in the thickest run | **in the air**. 60 %: y = (surface−2)+18 + rand(128−that). 40 %: y = max(surface−2, 35+rand 70) | y = max(surface−20, 40+rand 60) |
| layout | Boss room 16×14×16 (locked carved stone, ~5 % locked sentry stone). Square tunnels 6×6×6 (holystone, 20 % mossy). Chest rooms 12×8×12 (carved stone, 10 % sentry stone, 13 % trapped stone on floors; chests 60 % chest / 40 % mimic). Lobby 12×12×12. Up to 8 rooms within 3 chunks of the start. End corridor 6×8×5 dug outward until it reaches air (≤100 blocks). Surface ruins: ragged holystone, mossy holystone and slab columns plus a purple/white flower patch | Cloud bed: 100 walks × 10 cold-cloud blobs over a 50×77 area at y−1. Rear block 32×30×31 (locked angelic stone, holystone, pillars). Outer frame 32×27×26. 3×3×3 grid of 7-block rooms (chest rooms and staircases; 11.7 % trapped angelic stone, 5 % light). Boss room 22×17×26 (angelic stone, glowstone, ambrosium torches, water, 2 golden-oak saplings) | Island **38×43×38** (holystone, ~1084 sky dirt, ~1084 grass). 8–12 stub islands (15×15×15) at radius 16.8–19.8, dropped 0–7. 18 round tunnels (≈7.5 blocks long, radius ≤2.4, carved through holystone/dirt within ±24 of the centre). Boss room 23×9×28 of locked hellfire stone (10 % light). Tunnel 7×5×16. Golden oaks on top |
| boss | Slider | Valkyrie Queen (+ valkyries) | Sun Spirit |

Room shapes are ours to build by hand (or in code); the sizes above are the target footprint.

### 3.10 Clouds
- **Cloud chain**:
  - Start at the origin + (−rand 8, 0, ±(0..8)), with a random direction.
  - Repeat `bounds` times:
    - Step the cursor: x += rand(0..1), y += (coin ? rand(−1..1) : 0), z += ±rand(0..1).
    - Fill a box 3–4 (x) × **2** (y) × 3–4 (z) where the cell is air and |dx|+|dy|+|dz| < 4 + rand(0..1).
  - The result is a diagonal chain of rounded 2-tall puffs.

| cloud | block | bounds (blobs) | size | y | rarity |
|---|---|---|---|---|---|
| cold | `cold_aercloud` | 16 | ≈10–13 × 2–4 × 10–13 | uniform **32–96** | **1/7** chunks |
| blue | `blue_aercloud` | 8 | ≈6–9 across | uniform 32–96 | **1/24** |
| golden | `golden_aercloud` | 4 | ≈4–6 across | uniform **96–128** | **1/75** |

- **Large cloud ribbons** (a structure):
  - Grid spacing **6** chunks, separation 3. That is about one per 96×96 blocks.
  - Altitude: **y = rand(32)**, i.e. under all islands.
  - Shape: 64 steps.
    - y drifts ±1 with chance 1/10.
    - x/z drift by rand(−1..1) + a per-axis tendency in {−1,0,1}.
    - Each step fills 9–12 × 2 × 9–12 where Manhattan distance < 12 + rand(0..1).
  - Result: **long cold-cloud ribbons ≈70–130 blocks long and 9–12 wide**, forming a patchy cloud floor.

---------------------------------------------------------------------------------------------------------
## 4. Blocks
Target look and behaviour. Colours are the average RGB we want for each 32 px tile (our own art).
"Clear" counts the transparent pixels in a 16×16 reference (scale ×4 for 32 px).

| block | render | light | behaviour | colour |
|---|---|---|---|---|
| `holystone` | cube | 0 | stone, strength 0.5 | #A7A7A7, neutral grey |
| `mossy_holystone` | cube | 0 | dungeon/ruin variant | #95A5A2 |
| sky grass | cube, top/side/bottom, **untinted** | 0 | random ticks, spreads | top #72A785 · side #667E7E (grey-blue dirt with a mint fringe) · bottom sky dirt · snowy side variant |
| enchanted sky grass | cube | 0 | made with ambrosium as bonemeal; not generated | top #D4C560 |
| sky dirt | cube | 0 | dirt | **#63777F slate blue-grey** |
| `quicksoil` | cube | 0 | **friction 1.1** (ice 0.98), so you speed up while sliding; capped to 0.99 once \|vx\| or \|vz\| > 1 | #D8CE82 pale sand |
| `icestone` | cube | 0 | random ticks; freezes nearby water to ice and lava to obsidian | #BCBCBC |
| `ambrosium_ore` | cube | 0 | strength 3, 0–2 xp | #ADAB93 |
| `zanite_ore` | cube | 0 | strength 3, 3–5 xp | #9386A5 |
| `gravitite_ore` | cube | **0** | floating block: after a block update, if the block above is air, fire, liquid or replaceable, it becomes an entity that **rises** (falling sand in reverse). Generation does not trigger it | #A69AA5 |
| `cold_aercloud` | **translucent**, faces drawn inward and outward, alpha 225/255 | 0 | Collision is a 0.01 sheet at the block bottom; full cube if another cloud is above; 0.9 tall if falling > 2.5. Inside: fall distance resets and **vy ×0.005 while moving down** (slow sink, no damage). No occlusion, never suffocates | #F2F2F2 |
| `blue_aercloud` | same | 0 | **no collision**. Sets **vy = 2.0 blocks/tick**, launching you ≈20 blocks in 21 ticks. 50 splash particles and a wobble sound. Sneaking (or riding) cancels the launch | #C2F2F2 pale cyan |
| `golden_aercloud` | same | 0 | same as cold (soft sink); gold map colour | #F2D57B |
| `skyroot_log` | log column | 0 | as oak log | side #57553E dark olive-grey · end #65664B |
| `golden_oak_log` | log column | 0 | — | side #665B3B · end shares the skyroot end |
| `skyroot_leaves` | leaves, holes (72/256 clear) | 0 | decays | #89A961 |
| `golden_oak_leaves` | leaves (84 clear) | 0 | **particles** (§5) | #CCAF38 |
| `crystal_leaves` / `crystal_fruit_leaves` | leaves (55 / 40 clear) | 0 | particles; the fruit variant drops white apples | #5AA8C1 · #609DB2 |
| holiday leaves / decorated holiday leaves | leaves | 0 | particles (white) | #797AA2 · #7A7596 |
| `white_flower` | cross, cutout | 0 | small flower (stew effect, 4 s) | #B8BFBB |
| `purple_flower` | cross, cutout | 0 | flower (inebriation, 12 s) | #696795 |
| `berry_bush` | **full leafy cube** (cutout) + inner X of stem | 0 | harvest → blue berries; leaves a bare stem (`berry_bush_empty`) | #75945F, 82 clear |
| bare berry stem | cross | 0 | regrows | — |
| skyroot / golden oak sapling | cross | 0 | grows the §3.1 / §3.2 trees | — |
| present | cube | 0 | holiday decoration | — |
| `carved_stone` / `sentry_stone` | cube | 0 / **11** | bronze dungeon | #797979 · #74797F |
| `angelic_stone` / light angelic stone | cube | 0 / 11 | silver dungeon | #B7A075 |
| `hellfire_stone` / light hellfire stone | cube | 0 / 11 | gold dungeon | #8D4E46 |
| `pillar` / `pillar_top` | log column | 0 | silver dungeon | carved face #E9CDC0 |
| locked / trapped / boss door / treasure door variants | cube with overlay | as base | locked = unbreakable; trapped = spawns a sentry, valkyrie or fire minion when stepped on; overlay marks: lock, exclamation, door, treasure | — |
| ambrosium torch | torch | 14 | | — |
| Skylands portal | translucent pane | 11 | | animated, 32 frames, 2 ticks each |
| also used | short grass, tall grass (tinted #B1FFCB), water, snow layer, dandelion, poppy (gold island) | | | |

Only the portal (and some decorative blocks) animate. No generated block is animated.

---------------------------------------------------------------------------------------------------------
## 5. Visuals
- **Cloud floor**: a flat cloud layer at **y 9.5**, under every island, scaled 12×1×12 and drifting
  +x at 0.03 blocks/tick.
  - Cloud colour is white × (0.9·f + 0.1) for R and G and (0.85·f + 0.15) for B, with f = clamp(cos(2π·t)·2 + 0.5, 0, 1).
- **Sky dome**:
  - Colour: **#C0C0FF** × the same f, greyed by rain and thunder.
  - No dark void disc below the horizon, so the sky below stays sky-coloured.
- **Sun and moon**:
  - Sun quad half-size 30, moon half-size 20.
  - Their opacity follows a **72000-tick day** (3× the Overworld's):
    - Sun opaque while dayTime%72000 < 38400; it fades out over 600 ticks.
    - Moon until 71400, then fades back to the sun.
- **Eternal day**:
  - Until the Sun Spirit is beaten, time is held at **18000** (noon), moving at most 10 ticks per tick toward it.
  - Afterwards time advances normally on the 72000-tick cycle.
  - Stars as in the Overworld (night only).
- **Sunrise / sunset fan**: for cos(2πt) in ±0.4, with f3 = cos/0.4·0.5 + 0.5:
  - Default: RGB = (0.65 + 0.3·f3, 0.25 + 0.7·f3², 0.4), alpha = (1 − (1 − sin(π·f3))·0.99)². A pinker,
    bluer sunset than the Overworld's (0.7 + 0.3f, 0.2 + 0.7f², 0.2).
  - Optional "green sunset" setting: (0.5·f3, 0.3 + 0.3·f3², 0.3 + 0.5·f3²).
- **Fog**:
  - **#9393BC** through the usual day/weather mixing.
  - Weather can only lighten it: rain ×(1+0.8r) on RG and ×(1+0.56r) on B, thunder ×(1+0.66) and ×(1+0.76).
    Each channel is capped at the sky colour.
  - **Terrain fog starts at half the render distance** (the Overworld starts near far − far/10).
  - Near the bottom of the band (y ≲ 32), fog RGB is **divided** by d². It turns brighter and whiter as you
    sink, instead of darkening.
- **Lightmap**: warm by default. Optional "colder lightmap" gives a blue-tinted night.
- **Particles**:
  - No ambient biome particles.
  - Golden oak, crystal and holiday leaves: each display tick has a 1/10 chance to spawn **15** swirling
    portal-style particles within ±4 blocks, velocity ±0.25.
    - Tints: golden oak **#F9BE00** (0.976, 0.745, 0), crystal **#00A5BE** (0, 0.645, 0.745), holiday white.
  - Blue cloud: 50 splash particles per bounce.
  - Whirlwinds have their own cloud particles.
- **Music** (synthesised like all our audio):
  - Skylands ambience every 12000–24000 ticks (10–20 min), only in the Skylands biomes.
  - Boss tracks: Slider, Valkyrie Queen, Sun Spirit fights.
- **No weather visuals**: no precipitation, so rain or thunder only greys the sky.

---------------------------------------------------------------------------------------------------------
## 6. Gap list: `src/gen/skylands.cpp` now vs this design
Ordered by how visible the difference is. Written against the generator as of this note; re-check before
starting each item.

1. **Island shape and altitude (highest impact).**
   - Now:
     - One heightmap slab per column.
     - Mask = fbm2(x/180, z/180, 3) > 0.05, top = 70 + 10·fbm(x/60) + 120, giving grass at band ≈183–197.
     - The slab hangs down `(mask−0.05)^1.35·105·(0.6 + 0.4·noise3)` to a point ("upturned hill").
     - Extra islets at a fixed band 216 and crystal islets at 228.
   - Target:
     - The 3D density of §1.2 with the y gradients 8–40 and 56–128.
     - Land in **y 18–105, peak 40–60**, on 28–33 % of columns.
     - **6–7 % of columns have 2+ stacked slabs** (gap p50 7). Slabs p50 11–14 thick.
     - Holes, arches, terraces, rounded undersides.
   - To do: write the density (blended noise + cell interpolation) into `SkylandsGen::generate`.
     Then make `sample()`, `islandTop()`, `trees()` and the structure probes (`skyTop`, dragon lair,
     `islandMask`) use the topmost solid y. They currently assume one heightmap.
2. **Soil colour.**
   - Now: `sky_grass` (top #99C2B9) over **brown `dirt`** (#7E5A3F), 2 deep.
   - Target: sky grass in the slate tone (#72A785, grey-blue side #667E7E) over **sky dirt #63777F**, ≈3
     deep (0–5), on every floor, holystone below.
   - The brown dirt band on every island side is the most out-of-place thing on screen.
   - To do: add the slate sky grass / sky dirt blocks (or retint `sky_grass` and add a sky dirt).
3. **Cloud layers.**
   - Now: the three flat noise-masked cloud sheets have been removed; only islands are land in the sky.
     All three cloud blocks are flagged `"bouncy"` in data/blocks.json, i.e. a slime bounce: vy = −vy·0.82
     (sim/player.cpp).
   - Target:
     - Scattered 2–4-tall puff chains: cold 1/7 chunks at y32–96, blue 1/24 (small), golden 1/75 at y 96–128.
     - Cold-cloud ribbons 70–130 long at y 0–31 every ~96 blocks.
     - The flat cloud plane at y 9.5.
   - Behaviour to do:
     - Cold and golden should **sink-catch**: no bounce, vy ×0.005, no fall damage.
     - Only blue should launch, with vy 2.0 (≈20 blocks). Never place blue in whole sheets.
   - The fallbacks `bounce_cloud`, `cloud_puff`, `sun_cloud` (sun_cloud gives slowfall and light 6) should
     follow the same rules or go.
4. **Trees.**
   - Now: our own canopy, layers H−2…H+1, radius 2/1, height 4–6 (+2 in forest).
   - Golden oak at **8 % (40 % in woodland)** instead of 1 %, and golden oaks use the generic shape.
   - `sunwood` trees (gold leaves, light 4) and cloud pines are our own extras; decide whether they stay.
   - Density now: 0–2 per chunk + 2 extra in forest / 1 in woodland. Target: ≈3 / 2.55 / 1.05 / 0.5
     per chunk **per island layer** for forest / woodland / grove / meadow.
   - To do:
     - Skyroot = plain oak shape (§3.1).
     - Golden oak = 10-log trunk + branches + r≈3.7 ball (§3.2).
     - Drop the `birch_log` / `cloud_leaves` fallbacks.
5. **Ground cover.**
   - Now: `tall_grass` on 8–42 % of tops, plus `sky_bloom`, dandelion, `bluebells`, `marigold`, `azure_bluet`,
     and berry bushes on 3 % of meadow.
   - Target:
     - Dense **short grass tinted #B1FFCB** (5–10 patches × 32 tries per layer).
     - Rare double tall grass.
     - Only two flowers: **white_flower** (1/8 chunks) and **purple_flower** (1/16), patches of 64 tries.
     - **berry_bush** patches 1/8.
   - `berry_bush` renders as a cross plant (render 3); the target is a full leafy cube with a stem.
6. **Water.**
   - Now: one water block replacing grass in meadow/grove dips.
   - Target: lakes (1/15 chunks, up to ≈14×14×4, grass rim) and **30 springs/chunk**, so many **waterfalls off
     island sides**. This is the Skylands' signature sight and is missing.
7. **Sky colour and fog.**
   - Now (`render/sky.cpp` DIM_SKYLANDS): zenith #6EB6FF, horizon #FFF6E4, fog #E7F4FF, fogStart 0.55.
   - Target: sky **#C0C0FF**, fog **#9393BC** (lavender-grey), fog start at half render distance, a brighter
     haze toward the bottom, pink sunset fan, 72000-tick day held at noon (eternal day).
   - The fixed sun direction (0.45, 0.82, 0.22) is a permanent noon, which already roughly matches eternal day.
   - The Overworld cloud layer (`CLOUD_BASE_Y` 724) plus the `cloudBank` band 0–30 already resemble the
     y 9.5 cloud floor.
8. **Ores.**
   - Now: per-block hash every 3rd y between 44 and 110. Single blocks at gravitite 3.5 % (<72),
     zanite ≈4.5 %, ambrosium ≈7 % of eligible stone, far denser than the target and never in veins.
     Plus `skyland_crystal_ore` blobs and `driftstone` (floats up when freed, a gravitite look-alike).
   - Target veins (§3.7): ambrosium 20×16, zanite 14×5, gravitite 7×4 + 5×3, **icestone 10×32**, **sky dirt
     blobs 20×33** inside holystone.
   - `icestone` exists in data but is not generated yet.
   - Data mismatch: `gravitite_ore` has light 6; the target is 0.
9. **Quicksoil.**
   - Now: quicksoil as the *top* block where the mask is < 0.1, plus a 2-block lip on mask 0–0.05 at the top height.
   - Target: flat 1-thick disks (r ≈ 3.46) **under** thin grass rims at y < 48, 1/5 chunks. No quicksoil on top.
   - The `glimmersand` fallback (slippery: accel 2 vs 14) is not friction 1.1.
10. **Crystal islets.**
    - Now: calcite and amethyst islets at band 228 with amethyst buds.
    - Target: 13+13+5-block grass/holystone islet with a **crystal tree** (skyroot log, crystal leaves, crystal
      fruit leaves), 1/50 chunks, y 32–96.
11. **Structures.**
    - Now: sky_temple (quartz, Valkyrie Queen), sky_shrine, skyship, moa_nest, sky_sanctum and storm-dragon lairs.
    - To add: Bronze (in-island, carved and sentry stone, Slider), Silver (sky temple on a cloud bed, angelic
      stone and pillars, Valkyrie Queen), Gold (floating 38×43×38 island with stubs and golden oaks,
      hellfire-stone room, Sun Spirit), large cloud ribbons.
    - Blocks still missing: light angelic / light hellfire stone, ambrosium torch, treasure chest, the
      locked/trapped/door variants.
12. **Holiday tree, presents, snow.** Seasonal only. Missing holiday leaves, decorated holiday leaves, present.
13. **Particles.**
    - Now: meadow "white motes" and woodland falling gold leaves (`render/env_layers.cpp`).
    - Target: no ambient particles. Only portal-style swirls from golden oak (#F9BE00), crystal (#00A5BE) and
      holiday leaves.
14. **Mobs.**
    - Spawning now: whirlwind, aerbunny, moa, phyg, flying_cow, sheepuff, aerwhale, zephyr, valkyries.
    - Check spawning for: cockatrice (dark), aechor plant, blue and golden swet, evil whirlwind, bosses Slider and
      Sun Spirit, sentry, mimic, fire minion.
15. **Tile colours** (our 32 px average now vs target):

    | block | now | target |
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

    cold_aercloud (#DDE8F2) and golden_aercloud (#EDC443) are close already.

Fallback blocks and what they stand in for:
- `sky_grass` → slate sky grass
- `dirt` → sky dirt
- `skystone` → holystone
- `glimmersand` → quicksoil
- `birch_log` / `cloud_leaves` → skyroot_log / skyroot_leaves
- `sunwood_*` → golden_oak_*
- `tall_grass` → short grass (#B1FFCB)
- `sky_bloom`, `flower_yellow`, `bluebells`, `marigold`, `azure_bluet` → white_flower / purple_flower
- `bounce_cloud` → blue_aercloud
- `cloud_puff` → cold_aercloud
- `sun_cloud` → golden_aercloud
- `skyland_crystal_ore` → ambrosium_ore
- `driftstone` → gravitite_ore (behaviour)
- `calcite` / `amethyst_*` → crystal islet + crystal leaves

All textures, sounds and structures for these are made by us: tiles are our own 32 px art, sounds are
synthesised, dungeon rooms are built in code. The numbers in this note are design targets only.
