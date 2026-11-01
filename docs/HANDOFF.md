# DokuQuest — session handoff (2026-11-01)

Start here in a new session. Read this file, `CLAUDE.md` (the code map), and only the files a task needs.
The full feature list is `docs/MASTER_PLAN.md` (the user's master doc, V2); the BTA detail is `docs/BTA_GAP.md`; the
Herobrine lore and ideas are `docs/HEROBRINE_LORE.md`.

## How the user wants us to work (budget first)
- **Save usage, not time.** One stream of work, no parallel agents. Targeted reads and edits. **No screenshots
  unless the user asks for one** (2026-10-31: visual problems get caught in the playthrough bug-test phase).
  Automated, text-only checks (smoke scripts with `expect`/`report`).
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
- **Branches:** latest release is **alpha.7** (main = `chisel` at b444e69; released from c734ba8). Everything since is on
  `chisel` (see "Since alpha.7" below), not released yet: next release is **alpha.8** (one per day, not on Oct 31).
- **Build:** a fresh session has no deps or ccache: `bash tools/fetch_deps.sh`, then `cmake -S . -B build -G Ninja
  -DCMAKE_BUILD_TYPE=Release` and `ninja -C build -j2 dokuquest` in the background (~25 min first time, 2 cores).
  Never edit a .cpp while it's being compiled; touch edited files and rebuild if unsure.
- **Headless test:**
  - Run Xvfb on :90 if it isn't up: `Xvfb :90 -screen 0 1280x720x24 &`
  - `DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 ./build/dokuquest --userdir /tmp/x --create T --seed hello --smoke-script s.json`
  - Ops: wait, cmd, anchor, fill {rel,from,to,key,meta}, setBlock, tp, lookAt {rel x,y,z | entity}, give, select,
    mouse {button,hold}, expect {block,is | entity,near}, hurt {entity}, key, pad, shot, report, quit.
  - Gotchas: `/summon` puts creatures 3 blocks ahead; use reach is ~4.5 blocks; `/kill` kills the PLAYER; tp rel
    first so the player stands at a block centre.
  - Playtests in `tools/playtests/`: chisel, eggs, lavaboat, heads, pumice, paint, slabtorch, lead, jar, sensor,
    sit, sieve, journal, portal_size. Slow on llvmpipe: run long ones with `run_in_background` and a 300-400 s
    timeout (a script hit by timeout prints nothing). `/clear` after `fill` (fills drop picked-up plants); time
    names: `/time set midnight|noon`. `/locate <structure>` prints coordinates (e.g. end_city).
  - Data is copied into build/ by ninja (`dq_runtime_data`): rerun ninja after changing data/ before a test.
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
  (their tree, with `tools/_import_*.py`). Checked 2026-10-31 against their `Chisel_wip` zip: its sounds.json/.bin
  are EXACTLY the repo's set plus 5 C418 profiles appended to the end of the blob (06, 07, 08, 13, 18 = events
  `music.mc_*`, never played by code). The repo's set is that file with them cut off: nothing else to take.
  Only other differences: their file maps wolf howl -> death sound, duck death -> splash, horses -> ghost/undead
  variants; alpha.5.9 changed those on purpose (asked the user whether to restore; no answer yet).
- `fx.pumpkin_carve` (added 2026-10-31) plays their `block/pumpkin/carve1-2` when a pumpkin is carved.

## The chisel work (released in alpha.7)
**Done:** the user's chisel merge (carve byte = 2×2×2 octants, `iron_chisel`, music instruments, 529 pictures),
mesher fix (blocks on paths / floating blocks vanished), chains visible, slabs and stairs are chiseled blocks
(`src/world/carve_forms.*`), torches/lanterns/plants fix only to a whole face and drop when it's carved
(`world/carve_faces.h`, `onCarveChangedRules` in sim/blockfx.cpp), path -> dirt under a solid block, jack o'lantern
sounds like wood, chisel uses the block's own sound.

**Still open:**
- Waterlogging chiseled blocks (waterlogged slabs/stairs stay as the old blocks until then).
- 2×2 / 4×4 / 8×8 chisel resolution: needs sparse sub-voxel storage beyond the carve byte, plus mesher, physics,
  pick and save support.
