# Weather and Atmosphere Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Particle-Rain-quality rain, snow, storms, sandstorms, ground fog and height fog on all three renderers, with weather that is the same for every player in multiplayer.

**Architecture:** A pure-logic weather particle simulation (`render/weather_sim.*`, no GL, unit-tested) fed by a deterministic wind field (`sim/wind_field.*`), drawn by an instanced renderer (`render/weather_draw.cpp` + `weather.glsl`) from a sprite sheet built from the resource pack (`render/weather_sprites.cpp`), owned by a new `WeatherLayer` that replaces the rain/snow/dust half of `WeatherFxLayer`. Height fog is a closed-form term in `common.glsl applyFog`. The host sends a compact weather state that guests follow.

**Tech Stack:** C++20, OpenGL 3.3/4.6 (+ Vulkan translation layer), GLSL 330 subset, nlohmann::json, Python 3 + numpy + ffmpeg (sound profiles only).

**Spec:** `docs/superpowers/specs/2026-11-10-weather-atmosphere-design.md`

## Global Constraints

- One feature per file, files < ~1500 lines; new `.cpp` files are picked up by CMake globs (only new executables need CMake edits).
- Never rely on static constructors in other .cpp files; register through `registerEnvironmentLayers()` (render/env_layers.cpp) / `initCore()`.
- Shaders: one file both stages (`#ifdef VERTEX` / `#ifdef FRAGMENT`), GLSL 330 subset, `#include "common.glsl"`; must compile on Legacy, Modern and Vulkan.
- No audio files shipped: sounds are profiles in `data/sounds.json` / `data/sounds.bin`.
- Budget: weather adds ≤ 0.5 ms/frame at "Normal" (1500 particles, distance 16) on Modern GL, Vulkan and Legacy (`DQ_FRAME_STATS=1`).
- Particle Rain numbers used verbatim: particle amount 1500, storm density ×1.5, sky distance 16, surface range 100, wind strength 0.03, gust frequency 0.15, modulation speed 0.1, height ramp start 50 over 40 blocks (band-local), tint mix 0.6, ripple 8 frames / lifetime 7 ticks, fog lifetime 250 ticks, fog size 2, fog distance scaling 0.1, after-rain fog up to 6000 ticks (5 min). 1 tick = 1/20 s.
- Options (game Options → Video → Weather, and launcher): Weather Particles Off/Low/Normal/High = 0/600/1500/3000 (default Normal); Weather Distance 12–32 (default 16); Splashes & Ripples (on); Ground Fog (on); Height Fog (on). Presets: potato Low/12/off/off/on, low Low/14, medium Normal/16, high High/20, ultra High/24.
- Particle Rain's MIT notice goes into `docs/CREDITS.md`.
- No git repository: "commit" steps are replaced by "smoke test + note in docs/CHANGELOG.md" checkpoints.

## Review Focus

1. Wind-blown rain or blizzard snow next to a wall or under an overhang must not appear indoors → collision test in Task 4.
2. Moving fast (sprinting, falling, flying, elytra) must not leave the camera in a weather "hole" → spawn-distribution test in Task 4 (fast mode uses the whole sphere).
3. Standing in a cave or house during a storm: no sky particles inside, rain sound muffled not silent → Task 4 roof rejection test, Task 9 indoor-rain check.
4. Changing dimension band (falling from the Aether into the Overworld, portals) must clear weather particles of the old band → Task 4 test `clearsOnBandChange`.
5. A guest joining mid-storm must see the storm, not clear sky → Task 1 test (guest joins after `/weather thunder`).

---

### Task 1: Multiplayer weather sync

**Files:**
- Modify: `src/sim/weather.h` (declare), `src/sim/weather.cpp` (implement; `tick` guest branch)
- Modify: `src/net/server_sync.cpp:227` (send `netState()` instead of the saved JSON)
- Modify: `src/net/client.cpp` `NetClient::applyEnv` (call `applyNet`)
- Test: `tools/test_net.cpp`

