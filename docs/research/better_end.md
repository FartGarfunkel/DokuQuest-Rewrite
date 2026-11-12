# Better End 3.2.5 — re-implementation spec for DokuQuest's End band

Sources: jar `/home/admin/Documents/Projects/Better END/` (MC 1.19.4, BCLib 2.3.x). Decompiles: `research_be/src/org/betterx/betterend/**` (CFR) and `/tmp/be-gen/...`.
Raw dumps next to this file: `biomes_raw.txt`, `biome_tables.md`, `blocks_raw.txt`, `tex_alpha.txt`; helper scripts in `scripts/` (`jrand.py` = Java LCG, `pnginfo.py` = PNG decoder).

Conventions
- "BE y" = Better End world y. The End noise column is 0..127 (vanilla End noise settings: height 128, cell 8 x 4 blocks); level height is 256.
- RNG `class_5820` = LegacyRandomSource = `java.util.Random` LCG (`seed ^ 0x5DEECE66D`, 48 bit). `nextFloat = next(24)/2^24`, `nextDouble = ((next(26)<<27)+next(27))/2^53`.
- `MHelper.randRange(int a,b)` = `a + nextInt(b-a+1)`; float/double = `a + next*(b-a)`.
- **[BCLib*]** marks behaviour that lives in BCLib, which is not in this jar. Those parts are inferred from call sites and from BCLib's public source, so check them before you copy a number exactly. Everything else comes from the decompiled classes or the jar's JSON.
- Noise: BE ships its own `org.betterx.betterend.noise.OpenSimplexNoise`, which is Kurt Spencer's 2014 OpenSimplex with a `long` seed. 2D and 3D `eval` return roughly -1..1.

---------------------------------------------------------------------------------------------------

## 1. Sky

### 1.1 Registration and config
- `BetterEndClient`: if `ClientOptions.customSky` (config `client.json` root key `customSky`, default **true**), it calls `DimensionRenderingRegistry.registerSkyRenderer(END, new BetterEndSkyRenderer())`. This replaces the vanilla End sky box (end_sky.png) completely.
- Other client options (all default true): `blendBiomeMusic` (cross-fades biome music) and `sulfurWaterColor` (water near brimstone is recoloured, see 1.8).
- The background behind every layer is the GL clear colour, which equals the fog colour of 1.7. Nothing else is drawn first.

### 1.2 Time and rotation
```
time  = ((dayTime + tickDelta) % 360000) * 1.7453292e-5      // = π/180000 rad per tick (0.001° per tick)
time2 = 2*time, time3 = 3*time                                // one full turn of `time` takes 360000 ticks = 5 h
```
`dayTime` is `Level.getDayTime()`. It keeps advancing in the End: the End's fixed_time only changes timeOfDay, not dayTime. The rotation axes come from `LegacyRandomSource(131)`, three `nextFloat()` per axis in order axis1..axis4, then normalised. All four land in the +x+y+z octant. Values computed with `scripts/jrand.py`:

| axis | x | y | z |
|---|---|---|---|
| axis1 | 0.8852 | 0.1505 | 0.4403 |
| axis2 | 0.4635 | 0.1207 | 0.8779 |
| axis3 | 0.5913 | 0.2016 | 0.7809 |
| axis4 | 0.8559 | 0.4956 | 0.1479 |

### 1.3 Buffers (built once, centred on the camera, rotation only)

| buffer | texture | generator | tries → kept | seed | half-size (units) | distance | shader |
|---|---|---|---|---|---|---|---|
| stars1 | none (flat colour) | makeStars | 3500 → **1816** | 41315 | 0.10–0.30 | 100 | POSITION |
| stars2 | none | makeStars | 2000 → **1036** | 35151 | 0.10–0.35 | 100 | POSITION |
| stars3 | `sky/stars.png` | makeUVStars | 1000 → **536** | 61354 | 0.4–1.2 | 100 | POSITION_TEX |
| stars4 | `sky/stars.png` | makeUVStars | 1000 → **524** | 61355 | 0.4–1.2 | 100 | POSITION_TEX |
| nebula1 | `sky/nebula_2.png` | makeFarFog | 30 → **19** | 11515 | (40–60)·2 = 80–120 | 200 | POSITION_TEX |
| nebula2 | `sky/nebula_3.png` | makeFarFog | 10 → **6** | 14151 | 80–120 | 200 | POSITION_TEX |
| horizon | `sky/nebula_1.png` | makeCylinder(16 seg, h=50, r=100) | — | — | y ±50 | r 100 | POSITION_TEX |
| fog | `sky/fog.png` | makeCylinder(16 seg, h=50, r=70) | — | — | y ±50 | r 70 | POSITION_TEX |

The field names are swapped: `NEBULA_1` holds nebula_2.png, `NEBULA_2` holds nebula_3.png and `HORIZON` holds nebula_1.png.

Star quad generator, shared by all three makers:
```
for i in 0..count-1:
  x = rnd.d*2-1; y = rnd.d*2-1 (makeFarFog: y = rnd.d-0.5); z = rnd.d*2-1; size = randRange(min,max) // 4 draws
  L = x²+y²+z²; if !(L<1 && L>0.001) continue                // rejection sampling inside the unit ball
  dist = 1 (far fog: 2, and size *= 2); n = (x,y,z)/sqrt(L)*dist; P = n*100
  a1 = atan2(n.x,n.z); a2 = atan2(sqrt(n.x²+n.z²), n.y); a3 = rnd.d*2π  // in-plane spin
  (UV stars only: minV = rnd.nextInt(4)/4 picks one of the 4 frames in stars.png)
  for k in 0..3: qx=((k&2)-1)*size; qy=(((k+1)&2)-1)*size
     aa=qx*cos3-qy*sin3; ab=qy*cos3+qx*sin3; dy=aa*sin2; ae=-aa*cos2
     dx=ae*sin1-ab*cos1; dz=ab*sin1+ae*cos1; vertex P+(dx,dy,dz)
     uv: stars u=(k>>1)&1, v=(((k+1)>>1)&1)/4+minV;  far fog u=(k>>1)&1, v=((k+1)>>1)&1
```
The cylinder has 16 quads with u = i/16 → (i+1)/16, v 0 at y=-50 and v 1 at y=+50. It is unrotated, so the band always sits on the horizon. Each quad is a camera-facing billboard on the sphere, spun randomly in its plane.

Angular sizes: stars1/2 have a half-angle of 0.06°–0.17°, which is about one pixel. UV stars are 0.23°–0.69°. Nebula sprites are 22°–31° half-angle, so each spans 44°–62°. The horizon band covers ±26.6° elevation and the fog band ±35.5°.

### 1.4 Draw order, colours and blending
State: `FogRenderer.levelFogColor()`, `depthMask(false)`, blend `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`. Each layer sets `ShaderColor = (r,g,b,a)` and the output is `tex*ShaderColor`; the POSITION layers output the flat colour. None of the sky shaders apply distance fog.
`blindA = 1 - BackgroundInfo.blindness` (0..1), so blind02 = 0.2·blindA and blind06 = 0.6·blindA.

| # | layer | rotation (Quaternion) | RGB | alpha | drawn when |
|---|---|---|---|---|---|
| 1 | horizon (nebula_1) | Y by +time | 0.77, 0.31, 0.73 | 0.7·blindA | blindA>0 |
| 2 | nebula1 (nebula_2) | Y by −time | 0.77, 0.31, 0.73 | 0.2·blindA | blindA>0 |
| 3 | nebula2 (nebula_3) | Y by +time2 | 0.77, 0.31, 0.73 | 0.2·blindA | blindA>0 |
| 4 | stars3 (UV) | axis3 by time | 0.77, 0.31, 0.73 | 0.6·blindA | blindA>0 |
| 5 | stars4 (UV) | axis4 by time2 | 1, 1, 1 | 0.6·blindA | blindA>0 |
| 6 | fog cylinder (fog.png) | none | fogColor (1.7) | a = clamp(fogDensity−1, 0, 1) | a>0 |
| 7 | stars1 (points) | axis1 by time3 | 1, 1, 1 | 0.6·blindA | blindA>0 |
| 8 | stars2 (points) | axis2 by time2 | 0.95, 0.64, 0.93 | 0.6·blindA | blindA>0 |

Afterwards the renderer restores `depthMask(true)` and the default blend, and disables blending. Because the fog cylinder (6) is drawn before the small point stars (7, 8), those stars stay visible through a full haze while the nebulae and big stars get veiled. With fogDensity ≥ 2 the horizon band ±35° is a solid fog-coloured wall.

### 1.5 Sky textures (`assets/betterend/textures/sky/`, all verified)

| file | size | type | content |
|---|---|---|---|
| nebula_1.png (horizon) | 2048×256 | RGBA | avg weighted colour #9EB9DC, mean α 51. Alpha by row band, top→bottom 0,2,5,12,26,59,119,192,193,123,56,20,9,4,1,0, so it peaks on the horizon |
| nebula_2.png | 512×512 | RGBA | #C0A6D9, mean α 44, 21% fully clear |
| nebula_3.png | 512×512 | RGBA | #90728D, mean α 36, 32% fully clear |
| stars.png | 16×64 | grey+α | 4 frames of 16×16 (v = frame/4). Frame 0 is the largest (rows 0–14) and frame 3 the smallest (rows 4–10); all white |
| fog.png | 2048×256 | grey+α | white. Alpha by row band 1,10,33,81,148,214,250,255,255,251,218,156,88,35,11,1 |

### 1.6 Blindness and night vision [BCLib*]
- BCLib's `CustomFogRenderer` sets `BackgroundInfo.blindness`: 1 if a Blindness effect has more than 20 ticks left, otherwise duration/20. The same factor lerps the fog to start 0 and end ×0.03.
- Night vision skips the ×4 fog-colour boost in 1.7, which leaves the sky about 4× darker.