- "Campfire with cobblestone texture": not reproduced with the base pack; ask the user for a save or which pack.
- The crosshair raycast treats a chiseled block as its full cell (you can't click through a notch).

## Since alpha.7 (on `chisel`, 2026-10-31 evening) — goes into alpha.8's notes
Fixes from the user's play:
- Chiseled blocks lit properly (world/light.cpp: a carved opaque block takes light in but passes none on; setCarve
  notifies the light engine). Torches/lanterns/carpets sink onto slab tops; torches and frames fix to a vertical
  slab's inner face (`carveFaceDepth` in world/carve_faces.h; `slabGap`/`sinkOffset` carve-aware). Clicks on chiseled
  blocks hit the aimed face (physics.cpp raycast rounding).
- Flags reach their fence poles (mesh_shapes.cpp cloth reach). End rods drawn from MC-layout picture (ShapeBox
  `tu`/`uvT`). Grey vines/ferns/leaves: their pictures lost the alpha-253 tint marker (data_patches.py restores it).
- Sitting: `ridingOf` (sim/vehicles.cpp) cleared `p.riding` for anything not a vehicle (seats, mounts). Sitting on
  chiseled slabs/stairs too (seatAt).
- Town jack-o'-lanterns face the street; FacingFront meta is 0 S, 1 E, 2 N, 3 W, so `(d(f)+2)&3` was wrong for
  E/W: towns use `frontMeta(f) = (2 - d(f)) & 3`; SP::chest fixed likewise.
- Portals built on the far side match the entry portal's opening size and axis (arrivePortal ow/oh/axis).
Features (BTA list done except seasons; #11 End and Skylands done):
- Nether: Cinder Isles, Emberglass Groves, Ashen Wastes, Sulphur Springs (sulphur + charcoal -> gunpowder).
- Overworld: Thornscrub (thorn trees, thorn leaves prick like cactus).
- Lead (sim/leads.cpp, rope drawn in render/content_layers.cpp), glass jars + fireflies (sim/jars.cpp), iron mesh
  (items fall through: MoveParams::item), motion sensor (redstone plate kind "motion"), sieve (sim/sieve.cpp),
  Travel Journal (sim/travel_journal.cpp + ui/travel_journal.cpp).
- End: outer lands (spire fields, crater barrens, violet moors of end moss, arches, floating shards; gen/theend.cpp),
  new End cities (gen/structures_far.cpp: hall, storeyed tower, crowns, bridges to hanging towers, ship with dragon
  head + elytra frame), drifting violet motes.
- Skylands: tapered undersides, glimmersand rims, high islets, cloud banks (cloud_puff soft, bounce_cloud launches,
  sun_cloud slow-fall), sunwood trees, sky blooms, breeze motes.
- New blocks/items are generated by `tools/port/bta_blocks.py` / `data_patches.py` (idempotent; run
  `python3 tools/port/data_patches.py data`).
- Mobs: shulkers (End cities, sim/end_shulkers.cpp spawns them once per city; homing "shulker" bolt + levitation
  effect), the Wither (sim/wither.cpp: summon on the third wither skeleton skull; "wither_skull" projectile, "wither"
  effect), biome fuselings (frost/dune/bog: fuseBurst variants), biome village golems (npcSpawn by village style).
- Feel: Minecraft elytra glide (sim/elytra.cpp, 20 Hz steps), pearls into End gateways, Vintage Story ingot piles
  (render/pile_layer.cpp "solo"), held torch flame + smoke (render/content_layers.cpp heldFlames).
- alpha.8 notes are drafted in docs/CHANGELOG.md and assets/news/news.dqn ("Far Lands"); bump DQ_VERSION to
  1.0.0-alpha.8 on release day, merge chisel -> main, wait for CI, release.
Later the same night (all in the alpha.8 notes except the rift work, which the notes must never hint at):
- Leather/pelts/hides/saddles sculpt "thin" (assets/item_models.cpp sculptKind); flat things keep a sliver.
- Chiseled blocks hold water (WLOG meta bit on carved blocks; build.cpp bucket/afterBreak; light/mesher emitLiquid).
- Coloured + inverted lamps, metal bricks, butterfly jar, glass door/trapdoor, bluebells/marigold/spinifex, sugar
  cane bundle, hand sieve, the steel set (door/trapdoor need redstone: block prop `rsOnly`; `blastproof` skips
  explosions in sim/explosion.cpp).
- Particles: sprint crumbs, landing ring, crit stars, creature dust, breath fog where snow falls (controller.cpp
  breathAcc, particle "breath"), snow/sand/gravel puffs per step.
- Magic hands (sim/magic_hands.cpp): left-hand spell `stats.spellOff` (spellbook L / X / right-click); dual cast,
  mixes + fusions (gazePoint burst), sword/axe casts the left spell and sets `MagicState::bladeEl` (combat.cpp
  spellbladeHit). Spell ranks I..X: `stats.spellXp`, `spellRankPower/Mana` applied in castSpellKey (old castSpell
  body, sim/magic_spells.cpp; `handMods()` carries the hand's power/mana/noStaff). `/spell learn|right|left|cast`.
  Old spells (OLD/ARCANA) need a staff in that hand. Test report shows "spells R .. L .. mana .. blade ..".
- Tool Forge (tools/port/forging.py): heads `<mat>_<tool>_head` (12 materials x 5 tools, item prop `forgeHead`),
  made in the forge block (stonecutter screen with InvOpen.kind "forge": sim/stations.cpp forgeOptions/forgeCost);
  head + haft shapeless recipes -> tool (stick = the existing tool where there is one, else `<mat>_<tool>`;
  `<haft>_hafted_<mat>_<tool>` for bone/cinder/steel). Creative tab "Forged". Icons: iron tool picture split into
  head/haft by colour (outline pixels take the nearest colour's side), recoloured.
- Rift story (Phase 11), on `chisel`: RIFT_NEED 10 (sim/endstory.h); stability = 100 - 92*frags/10 paces the
  haunting; the old fog (render/sky.cpp `f.nearFog` -> `uNearFog` in common.glsl applyFog, layered over the far fog)
  in dread and on nights once stability < 75; corruption (rift.cpp corruptTick: data `corrupt`, judged once per
  creature via `rc`; purple flicker + white eyes in render/creature_layer.cpp; animals stare and vanish within 5);
  stripped groves and ocean sand pyramids (haunt.cpp sandPyramid); watcher glitches by stability
  (render/endstory_layer.cpp); the fortress scene (rift.cpp riftFortress: a tear, him beside it, wither skeletons
  walk in); the final boss fights at range (teleports away under 6 blocks, circles, more bolts, no melee).
  Not done from Phase 11: the player-triggered Wither Storm climax.
Then (user asked mid-session, all on `chisel`, in the alpha.8 notes):
- Branching trees (user's request, Cubyz idea only, our own code): gen/trees.cpp `branch()` (sideways logs with axis
  meta 1/2, climbing, forks, leaf clumps; writeTreeParts keeps a part's meta for axis blocks). Big oaks (h > 0.78),
  dark oak, jungle, cherry, sometimes oak/birch; bare trees keep branches. Giant rainforest trees (jungle h > 0.93,
  2x2). Saplings now grow the generator's shapes (sim/worldsys.cpp growTree used treeParts; hooks().growTree was
  never set). Palms (TREE_PALM: beaches, thornscrub; palm wood/leaves/sapling on sand; coconuts from felled palms).
- Quiver (sim/quiver.cpp, tag "arrows", bow draws offhand -> quiver -> loose). Paintings (render/painting_layer.cpp:
  30 procedural pictures; block entity "painting" n; use cycles).
- Music frequency setting (Settings::musicFreq 0..3, AudioEngine::Impl::musicFreq paces the director's wait).
- In-game Settings re-laid like the launcher's Options (ui/options.cpp Row kit, two columns over 760 px; sections
  Graphics, Packs, Sound, Controls, Game). ui::slider got an optional width.
- Music Studio (launcher/music_studio.cpp, Mod Studio page "music"): audio/song.h/.cpp (Song JSON in
  userDir()/music, SMF in/out, analyseRecording: spectral-flux comb tempo + phase, Krumhansl key with relative-minor
  check, triad per bar; songFromMood; songFromAnalysis: our own composer, styles calm/folk/epic/dreamy).
  AudioEngine::audioClock/musicNote/musicClear. Songs with a realm play in that land (music.cpp realmSong, 40%).
  build/test_song <wav> checks it (synthetic Am F C G at 100 bpm -> 99.5, A minor, right chords).
Done since then: the Wither Storm (rift_core), statue, basket, activator, siphon, handcannon and the steel set.
The studio has not been heard (no sound device here). Item 2 of the list below (crowds) was already in: crowdPush in
sim/creature_spawn.cpp.
Then (2026-10-31 night, the user asked for a faster pace, all on `chisel`, alpha.8 notes updated):
- Sound Studio (launcher/sound_studio.cpp, Mod Studio page "sounds"): list/filter, waveform, pitch/gain/trim/fades/
  low-pass, "Another take", normalise, import WAV/MP3/FLAC (ma_decode_file), save userDir()/sounds/<name>.wav
  (AudioEngine::playSamples previews).
- Texture Studio (launcher/texture_studio.cpp, page "textures"): tile list with atlas thumbnails (Blocks/Items/Yours),
  pencil/fill/picker/eraser, mirror, grid, undo/redo, palette from the tile, cube + tiled preview, PNG import
  (fitTile), "Use in the game" = userDir()/textures/<tile>.png, laid over any pack by assets/packs.cpp
  applyUserTiles (keeps the 253 biome-tint marker where the base tile had it). Controller paint mode like the music
  studio. DQ_TEX_OPEN=<tile> opens one (tests). applyResourcePack now holds a mutex (two loader threads).
- Grass grows back (sim/worldsys.cpp grassTick: Minecraft's 4 tries in 3x5x3, every column ~once a minute, looks
  through leaves/logs). Game rule `randomTickSpeed` (number, default 1) scales all random ticks. Playtest grass.json.
- Doors/gates/trapdoors swing (render/swing_layer.cpp): World::changeOldMeta tells the open bit flipped; the block
  is left out of the mesh (render/mesher.h meshHiddenBlocks, applied in gatherMeshInput) for 0.22 s and drawn as
  turning boxes (render/tile_box.h drawTileBox/tileTexture). DQ_SWING_SLOW=1 slows it 25x (tests).
- Elytra ride the weather's wind (sim/elytra.cpp). Test op `wear` now fills Player::cape.
- Held torches burn (render/held_light_fx.cpp): first person reads HeldItemLayer::torchTip (set in drawOverlay).
- The End's ambience (audio/audio.cpp Drone loop + ambience.cpp far ender_tp).
- Hardcore (rule `hardcore`, New World mode "Hardcore"): death screen offers Spectate / Title screen (ui/hud.cpp).
- Sorting (InvSession::sortGroup): middle click or R3 over bag/chest/backpack slots; hotbar untouched.
- /playsound and /particle (sim/gamecommands.cpp).
- Nightmares (rule `nightmares`, in World settings): asleep in the dark (block light < 8) in the Overworld, 30% a
  zombie/skeleton/spider spawns 2-5 blocks off and wakes everyone (sim/blockfx.cpp tickSleep). The user: the toggle
  must NEVER affect the story's dreams: riftDream (sim/rift.cpp) runs on every morning skip regardless (rift open,
  not sealed; chance grows as stability falls). Dreams stay out of the patch notes.
- Hotbar rows (BTA): Tab (Act::HotbarRow; PlayerList lost its unused Tab default) turns the bag rows into the hotbar.
- December (started, then the user said: finish the core list first, holidays later): common/holiday.h christmas()
  and newYear() (DQ_EVENT=christmas|newyear); villagers' winter caps and scarves (render/halloween_costumes.cpp
  HW_WINTER_CAP/HW_SCARF, render/villager_layer.cpp); wrapped chests (assets/holiday_tiles.cpp); lights under the
  eaves and a decorated tree on the square (render/winter_village_layer.cpp, render-only). NOT done: carols in the
  music director (public-domain melodies), New Year fireworks over villages, a brighter tree. Not in the notes yet.
- Answered the user: items are already true voxel models; "detail blocks" (any block a 16^3 grid of 6.25 cm voxels:
  storage, mesher, collision, picking, water, chisel) is the recommended path if they want 6 cm building; a full 6 cm
  world is not. Waiting for their answer (16 or 32 per block).
- The full playtest run (/tmp/pt/run.sh pattern: every tools/playtests/*.json with --create P) shows all.json and
  containers.json are stale: they expect a world called T1 (`open`), the old pad-Y quick move (Y now closes the
  inventory if nothing was done yet) and the old menu tab order.

## Agreed order after the chisel (features first, then structure and GUI)
1. **Quick wins.** Done in alpha.7: spawn eggs (`tools/port/spawn_eggs.py`, creative "Spawn Eggs" tab, never one for
   the story's figure), pack skins for enderman/ghast/fuseling (render/creature_models.cpp packMobs; `PMSpec::box` =
   Minecraft boxes in model space), ghast `#shooting` model while charging, angry enderman lifts its head off a jagged
   dark jaw (models.json `jaw` part via data_patches.py; the user REJECTED white teeth), enderman legs ±2 px, the
   placing loop (item_layers.cpp), lava boat. NOTE: the fuseling's runtime rig is `creeperModel()` in
   sim/hallowmobs.cpp, not data/models.json. Left:
   - bees (AI, hive tracking, flying)
   - rare mob/wither skull drops plus wearable/placeable heads
   - capes and cloth moving in the wind
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
