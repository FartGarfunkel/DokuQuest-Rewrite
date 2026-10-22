# DokuQuest Rewrite — map for AI sessions

Read THIS file, then only the file(s) your task names below. Do not scan the repo.

## Rules (token budget)
- Bounded context: voxel work → `common_server` + the one engine file; never read launcher/CI for it.
- Targeted edits (Edit / small diffs), never reprint whole files.
- If a build error survives 2 fix attempts, stop and explain it to the user.
- Keep one feature per file; new feature = new plugin file + one line in `main.rs`.
- Update the "Port status" table when a JS module is ported.

## Crates
| Crate | What | Compiles offline? |
|---|---|---|
| `crates/common_server` | Engine-agnostic core + `dq-server` bin. **Zero deps.** | yes — `cargo test -p common_server` |
| `crates/game_engine` | Bevy **0.16** client, bin `dokuquest` | needs crates.io (CI) |
| `crates/launcher` | Tauri launcher: original 0.9 `web/launcher.html` + Rust updater, bin `dokuquest-launcher` | needs crates.io (CI) |

## Where things live
| Task | File |
|---|---|
| Blocks / items / recipes / mobs / textures data | GENERATED from the JS game: `tools/port/extract.py` + `gen.py` → `common_server/data/*.tsv`, `tiles.bin`, `src/gen_blocks.rs` (never hand-edit) |
| Block flags, shape boxes, mining rules | `common_server/src/blocks.rs`, `items.rs` (`drop_of`, `break_time`) |
| Inventory, crafting, smelting logic | `common_server/src/items.rs` |
| Mob data + rig parsing | `common_server/src/mobs.rs` |
| Villages | `common_server/src/villages.rs` |
| Micro-voxel colouring from tiles | `common_server/src/style.rs`, `tiles.rs` |
| Terrain, caves, ores, trees, plants | `common_server/src/worldgen.rs` |
| Add/move a dimension (stacked Y bands) | `common_server/src/dims.rs` + generator in `worldgen.rs` |
| Meshing, LOD detail, plant/item voxel models | `common_server/src/mesh.rs` |
| Save format, data folder | `common_server/src/save.rs` |
| Chunk streaming, LOD rings, edits, autosave | `game_engine/src/world.rs` |
| Input (keyboard/mouse/gamepad) → `Controls` | `game_engine/src/controls.rs` |
| Movement, collision, mining, placing, eating, attacking, `Ui` screen state | `game_engine/src/player.rs` |
| Health, hunger, death, dropped items | `game_engine/src/survival.rs` |
| Mob spawning, AI, bodies, animation, loot | `game_engine/src/mobs.rs` |
| Inventory / crafting / furnace screens | `game_engine/src/invui.rs` |
| UI palette + button helpers (0.9 look) | `game_engine/src/theme.rs` |
| Title, world list, options, pause, death, settings | `game_engine/src/menu.rs` |
| Sky, sun, fog, day/night, dimension skies | `game_engine/src/sky.rs` |
| HUD (hotbar icons, bars), toasts, tile→icon cache | `game_engine/src/hud.rs` |
| Item voxel models (cache), held item | `game_engine/src/items.rs` |
| Tree felling, falling sand/gravel, sapling growth | `game_engine/src/fell.rs` |
| Resource packs (GeruDoku etc., loaded from `<data>/resourcepacks`) | `game_engine/src/packs.rs` + `data/packmap.tsv` |
| Chests / furnaces (+ saving) | `game_engine/src/invui.rs` |
| Falling leaves / ambient effects | `game_engine/src/fx.rs` |
| Launcher bridge (`window.emberLauncher`) / updater | `launcher/src/main.rs`, `update.rs` |
| CI; releases: EVERY push to main publishes `v0.2.<run>` automatically | `.github/workflows/` |

## Key design facts
- One world, stacked dimensions: Nether y −256..0, Overworld 0..256, Skylands 256..512, End 512..768. Chunks are 16³ cubes; only ±3 layers around the player load.
- Visual style: every block face is 16×16 micro-voxels (colour + relief) from `style.rs`; no texture files. LOD by chunk distance: 16 → 8 → 4 → 1 (far = one colour per face, ores look like stone).
- Resource packs are never committed (licences): players drop zips in `<data>/resourcepacks/`; Options picks one.
- Saves: `<data>/saves/<World>/world.txt` + edited chunks only. `<data>` = `~/.local/share/DokuQuest` (Linux) / `%APPDATA%\DokuQuest`.

## Port status (JS 0.9.0.90 → Rust)
| JS area | Rust | Status |
|---|---|---|
| core/blocks.js, items.js (+TAGS, SMELTING) | generated data + blocks.rs, items.rs | all 1114 blocks, 1632 items, 1082 recipes, 127 smelts |
| render/textures.js, doku*.js atlas | tiles.bin + style.rs | all tiles; each pixel = a micro-voxel |
| render/rigs*.js, game/entities.js MOB_TYPES | mobs.rs (both crates) | 125 mob types + rigs; AI is an alpha subset |
| world/* | chunk/worldgen/villages/world.rs | terrain, 4 stacked dims, caves, ores, trees, villages; JS structures/towns TODO |
| game/player.js, physics.js, input/* | player.rs, controls.rs | movement, step-up, survival, gamepad |
| game/inventory.js, ui/invui.js | items.rs, invui.rs | inventory, 2×2/3×3 crafting, furnace + chests (saved) |
| render/packs.js | packs.rs | Minecraft packs mapped like the JS |
| entities.js felled trees | fell.rs | trees topple; sand/gravel fall; saplings grow |
| ui/title.js, ui.js, hud.js | menu.rs, hud.rs, theme.rs | title/worlds/options/pause/death, HUD; skin editor/creator TODO |
| launcher/desktop | launcher (Tauri) | same page; LAN + app self-update TODO |
| everything else in game/ (ships, magic, redstone, enchanting, farming growth, net, mods…) | — | not started |

## Next chunks (suggested order)
1. Shaders: wind sway for plants/leaves, wave water + shoreline foam, sky-gradient (port idea of TanTan's `bevy_sky_gradient`).
2. Crop growth stages; tool durability; ranged mobs; light propagation (torches, dark caves).
3. Port JS structures (towns, dungeons, ruins) into worldgen.
4. Networking in `dq-server`; mod API (DQ) via scripting.
5. Resource packs mapped onto micro-voxels; settings menu (render distance, detail, potato preset).
