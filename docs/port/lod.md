# Distant terrain, skies, clouds, weather, particles

Workstream `ws/lod`. Ported from DokuQuest 0.9.0.90: `game/distant.js`, `game/skies.js`, `render/clouds.js`,
`game/weather.js`, `game/particles2.js`, `game/ambientfx.js`, plus the parts they build on: `game.js`
computeEnv/burst/flame, `seamless.js` / `theend.js` / `riftarc.js` skies, `gl.js` skyFS, `renderer.js`
sun/moon billboards and buildParticles, `entities.js` Particles, `vehicles.js` Wind, the emitter half of `blockfx.js`.

## Files
| File | What |
|---|---|
| `src/sim/weather.{h,cpp}` | `WeatherSystem` (LevelSystem, runs on the server too): weather.js state machine + skies.js fronts/trails, zones, showers, sandstorms, blizzards, ash, mist, sea fog, rainbow timer, lightning (high points, rods, glass, charred trees, fires), puddles/snow layers/ice + thaw, the Wind, far bolts. Saved in `LevelInfo.data["weather"]`. |
| `src/render/sky.{h,cpp}`, `assets/shaders/sky.glsl` | Replaces the old sky. `computeSkyEnv()` = the computeEnv chain (per land, weather greying, pre-storm grey, storm bank, aurora, rainbow, mist/sand/ash, cloud bank between bands, underwater). Sun + moon with 8 phases (day % 8) drawn in the sky shader as the original pixel-art billboards. Fills `FrameCtx`. |
| `src/render/clouds.{h,cpp}`, `assets/shaders/clouds.glsl` | Cube-World clouds: the 10 greedy-merged heap templates + 4 wisp templates (same seeds as JS), cloudsAround with the weather field, storm darkening, thunderheads, lightning glow, sunset tint. Instanced: one draw per template. |
| `src/render/distant.{h,cpp}`, `distant_build.cpp`, `assets/shaders/distant.glsl` | Distant terrain (below). |
| `src/render/particles.{h,cpp}`, `assets/shaders/particles.glsl` | Particle engine. |
| `src/render/env_layers.{h,cpp}` | The render layers (distant, clouds, weather fx, particles), rain/snow drops, sand/ash, wind strokes, gust sand, fireflies, embers, silt, falling/tumbling leaves, block emitters (torch flames + sparks + smoke, fire, lava pops, lit furnaces, candles, end rods, crying obsidian, spore blossoms, portal motes, redstone sparks, campfire smoke), drips (water/lava under liquids, rain off leaves), lightning bolts. |

`registerEnvironmentLayers()` is called once in `App::run` (src/client/app.cpp, after `reloadTiles()`).
`registerWeatherSystem()` is called from `initCore()` (src/core/init.cpp).

## Distant terrain
- Quadtree tiles of 32x32 columns; level L has columns of `2 << L` blocks: 2, 4, 8 ... 512 (tiles 64 .. 16384 blocks),
  top level chosen so ~5x5 top tiles cover the range. Selection is distSelect: split a tile when the camera is within
  1.25 tile sizes of its centre and all 4 children are ready (never a hole), coarse ones requested first.
- Range = settings.farTerrain (up to 16384), at least render distance + 160; the original's adaptive scale (draws
  closer when fps < 40 for 5 s, back out after 12 s at ≥55) is kept.
- Built on the ThreadPool (≤ min(3, threads) tiles in flight, priority after near chunk generation) from
  `DimGenerator::sample()` and from `ChunkSummary`s (8x8 cells of 2x2 columns: ground, canopy top/bottom, water depth,
  floating bottom, colours) of: chunks loaded this session (summarized on the main thread, 6/frame, re-summarized when a
  hash of the height map + top blocks changes) and chunks stored in the save (`RegionStore::listStored` + decode on
  workers in batches of 48, once per world). Summaries capped at 12000 (~13 MB). Changes mark the tiles above dirty;
  dirty tiles keep drawing until rebuilt.
