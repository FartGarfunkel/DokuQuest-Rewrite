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
| `crates/launcher` | Auto-updater, bin `dokuquest-launcher` | needs crates.io (CI) |

## Where things live
| Task | File |
|---|---|
| Add/edit a block (colours, look) | `common_server/src/blocks.rs` (one line) |
| Change a block's micro-voxel pattern | `common_server/src/style.rs` (`cell`, `far`) |
| Terrain, caves, ores, trees, plants | `common_server/src/worldgen.rs` |
| Add/move a dimension (stacked Y bands) | `common_server/src/dims.rs` + generator in `worldgen.rs` |
| Meshing, LOD detail, plant/item voxel models | `common_server/src/mesh.rs` |
| Save format, data folder | `common_server/src/save.rs` |
| Chunk streaming, LOD rings, edits, autosave | `game_engine/src/world.rs` |
| Movement, collision, break/place, hotbar | `game_engine/src/player.rs` |
| Title screen + rotating panorama | `game_engine/src/menu.rs` |
| Sky, sun, fog, day/night, dimension skies | `game_engine/src/sky.rs` |
| HUD, toasts | `game_engine/src/hud.rs` |
| Held items (Cube-World voxel sprites) | `game_engine/src/items.rs` |
| Falling leaves / ambient effects | `game_engine/src/fx.rs` |
| Update check / download / launch | `launcher/src/main.rs` |
| CI / releases (publish a release, tag `vX.Y.Z`, e.g. `gh api repos/FartGarfunkel/DokuQuest-Rewrite/releases -f tag_name=vX.Y.Z`) | `.github/workflows/` |

## Key design facts
- One world, stacked dimensions: Nether y −256..0, Overworld 0..256, Skylands 256..512, End 512..768. Chunks are 16³ cubes; only ±3 layers around the player load.
- Visual style: every block face is 16×16 micro-voxels (colour + relief) from `style.rs`; no texture files. LOD by chunk distance: 16 → 8 → 4 → 1 (far = one colour per face, ores look like stone).
- Saves: `<data>/saves/<World>/world.txt` + edited chunks only. `<data>` = `~/.local/share/DokuQuest` (Linux) / `%APPDATA%\DokuQuest`.

## Port status (JS 0.9.0.90 → Rust)
| JS module | Rust | Status |
|---|---|---|
| core/blocks.js | blocks.rs | 26 core blocks; rest to port |
| world/worldgen*.js | worldgen.rs | new terrain + 4 bands; villages/structures TODO |
| world/mesher.js | mesh.rs | rewritten for micro-voxels |
| world/chunk.js, world.js | chunk.rs, world.rs | done (cubic chunks) |
| game/player.js, physics.js | player.rs | basic; survival rules TODO |
| ui/title.js | menu.rs | panorama + continue/new; world list/options TODO |
| render/items3d.js | items.rs | sword + block items; port the rest as sprites |
| save/storage.js | save.rs | new format; JS world import TODO |
| everything else (mobs, inventory, crafting, villages, magic, redstone, net, mods…) | — | not started |

## Next chunks (suggested order)
1. Shaders: wind sway for plants/leaves, wave water + shoreline foam, sky-gradient (port idea of TanTan's `bevy_sky_gradient`).
2. Inventory + crafting + survival break times; port `core/items.js`.
3. Mobs + AI; then villages/structures in worldgen.
4. Networking in `dq-server`; mod API (DQ) via scripting.
5. Resource packs mapped onto micro-voxels; settings menu (render distance, detail, potato preset).
