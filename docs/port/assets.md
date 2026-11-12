# Assets: resource packs, voxel items, icons, held & dropped items

Workstream: resource packs, 3D voxel items, item icons, held and dropped items (`src/assets`, `src/render/item_layers.*`).

## Ported modules

| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| render/packs.js | assets/packs.cpp, assets/pack_fs.* | folders and .zip (miniz), wrapped zips, legacy `textures/blocks` packs, packCandidates (data/packmap.json), PACK_LEGACY/SKIP/TINTED_SRC/FIXED, packFrame (cut-out vs soft alpha, 16→32 doubled, 64→32 averaged), the 253 tint marker, grass side overlay, water alpha, `.mcmeta` animations, chests from `entity/chest/*.png` |
| render/packarmor.js | assets/pack_armor.* | layer 1/2 pictures (+ 1.21 `entity/equipment` paths), leather dye + overlay, bare-arm check, the box table, a skin-box voxeliser |
| render/items3d.js | assets/item_parts.cpp (`buildItems3d`), item_models.cpp (blocks, torch) | ingots/bars, bow (+ `bow_pulling_0..2`), fishing rod (+ `_cast`, `_tp`), backpacks, placed-model block items, the 3D torch |
| render/models3d.js | assets/item_parts.cpp (`buildModel3d`) | every T3D family (pickaxe … charm) with the same numbers |
| render/sculpt.js | assets/item_models.cpp (`sculpt`), item_parts.cpp (`buildModelM3d`) | local-thickness sculpt with SCULPT_PROFILE, ITEM_SIZE, the fill picture (modelFill), potion/bucket/bowl/book/armour/arrow models |
| render/models.js itemMesh, renderBlockIcons, extrudeTile | assets/item_models.*, assets/icons.cpp | the itemMesh chain (models3d → sculpt → items3d → models) |
| render/gear3d.js (shapes) | assets/gear3d.* | gearRead/gearWrite (look in the skin's spare pixels), gearShapes, gearArmorShapes, gearPivots, stature, a per-voxel gearPaint |
| game/local.js hand(), mcSwing, swing/equip/eat timers; player.js bob | render/item_layers.cpp `HeldItemLayer` | first person only |
| game/entities.js ItemEntity.draws | render/item_layers.cpp `DroppedItemLayer` | hover, bob, spin, 2–5 copies by stack size |
| (data) world/mesher.js SHAPES, shapeItemBoxes, BLOCK_FACES | data/itemshapes.json (tools/port/itemshapes_expr.js) | per block item: cube faces / boxes with per-face tiles / torch / picture |

game/anim.js is all third-person bodies (left for the characters/creatures workstreams).

## Agreed changes / differences from the original

- **Every item is a voxel model** (no textured quads): grids at 32 voxels per block (one voxel per pixel of a
  32px picture), greedy-merged faces with per-voxel colours and baked AO, shaded by the shared voxel shader
  (`assets/shaders/voxel.glsl`: world lighting, fog, glow voxels, enchantment glint, hurt tint).
- Part models are voxelised from the original's shapes (centre-inside test, every part at least one voxel
  thick). Each part takes its colour from where it lies over the item picture "filled out" past its outline
  (the original's MODEL_FILL), so **pack sprites show on the 3D models**.
- Round things are true voxel spheres/discs/rings (the original stacked octagons); sword/dagger blades are
  bevelled toward their edges; staff stones and torch flames glow; berries are little balls.
- Block items: cube blocks are a 32³ micro-voxel cube; cut-out blocks (glass, leaves, panes) keep only the
  shell voxels under opaque pixels, so they are hollow like the real block. Faces are coloured per face from
  the block's tiles (SHAPE_ST mapping), the 253 tint marker takes the original's default item tint.
- `white_wool` (STRING_TILE in items3d.js) didn't exist in the original atlas (it fell back to tile 0); it maps
  to `wool` here.
- Icons: every item's model rendered offscreen (2× supersampled, un-premultiplied, mipmapped) into a 64px
  atlas; blocks with the original's block-icon view, items turned to a 3/4 view and fitted. The atlas is cached
  in `userDir()/cache/icons-<hash>.png` (hash of the tiles' pixels, the items and the builder version — bump
  `VERSION` in icons.cpp when a builder changes). First render ≈ 2–5 s on 2 busy cores, cached ≈ 0.4 s.
- Colormaps (`colormap/grass.png`, `foliage.png`) drive biome tints when the pack has them (engine addition;
  hooked in `render/mesher.cpp climateTint`).
- Resource pack choice is `settings.resourcePack`: `"auto"` = a pack whose name contains "gerudoku", `"none"`,
  or a folder/zip name in `userDir()/resourcepacks/`.

## APIs for other areas

- `assets/pack_fs.h` — `PackFS* activePack()`; `read(rel)`, `readJson`, `exists`, `list(prefix)`, `texRoot`,
  `legacy`, `packFormat`. Audio: `assets/minecraft/sounds.json` + `sounds/…ogg`; creatures: `texRoot + "entity/…"`.
- `assets/tileset.h` — `TileSet::anims` (`TileAnim{layer, frames, frameTime}`, frames are appended tiles),
  `TileSet::res`, `appendTile`, `packTint(kind, temp, humid, ok)`.
- `assets/gui_textures.h` — `guiSprite("widget/button" | "hud/hotbar" | "hud/heart/full" | "container/inventory" …)`
  and `guiTexture("gui/…png", rect)`: GL texture + uv rect (`id()` for ImGui), invalid when the pack lacks it.
- `assets/icons.h` — unchanged API: `itemIcons().get(key)`, `.tile(layer)`, `iconSize()`.
- `assets/item_models.h` — `const VoxelModel& itemModel(key)` (GL thread, built on a worker, `ready()` when
  uploaded; `kind` Flat/Block, `torch3d`, `stand`, `depth`, `matrix()` = grid → the original mesh frame),
  `buildItemModel(key, out)` (CPU, any thread), `isHandheld`, `itemSize`.
- `assets/voxel.h` — `VoxelGrid`, `buildVoxelMesh(grid, out, faceColor*)`, `VoxelMeshGL`, `VoxelFrame`,
  `voxelRenderer().begin(frame, viewProj) / beginNeutral / draw(mesh, model, light, tint, glint)`.
  `assets/voxel_paint.h` — `VoxelPainter` (box, seg, rod, hexa), `SpriteInfo`, `tileVoxel`. Use these for bodies.
- `assets/pack_armor.h` — `packArmorHas(mat)`, `packArmorLayer(mat, layer, dye)`, `packArmorPieces(bareArms)`,
  `skinBoxColor`, `paintSkinBox` (also works for 64×64 skins with TW=64, TH=64).
- `assets/gear3d.h` — `gearRead(skin)`, `gearShapes(look, aw, helm, chest)`, `gearArmorShapes`, `gearPivots`,
  `gearStature`, `gearColor`, `paintGearShapes(painter, shapes, look, tiles, part)` (model pixels).
- `render/item_layers.h` — `HeldItemLayer` (`swingArm()`, `eat()`, `setSwinging()`; reach it with
  `session.layer<HeldItemLayer>()`), `DroppedItemLayer` (entities `type == "item"`, `data["stack"]` = ItemStack
  json, optional `data["floating"]`), `firstPersonArm()`. Registered by `registerItemLayers()`, called once in
  `GameSession`'s constructor.

## Changes outside src/assets

- `src/client/session.cpp`: include + one call `registerItemLayers()` before the layer factories run.
- `src/render/mesher.cpp`: include + one line in `climateTint` (pack colormap first).
- `src/render/item_layers.*` (new), `assets/shaders/voxel.glsl` (new), `data/itemshapes.json` (new),
  `tools/port/itemshapes_expr.js` (new).

## Dev aids

- `DQ_DEV_ITEMS="held,offhand,drop1,drop2,…"` — puts items in the hands and drops the rest in front of you
  (logs whether the pack's GUI sprites were found).
- `DQ_ICON_DUMP=/path/atlas.png` — saves the rendered icon atlas (+ `.json` slot map).

## Left to do

- Skin-coloured first-person arm (`firstPersonArm()` is the plain sleeve + hand; the characters workstream
  should swap in the skin's voxel arm and worn chestplate sleeve).
- Hand poses for bow drawing (game/bows.js fpBow), fishing cast wind-up, Carry On, ledge hanging, the map in
  hand (maps.js) — hooks belong to those gameplay modules.
- Torch flame/smoke particles at the held torch's tip (noteTip) — particles workstream.
- Animating the compass needle / clock face on held/dropped dials (static picture now).
- Drawing worn armour and gear shapes on bodies (characters workstream; data + voxeliser are here).
- Pack sounds (audio) and creature skins (creatures) read through `activePack()`.
- Dropped items use the entity position directly (no tick interpolation yet).