**Interfaces:**
- Produces: `json WeatherSystem::netState() const;` `void WeatherSystem::applyNet(const json& j);` Keys: `k` kind, `l` left, `f`/`tr` fronts (`on,kind,ux,uz,edge`), `r` rain, `th` thunderK, `s` sand, `b` bliz, `m` mistNow, `wd` windDir, `ws` windStrength, `g` gust, `cx`/`cz` cloud offsets, `sk` showerK, `z` zone, `a` anchor `[x,z]`.

- [ ] **Step 1: Write the failing test** — in `tools/test_net.cpp` after both clients have joined: run server console `weather thunder 600`, step the loop ~3 s, then a second client joins; assert for both clients `weatherOf(*c.lv)->stormKind() == "thunder"`, `!weatherOf(*c.lv)->hostRules`, and `std::abs(guest.windDir - host.windDir) < 0.05f`. Name the checks `"guest weather follows the host"` and `"late joiner sees the storm"`.
- [ ] **Step 2: Run** `ninja -C build test_net && ./build/test_net` — expected FAIL on both checks (guests run their own cycle).
- [ ] **Step 3: Implement** `netState`/`applyNet` (applyNet sets `hostRules=false` and copies values; floats the client smooths — `rain`, `thunderK`, `sand`, `bliz`, `mistNow` — are stored as targets and eased in `tick`). In `tick`, when `!hostRules`: skip the storm cycle, front creation/ending and lightning strikes (bolts still arrive as fx), but still run `localWeather` smoothing, `updateWind` only advancing `cloudX/Z` from the host's wind. Host sends `netState()` in `sendEnv`; guest `applyEnv` calls `if (auto* W = weatherOf(*lv)) W->applyNet(m["w"])` when `m["w"]` is an object.
- [ ] **Step 4: Run** `./build/test_net` — expected all PASS, including the existing checks.
- [ ] **Step 5: Checkpoint** — `./build/dq-test-gameplay` passes; add a CHANGELOG line.

### Task 2: Wind field

**Files:**
- Create: `src/sim/wind_field.h`, `src/sim/wind_field.cpp`
- Modify: `src/client/client_fx.cpp:137` area (set `AudioScene.gust`)
- Test: `tools/test_gameplay.cpp` (new section "wind field")

**Interfaces:**
- Consumes: `WeatherSystem::windDir, windStrength, gust`, `LevelInfo::time`.
- Produces: `struct WindParams { float dir, strength, gust; double t; };` `WindParams windParams(const Level& lv);` `vec3 windAt(const WindParams& w, double x, double y, double z);` (y = global y; band-local ramp inside) `float gustAt(const WindParams& w, double x, double z);` (0..1).

- [ ] **Step 1: Write the failing tests** — `windAt` with the same params and position returns bit-identical results twice; at band-local y 40 the result is `vec3(0)`; at band-local y 100 its length is > 0 and ≤ `0.03f * 2 * (0.5f + strength)`; its mean over a 64×64 grid of x,z points has a direction within 30° of `dir`; `y` component is always 0; `gustAt` stays in [0,1].
- [ ] **Step 2: Run** `ninja -C build dq-test-gameplay && ./build/dq-test-gameplay` — FAIL (no such function).
- [ ] **Step 3: Implement.** Particle Rain's field per axis, then rotated so its mean points along `dir` and scaled by `(0.5 + strength)`:

```cpp
// PR: -(sin(p*f + t) * sin(p*f*0.5 + t*0.5) * max + max), f = 0.15, t = ticks * 0.1, max = 0.03
static float one(float p, float t) { return -(std::sin(p * 0.15f + t) * std::sin(p * 0.075f + t * 0.5f) * 0.03f + 0.03f); }
```
Magnitude along the wind = `-(one(x,t) + one(z,t)) / 2`, lateral wobble = `(one(x,t) - one(z,t)) / 2`; height ramp `clamp((bandY - 50) / 40, 0, 1)`; multiply by `1 + gust`. `gustAt` = normalised magnitude. In `client_fx.cpp` set the audio scene's `gust = gustAt(windParams(lv), cam.x, cam.z)` each frame.
- [ ] **Step 4: Run** `./build/dq-test-gameplay` — PASS.
- [ ] **Step 5: Checkpoint** — smoke run, CHANGELOG line.

