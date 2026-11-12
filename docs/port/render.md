# Render workstream: meshing, light, terrain shading

Ported from the original 0.9.0.90 JS: `world/mesher.js`, `world/shapes.js`, `world/plants3d.js`,
`game/perf.js` (plant detail), the cloth of `game/townlife.js`, the chorus shape of `game/theend.js`,
the SHAPES model table (mesher.js + game/*.js), the terrain parts of `render/gl.js` + `render/renderer.js`
(light curve, fog, passes, outline, cracks) and the animated tiles of `render/doku.js` (ANIMS).

## Files
| File | What |
|---|---|
| `src/world/shapes.h/.cpp` (dqcore) | every ShapeKind's boxes (exact port, same meta encoding), stair corners, fence/wall/pane joins, wire connections, rails, attach/box6 helpers, `BlockTraits` (pre-parsed per-block props), static model table (`data/shapes.json`), cloth boxes |
| `src/world/light.h/.cpp` (dqcore) | `initialChunkLight` (worker) + `LightEngine` WorldSystem (incremental, across chunk and band borders) |
| `src/world/showcase.cpp` (dqcore) | test world type `showcase`: every block on a grid + shape groups, crops, pool + waterfall, lava, dark room |
| `src/render/mesher.h/.cpp` | vertex format, gather, climate tints, cubes (smooth light + AO + greedy merging), liquids, torches, section loop |
| `src/render/mesh_shapes.cpp` | shaped blocks (emitBox with interpolated smooth light), models (lanterns swing, campfire flames, cauldron water, potted plants, cloth flapping), redstone dust, rails, lever handles, candle flames |
| `src/render/mesh_plants.cpp` | 3D voxel plants (see below) |
| `src/render/world_renderer.h/.cpp` | arena vertex buffer, Modern = multi-draw-indirect, Legacy = per-section base-vertex draws, plant-detail scheduling, stats, mesh FX lists |
| `src/render/terrain_fx.cpp` | RenderLayer: selection outline (real select boxes), breaking cracks, tile animation, underwater/lava fog, env light floor + wind, test hooks |
| `src/render/tile_anim.h/.cpp` | built-in animations (`data/tile_anims.png/.json`, extracted from the original's ANIMS) + GPU animator |
| `assets/shaders/chunk.glsl`, `common.glsl`, `terrain_fx.glsl` | terrain shading, micro-voxel relief, waves, foam, waterfalls, lava |
| `data/shapes.json`, `data/tile_anims.*` | extracted with `tools/port/jsdump.py` from the running original (SHAPES, ANIMS) |

## Vertex format (`ChunkVertex`, 20 bytes)
pos i16×3 (1/256 block, section-relative) · tile u16 (layer | normal<<12; normal 6 = none) · uv u16×2 ((tile units + 64)·256,
wraps) · sway u8 (the original SWAY codes: 1 plant tip, 2 leaves, 3 water, 4..15 hanging, 16..31 partway up a plant,
32..47 water + openness, 48..79 cloth) · sky u8, blk u8 (level·17, smooth) · shade u8 (face shade × AO; liquid tops: shore foam)
· rgb tint · mat flags (Emissive, Liquid, Cutout, Translucent, NoRelief, Lava, Flowing, TintAll).
Tints follow the original: TintAll (water, dust) multiplies the whole picture ×1.25, otherwise only tint-marked
pixels (alpha 253) take the climate tint ×1.3 (mip-blended smoothly).

## Light
- Same rules as the original flood fill: sky 15 falls straight down through clear cells, spreads losing 1 + lightAtt;
  block light from emitters (`Registry::metaLight`, candles/pickles by meta); opaque stops both.
- Persistent per section (`light = sky<<4 | block`). `initialChunkLight` (worker): column sky + chunk-local flood +
  emitters; all-air sections stay unallocated only when their light is uniform (`Chunk::compact` now checks that).
- `LightEngine` (added in `World::World`, so server and side realms have it): on chunk arrival merges light across the
  4 side borders and the bands above/below; on `onBlockChanged` queues the cell; `flush()` runs removal BFS +
  re-propagation (incremental, no ghost light) and marks touched sections (and edge neighbours) dirty. `flush` runs every
  tick and in `WorldRenderer::update` before gathering, so a placed torch lights up in the same frame.
- Sky enters each band at its top: 15, except the Ember Depths (0). The End's sky is lit at day 0.78 like the original
  (sky.cpp `dimSky`), Depths 0.22.
- Shader: `curve(l) = l / (3.2 - 2.2 l)`, sky × env.day, block × (1, .85, .62), floor `minLight` = 0.025 + brightness·0.07
  (Depths ≥ .07, End ≥ .12), per-micro-voxel glow from the height texture's G channel (assets fill `TileSet.emissive`).

## Plants (3D voxel, Lay of the Land style; `settings.plantDetail` off/near/all, near = 44 blocks like perf.js)
- A plant picture becomes a voxel slab on the 32×32 grid: each micro-voxel 1–3 micro-voxels thick each side, as thick as
  its part is wide (plantDepth at 32 res), greedy front/back rectangles, step walls between thicknesses. Crops (every stage)
  = # of four slabs; saplings, mushrooms, bushes, hanging vines, seagrass, coral, dead bushes = two crossed slabs.
- Flowers: 3D stalk + leaves + fan/cup/ball/bells of petals, colours from the picture (emitFlower3D).
- Short grass, ferns, jungle grass and both halves of tall grass/large fern: clumps of 6 blades one micro-voxel thick.
- Sugar cane: real stalks with bands and leaves; kelp: swaying stem with leaves (emitCane3D/emitKelp3D).
- Vines and lily pads: voxel plates against their wall / flat on the water.
- Beyond the detail radius (or off): the original flat planes (emitCrossFlat). Sections are re-meshed with detail as you
  approach (6 per 0.25 s, nearest first).

## Micro-voxel relief (`settings.microVoxels`)
0 flat · 1 shaded micro-voxels (depth darkening + bevel rim from neighbour heights) · 2 relief: the fragment shader
ray-marches the face's height field one micro-voxel cell at a time (2D DDA, depth quantised to 0..4 micro-voxels),
showing column tops and side walls (shaded like block faces), crevice darkening and a sun shadow ray (Modern only),
fading to flat past `microVoxelDist` and at grazing angles. Tangent frame from derivatives snapped to block axes (works
for rotated logs/f6 faces and merged faces). Legacy: 10 steps, no shadow ray; Modern: 24 steps.
Applies to cube faces and shaped-block/model faces; plants, liquids, cutout (leaves, glass) and tilted parts are NoRelief.

## Water, lava, animation
- Liquids: original corner heights (cornerH/flatPool), flow direction from the corners → the flow picture is turned
  downstream (rounded to 45°); side faces use the flow picture with v down (waterfalls); waves via sway 32+openness
  (sea swell `uSea`); shoreline foam (per-corner shore amount → animated broken foam lines); falling water faces get
  white-water streaks (Mat::Flowing); sky reflection (fresnel) + sun glint on top faces; lava full-bright with a slow glow.
- Animated tiles: `TileSet::anims` (`TileAnim{layer, frames, frameTime ms}`); built-ins (water, flowing water/lava, lava,
  fire, soul fire, campfire flames, portals, magma, sea lantern, torches, candle flame, lit furnaces) are appended as extra
  layers; `TileAnimator` uploads the current frame + its mip chain into the animated layer.
- Underwater / in lava: fog colour (0.09, 0.23, 0.48) / (1, 0.35, 0.04), range 0..22 (original viewEnv).

## Passes & performance
- Opaque (cubes, no discard) → cutout (plants, shapes, models, leaves, glass; alpha test, back-face culled; double-sided
  where the original drew both windings) → translucent (water, stained glass; back to front by section).
- One arena vertex buffer (64 MB start, doubles; best-fit free list with coalescing). Modern: 1 `glMultiDrawElementsIndirect`
  per pass with per-draw origins as an instanced attribute (baseInstance). Legacy: `glDrawElementsBaseVertex` per section.
- Greedy merging of cube faces whose four corners have equal light, AO and tint and no sway/rotation (flat sunny ground
  becomes a few big quads); everything else keeps per-vertex smooth light + AO.
- Measured on llvmpipe (2 shared cores, 1280×720, Modern, microVoxels 2, default debug terrain, after loading):
  | render distance | microVoxels | ms/frame (wall) | drawn/loaded sections | arena used |
  |---|---|---|---|---|
  | 8 | 0 flat | 44 | 204/732 | 13 MB |
  | 8 | 1 shaded | 48 | 204/732 | 13 MB |
  | 8 | 2 relief | 421 | 204/732 | 13 MB |
  | 12 | 2 relief | 330 | 436/1477 | 28 MB |
  | 16 | 2 relief | 216 | 758/2546 | 50 MB |
  The CPU was shared with 6 other builds, so the numbers are noisy (RD 16 coming out faster than RD 8 is noise); what
  they do show: on a software rasteriser the frame is fill-bound, the section count barely matters (multi-draw keeps draw
  cost flat), and the relief ray-march is ~8x the cost of flat/shaded. **Relief has not been measured on real hardware
  (Intel HD 530)**: if it is too slow there, lower RELIEF_STEPS / microVoxelDist for that preset or default the low/potato
  presets to mode 1 (they already use 0/1).
- Chunk mesh time (worker, per 16³ section): 0.15–0.9 ms (worker, debug terrain; higher with 3D plants near the player and under CPU contention); the light engine's flush is 0.1–2.5 ms on chunk arrival, 0.3 ms for a block edit that re-lit ~3000 cells.

## APIs for other areas
- Collision/picking (gameplay): `bool blockShapeBoxes(const World&, x, y, z, ShapePurpose::Collide|Select, std::vector<BoxF>&)`
  (world-space boxes with slab sink and torch set-back; `false` = plain full cube). Lower level: `shapeBoxes(id, meta, ShapeNb, purpose, out)`
  in pixels, `stairShape`, `wireConnects`, `railShape`, `RAIL_EXITS`, `traits(id)` (RsRole, cond, rail kind...).
- Particles: `WorldRenderer::waterfallBases(near, radius)` (lowest falling-water cells), `forEachFx(fn)` → `MeshFx` per
  section: `waterfalls`, `drips` (x,y,z,kind 1 water 2 lava 3 leaves 4 honey), `emitters` (fire, torches, campfires, candles, portals).
- Light: `w.system<LightEngine>()->flush(w)` before reading fresh light; `skyTopLevel(dim)`.
- Assets: fill `TileSet::anims` from `.mcmeta` (frametime ticks × 50 = ms) and call `dropTileAnim(ts, layer)` when a pack
  replaces an animated tile; `TileSet.emissive` drives per-micro-voxel glow; `deriveMicroVoxelHeights` unchanged.
- FrameCtx gained `minLight`, `eyeFluid`, `sea` (weather can set `sea` and `wind`; the terrain layer sets defaults at order -100).
- Tests: `--world-type showcase`; env `DQ_RENDER_CAM=x,y,z,yaw,pitch` (player position; eye is +1.62), `DQ_RENDER_TIME=secs`,
  `DQ_RENDER_STATS=1` (log every 120 frames), `DQ_RENDER_EDIT="x,y,z,key;..."` (set blocks at frame 90).

## Differences from the original
- Light is persistent + incremental instead of recomputed per mesh (same values); sky per band.
- Plants are built on the 32×32 grid (the original sampled 16×16); seagrass/coral and tall grass are 3D too; grass blades
  have one micro-voxel of thickness; vines and lily pads are voxel plates.
- `hash32` jitter uses chunk-local x/z and band-local y like the original.
- River flow (`MESH_RIVER`, worldgen) is not wired yet: rivers' still water doesn't scroll until gen exposes it.
- Shore foam, waterfall streaks, water fresnel/glint, lava glow and micro-voxel relief are additions (the agreed voxel look).

## Left to do
- Cheaper relief path for weak GPUs if HD 530 measurements need it (fewer steps, skip the shadow ray, per-tile max-depth early-out).
- The original's "deep cave" skip (don't draw sections far below the surface when you're up on the land) is not ported; frustum culling only.
- Expose `breakFraction` from gameplay (terrain_fx recomputes hardness × 1.5 for the crack stage).
- Hook `waterfallBases`/drips into the particles engine; river flow from gen; `uSea` from weather.
- Better relief heights per material (`deriveMicroVoxelHeights` is the assets engineer's colour heuristic; grass tops are noisy).
- Persistent mapped buffers for the MDI command/origin streams; GPU frustum culling; LOD meshes for far sections.
- Translucent sorting within a section (water faces are sorted per section only).
