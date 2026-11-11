# Weather and atmosphere overhaul — design

Date: 2026-11-10. Status: design approved in conversation; this is the written spec.
Reference: Particle Rain 4.0.3 by pigcart (MIT, https://github.com/PigCart/particle-rain). Its ideas, numbers and
some algorithms are adapted here; its MIT notice goes in `docs/CREDITS.md`.

## Goal

Rain, snow, storms, sandstorms and fog that look and sound like Particle Rain (and better than the current
CPU-disc rain), on all three renderers, tunable for weak PCs, and the same for every player in multiplayer.

Success: (1) a forced storm, snowfall, blizzard, sandstorm and post-rain fog each look clearly better in side-by-side
screenshots against today; (2) at "Normal" the weather adds ≤ 0.5 ms per frame on Modern GL, Vulkan and Legacy
(measured with `DQ_FRAME_STATS`); (3) a guest sees the host's weather (state, storms, sand/blizzard, wind) within
~1 s; (4) nothing falls through roofs or walls.

## Current state (summary of the audit)

- `sim/weather.*` (`WeatherSystem`): kind clear/rain/thunder, travelling fronts (`stormAt`), zones, showers, `sand`,
  `bliz`, `mist`, wind (`windDir`, `windStrength`, `gust`, `cloudX/Z`), lightning, ground effects. Snow vs rain is
  `precipAt` (temperature, band height, season). Single-player-centric (`anchor` = lead player).
- `render/env_layers.cpp` `WeatherFxLayer` spawns rain/snow/dust into the generic `render/particles.*` system:
  opaque alpha-tested quads in a 30-block disc, column-top cache for roofs, no collision after spawn, 25 % splash.
- Fog: `common.glsl applyFog` is distance-only (+ near fog, sky blend). Mist is a global scalar from `sky.cpp`.
- Sound: one rain hiss (`ambience.cpp`), wind loop whose `AudioScene.gust` is never set.
- Multiplayer: guest never re-reads host weather; `hostRules` never false — each guest runs its own weather.

## Architecture

New units (one feature per file, < ~1500 lines each):

| Unit | Responsibility | Depends on |
|---|---|---|
| `sim/wind_field.h/.cpp` | `windAt(x,y,z,t)`: Particle Rain's gust field (sine products of position and time, height ramp 50→90 band-local) scaled and rotated by `WeatherSystem` wind direction/strength. Pure function of world time: same on every machine. | weather |
| `render/weather_fx.h/.cpp` | Weather particle pool (SoA), spawning, simulation, collision, budget. Emits `WeatherKind` particles: rain, heavy rain, snow, heavy snow, dust, skittering dust, shrub, dust haze, fog (horizontal/vertical), ripple, splash, streak, steam. | wind_field, world, weather |
| `render/weather_draw.cpp` + `assets/shaders/weather.glsl` | One instanced draw per blend group: per-instance pos, velocity, size, rotation, sprite, tint, alpha. Rotation modes (velocity-stretched relative to camera, camera-facing, upright, flat, camera-copy) expanded in the vertex shader. Soft (blended, no depth write) for rain/snow/fog/haze; depth-tested against the scene. | weather_fx |
| `render/weather_sprites.cpp` | Sprite sheet: rain/snow from the resource pack (`environment/rain.png`, `snow.png`: 16 slices, 4 merged heavy) with built-in procedural fallbacks; ripple rings (procedural, 8 frames); streak; fog sheets (horizontal/vertical, soft noise); dust; dead bush from the tile set. | assets |
| `assets/shaders/common.glsl` (height fog) | `applyFog` gains analytic exponential height fog integrated along the view ray: density `d0·exp(-(y−base)/H)`. Uniforms `uHeightFog` (density, base y, falloff, on). | frame ctx |
| `sim/weather.cpp` (net) | `netState()/applyNet()`: compact state sent by the host each second; guest sets `hostRules=false`, stops its own cycle, follows the host. | net |
| `audio/ambience.cpp` (+ sound profiles) | Snow / blizzard / sandstorm loops from Particle Rain's recordings (analysed into profiles), "above" variants, muffled indoor rain, gust from the wind field. | audio |

The existing `WeatherFxLayer` rain/snow/dust code in `env_layers.cpp` is replaced by a `WeatherLayer` that owns
`weather_fx` (update in `update`, draw in `drawTranslucent`). Lightning, gust pickup and Nether ash stay.

## 1. Precipitation

- **Spawn**: points on a sphere of radius `weatherDist` (default 16) round the camera: centre-weighted, top half
  when slow (Particle Rain's `|r1²−r2²|` height distribution), whole sphere when moving fast (falling/flying);
  rejected below the column top (roof) and outside loaded chunks. Rate per frame from the budget, `rain`/`thunder`
  intensity, `dens` and camera speed (more when moving so density stays visual). Kind per point from `precipAt`
  (cached), heavy variants only when thunder > 0 (heavy snow when `bliz` > 0).
- **Motion**: gravity per kind (rain 0.9×, heavy 1.0× of ~20 m/s; snow 0.05–0.1 of that), wind from `windAt`
  times the kind's wind strength (storm strength when thundering), applied at spawn ×10 then per tick (as PR).
- **Collision**: each step tests the block at the new position (solid/liquid → hit); a hit on liquid makes a ripple,
  on a solid top a splash (rain) or a short settle-and-fade (snow); sideways hits stop the drop. Column-top cache
  stays for spawn rejection only.
- **Look**: rain is a quad stretched along velocity minus camera velocity (stretch clamp 0.25–1, shortens during the
  impact animation), tinted `mix(waterColour, fogColour, 0.6)`; snow rotates (±0.4·speed per tick), opaque-ish soft.
  Alpha fades out over the last third of `weatherDist`.
- **Surface FX** (Splashes & Ripples option): each frame a few random columns within `weatherDist` under open sky
  while it rains: ripple rings on water tops (8 frames, 7 ticks, horizontal), splashes on solid tops, streaks on
  sides of stone/glass/impermeable blocks (slide down, drip at the bottom edge), steam over lava/fire.

## 2. Sandstorms and dust

- Dry columns (no precipitation, ground sand/red sand/terracotta/sandstone/coarse dirt) in the Overworld band.
- **Dust**: sky-sphere spawn, tint = ground map colour × (0.8–1.0), small camera-facing, strong wind.
- **Skittering dust**: on block tops, bounces (0.15), lifetime 60 ticks, opaque.
- **Shrubs**: rare tumbling dead-bush sprites (density 0.01), bounce off slopes (PR's bounce rule).
- **Dust haze**: big soft camera-facing sprites, opacity 0.4, near the surface.
- Intensity: light (≈20 %) whenever wind strength > 0.5 in desert zones; full with `W.sand`.

## 3. Fog

- **Ground fog sheets**: horizontal (flat, fades below ~23° viewing angle) and vertical (upright, fades above 45°)
  soft sprites, size 2 scaled by distance (0.1), lifetime 250 ticks fading in and out, tinted by fog colour
  (darker while raining), spawned on the surface within `surfaceRange` (100). Always in swamp/jungle zones and
  rivers; for a random 0–5 min after rain stops elsewhere (except plains-like windswept land); at dawn where the
  existing dawn-mist term is high.
- **Height fog**: in `applyFog`, extra fog `1 − exp(−∫density)` along the ray from camera to fragment, closed form
  for an exponential layer. Base = band-local sea level (64) or valley floor near the camera; density from `mist`,
  zone wetness, time of day (dawn peak), rain. Coloured like the current fog. Off in the Nether/End (they keep
  theirs) and with shader packs.

## 4. Wind and sound

- `windAt` drives all weather particles, cloud drift stays as is, and `AudioScene.gust` = local gust magnitude.
- Sound profiles: Particle Rain's `snow-01..06`, `sandstorm-01..06`, `sandstormabove-01..06` analysed with
  `tools/port/sound_analyze.py merge` (needs ffmpeg) under a `particlerain/` prefix; events `weather.snow`,
  `weather.snow.above`, `weather.sandstorm`, `weather.sandstorm.above`. Played like PR: near the rain position at
  the snow/wind volume, pitch 0.5 and quieter when the camera is above the weather.
- Indoor rain: when sky light at the head is low but rain falls nearby, the rain loop is low-passed (roof patter)
  instead of only quieter.

## 5. Multiplayer sync

- `WeatherSystem::netState()` → small JSON (kind, timers, front origin/dir/progress, rain, thunder, sand, bliz,
  wind dir/strength/gust, showers, zone, anchor). Host sends it with `SEND_ENV` each second (not the save JSON).
- Guest: `applyNet()` sets `hostRules = false` and overwrites state; `update()` on a guest only smooths toward the
  host values and never starts/ends storms. Lightning already arrives as fx events.

## Options (Options → Video → Weather; game and launcher; follow graphics presets)

| Option | Values | Default | Presets |
|---|---|---|---|
| Weather Particles | Off / Low / Normal / High (0 / 600 / 1500 / 3000) | Normal | potato Low, low Low, medium Normal, high/ultra High |
| Weather Distance | 12–32 blocks | 16 | 12 / 14 / 16 / 20 / 24 |
| Splashes & Ripples | on/off | on | off on potato |
| Ground Fog | on/off | on | off on potato |
| Height Fog | on/off | on | on |

The existing Particles option still scales non-weather particles; Particles = Minimal forces Weather Particles Low.

## Error handling

- Missing pack sprites → procedural fallbacks. Missing sound profiles → current rain hiss only, no crash.
- Budget hard cap; the pool never grows past it. Particles outside loaded chunks are dropped.
- Renderer differences: instanced path needs GL 3.3 (Legacy OK), Vulkan via the translation layer (instancing is
  supported there).

## Testing

- `DQ_ENV_TEST` forced scenes (rain, thunder, snow zone, blizzard, desert sandstorm, after-rain fog, dawn valley):
  before/after screenshots on Modern, Legacy, Vulkan.
- `DQ_FRAME_STATS` A/B: weather off vs Normal vs High.
- Collision checks: rain under a roof and against a wall with strong wind (no drops inside).
- Multiplayer: host + guest on one machine (dedicated server or open-to-LAN smoke), guest weather matches host
  after a forced change.
- `dq-creature-test` / gameplay tests still pass (weather sim change).

## Out of scope

Puddles/flooding, wet-surface shading, ray-marched fog volumes and light shafts (possible later on top of height
fog), Particle Rain's in-game particle editor, data-driven particle JSON.