### Task 3: Weather sprite sheet

**Files:**
- Create: `src/render/weather_sprites.h`, `src/render/weather_sprites.cpp`

**Interfaces:**
- Consumes: the resource pack loader (`applyResourcePack` / pack file access used by `assets/tileset.cpp`), `TileSet` (dead bush tile).
- Produces: `enum class WSprite : u8 { Rain0 = 0 /*..15*/, HeavyRain0 = 16 /*..19*/, Snow0 = 20 /*..35*/, HeavySnow0 = 36 /*..39*/, Ripple0 = 40 /*..47*/, Streak = 48, FogFlat = 49, FogUpright = 50, Dust0 = 51 /*..53*/, Shrub = 54, Count = 55 };` `struct WeatherSprites { GLuint tex = 0; int cell = 32; int cols = 8; vec4 uv(int sprite) const; bool fromPack = false; };` `WeatherSprites& weatherSprites();` `void buildWeatherSprites(const TileSet& ts);` (called where tiles are reloaded, `App::reloadTiles`).

- [ ] **Step 1: Implement** a 8-column RGBA8 atlas (cell 32 px, linear filtering, clamp): rain/snow slices from the pack's `environment/rain.png` / `snow.png` (4 columns × 4 rows of equal squares, slice i = row i%rows, col i/rows as in PR `getSpriteFromSheet`), heavy = alpha-max merge of slices 4 at a time (PR `mergeSpritesFromSheet`); desaturate rain to grey (PR `desaturate`, tinted later); ripples = Bresenham circle radius `(cell/2/8)*(i+1)` clamped to `[1, cell/2-1]`; streak, fog sheets (soft radial-noise blobs, flat one squashed), dust (2×2, 3×3, 4×4 soft dots) and shrub (the `dead_bush` tile) procedural/from tiles. Fallback when the pack has no rain/snow: procedural streak and 6-arm flake. Log `weather sprites: 55 (rain/snow from pack|built in)`.
- [ ] **Step 2: Verify** — smoke run logs the line with "from pack" (GeruDoku installed) and a run with `--userdir` without packs logs "built in"; no GL errors.
- [ ] **Step 3: Checkpoint.**

### Task 4: Weather particle core — rain and snow

**Files:**
- Create: `src/render/weather_sim.h`, `src/render/weather_sim.cpp` (no GL), `src/render/weather_draw.cpp`, `assets/shaders/weather.glsl`, `src/render/weather_layer.cpp`
- Modify: `src/render/env_layers.cpp` (remove rain/snow spawning from `WeatherFxLayer::drops()` and register `WeatherLayer`), `src/common/settings.h/.cpp` (fields + presets), `src/ui/options.cpp`, `src/launcher/options.cpp`
- Test: `tools/test_weather.cpp` (new executable `test_weather`, links `dqclient`), `CMakeLists.txt`

