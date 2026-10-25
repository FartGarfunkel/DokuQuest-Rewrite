# Changelog

Release notes for the C++ versions of DokuQuest, newest first. The release workflow
(`.github/workflows/release.yml`) publishes the section whose heading starts with the tag's version as the
GitHub release notes. Keep it in step with `assets/news/news.dqn` (the launcher's news). Patch notes never
mention Herobrine and every entry ends with "Removed Herobrine."

## 1.0.0-alpha.1 — The C++ Rewrite (October 2026)

DokuQuest is being rebuilt as a native C++ game, as close as possible to Alpha 0.9.0.90, with no browser underneath.
- **One seamless column of worlds:** the Skylands, the Overworld, the Ember Depths and The End are stacked 256 blocks each, with no loading screens between them.
- **Everything is voxels:** every block is 32×32×32 little voxels coloured from its texture, so the GeruDoku resource pack (or the built-in Doku tiles) gets real depth.
- **Two renderers:** *Modern* (OpenGL 4.6) and *Legacy* (OpenGL 3.3) for older PCs. Vulkan comes later.
- **A new launcher:** the news, your worlds, your player name and Play, now as a native program. Options choose the renderer and a graphics preset; the launcher looks at your graphics card and suggests one.
- **The dedicated server lives in the launcher:** pick or make a server world, set the port, players, MOTD, mode, difficulty and password, and watch its console with TPS and players.
- **The Mod Studio is built in:** write DQ mods with templates and a syntax check, switch installed mods on and off, make data packs (blocks, items, recipes) without code, and browse the engine's source.
- **Mods, like Fabric, Forge and OptiFine together:** JS mods with the original `DQ` API, data packs, native plugins, shader packs and resource packs.
- **Updates from GitHub Releases:** the launcher checks for new versions, downloads and checks them, and keeps the previous version in case the new one won't start.
- Linux AppImage (and a tar.gz). A Windows build comes later. Your worlds are kept in the game folder.
- Removed Herobrine.