### 1.7 Fog colour, fog density and fog distance (End only)
Fog colour per frame (vanilla 1.19.4 `FogRenderer.setupColor` followed by the BCLib `@Inject RETURN`):
```
F  = gaussian 6×6×6 sample of biome fog_color at quart positions round (camPos-2)/4
F *= 0.15                                   // DimensionSpecialEffects.EndEffects.getBrightnessDependentFogColor
h  = 1 - (0.25 + 0.75*renderDistChunks/32)^0.25
F  = F + (sky - F)*h,  sky = biome sky_color·brightness = 0 for every End biome
F *= 4                                      // [BCLib*] End only, unless the camera is in water or has night vision
```
The result is `fogColor ≈ biomeFog × 0.6 × (1-h)`. That gives ×0.49 at rd 8, ×0.51 at rd 12, ×0.53 at rd 16 and ×0.60 at rd 32. This colour is the clear (sky background) colour, the terrain fog colour, and the tint of sky layer 6.

Fog density [BCLib*]: `BackgroundInfo.fogDensity` is the trilinear interpolation of the biome `fogDensity` at the 8 corners of the 32-block grid cell holding the eye. Non-BCL biomes (vanilla End biomes) use 1.0. The values are listed in table 2a.
Fog distances [BCLib*] replace vanilla `setupFog`. They are always on in the End, where `vd` is the render distance in blocks:
```
fogStart = vd * 0.25 / fogDensity
fogEnd   = vd        / fogDensity        // so density 2 halves the visible range and 3 cuts it to a third
```
The "thick fog" branch (`0.05·vd/d` … `min(vd,192)·0.5/d`) only applies to Nether thick-fog areas. Fog is linear.

### 1.8 Water colour hack (`BiomeColorsMixin`)
If `sulfurWaterColor` is on, the 20 columns in a 5×5 ring round a water block (corners and centre excluded, sorted by distance) are checked at the same y. If the first brimstone found is in one of the 4 nearest columns, the water becomes **#5CA04E** (poison). Otherwise it becomes **#69D5F4** (stream).

### 1.9 Music
- `MinecraftClientMixin`: in the End, if the boss overlay is visible and the player is within 500 blocks of x=z=0 the music is the dragon-fight track. Otherwise it is the biome's music, falling back to vanilla END music.
- Every BE biome uses `min_delay 600`, `max_delay 2400`, `replace_current_music true`. `MusicTrackerMixin` fades the old track out at 5e-4 per ms (about 2 s) when the biome's track changes, then waits `rand(0, maxDelay/2)` before starting the next.
- Music events (`sounds.json`) are forest, water, dark, openspace and caves, four tracks each by third-party artists.
- Every biome uses the mood sound `betterend.ambient.dust_wastelands` (tick_delay 6000, block_search_extent 8, offset 2). Ambient loops are per biome.

---------------------------------------------------------------------------------------------------

## 2. Biomes (25: 18 land, 1 void, 6 cave)

These defaults apply to every BE biome (BCLBiomeBuilder):
- sky_color 0, no precipitation, temperature 0.5 (Ice Starfield 0.0), downfall 0.5. Water #3F76E4 and water fog #050533 unless overridden.
- Default features in every biome: `round_cave` 1/2 and `tunel_cave` ×1, both in land biomes with has_caves only; `crashed_ship` 1/500; `flavolite_layer` ×6; `thallasium_ore` ×24; `ender_ore` ×12; `minecraft:end_gateway_return` (not in cave biomes).

Placement legend: `L0-N` means vanilla `count_on_every_layer` uniform[0,N]. That runs per chunk per "layer": the n-th surface from the top in each column, so the feature also lands on lower ledges and islands below. `xN` means count N plus in_square. `1/N` is a rarity filter. Lang names come from `en_us.json`.

### 2a. Climate / client effects

| id | display name | kind | fog | fogDensity | water / water-fog | grass=foliage | particle (prob/block/tick) | music | ambient loop |
|---|---|---|---|---|---|---|---|---|---|
| amber_land | Amber Land | land | #FFB847 | 2.0 | 916C48 | DB7326 | amber_sphere 0.001 | forest | amber_land |
| blossoming_spires | Blossoming Spires | land | #F192E5 | 1.7 | 3F76E4/050533 | 7A2D7A | - | forest | blossoming_spires |
| chorus_forest | Chorus Forest | land | #571A57 | 1.5 | 491E49 | 7A2D7A | mc:portal 0.01 | dark | chorus_forest |
| crystal_mountains | Crystal Mountains | land | #A080A0 | 1.0 | 3F76E4/050533 | FF85D3 | - | openspace | - |
| dragon_graveyards | Dragon Graveyards | land | #F42E4F | 1.3 | CB3BA7 | F42E4F | firefly 0.0007 | openspace | glowing_grasslands |
| dry_shrubland | Dry Shrubland | land | #84230D | 1.2 | 715835 | ED7A42 | - | openspace | - |
| dust_wastelands | Dust Wastelands | land | #E2EFA8 | 2.0 | C0B483 | - | mc:white_ash 0.01 | openspace | dust_wastelands |
| foggy_mushroomland | Foggy Mushroomland | land | #297AAD | 3.0 | 77E3FA | 49D2D1 | glowing_sphere 0.001 | forest | foggy_mushroomland |
| glowing_grasslands | Glowing Grasslands | land | #63E4F7 | 1.3 | 5CFAE6 | 49D2D1 | firefly 0.001 | openspace | glowing_grasslands |
| lantern_woods | Lantern Woods | land | #BD5246 | 1.1 | ABEAE2 | FE5539 | glowing_sphere 0.001 | forest | - |
| megalake | Megalake | land | #B2D1F8 | 1.75 | 60A3FF | 49D2D1 | - | water | megalake |
| megalake_grove | Megalake Grove | land(sub) | #B2D1F8 | 2.0 | 60A3FF | 49D2D1 | glowing_sphere 0.001 | water | megalake_grove |
| neon_oasis | Neon Oasis | land(sub) | #E2EFA8 | 2.0 | 6AEED7 | - | mc:white_ash 0.01 | openspace | dust_wastelands |
| painted_mountains | Painted Mountains | land(sub) | #E2EFA8 | 2.0 | C0B483 | - | mc:white_ash 0.01 | openspace | dust_wastelands |
| shadow_forest | Shadow Forest | land | #000000 | 2.5 | 2A2D50 | 2D2D2D | mc:mycelium 0.01 | dark | chorus_forest |
| sulphur_springs | Sulfur Springs | land | #CFC23E | 1.5 | 195A9D/1E413D | - | sulphur_particle 0.001 | openspace | sulphur_springs |
| umbra_valley | Umbra Valley | land | #646464 | 1.0 | 456886 | ACBDBE | amber_sphere 0.0001 | dark | umbra_valley |
| umbrella_jungle | Umbrella Jungle | land | #57DFDD | 2.3 | 77C6FD | -/1BB7C2 | jungle_spore 0.001 | forest | umbrella_jungle |
| ice_starfield | Ice Starfield | void | #E0F5FE | 2.2 | 3F76E4/050533 | -/C1F4F4 | snowflake 0.002 | mc:end | - |
| empty_end_cave | Empty End Cave | cave | #A080A0 | 2.0 | 3F76E4/050533 | - | - | caves | caves |
| empty_aurora_cave | Empty Aurora Cave | cave | #961E44 | 2.0 | BA4DED | 6C192E | glowing_sphere 0.001 | caves | caves |
| lush_aurora_cave | Lush Aurora Cave | cave | #961E44 | 2.0 | BA4DED | 6C192E | glowing_sphere 0.001 | caves | caves |
| empty_smaragdant_cave | Empty Smaragdant Cave | cave | #00FDB6 | 2.0 | 1FA7D4 | 008391 | smaragdant_particle 0.001 | caves | caves |
| lush_smaragdant_cave | Lush Smaragdant Cave | cave | #00FDB6 | 2.0 | 1FA7D4 | 008391 | smaragdant_particle 0.001 | caves | caves |
| jade_cave | Jade Cave | cave | #769670 | 2.0 | 5FDFFF | - | - | caves | caves |

How particles behave: vanilla `animateTick` samples about 667 random blocks within ±16 / ±32 of the player each tick and spawns one particle per air block with chance = prob. BE particle classes:
- glowing_sphere, firefly, amber_sphere, sulphur and snowflake live 150–300 ticks with a quad size of 0.05–0.15 (snowflake 0.05–0.2).
- smaragdant lives 60–120 ticks.
- jungle_spore drifts upward at 0.02–0.04 per tick with gravity −0.001 per tick.
- Textures: `textures/particle/<name>_0..7.png` (firefly.png and sulphur_particle.png are single files).

### 2b. Terrain settings and surface

