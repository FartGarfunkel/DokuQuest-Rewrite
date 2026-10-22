# DokuQuest (Rust rewrite)

The DokuQuest voxel sandbox rebuilt in **Rust + Bevy**, with a fully voxel look: no textures, every
block is made of 16×16 micro-voxels styled after Doku/GeruDoku colours (Ace of Spades / Lay of the
Land feel), plants are tiny voxel models, held items are Cube-World-style voxel sprites.

## Why Rust + Bevy
- **Rust over HTML/JS:** world gen and meshing now run on every CPU core, native GPU instead of WebGL
  limits, much smaller memory use, and one codebase for game, server and launcher.
- **Bevy over a from-scratch engine:** windowing, input/gamepads, audio, UI, shadows, fog and asset
  loading come for free; its plugin/ECS layout keeps each feature in its own file (cheap to work on
  in small chunks). All hot voxel code (worldgen, meshing) is plain Rust in `common_server`, so it is
  engine-independent and fast to test.

## Play
Download `dokuquest-launcher-<platform>` from Releases and run it — it installs and auto-updates the
game. Or build: `cargo run --release -p game_engine`.

Controls: WASD, mouse look, Space jump, F fly, Ctrl sprint, Shift descend (flying), LMB break,
RMB place, 1–9 / wheel hotbar, Esc save & return to title.

## Layout
See [CLAUDE.md](CLAUDE.md) for the file map, design facts and port status.

## Release
Publish a GitHub release with a new tag (e.g. `v0.1.1`) → GitHub Actions builds Linux + Windows and
attaches the assets the launcher looks for.