**Interfaces:**
- Consumes: `windAt`, `windParams` (Task 2), `WSprite`/`weatherSprites()` (Task 3), `WeatherSystem::precipAt`, `topAt`, `rain`, `thunderK`, `bliz`, `dens`.
- Produces:
  - `enum class WKind : u8 { Rain, HeavyRain, Snow, HeavySnow, Dust, SkitterDust, Shrub, Haze, FogFlat, FogUpright, Ripple, Splash, Streak, Steam };`
  - `enum class WRot : u8 { Velocity, Camera, Upright, Flat, CameraCopy };`
  - `struct WKindDef { float gravity, wind, stormWind, size, rotSpeed, bounce, opacity, distScale; int lifetime /*ticks, 0 = until out of range*/; WRot rot; bool blended; };` `const WKindDef& kindDef(WKind k);` with the spec's PR values (rain 0.9/0.4/0.8/0.75, heavy rain 1.0, snow 0.05/0.01/0.1/0.75 rot 0.4, heavy snow 0.1, …).
  - `class WeatherSim { public: void setBudget(int max); int count() const; void update(const WeatherInputs& in, double dt); void clear(); template<class F> void forEach(F&& f) const; Hits& hits(); };`
  - `struct WeatherInputs { World* world; WeatherSystem* W; WindParams wind; vec3 cam, camVel; int band; float dist /*sky radius*/, surface /*100*/; bool surfaceFx, groundFog; double t; };`
  - `struct WeatherHit { vec3 pos; WKind kind; int face /*0 top,1-4 sides*/; bool liquid; };` (Task 5 consumes)
  - `vec3 skySpawnPoint(Rng& r, float radius, bool fast);` (PR centre-weighted upper hemisphere; whole sphere when fast)
  - `bool sweepHit(World& w, vec3 from, vec3 to, WeatherHit& out);` (block-DDA over the segment; solid or liquid stops)