| id | terrainHeight | genChance | edge | parent | caves | top / alt-top / under | floor rule |
|---|---|---|---|---|---|---|---|
| amber_land | 0.1 | 1.0 | 0 | - | y | amber_moss / amber_moss / end_stone | y |
| blossoming_spires | 0.1 | 1.0 | 0 | - | n | pink_moss / pink_moss / end_stone | y |
| chorus_forest | 0.1 | 1.0 | 0 | - | y | chorus_nylium / chorus_nylium / end_stone | y |
| crystal_mountains | 0.1 | 1.0 | 0 | - | y | crystal_moss / crystal_moss / end_stone | y |
| dragon_graveyards | 0.1 | **0.1** | 0 | - | y | sangnum / sangnum / end_stone | y |
| dry_shrubland | 0.1 | 1.0 | 0 | - | y | rutiscus / rutiscus / end_stone | y |
| dust_wastelands | **1.5** | 1.0 | 0 | - | y | endstone_dust / endstone_dust / end_stone | y |
| foggy_mushroomland | 0.1 | 1.0 | 0 | - | y | end_moss / end_mycelium / end_stone | y |
| glowing_grasslands | 0.1 | 1.0 | 0 | - | y | end_moss / end_moss / end_stone | y |
| lantern_woods | 0.1 | 1.0 | 0 | - | y | rutiscus / rutiscus / end_stone | y |
| megalake | **0.0** | 1.0 | 0 | - | y | end_moss / endstone_dust / end_stone | y |
| megalake_grove | **0.0** | 1.0 | 0 | megalake | y | end_moss / end_moss / end_stone | y |
| neon_oasis | 0.1 | **0.5** | 0 | dust_wastelands | y | endstone_dust / end_moss / end_stone | y |
| painted_mountains | 0.1 | 1.0 | 0 | dust_wastelands | y | endstone_dust / endstone_dust / end_stone | y |
| shadow_forest | 0.1 | 1.0 | 0 | - | y | shadow_grass / shadow_grass / end_stone | y |
| sulphur_springs | **0.0** | 1.0 | 0 | - | n | flavolite / end_stone / end_stone | n (custom) |
| umbra_valley | 0.1 | 1.0 | 0 | - | y | umbralith / pallidium_full / umbralith | n (custom) |
| umbrella_jungle | 0.1 | 1.0 | 0 | - | y | jungle_moss / jungle_moss / end_stone | y |
| ice_starfield | 0.1 | **0.25** | 0 | - | n | end_stone / end_stone / end_stone | (void) |
| caves (all 6) | 0.1 | 1.0 | 0 | - | n | end_stone; lush_* top = cave_moss | y |

Surface rules (from `data/betterend/worldgen/betterx/surface_rules/*.json` and the biome classes):
- **Default**: the floor block is `top`, with everything below it `under`. When top ≠ alt, a floor block is alt if `bclib:threshold_condition` (OpenSimplex seed 4141, scale 0.1) + U(−0.4, 0.4) > 0, and top otherwise. This alt/top dither applies to foggy mushroomland (end_mycelium patches in end_moss) and megalake (endstone_dust patches).
- **SplitNoiseCondition** (seed 4141): `noise(x·0.1, z·0.1) + U(−0.4, 0.4) > 0` → 1. Crystal Mountains adds a rule at priority 1: floor = [end_moss, end_stone][split]. Neon Oasis uses [endstone_dust, end_moss][split].
- **Dust wastelands / neon oasis**: `ceil` = end_stone, and the first 5 blocks below a floor are endstone_dust (stone_depth 5).
- **Sulphur springs** (SulphuricSurfaceNoise, seed 5123): `v = n(0.03) + n(0.1)·0.3 + U(±0.1)`. v < −0.6 → end_stone, < −0.3 → flavolite, < 0.5 → sulphuric_rock, otherwise brimstone. Applied to the floor and to stone_depth 5.
- **Umbra valley** (UmbraSurfaceNoise, seed 1512): same formula. v > 0.4 → pallidium_full, > 0.15 → pallidium_heavy, > −0.15 → pallidium_thin, > −0.4 → pallidium_tiny, otherwise umbralith. The filler is umbralith.
- **Painted mountains**: rule 9, `VerticalBandNoiseCondition`: `idx = int(y/4 + normalNoise(x·6, y·13, z·6)·4)` (the noise is the vanilla `Noises` key `class_6731.field_35372`; the intermediary name was not resolved, so it is probably but not certainly `minecraft:surface`). The block is [end_stone, flavolite, violecite][idx mod 3], via [BCLib*] SwitchRuleSource.
- **Jade cave walls**: `depth = n2(x·0.02, z·0.02)·0.2 + 0.5`, `i = |floor((y + n1(x·0.2, z·0.2)·1.5)·depth + 0.5)| % 3`, giving [virid, azure, sandy]_jadestone. Noise seeds are `"jade_cave".hashCode()` and `"depth_noise".hashCode()`.

### 2c. Biome-specific features and mobs (defaults omitted)
- **amber_land**: end_lake_rare[1/40], amber_ore[x60], helix_tree[L0-1], lanceleaf[L0-2], glow_pillar[L0-1], amber_grass[L0-7], amber_root[L0-1], bulb_moss[L0-1], bulb_moss_wood[L0-15], charnia_orange[L0-10], charnia_red[L0-10]. Mobs: enderman 50 1-4, end_slime 30 1-2.
- **blossoming_spires**: spire[1/4], floating_spire[1/8], silk_moth_nest[1/2], structures(nbt)[1/10], tenanea[L0-3], tenanea_bush[L0-12], bulb_vine[L0-5], bushy_grass[L0-10], bushy_grass_wg[L0-8], blossom_berry[L0-3], twisted_moss[L0-15], twisted_moss_wood[L0-25]. Mobs: silk_moth 5 1-2, enderman 50 1-4.
- **chorus_forest**: end_lake_rare[1/40], structures[1/10], violecite_layer[x8], pythadendron_tree[L0-1], pythadendron_bush[L0-3], vanilla chorus_plant, purple_polypore[L0-5], chorus_grass[L0-3], chorus_mushroom[L0-1], tail_moss[L0-15], tail_moss_wood[L0-25], charnia_purple[L0-10], charnia_red_rare[L0-2]. Mobs: end_slime 5, enderman 50.
- **crystal_mountains**: structure `mountain` (spacing 3, separation 2 chunks), crystal_grass[L0-5]. Mobs: enderman 50 1-2.
- **dragon_graveyards**: obsidian_pillar_basement[1/8], fallen_pillar[1/20], obsidian_boulder[1/10], dragon_bone_ore[x24, size 8], gigantic_amaranita[L0-1], large_amaranita[L0-5], small_amaranita[L0-4], globulagus[L0-6], clawfern[L0-5]. Mobs: enderman 50.
- **dry_shrubland**: lucernia_bush_rare[L0-1], orango[L0-6], aeridium[L0-5], lutebus[L0-5], lamellarium[L0-6]. Mobs: enderman 50.
- **dust_wastelands**: defaults only; its look comes from terrainHeight 1.5 (dunes) and white_ash. Mobs: enderman 50.
- **foggy_mushroomland**: structure `giant_mossy_glowshroom` (spacing 16, separation 8), end_lake[1/4], structures[1/10], mossy_glowshroom[L0-2], blue_vine[L0-1], umbrella_moss[L0-3], creeping_moss[L0-3], dense_vine[L0-3], cyan_moss[L0-15], cyan_moss_wood[L0-25], end_lily[L0-10], bubble_coral[L0-10], charnia_cyan/light_blue[L0-10], charnia_red_rare[L0-2]. Mobs: dragonfly 80 2-5, end_fish 20 2-5, cubozoa 10 3-8, end_slime 10, enderman 10.
- **glowing_grasslands**: end_lake_rare[1/40], lumecorn, blooming_cooksonia, salteago, vaiolush_fern, fracturn [each L0-5], umbrella_moss_rare, creeping_moss_rare, twisted_umbrella_moss_rare [each L0-2], charnia_cyan/green/light_blue[L0-10], charnia_red_rare[L0-2]. Mobs: enderman 50.
- **lantern_woods**: end_lake_normal[1/20], structures[1/10], flamaea[L0-20], lucernia[L0-3], lucernia_bush[L0-10], filalux[L0-1], aeridium[L0-5], lamellarium[L0-6], bolux_mushroom[L0-2], aurant_polypore[L0-5], pond_anemone[L0-10], charnia_orange/red[L0-10], ruscus[L0-10], ruscus_wood[L0-10]. Mobs: enderman 50.
- **megalake**: structure `megalake` (spacing 4, separation 1), end_lotus[L0-6], end_lotus_leaf[L0-4], bubble_coral_rare[L0-4], end_lily_rare[L0-4], umbrella_moss[L0-3], creeping_moss[L0-3], charnia_cyan/light_blue[L0-10], charnia_red_rare[L0-2], menger_sponge[L0-1]. Mobs: dragonfly 50 1-3, end_fish 50 3-8, cubozoa 50 3-8, end_slime 5, enderman 10.
- **megalake_grove**: structure `megalake_small` (spacing 4, separation 1), lacugrove[L0-4], plus the megalake list. Mobs: dragonfly 20, end_fish 20, cubozoa 50, end_slime 5, enderman 10.
- **neon_oasis**: desert_lake[1/8], neon_cactus[L0-2], umbrella_moss[L0-3], creeping_moss[L0-3], charnia_cyan/green/red[L0-10]. Mobs: enderman 50.
- **painted_mountains**: structure `painted_mountain` (spacing 3, separation 2). Mobs: enderman 50.
- **shadow_forest**: end_lake_rare[1/40], structures[1/10], violecite_layer[x8], dragon_tree[L0-2], dragon_tree_bush[L0-5], shadow_plant[L0-5], murkweed[L0-2], needlegrass[L0-1], shadow_berry[L0-1], twisted_vine[L0-1], purple_polypore[L0-5], tail_moss[L0-15], tail_moss_wood[L0-25], charnia_purple[L0-10], charnia_red_rare[L0-2]. Mobs: shadow_walker 80 2-4, enderman 40 1-4, phantom 1 1-2.
- **sulphur_springs**: geyser[1/8], sulphuric_cave[x2], sulphuric_lake[1/8], surface_vent[1/4], hydralux[L0-5], charnia_green/orange[L0-10], charnia_red_rare[L0-2]. Mobs: enderman 50 1-4, end_fish 50 3-8, cubozoa 50 3-8.
- **umbra_valley**: umbralith_arch[1/10], thin_umbralith_arch[1/15], inflexia[L0-16], flammalix[L0-5]. Mobs: none.
- **umbrella_jungle**: end_lake[1/4], structures[1/10], umbrella_tree[L0-2], jellyshroom[L0-2], twisted_umbrella_moss[L0-3], small_jellyshroom_floor[L0-2], jungle_grass[L0-6], cyan_moss[L0-15], cyan_moss_wood[L0-25], jungle_fern_wood[L0-12], small_jellyshroom_wall[L0-4], small_jellyshroom_wood[L0-8], small_jellyshroom_ceil[L0-8], jungle_vine[L0-5], charnia_cyan/green/light_blue[L0-10], charnia_red_rare[L0-2]. Mobs: enderman 50.
- **ice_starfield** (VOID biome): structure `giant_ice_star` (spacing 16, separation 8), ice_star[1/15], ice_star_small[1/8]. Mobs: enderman 20 1-4.