- Look: columns with merged tops (2D greedy, same height+colour, colours quantised to 4), walls merged along each edge,
  wall feet shaded darker, skirts at tile edges (4 + 0.75·cell) so tiles of different levels never show cracks,
  undersides of floating land (Skylands / End islands), tree crowns with trunks at L0/L1, forests blended into the
  ground from L2 (a tree cell with ≥2 tree neighbours), water lighter in shallows / deep blue far out, lava glows,
  per-cell colour variety, the storm's shadow and rain haze over the far land, fog at the far edge.
  Colours = `TileSet::avgColor` of the block's top/side tile (resource-pack aware: rebuilt when the pack changes)
  × climate tint (`climateTint(kind, temp, humid)` with the column's climate bytes, as the chunk mesher tints).
- Cut: an R8 mask of chunks drawn for real (loaded > 0.6 s) round the camera discards LOD fragments there; tiles
  fully covered by real chunks are skipped; LOD is dropped 0.6 blocks so real chunks win at the seam.
- Stacked world: one quadtree per band. Overworld camera → Overworld + Skylands LOD (islands' undersides overhead);
  Skylands camera → Skylands + the Overworld far below. End → End islands. Nether: none (roofed; fog, like the original).
- Vertex 12 bytes (u16 x,z in tile/32768, i16 y in 1/8 block, rgb+shade, flags), shared quad index buffer, one draw per tile.
- Memory budget: `24 MB + farTerrain/16384 × 200 MB` of tile meshes (LRU of tiles not drawn for 2 frames).

### Measured (this machine: 2 shared cores, Mesa llvmpipe; first rows with the real generator, the rest with the debug one)
| Setting | Tiles built | Avg / max build | Tiles held | GPU mesh memory |
|---|---|---|---|---|
| real generator: farTerrain 2048, rd 8, at spawn (incl. trees() for near tiles) | 174 | 3.1 ms / 88 ms | 164 | 16.3 MB |
| real generator: farTerrain 2048, camera y 690 (both bands) | 245 | 1.7 ms / 40 ms | 238 | 23.9 MB |
| farTerrain 2048, rd 8, camera at sea level | 171–174 | 0.7–1.5 ms / 21–36 ms | ~160 | 10.6–11.8 MB |
| farTerrain 2048 + Skylands band (camera y 640) | 232–234 | 1.6 ms / 61–67 ms | 222 | 16.4 MB |
| End islands, farTerrain 2048 | 125 | 2.7 ms / 72 ms | 112 | 4.7 MB |
| potato: farTerrain 512, rd 4, Legacy, both bands (y 640) | 107 | 0.56 ms / 7.8 ms | 107 | 6.5 MB |

Max times include waiting on the 2 shared cores. The real generator's `sample()` will dominate build time
(1156 samples per tile); at ~10 µs a sample a tile is ~12 ms on one worker.

## Sky
Per land (game.js / theend.js / riftarc.js): Overworld day cycle with sunrise/sunset glow and stars; Ember Depths haze;
Skylands always day (as the original); The End purple dark; pocket realm; his realm. Weather greying, the storm's clouds
greying the light before the rain, a storm bank on the horizon (front coming / trail leaving), northern lights over cold
land on clear nights, a rainbow after rain, mist/sea fog/sandstorm/blizzard/ash fog, lightning flash. Crossing bands
blends the two skies halfway, with the cloud bank (fog closes in to 14 blocks) between Overworld and Skylands; the
Nether/Overworld/End edges blend over 16 blocks. Fog range: `viewFar·fogStart .. viewFar−6`, viewFar = max(render
distance, LOD range) — the far fog sits at the LOD's edge when LOD is on.

`FrameCtx` additions (render/frame.h): `horizonColor, minLight, rain, thunder, flash, mist, sea, cloudBank, windDir,
underwater, sky` (`std::shared_ptr<const SkyEnv>`: each view's full sky, so portal views get their own; `skyOf(f)`); `wind` is now `0.45 + windStrength·0.9` (plants sway more in storms).

## Weather (sim)
State machine and timings exactly as weather.js + skies.js: clear 900–2100 s → a front arrives from upwind (edge
1650 blocks, 7 blocks/s; 30 % thunder), rain/thunder 300–660 s, the trailing edge clears over the last 80 s. Rule
`weatherCycle`. Precipitation per 8x8 patch (4 s cache): none in deserts / dry warm land, snow when cold or high,
showers only in jungle/swamp. Biome, climate and the original height come from the generator (`sample()`,
`terrain()`), the current ground height from the loaded chunk.
Sounds via `Level::fx`: `thunder`, `thunder_far` (delayed by distance), `gust`, `fire_crackle`, `lava_pop`.
`rainLevel`/`rainOpen` are exposed for the audio engine's rain hiss. Events: `lightning [x,y,z,rod]`,
`lightningRod [x,y,z]` (for redstone's pulse).

API: `WeatherSystem* weatherOf(Level&)`; `set(kind, secs)`, `startFront(kind)` (for `/weather` and
`/weather front`), `stormAt(x,z)`, `precipAt(world,x,y,z)`, `topAt`, `strike(level,x,y,z)`,
`blockTick(level, world, x,y,z,id,meta)` (puddles dry, storm snow melts — call it from the random-tick system).

## Particles
`ParticleSystem& particles()` (render/particles.h); `particles().spawn(kind, pos, count, extra)` — same signature as
`LevelFx::particles`, so the client LevelFx can forward to it. Low level: `add(Particle)` with all the original fields
(grav, rise/pool under ceilings, wind drift, grow/shrink, flutter, blink, hang, drip landing, dry, streak, vert, flat,
tint, light). `void addWaterfallSplash(vec3)` for the meshing engine's waterfall bases.
Kinds: `block`/`break`/`crumbs` {block, speed, grav}, `dust` {block}, `burst` {tile, speed, grav}, `flame`, `smoke`,
`smoke_dark`/`fire_smoke`, `torch_smoke`, `campfire_smoke`, `explosion` {r}, `splash` {big}, `rain_splash`, `poof`
{w,h}, `crit`, `slime`/`magma_slime` {size}, `drip_water`/`drip_lava`/`drip_honey`/`drip_leaf`/`drip_tear`, `leaf`
{tile, speed, gust}, `torch_flame`/`soul_flame`/`held_flame`, `ember`/`spark`, `firefly`; anything else = a burst of
that tile or block (like `game.burst(x,y,z,name,n,speed)`), e.g. `spawn("white", p, 10, {{"speed",1.4}})`.
Caps: 1800 particles + rain at setting 2 (700 at 1), off at 0. One CPU-built quad batch, one draw, alpha-tested.

## Differences from the original
- Tiles are built on worker threads instead of a few ms a frame on the main thread; levels go to 512-block columns
  (16384-block range) instead of 16; tops merged in 2D and walls merged along edges (fewer quads).
- LOD per stacked band (Skylands overhead / Overworld below); floating undersides.
- Trees: near tiles (L0/L1) take the generator's real trees (`DimGenerator::trees()`, the port of treesForChunk +
  treeParts) with their crowns and trunks; far tiles blend the forest cover the generator gives in
  `ColumnSample.tree/treeTop/leaves` (distant.js). Villages from `villageAt` are not drawn yet (no C++ villages).
- Weather zones use the generator's biome, climate bytes, river flag and the original's height
  (`TerrainQuery::terrainColumn().jsY`), so the original's rules (snow above y 110, cold above 104, swamp below
  51...) apply unchanged; heights are mapped with `vmap::owSurface`. Other world types fall back to a guess from
  the surface.
- Clouds: one layer at global y 724 (Overworld band-local 212: above the highest peaks, under the cloud bank); reach grows to 1400 blocks with far LOD on the Modern
  renderer; drawn in the opaque layer pass (the camera's far plane already reaches past them) instead of a separate
  far pass.
- Moon phases are new (the original had one moon picture).
- Lightning bolts are drawn as glowing particle strokes; entity damage from lightning is applied directly
  (hp −5, `data.burn`), the redstone pulse is an event.

## Left to do
- Held/carried torch flames (`ambientfx` handFlames/carriedFlames) need the hand/held-item renderer: use
  `spawn("held_flame")` / `spawn("torch_smoke")` at the tip. Foot dust (`spawn("dust", {block})`), splash on entering
  water (`spawn("splash", {big})`), death poofs, crits, slime splats are ready for the gameplay/creature areas to call.
- The client `LevelFx` should forward `particles()` to `particles().spawn` (whoever owns the client LevelFx).
- Split-screen per-player mist (`localFor`) and rain from several local players; multiplayer replication of the
  weather state (send `toJson()`; set `hostRules = false` on guests).
- Campfire smoke is a simple column (camping.js signal fires/hearth chimneys not ported).
- Chunk shader could use `FrameCtx.rain` for the wet look and `FrameCtx.sea` for swell (meshing/water area).
- Depth precision with farZ = farTerrain+512 and near 0.05: fine to ~4096 here; reversed-Z (GL 4.5 `glClipControl`)
  would help the Modern renderer at 8192+.
- Testing aid: `DQ_ENV_TEST="x y z yaw pitch [dayTime] [clear|rain|thunder|front]"` holds the player there (and turns
  off the adaptive range); the debug overlay (F3) shows LOD/particle/weather stats.
