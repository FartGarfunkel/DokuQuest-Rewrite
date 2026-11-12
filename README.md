# DokuQuest (C++)

DokuQuest rebuilt as a native C++ game, as close as possible to the original HTML5 game (Alpha 0.9.0.90),
with a stacked seamless world (Skylands, Overworld, Ember Depths, The End — 256 blocks each, no loading
screens), true see-through portals, Distant-Horizons-style far terrain, 32³ micro-voxel blocks coloured from
the GeruDoku resource pack (or the built-in Doku tiles), 3D voxel items and characters, sounds generated on
the fly, and mods built in (JS mods with the original DQ API, data packs, native plugins, shader packs).

## Play (Linux)
Download `DokuQuest-<version>-x86_64.AppImage` from the Releases page, make it executable
(`chmod +x`, or Properties → Allow executing) and run it. It opens the launcher: news, Play, options
(renderer: Modern OpenGL 4.6 or Legacy OpenGL 3.3 for older PCs; graphics presets from potato to ultra),
the dedicated server and the Mod Studio.
Resource packs go in `~/.local/share/DokuQuest/resourcepacks/` (a GeruDoku pack there is picked automatically).

## Build
```
tools/fetch_deps.sh                 # third-party sources (SDL2, Dear ImGui, miniaudio, ENet, QuickJS-ng, ...)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build                      # dokuquest, dokuquest-launcher, dokuquest-server
```
See `CLAUDE.md` for the code map and `docs/` for the port notes per area. The previous Rust + Bevy
attempt is kept on the `rust-bevy-archive` branch.