Plant scatter configs (`configured_feature/*.json`): `single_plant_feature {radius, chance}` tries random offsets within `radius` of the origin, and `chance` is a 1/chance gate. For example flamaea r12 c5, inflexia r7 c3, jungle_grass r7 c3, amber_grass r6 c1, crystal_grass r8 c1. Wall plants have radius 3–6. Vines: max_length 24 (magnula and rubinea 8).

### 2d. Cave biomes
Cave biomes are picked by a separate `HexBiomeMap(seed, biomeSizeCaves = 32, CAVE_BIOMES)`. The map is sampled at the cave's (x, z), all 6 have weight 1.0, and the result is written into the cave voxels' biome. Per cave voxel, floor and ceiling features are drawn from weighted lists (Java constructors below; the datapack JSON for the lush caves lists slightly different weights).

| cave | floor density | floor features (weight) | ceil density | ceil features (weight) |
|---|---|---|---|---|
| empty_end_cave | 0.1 | end_stone_stalagmite 1 | 0.1 | end_stone_stalactite 1 |
| empty_aurora_cave | 0.01 | big_aurora_crystal 1 | 0.1 | end_stone_stalactite 1 |
| lush_aurora_cave | 0.2 | big_aurora_crystal 1, cave_bush 5, cave_grass 40, stalagmite_cavemoss 5 | 0.1 | cave_bush 1, cave_pumpkin 1, rubinea 3, magnula 1, stalactite_cavemoss 10 |
| empty_smaragdant_cave | 0.1 | smaragdant_crystal 1, smaragdant_crystal_shard 20 | 0.1 | end_stone_stalactite 1 |
| lush_smaragdant_cave | 0.1 | same as empty smaragdant | 0.1 | same |
| jade_cave | — | walls are jadestone bands (2b) | — | — |

The lush caves' ceiling block is cave_moss (shape=top).

---------------------------------------------------------------------------------------------------

## 3. Terrain (`TerrainGenerator`, `IslandLayer`, `GeneratorOptions`, `LayerOptions`)

### 3.1 When it runs
BE replaces the noise column's density only when the End biome source is BCLib's with `generatorVersion == PAULEVS` [BCLib* default for new worlds]. The column is vanilla End noise: height `H = 128`, cells 8 (xz) × 4 (y), density > 0 = solid. `fillSlice` → `fillTerrainDensity(buffer, cellX*8, cellZ*8, 8, 4, 128)` is evaluated at cell corners and then trilinearly interpolated by vanilla. Seeds are drawn from `LegacyRandomSource(worldSeed)` with `nextInt()` in this order: largeIslands, mediumIslands, smallIslands, noise1, noise2.

### 3.2 Layer options (config `generator.json`, defaults)

| layer | distance (grid) | scale (SDF unit = blocks) | averageHeight | heightVariation | Y range at H=128 | coverage cfg → threshold | centerDist = floor(1000/dist) | hasCentralIsland |
|---|---|---|---|---|---|---|---|---|
| bigIslands | 300 | 200 | 0.546875 | 0.078125 | 60.0–80.0 | 0.5 → −0.0001 | 3 | false |
| mediumIslands | 150 | 100 | 0.546875 | 0.15625 | 50.0–90.0 | 0.5 → −0.0001 | 6 | **true** |
| smallIslands | 60 | 50 | 0.546875 | 0.234375 | 40.0–100.0 | 0.5 → −0.0001 | 16 | false |

`threshold = 0.9999 − coverage·2`. Other config defaults: `voidRingSize` 1000 (central void radius), `generateCentralIsland` true, `biomeSizeCaves` 32, `endCityFailChance` 5, custom portal and obsidian spikes on, obsidian platform on, `changeSpawn` false (20, 65, 0).

### 3.3 Island shape (one SDF, in units of `scale`)
`makeCone(rBottom, rTop, height, minY)` is a capped cone of half-height h/2, translated so it spans y = minY..minY+height.

| part | rBottom → rTop | y span | role |
|---|---|---|---|
| cone1 | 0.00 → 0.40 | −0.30 → −0.10 | inverted root spike |
| cone2 | 0.40 → 0.50 | −0.10 → 0.00 | underside shoulder |
| cone3 | 0.50 → 0.45 | 0.00 → 0.03 | top rim |
| cone4 | 0.45 → 0.00 | 0.03 → 0.05 | low dome |

```
bottom = smoothUnion(r=0.02)(cone1, cone2); top = smoothUnion(r=0.02)(cone3, cone4)
top'   = RadialNoiseMap(seed=layerSeed, radius 0.5, intensity 0.2)(top)   // per-call: intensity=h, radius=0.5/(1+h)
island = smoothUnion(r=0.01)(top', bottom)
per island: SDFScale(s), s = nextFloat()+0.5 (0.5–1.5), RNG reseeded with hash(posX,posZ); the central island uses s = 1.3
d_island(p) = island((p - centre)/layer.scale)
```
In blocks: a big island with s = 1 has radius 100, top +10 above its centre and root −60 below. Small islands have a radius of 12–37.

[BCLib*] `SDFRadialNoiseMap` adds to the distance:
```
d = |(x,z)|/radius; if d > 1 → 0
(1 - d) * N(rot(x,z)·0.75 + offset) * intensity    // rot = 0.5 rad; offset = island (x,z) & 32767
N(p) = n(p) + 0.5·n(3p + (1000,0)) + 0.2·n(9p + (1000,0))
```
The result is gentle bumps on the island top. Intensity is the biome terrain height in 3.5, so it is about 0 on lakes and sulphur, 0.05 on most land and 0.75 in Dust Wastelands (dunes).

Other [BCLib*] operators: capped cone (Inigo Quilez form); `smoothUnion(r)` with `h = clamp(0.5+0.5(b−a)/r, 0, 1)`, `lerp(b,a,h) − r·h(1−h)`; `SDFScale(s)` = `f(p/s)·s`.

### 3.4 Island positions (`updatePositions(x, z, H)`, cached per grid cell)
```
ix = floor(x/dist), iz = floor(z/dist)
for px in ix-1..ix+1, pz in iz-1..iz+1:
   if px²+pz² <= centerDist: skip                         // empty ring round the origin, per layer
   rnd.setSeed(hash(px,pz)); hash: h=seed+px*374761393+pz*668265263; h=(h^h>>13)*1274126177; h^h>>16
   cx=(px+rnd.f)*dist; cy=randRange(minY,maxY)*H; cz=(pz+rnd.f)*dist
   keep if densityNoise.eval(cx*0.01, cz*0.01) > threshold  // OpenSimplex(layerSeed): about 50% kept
if |ix|,|iz| < 125: drop islands with cx²+cz² < 1000²; medium layer also adds the CENTRAL island at (0,64,0)
```

### 3.5 Density (`fillTerrainDensity`, per 8-block column at cell corners)
```
dist1 = n1(x·0.1, z·0.1)·20 + n2(x·0.2, z·0.2)·10 + n1(x·0.4, z·0.4)·5   // x,z = cell index (block/8)
dist2 = n2(0.1)·20 + n1(0.2)·10 + n2(0.4)·5  → px = x·8 + dist1, pz = z·8 + dist2   (domain warp of up to ±35 blocks)
h = avgTerrainHeight(quart x·2, z·2)·0.5
for y-cell: py = y·4
  d = large(px,py,pz,h); if d<=1: d = max(d, medium(...)); if d<=1: d = max(d, small(...))   // density = −minSDF per layer
  if d > −0.5: d += n1(p·0.01)·0.02+0.02 + n2(p·0.05)·0.01+0.01 + n1(p·0.1)·0.005+0.005   // 3D noise: rough surfaces
  if py >= 128: d = −1; else if py > 100: d = lerp((py−100)/27, d, −1)                     // top fade, 100..127
```
`avgTerrainHeight` returns 0 if the centre biome's terrainHeight is below 0.1. Otherwise it is the weighted sum of terrainHeight over the 29 offsets with |o| ≤ 3 quarts (12 blocks), each weighted by |o|/3 and normalised so the weights sum to 1; the centre's own weight is 0. With every biome at 0.1 the result is h = 0.05, Dust Wastelands gives h = 0.75, and Megalake and Sulphur give 0 (flat tops).

