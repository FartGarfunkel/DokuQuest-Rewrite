# DokuQuest — session handoff (2026-10-31)

Start here in a new session. Read this file, `CLAUDE.md` (the code map), and only the files a task needs.
The full feature list is `docs/MASTER_PLAN.md` (the user's master doc, V2); the BTA detail is `docs/BTA_GAP.md`; the
Herobrine lore and ideas are `docs/HEROBRINE_LORE.md`.

## How the user wants us to work (budget first)
- **Save usage, not time.** One stream of work, no parallel agents. Targeted reads and edits. Few screenshots
  (crop and downscale before viewing). Automated checks over pictures.
- **Releases:** one release per day, not one per fix. Ask the user to batch bug reports.
- **The user's standing rules:**
  - no hunger, no phantoms
  - every menu works with controller AND keyboard/mouse
  - patch notes never mention or hint at Herobrine and always end with "Removed Herobrine."
  - releases ship the Linux AppImage + tar.gz and the Windows zip
  - pack and folder names plain (`[A-Za-z0-9_ .-]`); Windows `copy_directory` broke on `[brackets]`
- **Decisions (2026-10-31):**
  - Stay on C++.
  - Cubyz (Zig, GPL-3) is ideas only: write our own code, never translate it. The user may later choose GPL; ask
    before copying anything.
  - Horizon (Rust, MIT) can be used with a credit line in `docs/CREDITS.md`.
  - No C418 tracks or profiles in the game.
  - WizardGenie is not used.
  - Structure refactor and the engine GUI (with the user's Strata AI) come AFTER all features and fixes are in.

## Where things are
- **Repo:** `/home/claude/dq` (GitHub FartGarfunkel/DokuQuest-Rewrite).
- **Branches:** latest release is **alpha.6.1** (main). Work in progress is branch **`chisel`** (d05c83b), not
  merged to main yet.
- **Build:** `/home/claude/dqbuild.sh /home/claude/dq dokuquest` (ninja -j1 gate + ccache). Never edit while a build
  of the same tree runs.
- **Headless test:**
  - Run Xvfb on :90 if it isn't up: `Xvfb :90 -screen 0 1280x720x24 &`
  - `DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 ./build/dokuquest --userdir /tmp/x --create T --seed hello --smoke-script s.json`
  - Ops: wait, cmd, anchor, fill {rel,from,to,key,meta}, tp, lookAt, give, select, key, pad, shot, report, quit.
  - Block keys are the game's own, e.g. oak planks = `planks`.
  - Gameplay harness: `dq-test-gameplay`.
- **Releases:**
  1. Bump `DQ_VERSION` in CMakeLists.txt.
  2. Write notes in `docs/CHANGELOG.md` and `assets/news/news.dqn`.
  3. Push main and wait for CI on the SHA (ci.yml).
  4. `git push -f origin SHA:refs/heads/release`. That runs release.yml, which publishes; publish re-runs now
     upload onto an existing release.
  5. If it fails, `gh run rerun --failed` once.
  - Versions: alpha.7 next, then alpha.7.1, ... Never alpha.N.10, because GitHub sorts it below .9.
  - Session limits: we can't delete or edit releases from here. GraphQL is blocked (use the REST `gh api`).
  - `pkill -f` on a pattern that appears in your own command kills your shell: use `[x]` tricks, or run it alone.
- **Reference sources** (re-extract if /tmp was wiped):
  - Horizon: `/mnt/user-data/attachments/cf5aa8f7-9a50-50d5-8270-c5644c73a5ee/88ea2ea7-rust-main.zip`
    → `/tmp/ug/ref/rust/rust-main`
  - Cubyz: `.../1e13bad2-Zig_src.zip` → `/tmp/ug/ref/zig/src`
  - Original JS: `/home/claude/src_in/DokuQuest-0.9.0.90/src/js`
  - GeruDoku pack: `/home/claude/pack_in`
- **Music references:** the user's three examples in the same attachments folder (`*Dawn_in_the_Voxel_Valley.mp3`,
  `*The_View_From_High_Ground.mp3`, `*Warm_Blocks_FULL_SONG_musicgpt.mp3`). Measured: ~3 min each, warm and
  bass-heavy (over half the energy below 120 Hz, little above 2 kHz), gentle dynamics, slow-to-mid tempo, keys
  F♯ major / F major / C minor. Direction: Yáhno-style dulcimer melodic EDM plus a C418-like sparse feel,
  Overworld like the examples, every realm its own feel.

## Sounds: where the user's sound work lives (all committed in the repo, main and chisel)
- **`data/sounds.json` + `data/sounds.bin`:** the user's sound set as resynthesis profiles (1,125 sounds, 284 events:
  GeruDoku plus their imported creature and block sounds, merged in alpha.5.9 with the mob fixes: wolf howl, duck
  death, horse). Format: `sounds` = profile index into the `raw` blob in `sounds.bin`; `events` = "mob.<name>" /
  "<material>.<kind>" / "fx.<name>" -> [[file, pitch, vol]...]. Loaded by `src/audio/resynth.cpp` (PackSounds).
