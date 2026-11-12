# Characters: skins, voxel bodies, the creator and the skin editor

Workstream: voxel player characters, skins, the character creator and skin editor
(`src/assets/skin.*`, `char_creator.*`, `char_paint.cpp`, `char_model.*`, `src/render/char_anim.*`, `char_layer.*`,
`src/ui/char_preview.*`, `char_screens.h`, `creator_screen.cpp`, `skineditor_screen.cpp`).

## Ported modules

| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| render/skins.js (data) | assets/skin.cpp | paintDefaultSkin (same pixels and noise), normalizeSkinImage (HD skins scaled, 64x32 converted, slim detection incl. the flat-strip rule), the skin net layout, skeMap/skeMirror, arms conversion, legacy conversion |
| render/skins.js buildSkinParts / addSkinVoxels | assets/char_model.cpp | the six parts + limb halves overlapping 2 px at the joints; the outer layer as a raised voxel shell (corners owned like the original: front/back reach round the sides, sides reach up and down) |
| render/skins.js avatarAnim, avatarDraws, armorMeshes, capeMesh/capeSegMesh, heldMatrix | render/char_anim.cpp, char_layer.cpp, assets/char_model.cpp | every pose: walk/run, idle sway, air/fall, landing crouch, swing, bow draw, eat, carry, cast wind-up, sneak, ledge hang/climb, sit, swim crawl, paddle/tread water, sleep, death tilt, flight lean; first-person body (FP_BODY) with the empty left hand |
| game/anim.js (players) | char_anim.cpp, char_layer.cpp | body yaw toward the way you go (backing off, turning), hip/shoulder twist, accel/turn leans, head nod, creative flight poses |
| game/capes.js | char_layer.cpp (cape angle/sway), char_model.cpp (meshes) | under a backpack the top lies flat and the hem swings |
| game/carried.js | char_layer.cpp | sword/staff/club across the back, bow over it, pickaxe/shovel at the right hip, axe/hoe at the left, potions on the pack or belt |
| game/charparts.js | char_layer.cpp | hideGear mask (from `Player::data["hideGear"]`); drawSkinHead (2D) not ported (menus can use the preview) |
| render/gear3d.js gearDraws, gearScaleDraws, gearPackOver, the creator's 3D look | char_layer.cpp, char_anim.cpp, char_model.cpp, char_paint.cpp | gear shapes per part (skirts follow the legs, the neck follows the head part way, cloak, tail), armour 3D pieces, stature per people (eye cap in first person), packs worn over robes/armour/capes |
| render/packarmor.js usage | char_model.cpp armorMeshes | pack pictures via assets/pack_armor.h, leather dye; otherwise the game's `armor_<mat>` tiles (+ `_face` on the helmet front) |
| ui/creator.js | assets/char_creator.cpp, char_paint.cpp, ui/creator_screen.cpp | every list/palette/outfit, ccDefaultLook, ccPaintSkin (same rules and random sequence), features, species, accessories over your own skin (accSet), undo |
| ui/skineditor.js | ui/skineditor_screen.cpp | pencil/eraser/fill/eyedropper, brush 1–3, mirror, outer layer, paint layer auto/base/outer, grid, undo/redo (100), part isolation, classic/slim conversion, start from current/default/blank, import, export, save, 3D painting by ray against the part boxes, keyboard shortcuts |

## Agreed changes / differences

- **Voxel bodies**: 2 voxels per skin pixel (`CHAR_VPP`), interior filled, faces coloured per face (edges show the right pixel),
  baked AO; ≈ the size of the world's micro-voxels. The overlay is a shell 1 px thick on the head and 0.5 px on body/limbs.
  See-through overlay pixels: alpha ≥ 100 are solid voxels, the rest are left out (the original blended 26–200).
- The overlay is hidden under **any** worn armour piece covering it (the original let it show through pack armour's gaps;
  with voxels both would occupy the same cells).
- **Creator additions** (Cube World / Veloren / Hytale): body sliders (height, build, head size, leg length), head shape
  (square/rounded/long/wide), brows (plain/thick/thin/none), nose (small/big as its own row), a cosmetic cape (worn when no cape
  item is), helmet shown/hidden, custom colour pickers next to every palette, try-on armour, walk preview, drag/stick turning.
