# DokuQuest C++ — map for AI sessions

DokuQuest is being rebuilt in C++ as close as possible to the original HTML/JS game
(DokuQuest Alpha 0.9.0.90), with these agreed changes:
- Worlds stacked in ONE seamless column, each 256 tall: Aether/Skylands (top, y 768–1023), Overworld
  (512–767), Nether/Ember Depths (256–511), The End (0–255). No loading screens anywhere.
- True portals: seamless walk-through and a live window to the other side.
- Distant Horizons / Voxy style LOD ("see forever"), well optimised.
- Everything is voxels (Veloren / Cube World / Lay of the Land): blocks are 32×32×32 micro-voxels coloured
  pixel-for-voxel from the tiles (GeruDoku resource pack when installed, else the original code-painted
  Doku tiles); items are real 3D voxel models that resemble their pack sprite; characters are voxel
  models built from a 64×64 skin or from the voxel character creator. Waving grass/foliage, waves,
  waterfalls, particles for everything.
- Sounds are generated on the fly, resynthesised from analysed profiles of the resource-pack sounds
  plus the original game's synthesised sounds. No audio files are shipped.
- Renderers: Legacy (OpenGL 3.3 core, potato PCs) and Modern (OpenGL 4.6/4.5); Vulkan later.
- Mods built in (Fabric + Forge + OptiFine idea): JS mods with the original DQ API (QuickJS), data
  packs, native plugins, shader packs, resource packs.
- The dedicated server and the Mod Studio live in the LAUNCHER, not the game.

The original source is the source of truth for behaviour, numbers and UI. Before writing a system,
READ the matching JS module(s) and copy them. The user's rules: no hunger (the original has none:
regen 1 HP / 4 s, food heals); full controller support in every menu; patch notes never mention
Herobrine and always end with "Removed Herobrine."; no phantoms, ever.

## Where the original lives (in this session)
- JS source: `/home/claude/src_in/DokuQuest-0.9.0.90/src/js/**` (+ `src/styles.css`, `src/index.html`,
  `launcher/desktop/launcher.html`, `launcher/news.html`, `notes/roadmap.md`)
- Resource pack: `/home/claude/pack_in/assets/minecraft/**` (GeruDoku 1.19.3, 32px). NEVER commit pack files
  (third-party art) — the game loads packs from `userDir()/resourcepacks/` at run time.
- Extract any JS data: `python3 tools/port/jsdump.py /home/claude/src_in/DokuQuest-0.9.0.90 out.json "<JS expr>"`
  (runs the whole original game headless; the expression can use any top-level const/function).

## Build & test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build -j2
# headless run (Xvfb + Mesa): SDL here only has EGL, so force it
Xvfb :99 -screen 0 1280x720x24 & export DISPLAY=:99 SDL_VIDEO_X11_FORCE_EGL=1
./build/dokuquest --userdir /tmp/ud --create Test --seed hello --smoke-frames 300 --smoke-shot /tmp/shot.png
./build/dokuquest --renderer legacy ...   # the GL 3.3 path
```
Only 2 CPU cores: compile single files while iterating (`ninja -C build CMakeFiles/dqcore.dir/src/gen/worldgen.cpp.o`).
Third-party code is in `third_party/` (SDL2, imgui, glad, miniaudio, enet, quickjs-ng, stb, glm, miniz,
nlohmann json) — `tools/fetch_deps.sh` fetches it on a clean checkout.

## Layout (one feature per file; keep files < ~1500 lines)
| Dir | What | Links |
|---|---|---|
| `src/common` | types, logging, fs/json/zlib, settings, thread pool, noise (exact util.js port) | — |
| `src/core` | registry (blocks/items/recipes/tags/mobs from `data/*.json`), ItemStack, `initCore()` | common |
| `src/world` | Chunk (16×256×16 band, 16 sections), World (streaming, blocks in global y, saving), light, regions | core |
| `src/gen` | world generators (DimGenerator per band), structures | world |
| `src/sim` | Level (realms, entities, time, systems), Entity, Player, physics, events, gameplay systems | world |
| `src/net` | ENet protocol, host/guest | sim |
| `src/mod` | mod loader, QuickJS DQ API, native plugin API | sim |
| `src/server` | DedicatedServer (used by launcher + `dokuquest-server`) | sim, net |
| `src/render` | GL backend, mesher, world/LOD/entity/portal/sky/water/particle renderers | client |
| `src/assets` | tiles (+ resource packs), item voxel models, skin → voxel character, character creator data | client |
| `src/audio` | miniaudio output + procedural synth | client |
| `src/ui`, `src/input` | ImGui screens styled like styles.css, HUD, inventory; keyboard/mouse/controllers | client |
| `src/client` | App (window, loop, screens), GameSession (open world, local player, render layers) | client |
| `src/launcher` | launcher exe: news, play, profile/skin, options (renderer), mods, server, Mod Studio, updater | client |
`dqcore` = common/core/world/gen/sim/net/mod/server (no GL; the server links only this).
`dqclient` = render/audio/ui/input/assets/client. CMake globs `*.cpp` per directory — new files need no CMake edit.

## Key design facts
- Coordinates: x/z unbounded ints, y global 0..1023. `dimOfY(y)`, `dimBase(d)`, band-local `y & 255`.
  World::getBlock/setBlock take GLOBAL y. Chunk accessors and generators use BAND-LOCAL y.
- Chunk = one dimension band (16×256×16) like the JS chunk (`colIdx` = `(y<<8)|(z<<4)|x`), stored as 16
  sections (nullptr = all air). meta u8 per block (same meaning as the JS meta), light = sky<<4|block.
- Overworld sea level is band-local 64 (the original was 48 in a 128-tall world; generators scale the
  original terrain to the taller band). See gen/ for the scaling rule.
- Side realms (pocket dimension, his realm) are separate `World`s (`Level::world(realm)`).
- Block ids are runtime-only; saves store keys (palette per chunk), so adding blocks never breaks saves.
- Global registration: NEVER rely on static constructors in other .cpp files (static-lib linking drops
  them). Expose `registerXxx()` and call it from `initCore()` (core/init.cpp) or the client equivalent.
- Threads: generation and meshing run on `ThreadPool` workers. Workers never touch `World` maps; they
  get copies (MeshInput) or produce new Chunks that the main thread inserts.
- Render passes per frame (GameSession::render): sky → opaque sections → cutout → RenderLayers opaque →
  translucent sections → RenderLayers translucent → overlay (hand). Add features as `RenderLayer`s
  (`registerRenderLayer`) instead of editing session.cpp.
- Shaders: `assets/shaders/*.glsl`, one file holds both stages (`#ifdef VERTEX` / `#ifdef FRAGMENT`),
  GLSL 330 subset, `#include "common.glsl"`. Version line + `DQ_MODERN` are injected. Shader packs override files.
- Gameplay events go through `EventBus` (sim/events.h) with the original DQ mod API names.
- Settings: `common/settings.h` (original option names + engine additions), saved in userDir()/settings.json.
- Smoke test before every push: create a world, run frames, no crash, screenshot looks right.

## Rules for AI sessions (token budget — the user pays per use)
- Read only what your task needs, but DO read the original JS for the system you port.
- Targeted edits; never reprint whole files you didn't write.
- If a build error survives 2 fix attempts, stop and report it.
- Don't take screenshots for the user; use them only to check your own work.
- Update `docs/PORT_STATUS.md` when you port a JS module.