- [ ] **Step 1: Write the failing tests** in `tools/test_weather.cpp` (a flat test world like `creature_test.cpp`'s setup):
  - `spawnUpperWhenSlow`: 10 000 `skySpawnPoint(r, 16, false)` → all `y >= -1e-3` relative and all lengths ≈ 16 (±1e-3).
  - `spawnWholeSphereWhenFast`: 10 000 with `fast=true` → ≥ 30 % have `y < 0`.
  - `sweepStopsAtWall`: stone wall at x=5; `sweepHit(from (4.5,Y+2,0.5), to (5.5,Y+2,0.5))` → true, `out.face` is a side, `out.pos.x <= 5.001`.
  - `sweepHitsWaterAsLiquid`: water block → `out.liquid == true`.
  - `noRainUnderRoof`: stone roof 5×5 at Y+4 over the camera, strong wind (`windStrength 1`), thunder; run `update` 400 × 0.05 s; assert no particle position is inside the roofed box (x,z in [−2,3], y in [Y, Y+4)).
  - `budgetIsCap`: budget 600, storm for 200 steps → `count() <= 600` always.
  - `clearsOnBandChange`: particles present, then `band` changes → `count() == 0` after one update.
- [ ] **Step 2: Run** `ninja -C build test_weather && ./build/test_weather` — FAIL (missing symbols).
- [ ] **Step 3: Implement `WeatherSim`.** SoA arrays (pos, prevPos, vel, kind, sprite, age, life, size, roll, alpha, tint, flags); spawn per update like PR `tickSkyFX`: `n = budget/25` per tick while active (×lerp to 1.5 with thunder, ×(1 + 2·camSpeed)), only for points above `topAt` and in loaded chunks, kind from `precipAt` at the column top (heavy kinds when `thunderK > 0` / `bliz > 0` with PR densities), rain/snow tint `mix(waterColour, fogColour, 0.6)` (water colour from the tile set's water tint at the column, fog colour from inputs), initial velocity `−gravity·20 m/s` + `windAt × stormWind-or-wind × 10` then `+windAt × wind` per tick; integrate with `sweepHit` against the previous position each step; on hit record a `WeatherHit` and start the impact shrink (PR `tickCollisionAnim`: stretch → 0 over speed); remove beyond `dist + 0.5`; alpha fade over the outer third of `dist`; clear all when `band` changes. Remove rain/snow from `WeatherFxLayer::drops()` (keep lightning, gust pickup, ash).
- [ ] **Step 4: Run** `./build/test_weather` — PASS.
- [ ] **Step 5: Implement drawing.** `weather.glsl`: per-instance attributes `iPos(vec3, camera-relative)`, `iVel(vec3, relative to camera velocity)`, `iSize`, `iRoll`, `iSprite`, `iTint(rgba)`; the vertex shader builds the quad for `WRot` (Velocity: PR `renderVelocityQuad` — axis = velocity, stretch `clamp(|v|², 0.25, 1)` × impact progress, face the camera around that axis; Camera; Upright: yaw to camera; Flat: horizontal; CameraCopy: camera rotation), applies `applyFog` and sky/block light from `lighting()` with a light byte per instance; fragment samples the weather atlas. `weather_draw.cpp`: one `GL_STREAM_DRAW` instance VBO, `glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n)` twice — blended group (premultiplied alpha, depth test on, depth write off, sorted far→near only for fog/haze kinds) and the opaque skitter-dust group. `WeatherLayer` (order 15) updates in `update`, draws in `drawTranslucent`, shows `weather fx: N` in the F3 overlay.
- [ ] **Step 6: Settings and options.** `Settings` fields `int weatherParticles = 1500; int weatherDist = 16; bool weatherSurfaceFx = true; bool groundFog = true; bool heightFog = true;` added to `DQ_SETTINGS_FIELDS`, clamped in `fromJson`, preset values from Global Constraints in `applyPreset`; rows in both options screens (Weather Particles cycle Off/Low/Normal/High, Weather Distance range 12–32, three toggles). Particles = Minimal forces Low at runtime.
- [ ] **Step 7: Verify visually** — `DQ_ENV_TEST="… rain"`, `"… thunder"`, a snow zone and a blizzard (cold zone + thunder): screenshots on Modern, Legacy, Vulkan; rain stretched and translucent, snow rotating, heavy variants in thunder, nothing inside the test house. `DQ_FRAME_STATS=1` at Normal: ≤ 0.5 ms over weather Off.
- [ ] **Step 8: Checkpoint.**

### Task 5: Surface effects — splashes, ripples, streaks, steam

**Files:**
- Modify: `src/render/weather_sim.cpp` (surface spawner + hit handling)
- Test: `tools/test_weather.cpp`

**Interfaces:**
- Consumes: `WeatherHit`, `WKind::{Ripple, Splash, Streak, Steam}`, `WeatherInputs::surfaceFx`.
- Produces: nothing new for later tasks.

- [ ] **Step 1: Write the failing tests:** `rippleOnWater` — rain over a water pool for 200 steps → at least one `Ripple` alive with `WRot::Flat`, all ripples' y within 0.02 of the water surface; `streakOnStone` — rain beside a stone wall with `surfaceFx` → at least one `Streak` with its x within 0.02 of the wall face; `noSurfaceFxWhenOff` — `surfaceFx=false` → zero Ripple/Splash/Streak/Steam.
- [ ] **Step 2: Run** `./build/test_weather` — FAIL.
- [ ] **Step 3: Implement** PR's block-FX rules: per tick ~`budget/200` random columns within `dist` under open sky while raining; water top → Ripple (8 frames over 7 ticks, size 0.25, fade out); solid top → Splash (small, 0.3 s); side of stone/glass/obsidian/impermeable → Streak (slides down at 0.1 blocks/tick with random gravity bursts, drips a `rain_splash` particle when it reaches the bottom edge, fades); lava or fire top → Steam (rising, 1 s). Raindrop hits add a Ripple/Splash at the hit point with 25 % chance.
- [ ] **Step 4: Run** `./build/test_weather` — PASS; screenshot of rain on a pond and a stone wall.
- [ ] **Step 5: Checkpoint.**

### Task 6: Sandstorms and dust

**Files:**
- Modify: `src/render/weather_sim.cpp`, `src/render/env_layers.cpp` (remove the old `extra()` sand particles; keep Nether ash)
- Test: `tools/test_weather.cpp`

**Interfaces:**
- Consumes: `mapColour` from `render/map_colors.h` (ground colour), `WeatherSystem::sand`, `zone`, `windStrength`.
- Produces: nothing new.

- [ ] **Step 1: Write the failing tests:** `dustTakesGroundColour` — desert test world of red sand, `sand = 1` → Dust particles exist and each tint is within 25 % of the red-sand map colour; `noDustWhenCalm` — `sand = 0`, `windStrength = 0.2` → zero Dust; `shrubsAreRare` — 2000 steps of full storm → Shrub count ≤ 2 % of Dust.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement** PR's dust set, gated to dry columns (`precipAt == 0`, ground sand/red sand/terracotta/sandstone/coarse dirt, Overworld band): Dust (sky spawn, size 0.25, gravity 0.1, wind 0.7/1.0, tint = map colour × 0.8–1.0), SkitterDust (block tops, size 0.2, bounce 0.15, lifetime 60 ticks, opaque), Shrub (density 0.01, size 1, bounce 0.15, gravity 0.5, tumbling roll), Haze (surface, lifetime 100 ticks, opacity 0.4, size 0.4, distance scaling 1, CameraCopy). Strength = `max(sand, windStrength > 0.5 && zone == "desert" ? 0.2 : 0)`. Bounce rule from PR `onCollision` (sides push up 0.5·bounce, max 3 edge bounces).
- [ ] **Step 4: Run** — PASS; sandstorm screenshot (`/weather thunder` in a desert).
- [ ] **Step 5: Checkpoint.**

### Task 7: Ground fog sheets

**Files:**
- Modify: `src/render/weather_sim.cpp`, `src/sim/weather.h/.cpp` (after-rain timer)
- Test: `tools/test_weather.cpp`, `tools/test_gameplay.cpp`

**Interfaces:**
- Consumes: `WKind::{FogFlat, FogUpright}`, `WeatherInputs::groundFog`, `surface` (100).
- Produces: `float WeatherSystem::afterRainT` (seconds left of after-rain fog; set to `rand(0, 300)` when rain ends, 0 when it starts) included in `netState()` as `ar`.

- [ ] **Step 1: Write the failing tests:** gameplay — after `set("rain")` then `set("clear")`, `afterRainT` is in (0, 300]; weather — swamp-zone inputs with `groundFog` → FogFlat and FogUpright particles appear within `surface` of the camera, each alpha ≤ 1 and 0 at age 0 and age = lifetime; `groundFog=false` → none; temperate zone, clear, `afterRainT = 0` → none.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement** PR's fog: surface spawn `x,z = triangle(cam, 100)` on the column top over dirt/grass/podzol/mud/moss, size 2 + distance × 0.1 × 2, lifetime 250 ticks, alpha `(age < half ? age/half : (life-age)/half) × opacity`, Flat fades when the view angle to it is below 0.4 rad, Upright fades above π/4, tint = fog colour × (0.7 + 0.2·(1 − rain)), blended; spawn when zone ∈ {swamp, jungle}, over rivers (`column().river`), when `afterRainT > 0` (not in windswept/plains-like zones), or at dawn where `mistNow > 0.3`. Density 0.5 (PR) × setting.
- [ ] **Step 4: Run** — PASS; screenshots of a swamp morning and after-rain fog.
- [ ] **Step 5: Checkpoint.**

### Task 8: Height fog

**Files:**
- Modify: `assets/shaders/common.glsl` (`applyFog`), `src/render/frame.h` (`vec4 heightFog`), `src/render/sky.cpp` (compute it), `src/render/world_renderer.cpp` (`FrameCtx::apply` sets `uHeightFog`)

**Interfaces:**
- Consumes: `mist`, zone wetness, time of day, `rain`, `Settings::heightFog`.
- Produces: uniform `vec4 uHeightFog` = (density at base, base global y, falloff height H, 0/1 on).

- [ ] **Step 1: Implement** in `applyFog`: with camera-relative `rel` and camera global y `cy`, optical depth of `d0·exp(-(y-base)/H)` along the ray is `d0·H·exp(-(cy-base)/H)·(1-exp(-rel.y/H))/rel.y·|rel|` (use the limit `d0·exp(-(cy-base)/H)·|rel|` when `|rel.y| < 1e-3`); `fogK = 1-exp(-depth)`; final `max(distanceFog, fogK)` toward the fog colour. In `sky.cpp`: base = band-local 64 + band base (valley floor near sea level), H = 6 + 10·(1-mist), d0 = 0.015 + 0.06·mist + 0.03·dawnPeak + 0.02·rain (×0 in the Nether/End, with shader packs, or setting off).
- [ ] **Step 2: Verify** — dawn valley screenshot (hills poke out of the fog, valley filled) on Modern/Legacy/Vulkan; `DQ_FRAME_STATS` unchanged within noise; no shader compile errors in the log on all three renderers.
- [ ] **Step 3: Checkpoint.**

### Task 9: Weather sounds

**Files:**
- Modify: `data/sounds.json`, `data/sounds.bin` (generated), `src/audio/ambience.cpp`, `src/audio/audio.h` (`AudioScene` fields), `src/client/client_fx.cpp` (fill them), `src/audio/audio.cpp` (rain loop low-pass when indoors)
- Test: `build/dq-sfx` renders

**Interfaces:**
- Consumes: `gustAt` (Task 2), `WeatherSystem::sand, bliz, rain, rainOpen`, `precipAt`.
- Produces: sound names `ws_weather.snow`, `ws_weather.snow.above`, `ws_weather.sandstorm`, `ws_weather.sandstorm.above` (pack events `fx.ws_weather.*` created by the merge); `AudioScene` fields `float snowK, sandK; bool above; float roof;`.

- [ ] **Step 1: Generate profiles:** `python3 tools/port/sound_analyze.py merge "/home/matt/Documents/particlerain-4.0.3+1.20.1-forge/assets/particlerain" particlerain data/sounds.json data/sounds.bin` — expected output lists 18 profiles; `data/sounds.json` gains events `fx.ws_weather.snow`, `fx.ws_weather.snow.above`, `fx.ws_weather.sandstorm`, `fx.ws_weather.sandstorm.above`.
- [ ] **Step 2: Verify** each renders: `./build/dq-sfx render ws_weather.snow /tmp/s.wav` (and the other three names) produce non-silent audio (peak > 0.05) of 2–10 s.
- [ ] **Step 3: Implement** PR's ambience: every ~0.1–0.3 s while snowing / sandstorming nearby, play the matching sound at a random point near the camera at volume `snowK` / `sandK` (snow: snowing columns × `rain`; sand: `sand`), pitch 0.5 and volume ×0.5 with the `.above` variant when the camera is above the weather (`cam y > topAt + 12`). Rain indoors: when sky light at the head < 8 but `rainOpen` sampling found rain within 16 blocks, the rain loop gets a 600 Hz low-pass and stays at 60 % instead of dropping toward 0.
- [ ] **Step 4: Verify** — `DQ_ENV_TEST` blizzard/sandstorm runs log the sounds played (`audio: N sounds played`) and no audio errors; indoor rain run logs the roof filter on.
- [ ] **Step 5: Checkpoint** — add Particle Rain's MIT notice (copyright line and permission text from its LICENSE) to `docs/CREDITS.md` naming what was adapted (spawning, wind field, particle kinds and numbers, fog sheets, sounds).

### Task 10: Final pass

**Files:**
- Modify: `docs/CHANGELOG.md`, `assets/news/news.dqn` (patch notes end with "Removed Herobrine.")

- [ ] **Step 1: Run all tests:** `./build/test_weather`, `./build/dq-test-gameplay`, `./build/test_net`, `./build/dq-creature-test` — all PASS.
- [ ] **Step 2: Performance:** `DQ_FRAME_STATS=1` storm scenes on Modern/Vulkan/Legacy at Off/Normal/High — Normal within +0.5 ms of Off.
- [ ] **Step 3: Screenshot set:** clear, rain, thunder, snow, blizzard, sandstorm, after-rain fog, swamp fog, dawn valley — before (old build in scratch) and after.
- [ ] **Step 4: Patch notes** describing the weather changes for players; last line "Removed Herobrine.".