- **`data/sfx.json`:** the original HTML game's synthesised sounds (recipes per sound name + material), the fallback
  when no profile exists.
- **`data/sounds/`:** a placeholder `.txt` per sound (blocks/mobs/effects). A real `.wav`/`.mp3`/`.flac` with the same
  name there, or in `<userDir>/sounds/`, replaces that sound (alpha.5.7).
- **Code:**
  - `src/audio/sfxlib.cpp` resolve order: recording file -> pack profile -> recipe
  - `src/audio/audio.cpp` mixer; under water muffled, cave reverb otherwise
  - `src/audio/music.cpp` procedural music, including the user's new instruments on the `chisel` branch
- **The user's raw sound work and import tools:** `/mnt/user-data/uploads/DokuQuest_C___Rewrite_Sounds.zip`
  (their tree, with `tools/_import_*.py`). Don't take its `sounds.json` (contains C418 music profiles).

## The chisel work (branch `chisel`)
**Done:**
- The user's chisel merge: carve byte (2×2×2 octants) per block, `iron_chisel` item, new music instruments in
  `src/audio/music.cpp` (dulcimer, mandolin, fiddle...), and the user's 529 edited pictures. Sound data stays at
  alpha.5.9 (their sounds.json held C418 profiles).
- Mesher bug fixed: blocks with no plain opaque neighbour were skipped (blocks placed on paths, floating blocks
  vanished).
- `end_crystal` picture added.
- Chains drawn as crossed planes using the picture's link strips (they were invisible), so hanging lanterns connect.
- Slabs and stairs are chiseled blocks now (`src/world/carve_forms.*`): converted on generation, old saves and
  `setBlock`; their items are hidden and their 151 recipes retired. Waterlogged ones stay as the old blocks for now.

**Left to do, before alpha.7:**
- `jack_o_lantern` sound is "glass" in `data/blocks.json`; should be "wood".
- The user reported "campfire with cobblestone texture". Not reproduced with the base pack: check village campfires
  and the user's pack.
- Waterlogging chiseled blocks (like Minecraft stairs).
- Torches, lanterns and items attach to chiseled blocks (support checks).
- 2×2 / 4×4 / 8×8 chisel resolution. Needs sparse sub-voxel storage beyond the carve byte, plus mesher, physics,
  pick and save support.
- Tidy the duplicated comments in the chisel block of `src/sim/build.cpp`; the old stairs→slab trim chain there is
  obsolete.
- Paths: the vanishing was the mesher bug. Optionally also do Minecraft's path→dirt when built on.
- Then merge `chisel` → main and release **alpha.7**.

## Agreed order after the chisel (features first, then structure and GUI)
1. **Quick wins:**
   - mobs: fuseling (creeper) scale and model with the pack skin, enderman angry face, ghast firing face, bees
   - drops and items: rare mob/wither skull drops plus wearable/placeable heads, spawn eggs for every mob
   - player: block-placing loop, capes and cloth moving in the wind, lava boats
2. **Crowds and AI:** no standing inside each other, gentle pushing, slipping through doorways; NPCs avoid wells;
   Skylands mobs don't walk off edges (check aitoolkit's licence); swimming overhaul.
3. **Villages:** gates swing properly, villagers use furniture, church banners; Animal Crossing-style house upgrades
   (bigger, basement, second floor) plus Skyrim-style furnishing; gold coins instead of emeralds.
4. **BTA world** (`docs/BTA_GAP.md`: biomes, blocks, items, structures, Nether), plus the End overhaul (proper End
   cities and ships, Nullscape-style terrain), the Skylands/Aether overhaul, and Horizon's caves and shaders.
5. **BTA mobs:** player-summoned Wither boss, biome fuselings, articulated biome golems.
6. **Feel:** bows (grip/draw/scale), elytra (Minecraft physics, back position, wind), End gateways via ender pearl,
   the particle overhaul, Vintage Story-style item placement (ingots stack 4 across, the next layer turned 90°).
7. **Music:**
   - a studio: import a song, read its tempo, key and chords, generate original tracks, MIDI-style piano roll, MIDI
     import/export
   - realm music sets from the examples
   - the sound overhaul
8. **Gear and magic:** RPG levelling, then procedural mix-and-match tool/weapon/armour crafting (Cubyz idea, our own
   code), then the magic overhaul.
9. **Rift story** (10 fragments, corruption, alpha fog, the watcher, `docs/HEROBRINE_LORE.md`), art unification,
   performance (cave culling, memory diet), Story Mode animation.
10. **Then:** the structure refactor (per-mob files, mob skin variant folders, release archive, game-only updates)
    and the engine GUI with Strata.