### 3.6 Land vs void, and biome choice
- `EndLandBiomeDecider.suggestType` → `TerrainGenerator.isLand(quartX, quartZ, 128)`. It runs the same warp and layer SDF at column (quart>>1)+0.5 for y = 0, 4, …, 124 without the h override, and finds land if any `d > −0.01`. Results are cached per 128×128 quart section.
- If there is land the type is END_LAND (END_CENTER stays CENTER); otherwise it is END_VOID (CENTER becomes END_BARRENS).
- Within a type, BCLib's End biome source [BCLib*] picks a biome on a hex map. Defaults are land and void size 256, centre and barrens 128, and an inner void of about 1000 blocks radius. Picks are weighted by `genChance`.
- The pools [BCLib*]: the 18 BE land biomes plus vanilla end_highlands/end_midlands; the void pool is ice_starfield plus small_end_islands; the barrens pool is end_barrens; the centre is the_end.
- Sub-biomes (megalake_grove in megalake; neon_oasis and painted_mountains in dust_wastelands) replace part of their parent's hex cell with probability by genChance [BCLib*]. Edge size is 0 everywhere, so there are no edge biomes.
- The void biome only changes fog and sky and adds ice stars. Terrain never depends on biome except through terrainHeight.

### 3.7 Caves
- `round_cave` (land biomes with has_caves, 1/2 per chunk): radius 10–30. It needs at least 1.3R+5 of solid rock above and below. The shape is an ellipsoid squashed ×1/1.6 in y, with a noise radius `r = noise(p·0.1)·0.25R + 0.75R`. The cave is filled with cave_air, assigned a cave biome (2d), and then floor, ceiling and wall decor is added.
- `tunel_cave` runs ×1 per chunk.
- In sulphur springs, `sulphuric_cave` ×2 is used instead.

### 3.8 Mapping BE y into DokuQuest's band (band-local y 0..255)
Use **ourY = beY + 56**. BE's centre island top (≈70) then lands at our 126, next to our current main-island top of 125. BE's solid range 0..127 maps to 56..183, its fade zone (100..127) to 156..183, floating spires (≤192) to ≤248 and ice stars (32..128) to 88..184. Our current outer islands (`theend.cpp::island`) are 2D heightmap slabs with tops at about 122 (58+64) and are thinner than BE islands, which have roots 20–90 blocks deep.

---------------------------------------------------------------------------------------------------

## 4. Main features (shape recipes)

[BCLib*] helpers:
- `makeSpline(a,b,n)` = n points evenly spaced from a to b.
- `offsetParts(sp,rnd,dx,dy,dz)` adds gaussian·(dx,dy,dz) to every point after the first.
- `buildSDF(sp,r1,r2)` = union of capsules along the polyline, radius lerped r1 → r2 from base to tip.
- `fillSpline` draws 1-block lines and stops at the first non-replaceable block; `fillSplineForce` draws through.
- `powerOffset(sp,d,p)`: point i gets y += (i/(n−1))^p·d.
- `SDFFlatWave(n,i,φ)`: d − cos(atan2(z,x)·n+φ)·i, which scallops an edge.
- `SDFScale3D` = f(p/s) with no distance correction. `SDFDisplacement` = d + fn(p).
- `fillRecursive` = 6-neighbour flood fill from the origin over cells with d < 0 that the replace predicate allows. Each cell takes the block of the closest primitive, then post-process lambdas run.

Tree logs: interior cells (log above and below) become `<wood>_log`; cells exposed at the ends become `<wood>_bark`. Leaves get `distance = 6` and are re-flooded to Manhattan distance ≤ 6 from bark veins. In leaf balls, 1 in 5 interior cells become bark "veins" (1 in 15 for lucernia).

"Ground ok" means the block below is in `#minecraft:nylium`. BE tags all its turf/moss blocks into that tag.

1. **Mossy glowshroom** (foggy_mushroomland L0-2; ground end_moss or end_mycelium)
   - Stem: height 10–25, spline floor(h/4) points with xz jitter σ1, capsule 2.1 → 1.5. The whole thing is scaled 0.75–1.1.
   - Head, built at the stem top +2.5:
     - cone1: capped cone, half-height 2.5, r 1.5 → 2.5.
     - cone2: half-height 3, r 2.5 → 13, at +5, minus the same cone ×2 at +12. This is the shell.
     - FlatWave 12 rays, intensity 1.3.
     - smoothUnion(r=3) of those, unioned with an inner shell (+1.25, xz ×1.2).
     - Glow cone (half-height 3, r 2 → 12.5, at +4.25, minus the ×2 cone) is the **hymenophore** under the cap.
     - Droop warp: `y += n(x·0.1+cx, z·0.1+cz)·d·0.3 − d·0.15` (d = horizontal distance).
   - Roots: sphere r4, y ×0.7, FlatWave 5 rays intensity 1.5 at a random angle.
   - Assembly: smoothUnion(r=4)(stem, union(head, roots)).
   - Blocks: cap (transition=true above stem), hymenophore (light 15), **fur** (light 15) on every free side and below the hymenophore, stem = log/bark.
   - Size: head radius about 10–14 blocks, total height about 20–35.
   - Giant variant (structure, spacing 16 chunks): same SDF, scale 2.0–3.5, plus SDFRound 1.5. Head radius 26–45.
2. **Pythadendron** (chorus_forest L0-1; ground chorus_nylium)
   - Trunk: size 10–20, 4-point spline with jitter 0.7, capsule 1.7 → 1.1 (bark outside, log inside).
   - Recursive Y-fork: depth = floor((size−10)·0.3+1), 1–4 levels. Each fork has two arms of ±size·0.15 horizontally, raised by powerOffset(size·U(1,2), 4), 1-block bark lines with jitter 0.3. The next fork turns +90° ± 0.1 rad with size ×0.75–0.95.
   - Initial branch scale: size·((10−(size−10))/10+1.5).
   - Leaf balls at arm tips once depth < 3: sphere r 4.5–6.5, y ×0.6, displaced by noise(p·0.2)·3 and U(±1.5), bottom cut by the same sphere shifted −r. The result is flat purple pads.
3. **Lacugrove** (megalake_grove L0-4)
   - Trunk: size 15–25, 6-point spline with jitter 1, capsule r 1.2–1.8 → 0.7. Points 4..6 are redrawn as a 1-block line.
   - Leaf ball at point 4: r = U(6,8)·((size−15)/20+1), displaced ±3 noise and ±1.5 random, minus a sphere at −r−2. If r > 5, add r·2.5 bark blocks scattered round the centre.
   - Prop roots: 100 tries in ±5 on a checkerboard. Each root is a column of height `3.5 − d/2` plunging into end stone (bark on top, log below), which gives a mangrove look.
4. **Dragon tree** (shadow_forest L0-2)
   - Trunk: size 10–25, 6-point spline with jitter 1, capsule 1.2–2.3 → 1.2.
   - Cap at spline index 3.5: R = size·0.5–0.7. `floor(R)` radial bark branches follow the curve (0,0)→(0.1,0.3)→(0.4,0.6)→(0.8,0.8)→(1,1), scaled R·0.85–1.15, each with two side twigs rotated ±0.5 rad.
   - Leaf disc at the cap top: sphere R·1.15+2, minus 5× the sphere shifted −5·R', y ×0.5, noise 1.5. The result is a wide flat umbrella.
   - Roots: R·1.5 curves (0,1)→(0.8,−0.2) scaled R, kept only where the tip is in end stone.