- The creator and the skin editor are separate screens linked both ways ("Paint by hand…" / "Character creator…"); editing a
  creator character in the editor saves back into its character file (look and body kept).
- Skins are files in `userDir()/skins/`; characters in `userDir()/characters/<name>.json` (look + body shape + the painted
  skin as base64 PNG). Settings::skin = `"<file>.png[#slim|#classic]"`, `"creator:<name>"` or `""` (default).
- Wizard hats (`hat` items: archmage etc.) draw as plain helmets for now (arcana.js hatMesh not ported); blinking eyelids and
  talking mouths (skinFace) not ported.

## APIs for other areas

- `assets/char_creator.h` — `resolveCharacter(ref)` → `CharacterSpec{skin, body, cape, hash}` (cached; never null),
  `invalidateCharacter(ref)`, `CharLook` (+ JSON), `ccPaintSkin`, `saveCharacter/loadCharacter/listCharacters`.
- `assets/skin.h` — `SkinImage`, `loadSkinPngData/encodeSkinPng` (send skins over the network), `registerSkin(ref, img)`
  (a received skin: set `Player::skin = ref`, e.g. `"net:<hash>"`), `listSkins()`, `skinsDir()`, `paintDefaultSkin()`.
- `assets/char_model.h` — `charModel(spec)` (meshes per part, cached by hash), `armorMeshes`, `capeMesh`, `packWornMesh`,
  `skinArmModel(spec)`; `charModelsReset()` (called by the layer when the resource pack changes).
- `render/char_anim.h` — `CharPose`, `charAnimate`, `charRig` (part matrices; NPC villagers can reuse it with a skin).
- `render/char_layer.h` — `CharacterLayer` draws every entity of type `"player"` (local one in F5 views; first-person torso and
  legs with `settings.fpBody`). Inputs beyond `Player` fields: `Player::data["back"|"cape"|"hook"]` (item keys),
  `data["hideGear"]` (6 × 0/1), `data["sleeping"]`, `data["hang"]`, `data["climb"]`, `data["invisible"]`;
  `Player::swingT/eatT/bowPull` (the layer copies the local player's from `HeldItemLayer`; networking should send them).
  Registered by `registerCharacterLayers()`, called in `GameSession`'s constructor. `localPlayerArm()` is used by
  `render/item_layers firstPersonArm()`.
- `ui/screen.h` — `makeCreatorScreen()`, `makeSkinEditorScreen()`; `ui/char_screens.h` — the same with a starting
  character/skin, and `wearSkin(app, ref)` (sets Settings::skin, saves settings, updates the local player).

## Changes outside this area

- `src/sim/player.h`: one line — `float swingT, eatT, bowPull` (arm timers the body shows).
- `src/client/session.cpp`: include + `registerCharacterLayers()` next to `registerItemLayers()`.
- `src/render/item_layers.cpp`: include + first line of `firstPersonArm()` returns the skin's arm (`localPlayerArm()`).
- `src/ui/screen.h`: the two factory declarations.

## Dev / test switches (environment)

- `DQ_VIEW=0|1|2` start in first person / behind / front view; `DQ_DEV_YAW`, `DQ_DEV_PITCH` turn the player.
- `DQ_OPEN=creator|skineditor` opens the screen after a few frames.
- `DQ_SKIN=<ref>` wear a skin; `DQ_SKIN=random[:seed]` makes and wears a fresh creator character "devchar"
  (`DQ_DEV_SPECIES=dwarf` etc.).
- `DQ_DEV_WEAR="iron_helmet,leather_chestplate#a03030,red_cape,leather_backpack,lantern"` puts things on.
- `DQ_DEV_POSE=walk|run|sneak|swim|fly|swing|bow|sit|sleep|hang|eat` animates the local player for screenshots.

## Left to do

- Worn chestplate sleeve on the first-person arm (fpArmDraws' pad/cuff); skin face blinks/talk (skinFace); arcana hat meshes;
  Carry On poses need the gameplay hook (`pose.carry` is ported, nothing sets it yet); `castWind` (fishing) likewise.
- Name tags over players (avatarTopOf is easy from `CharModel::stature`).
- Networked skins: send `encodeSkinPng` + body shape, `registerSkin` on arrival (net workstream).
- Menus entry points (Players & skins → Character creator… / Skin editor…) belong to the UI engineer's menus.
- A file dialog for importing skins (the editor lists `skins/` and takes a typed path).