5. **Tenanea** (blossoming_spires L0-3)
   - Branches: size 7–10, floor(size·0.45) = 3–4 arched branches on the curve (0,0),(.1,.35),(.2,.5),(.3,.55),(.42,.7),(.5,1), scaled size+U(0, size/2), jitter 1, 1-block bark lines.
   - At each tip a leaf ball: r = (size·0.3+U(0.8,1.5))·1.4 (about 4–6), y ×0.75.
   - tenanea_outer_leaves (fur) goes on every free face. One in six underside cells grows hanging **tenanea_flowers** 3–8 long (top/middle/bottom shapes, light 15, tint cycling #FA6FDE → #A759FF → #78CFEF → #FF57B6). A bark plus sits at the ball centre +1.
6. **Helix tree** (amber_land L0-1)
   - Double helix: 10 points per strand, radius (0.9 − 0.1i)·U(4.5,6), angle i rad, y = 2i; capsule 1.7 → 0.5. The second strand is the same rotated 180°.
   - A straight stem (0 → 20, r 1 → 0.5) rises from the top point. Everything is smooth-unioned (r=3) and scaled 0.5–1.0, then filled in a box of ±30s and −20s..+100s.
   - Leaves: a spiral of 20–30 points, angle 0.45i, y step `20/count·s·1.75` (about 35s tall), radius `(1 − (2(1−i·h)² − 1)²)·U(8,11)·s` (a bulb).
   - Each step draws a line from the axis to the spiral point, plus a parallel line 1 block over. COLOR rises 0..7 along each line.
   - Leaves tint rgb(237, 80+78·c/7, 20), from #ED5014 to #ED9E14. The top 3 blocks are colour 7.
7. **Umbrella tree** (umbrella_jungle L0-2)
   - Scale U(1, 1.7) in worldgen. size 10–20; floor(size·0.15) = 1–3 branches on the tenanea curve, scaled xz (size+U(0, size/2))·0.7 and y ×1–2, capsule 1.2 → 0.8.
   - Membrane per tip: sphere r = (size+U(0, size/2))·0.4 (4–12), minus a sphere at −4, y ×0.5, translated by 1−r/2, FlatWave with r..2r rays (at least 5) and intensity 0.6. A centre sphere of r 2.5 uses colour 0. All of it is smooth-unioned with r 2.
   - Membrane COLOR = clamp(floor(d/r·7), 1, 7) by distance to the nearest centre (textures membrane_1..7). 5–10 **clusters** (light 15) hang under colour < 2 at distance 1.5–2.5.
   - Roots: (size·0.5+3)·1.5.
8. **Jellyshroom** (umbrella_jungle L0-2)
   - Stem: height 5–8, 3-point spline from y −1 with jitter 0.5, capsule h·U(0.15,0.25) → 0.8.
   - Cap: shell of r = max(1.5, h·U(0.7,0.9)), same construction as the membrane, FlatWave r/2..r rays (at least 3) with intensity 0.2, smooth union 3.
   - Cap COLOR 0..7 by radial distance, tint lerp from (217,142,255) to (164,0,255), **translucent**.
   - Roots: h·1.75.
9. **Lucernia** (lantern_woods L0-3)
   - size 12–20; floor(size·0.3) = 3–6 tenanea-curve branches, scaled size·U(0.5,1).
   - Leaf balls r = (size·0.13+U(0.8,1.5))·1.4 (3.3–5.7), with lucernia_outer_leaves fur and bark veins in 1 of 15 cells.
   - One in six underside cells grows hanging **filalux** 3–8 long (light 15). Roots: size·0.35·1.5, starting at +3..5.
10. **Gigantic amaranita** (dragon_graveyards L0-1)
    - Stem: size 5–10, 5-point spline with jitter 0.7, capsule size·0.17 → 0.2 of amaranita_stem (exposed ends become hyphae), plus 3 jittered hyphae lines.
    - Head at top +1.5, radius floor(size/1.6) = 3–6. It is a blocky 3×3, 4×4 or 5×5 cap of amaranita_cap, radius+1 tall with a rounded top. Underneath: an amaranita_hymenophore ring, amaranita_lantern (light 15) cells, and amaranita_fur (light 15) hanging.
    - Its two smaller relatives are large_amaranita (3-tall model) and the small plant.
11. **Crystal Mountains** (structure `mountain`: spacing 3, separation 2; only if surface y > 5)
    - Centre: random point in a chunk. R = 50–100, height R·U(0.8,1.2).
    - Per column within R: `k = 1−(d²/R²)^0.3`; `maxY = cy + k·height·clamp12·(n(0.05)·0.3+0.7)·(n(0.1)·0.1+0.8)`. `clamp12` is a 12-block weighted average of "same biome and terrain within 4 of cy".
    - Fill end_stone from the surface (or ≥ cy−8) up to maxY. The top block is crystal_moss where split noise > 0, and crystal_moss_cover (light 7) goes on air next to end stone where |split| < 0.2.
    - Crystals per chunk: big ones, count clamp((h(8,8)−cy−24)/7, 0, 8), r 2–3, height r·U(1.5,3)+(y−80)·0.3, footprint |x|+|z| < r(1+fill), top sloped by coefX·x+coefZ·z with coefs in −1..1. Small ones, count clamp(…/2, 4, 8), r 1–2. All are **aurora_crystal** (translucent, light 15, colour cycling #F74DA1 → #78B8FF → #78FFA8 → #F33AFF by (x+y+z)·0.1).
12. **Sulphur springs** (geyser 1/8, surface_vent 1/4, sulphuric_lake 1/8)
    - *Geyser* (needs at least 25 solid blocks below):
      - halfHeight 10–20. Body: capped cone of sulphuric_rock r h/2 → h·0.1+0.5, raised h−3.
      - Plus h terraced bowls: inverted cones r_i = lerp(r1, r2, i/(h−1))·1.3, each a brimstone ring and water pool, noise-warped, step 2 blocks, spiralled 4 rad apart.
      - Inside: a water channel (cone plus a cave sphere of r h/2, ×1.5 in xz, at −h−10) wrapped in shells of brimstone (−2), sulphuric_rock (−4) and end_stone (−6).
      - A water column up to the surface. At the bottom, 150 hydrothermal vents on sulphuric_rock stacks with tube_worms, and vent_bubble_column above the vents. It ends with a sulphuric lake at the top.
    - *Surface vent*: 15–30 sulphuric_rock stacks of height ≤ 2–3 within r ≈ 2, each capped by a hydrothermal_vent.
    - *Sulphuric lake* (surface y ≥ 57): r 10–20, edge noise (x,z)·0.2. Water 1–2 deep is cut into the surface. Brimstone rings to r·1.5 are 1–3 deep; brimstone touching water becomes ACTIVE and grows sulphur_crystal (1/16 per face, age 0–2). Sulphur hill (5–13 stacked rings r 5–9 of water, brimstone and rock) exists but is not placed in any biome.
13. **Amber ore** (amber_land): vanilla ore, size 6, ×60 per chunk, uniform y 8..top−8 replacing end_stone. That makes amber veins very dense. **Flavolite / violecite layers**: OreLayer, a sphere of r = 6 (or 7.5) squashed y ×0.2 into a 12×2.4×12 disc, y-warped by `noise(x·0.1, z·0.1)·8`, at y 16–128, ×6 (or ×8) per chunk, replacing end_stone only.
14. **End lake** (end_lake 1/4, normal 1/20, rare 1/40)
    - r 10–20, depth r·0.5·U(0.8,1.2). It needs 4 probes at ±r to be within 5 of the centre height.
    - Water level = min of the probes. A bowl above it (radius grows 1.8 per block up to +5) is cut to air. Below it an ellipsoid is filled with water, with noise radius `r·(n(p·0.2)·0.25+0.75)`.
    - Floor and shore become endstone_dust, mixed 50/50 with the top material at the waterline.
15. **Megalake** (structure spacing 4, separation 1; R 32–64, depth 7–15; the grove uses R 20–40, depth 5–10)
    - Ellipsoid `(dx+n·10)² + ((y−cy)·R/depth)² + (dz+n'·10)² < (R·clamp8)²`. Below cy it is water, above it cave_air (a cut bank).
    - A ring out to R+8 below cy gets shore material (end_stone, top or dust).
    - Overhanging water gets end-stone pillars.
16. **Painted Mountains** (structure spacing 3, separation 2; surface y > 50)
    - R 50–100, height R·U(0.4,0.6). `k = 1−d²/R²`; maxY uses clamp10 and the noise factors (·0.3+0.7)(·0.1+0.9).
    - Strata: `idx = floor((y + n(0.07)·5 + n(0.2)·2 + 7)·0.65) % count`. There are count = floor(h·U(0.1,0.35)+1) slices, each picked at random from {end_stone, flavolite, violecite}.
17. **Spires / floating spires** (blossoming_spires)
    - Spire: base sphere r 2–3; then 3–7 times a new sphere r = U(1.3i+2.5, 1.3i+4) is put at the origin, the previous stack is lifted by r·(1+U(0,0.25)), and the two are smooth-unioned with r/2. The result is a tapering stack of spheres that is widest at the base.
    - Displacement `|n(0.1)|·3 + |n(0.3)|·1.3` erodes the surface. The skin is the biome top block, with the biome under block within 3 of the top. One in sixteen top cells grows a tenanea bush.
    - Floating spire: y = surface > 57 ? U(s, 2s)/2+32 : U(64, 192); the stack goes up 3–5 and back down, giving a double cone.
18. **Ice star** (ice_starfield; normal 1/15 with size 5–15 and 10–25 spikes; small 1/8 with size 3–5 and 7–12 spikes)
    - Spikes point to Fibonacci-sphere directions. Each is a capped cone r = 3+(size−5)·0.2 → 0, length = size, made of dense_snow. Placed at y U(32,128).
    - Layers by `dist + sdf·0.4 + U(0, 0.3size)`: below 0.7·size ancient_emerald_ice; below 0.9·size dense_emerald_ice; below iceR emerald_ice (translucent), where iceR = size < 7 ? 5·size : 1.3·size; outside that, dense_snow.
    - The giant ice star is a structure (spacing 16, separation 8).

---------------------------------------------------------------------------------------------------

## 5. Blocks needed

All textures are in `assets/betterend/textures/block/`. Every one listed exists (checked) and is 16×16 unless noted.

Render keys:
- CUBE = opaque cube. TURF = top/side cube with a vanilla end_stone bottom. COL = log column (top + side; bark uses the side on all 6 faces).
- LEAF = cutout cube. TRANS = translucent cube.
- CROSS = cutout X plant. WALL = flat panel on a wall. FUR = flat panel facing out (hanging hairs).
- VINE3 = hanging cross with roots/middle/bottom pieces. MODEL = custom JSON model (cutout).

L = light level. "glow" marks a Canvas emissive material in `materialmaps/block`; draw those faces unshaded / emissive. Average colours (alpha-weighted) are in `tex_alpha.txt`.

### 5a. Terrain, stones and turf

| id | name | render | L | textures (avg colour) | notes |
|---|---|---|---|---|---|
| endstone_dust | End Stone Dust | CUBE (falling) | 0 | endstone_dust #D3D99B | sand-like |
| end_moss | End Moss | TURF | 0 | end_moss_top #38A4A3, end_moss_side | |
| end_mycelium | End Mycelium | TURF | 0 | end_mycelium_top #217593, _side | |
| chorus_nylium | Chorus Nylium | TURF | 0 | chorus_nylium_top #7C2E7C, _side | |
| crystal_moss | Crystal Moss | TURF | 0 | crystal_moss_top #358E9E, _side | |
| shadow_grass | Shadow Grass | TURF | 0 | shadow_grass_top #171717, _side | |
| pink_moss | Pink Moss | TURF | 0 | pink_moss_top #905185, _side | |
| amber_moss | Amber Moss | TURF | 0 | amber_moss_top #8F4719, amber_moss_side_1/2/3 (random) | |
| jungle_moss | Jungle Moss | TURF | 0 | jungle_moss_top #1C6174, _side | |
| sangnum | Sangnum | TURF | 0 | sangnum_top #DC2A30, _side | |
| rutiscus | Rutiscus | TURF | 0 | rutiscus_top_1/2 #D95F49, rutiscus_side_1/2 | random variants |
| cave_moss | Cave Moss | TURF | 0 | cave_moss_top #C02856, _side | has a top/middle/bottom shape |
| pallidium_full/heavy/thin/tiny | Pallidium (Full/Heavy/Thin/Tiny Cover) | TURF | 0 | pallidium_*_top #9CAFB3 / #758690 / #5D6975 / #353C4D, _side | over umbralith |
| flavolite | Flavolite | CUBE | 0 | flavolite #E1C99B | |
| violecite | Violecite | CUBE | 0 | violecite #372C3E | |
| sulphuric_rock | Sulphuric Rock | CUBE | 0 | sulphuric_rock #594746, sulphuric_rock_top | |
| brimstone | Brimstone | CUBE | 0 | brimstone #BDA526 (active), inactive_brimstone #A7811F | ACTIVE state |
| umbralith | Umbralith | CUBE | 0 | umbralith, umbralith_2..7 (random) #191B2D | |
| virid / azure / sandy_jadestone | Virid/Azure/Sandy Jadestone | CUBE | 0 | #265041 / #336763 / #81924E | jade caves |
| mossy_obsidian | Mossy Obsidian | TURF-like | 0 | sangnum_top, mossy_obsidian_side | over obsidian |
| dragon_bone_block | Dragon Bone Block | COL | 0 | dragon_bone_block_top/_side #362849 | |
| mossy_dragon_bone | Mossy Dragon Bone | COL | 0 | sangnum_top, mossy_dragon_bone_side_ver, _cut | |
| dense_snow | Dense Snow | CUBE | 0 | dense_snow #D3F9FA | |
| emerald_ice | Emerald Ice | TRANS | 0 | emerald_ice #0697AA (α<255 everywhere) | |
| dense_emerald_ice / ancient_emerald_ice | Dense / Ancient Emerald Ice | CUBE | 0 | #06A0A6 / #039292 | |
| crystal_moss_cover | Crystal Moss Cover | FUR (glow-lichen-like, multi-face) | 7 | crystal_moss_cover 16×64, anim 4 frames, frametime 32, interpolated | |
| end_stone_stalactite(_cavemoss) | (Cave Moss) End Stone Stalactite | CROSS, 8 sizes | 0 | end_stone_stalactite_0..7, _cavemoss_0..7 | size 0–7 |

### 5b. Ores, crystals, vents and fluids

| id | name | render | L | textures | notes |
|---|---|---|---|---|---|
| amber_ore | Amber Ore | CUBE | 0 | amber_ore #CCA359 | |
| ender_ore | Ender Ore | CUBE | 0 | ender_ore | |
| thallasium_ore | Thallasium Ore | CUBE | 0 | thallasium_ore | |
| amber_block | Amber Block | CUBE | 0 | amber_block #E59110 | not placed by worldgen |
| aurora_crystal | Aurora Crystal | TRANS, tinted | 15 | aurora_crystal (semi-α), glow | 4-colour cycle (feature 11) |
| smaragdant_crystal | Smaragdant Crystal | CUBE | 15 | smaragdant_crystal_top/_side, glow | |
| smaragdant_crystal_shard | Smaragdant Crystal Shard | CROSS | 15 | smaragdant_crystal_shard, glow | |
| sulphur_crystal | Sulfur Crystal | MODEL (attached, age 0–2) | 0 | sulphur_crystal_0/1/2 | waterlogged |
| hydrothermal_vent | Hydrothermal Vent | MODEL (not full) | 0 | geyser_top, sulphuric_rock(_top) | emits bubbles |
| vent_bubble_column | Vent Bubble Column | invisible, water | 0 | none (particles) | |
| water | (vanilla) | LIQUID | 0 | biome water colours (2a) | |

### 5c. Tree and mushroom parts

| id | name | render | L | textures | notes |
|---|---|---|---|---|---|
| mossy_glowshroom_log/_bark | Mossy Glowshroom Log/Bark | COL | 0 | mossy_glowshroom_log_top #407A68, _log_side + _side_2..5 (random) | |
| mossy_glowshroom_cap | Mossy Glowshroom Cap | CUBE (random rotation) | 0 | mossy_glowshroom_cap #1989DD, _cap_transition | |
| mossy_glowshroom_hymenophore | Mossy Glowshroom Hymenophore | CUBE, glow | 15 | mossy_glowshroom_hymenophore_1..4 #51B6FE | |
| mossy_glowshroom_fur | Mossy Glowshroom Fur | FUR, glow | 15 | mossy_glowshroom_fur #63CCFE | |
| pythadendron_log/_bark | Pythadendron Log/Bark | COL | 0 | pythadendron_log_top #612C61, _log_side, _log_side_mossy | |
| pythadendron_leaves | Pythadendron Leaves | LEAF | 0 | pythadendron_leaves #893189 | |
| lacugrove_log/_bark/_leaves | Lacugrove … | COL / LEAF | 0 | lacugrove_log_top, _log_side(_2..4); lacugrove_leaves #2E918D | |
| dragon_tree_log/_bark/_leaves | Dragon Tree … | COL / LEAF | 0 | dragon_tree_log_top #2A1E39, _log_side(_2..4) #1C1822; dragon_tree_leaves #402D52 | |
| tenanea_log/_bark/_leaves | Tenanea … | COL / LEAF | 0 | tenanea_log_top/_side; tenanea_leaves #BE46A5 | |
| tenanea_outer_leaves | Tenanea Outer Leaves | FUR | 0 | tenanea_outer_leaves #C951B3 | |
| tenanea_flowers | Tenanea Flowers | VINE3, tinted, glow | 15 | tenanea_flowers, _bottom, _stem, _stem_bottom (grey, tinted) | |
| helix_tree_log/_bark | Helix Tree Log/Bark | COL | 0 | helix_tree_log_top #A06525, _log_side #5A534E | |
| helix_tree_leaves | Helix Tree Leaves | CUBE, opaque, tinted | 0 | helix_tree_leaves #B28259 | COLOR 0–7 |
| umbrella_tree_log/_bark | Umbrella Tree Log/Bark | COL | 0 | umbrella_tree_log_top #0D6371, _log_side #335483 | |
| umbrella_tree_membrane | Umbrella Tree Membrane | TRANS (slime-like) | 0 | umbrella_tree_membrane_0..7 (#26DAAB → #2655DA, α≈180) | COLOR 0–7 |
| umbrella_tree_cluster | Umbrella Tree Cluster | MODEL, glow | 15 | umbrella_tree_cluster #D179F5 (+ _empty) | |
| jellyshroom_log/_bark | Jellyshroom Log/Bark | COL | 0 | jellyshroom_log_top, _log_side #0D4D5E | |
| jellyshroom_cap_purple | Purple Jellyshroom Cap | TRANS, tinted | 0 | jellyshroom_cap (grey, α 170) | COLOR 0–7 gradient |
| lucernia_log/_bark | Lucernia Log/Bark | COL | 0 | lucernia_log_top #A84C45, _log_side(_2) | |
| lucernia_leaves | Lucernia Leaves | LEAF | 0 | lucernia_leaves_1..4 #B84B3C | |
| lucernia_outer_leaves | Lucernia Outer Leaves | FUR | 0 | lucernia_outer_leaves_1..3 | |
| filalux | Filalux | VINE3, glow | 15 | filalux_bottom, filalux_middle | |
| amaranita_stem / _hyphae | Amaranita Stem / Hyphae | COL / CUBE | 0 | amaranita_hyphae_top/_side #94D7C4 | |
| amaranita_cap | Amaranita Cap | CUBE | 0 | amaranita_cap #D83937 | |
| amaranita_hymenophore | Amaranita Hymenophore | CUBE (cutout overlay) | 0 | amaranita_hymenophore, _fur | |
| amaranita_lantern | Amaranita Lantern | CUBE, glow | 15 | amaranita_lantern | |
| amaranita_fur | Amaranita Fur | FUR, glow | 15 | amaranita_fur | |
| large_amaranita_mushroom | Large Amaranita Mushroom | MODEL, 3 tall | top 15 | large_amaranita_cap_top/_side/_bottom, amaranita_stem_top/_middle/_roots | |
| end_lotus_stem / leaf / flower | End Lotus … | MODEL | flower 15 | end_lotus_stem/_roots/_leaf_cutout; end_lotus_leaf_center/_side/_corner; end_lotus_petal(_2) #F35DD2, end_lotus_center(_2) | flower is glow |
| neon_cactus | Neon Cactus | MODEL (3 sizes), glow | 15 | neon_cactus_{small,medium,big}_side(_dust/_moss + _overlay), _top | |

### 5d. Plants (all CROSS unless noted)
Lit plants:
- umbrella_moss (11; tall 12; umbrella_moss_small/_end/_up/_sporophyte/_bottom)
- twisted_umbrella_moss (11; tall 12; twisted_umbrella_moss_*)
- creeping_moss (11; creeping_moss_leaves, _spores)
- bolux_mushroom (10)
- purple_polypore and aurant_polypore (WALL, 13)
- bulb_moss (WALL, 12; bulb_moss_leaves, _bulbs)
- dense_vine (VINE3, 15)
- bulb_vine (VINE3, 15; bulb_vine.png 16×32, anim 2 frames, frametime 32, interpolated; bulb_vine_middle, _roots, _fur)
- lumecorn (MODEL column; light parts 15; lumecorn_big_petal 32×32, lumecorn_light_*, _stem, _roots, _leaf_1/2)
- glowing_pillar_luminophor (15) and glowing_pillar_leaves (FUR 15) with glowing_pillar_roots
- blue_vine_lantern (15) and blue_vine_fur (FUR 15) with blue_vine (VINE, blue_vine, blue_vine_roots)
- end_lily (underwater, top 13; end_lily_flower(_small), _leaf(_small), _stem, _roots)
- pond_anemone (underwater, 13)
- flammalix (12)
- hydralux (underwater VINE + flower; glow parts 15; hydralux_*)
- cave_pumpkin (MODEL 10/15)
- cave_bush (LEAF bush; cave_bush_leaves_1..4)

Unlit plants:
- chorus_grass_01..04, cave_grass_1/2, crystal_grass_2..5, shadow_plant, bushy_grass_1/2, amber_grass, jungle_grass(_2)
- blooming_cooksonia (glow material), salteago, vaiolush_fern, fracturn (glow material)
- clawfern, globulagus (glow material), orango, aeridium (glow material), lutebus, lamellarium, inflexia, needlegrass_01..04, murkweed, small_amaranita_mushroom
- small_jellyshroom (MODEL floor/wall/ceiling: jellyshroom_cap_small, small_jellyshroom_stem, jellyshroom_wall_stem_1..3, jellyshroom_ceiling_stem)
- shadow_berry (+ _leaves), blossom_berry_seed (blossom_berry_seed_0..2, _flower, _fruit, _stem), amber_root_seed (amber_root_0..3), chorus_mushroom_seed (chorus_mushroom_0..3)
- lanceleaf (MODEL column: lanceleaf_stem(_top), lanceleaf_leaf_bottom/_middle/_pre_top/_top)
- charnia_red/purple/orange/light_blue/cyan/green (underwater), bubble_coral (underwater), menger_sponge (CUBE cutout), tube_worm_1..3 (underwater WALL)
- tail_moss, cyan_moss, twisted_moss(_2), jungle_fern (WALL, multi-part), ruscus (WALL)
- twisted_vine, jungle_vine, rubinea, magnula (VINE3: <id>, <id>_bottom; twisted also _roots)
- silk_moth_nest (MODEL: silk_moth_nest_top/_side/_front/_connect)

Most plants carry Canvas `waving_floor` or `waving_wall` materials (wind sway). The 6 charnia and the 4 berry/root crops are plain.

Animated textures: crystal_moss_cover 16×64 (4 frames, t32, interpolated) and bulb_vine 16×32 (2 frames, t32, interpolated). Furnace `_glow` textures are not needed.

---------------------------------------------------------------------------------------------------

## 6. Map to DokuQuest (`src/gen/theend.cpp` lands, `sky.cpp` `FOG[16]`)

| our land | Better End biome | match | our FOG[] vs BE fog | notes |
|---|---|---|---|---|
| 0 chorus forest | chorus_forest | yes | 571A57 = 571A57 | needs chorus_nylium turf, pythadendron, violecite layer; density 1.5 |
| 1 spire fields | blossoming_spires | close | F192E5 = F192E5 | spires should be stacked-sphere end_stone skinned in pink_moss with tenanea; no caves |
| 2 crater barrens | — (closest: dust_wastelands) | no | C8C4A0 vs E2EFA8 | BE has no craters; dust wastelands = h 0.75 dunes of endstone_dust, white_ash, density 2 |
| 3 glowing grasslands | glowing_grasslands | yes | 63E4F7 = 63E4F7 | end_moss turf, lumecorn and glowing ferns, fireflies, density 1.3 |
| 4 crystal mountains | crystal_mountains | yes | A080A0 = A080A0 | our +18..30 lift → BE mountain pieces R 50–100, aurora_crystal spikes, crystal_moss; density 1.0 |
| 5 sulphur springs | sulphur_springs | yes | CFC23E = CFC23E | flat (terrainHeight 0); 4-band surface; geysers/vents/brimstone lakes; magma_block is not BE |
| 6 umbrella jungle | umbrella_jungle | yes | 57DFDD = 57DFDD | jungle_moss, umbrella trees and jellyshrooms, density 2.3 |
| 7 shadow woods | shadow_forest | yes | **1A1024 vs 000000** | BE fog is pure black at density 2.5; dragon trees; shadow_grass |
| 8 ice starfield | ice_starfield | partial | E0F5FE = E0F5FE | in BE this is a VOID biome: no ground, floating ice stars at y 32–128 |
| 9 amber hills | amber_land | partial | FFB847 = FFB847 | BE is not hilly (terrainHeight 0.1); amber_moss turf + dense amber_ore; helix trees |
| 10 megalake | megalake (+ megalake_grove) | yes | B2D1F8 = B2D1F8 | lakes R 32–64 carved by structure, every 4 chunks; lotus; grove sub-biome has lacugroves |
| 11 lantern woods | lantern_woods | yes | BD5246 = BD5246 | rutiscus turf, lucernia trees with filalux, flamaea; density 1.1 |
| 12 painted mountains | painted_mountains | partial | E2EFA8 = E2EFA8 | BE strata are end_stone / flavolite / violecite (not terracotta); sub-biome of dust_wastelands |
| 13 neon oasis | neon_oasis | partial | **B6FF6A vs E2EFA8** | BE fog is the dust-wastelands haze; neon cactus (light 15) + desert lakes on endstone_dust / end_moss |
| 14 dragon graveyard | dragon_graveyards | yes | F42E4F = F42E4F | sangnum turf, obsidian pillars/boulders, dragon_bone_block ore, amaranita; BE genChance 0.1 (rare) |
| 15 dry shrubland | dry_shrubland | yes | 84230D = 84230D | rutiscus turf, orango / aeridium / lutebus / lamellarium, lucernia bushes |
| — | foggy_mushroomland | **missing** | 297AAD, density 3.0 | BE's signature biome (mossy glowshroom + giant glowshroom structure) |
| — | dust_wastelands, umbra_valley | **missing** | E2EFA8 2.0 / 646464 1.0 | umbra: umbralith + pallidium covers, arches |
| — | 6 cave biomes | **missing** | 2d | inside islands only |

Our stand-ins (`gendefs.h` / `blocks.json`): end_moss (end_stone sides), chorus_turf, glow_turf (L9), umbrella_turf (L2), lantern_turf (L6), crystal_turf (L5), neon_moss (L7), shadow_soil, amber_block (L4), glowshroom_stem / glowshroom_cap (L12), umbrella_cap (L3), blossom_cap (L3), dragon_log / dragon_leaves, lantern_log, null_stone. All are single-texture placeholders. BE turf blocks have **no** light; light in BE comes from plants, fur and hymenophores.

Prioritised gaps:
1. **Fog model** (small change, large visual effect). Use per-biome `fogDensity` (trilinear over a 32-block grid): fogStart = 0.25·vd/d and fogEnd = vd/d. Today `sky.cpp` uses start 0.09·far with end fixed at far−6. Set the fog and clear colour to biomeFog·0.6·(1−h) and the zenith to the same fog colour (BE has no zenith gradient; layers sit on the clear colour). Fix FOG[7] to 000000 and FOG[13] to E2EFA8; land 2 has no BE source.
2. **BE sky layers** (section 1). Add a geometry pass, or analytic billboards in `sky.glsl`: horizon nebula band, ~25 nebula sprites, ~1060 textured stars in 2 sets, ~2850 point stars in 2 sets, the fog band with alpha = density−1, and the 4 rotation axes. This needs the 5 sky textures, 1.3 MB of PNG in total.
3. **Terrain blocks**: 13 TURF blocks (top/side textures), the stones (flavolite, violecite, sulphuric_rock, brimstone, umbralith, endstone_dust, jadestones), the pallidium covers and amber_ore. These replace the placeholders; the textures exist in the jar.
4. **Per-land surface rules** from 2b. Add the split, sulphuric, umbra and vertical-band noises and the flavolite/violecite OreLayer discs, which show in cliffs.
5. **Trees**: mossy glowshroom (needs foggy_mushroomland), pythadendron, dragon tree, lucernia, umbrella tree, jellyshroom, tenanea, helix, lacugrove, amaranita. All are SDF capsule and sphere shapes, so one small SDF/spline helper (section 4 preamble) covers all of them.
6. **Structures and terrain features**: crystal mountain pieces + aurora crystals, painted-mountain strata, megalake carving, end lakes, geysers/vents/brimstone lakes, spires, ice stars.
7. **Missing biomes**: foggy_mushroomland, dust_wastelands (dunes via h = 0.75), umbra_valley, megalake_grove; then the cave biomes.
8. **Plants** (about 60 cross/wall/vine textures with light levels) and per-land particles (our env_layers motes roughly match; BE particle textures are in `textures/particle`).
9. Optional: replace our 2D island slabs with BE's 3-layer SDF islands (section 3). Land choice by hex map with genChance weights instead of the noise thresholds in `endBiomeOf`. Music per land.

---------------------------------------------------------------------------------------------------

## 7. Licence
- `fabric.mod.json`: `"license": "MIT (CC BY-NC-SA 4.0 for some Assets)"`. Authors are Quiqueck (code), paulevs (code & art), Bulldog83 (code & art), Edos (building), Yuki (art), Seaward (art) and Firel (music). Sources: github.com/quiqueck/BetterEnd.
- There is **no LICENSE file in the jar**: META-INF only has MANIFEST.MF. The jar does not say which assets are CC BY-NC-SA.
- Upstream repo (fetched now from the default branch, which may differ from the 3.2.5 tag) has `LICENSE` and `LICENSE.ASSETS`. `LICENSE.ASSETS` says CC BY-NC-SA 4.0 (attribution "Team BetterX") applies to these files only:
  - `assets/betterend/textures/gui/infusion.png`
  - `assets/betterend/icon_updater.png`
  - `assets/betterend/lang/de_de.json`
- Read that way, block, sky and particle textures, models and worldgen JSON fall under MIT. Before shipping, re-check `LICENSE.ASSETS` at the 3.2.5 tag.
- Music (`sounds/music/*`) is credited to third-party artists in `sounds.json` (Dan Henig, Jesse Gallagher, Emmit Fenn, Godmode, Asher Fulero and others). Don't assume it is MIT; leave it out.
