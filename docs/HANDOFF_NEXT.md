# DokuQuest — handoff for the next assistant (November 2026)

You are picking up DokuQuest, a C++20 voxel sandbox RPG (this repository). Read `CLAUDE.md` first (layout, build,
design facts), then this file. It lists the owner's rules, how to build/test/release, what was just finished, and the
queue of agreed work with enough detail to start each item.

## 1. The owner's rules (all of them stand)
- **Patch notes** (`docs/CHANGELOG.md` and `assets/news/news.dqn`, both updated every release): never mention
  Herobrine, except that every entry ends with the line `- Removed Herobrine.`
- **Never name other games, mods or their authors** anywhere in the repo: code, comments, data, docs, UI text. Describe
  mechanics in our own words. Don't copy other games' or mods' art, sounds or text; make our own.
- No hunger, no phantoms, no healing flask (regen 1 HP / 4 s; food heals).
- Full controller support in every menu. Immersive first person shows the player's real body (fix poses in the pose).
- The **DokuQuest Base** resource pack is primary; `data/textures` is only a fallback. New pictures go in both
  (generator scripts in `tools/port/*.py`, run by `tools/port/data_patches.py`, show the pattern).
- "Double check your work. No regression, only progress." The owner especially does not want a broken network
  (multiplayer) release. Run the tests; find root causes, don't loosen tests.
- Commits: message starts with `[claude] ` (keep the prefix convention) and ends with the co-author line used in
  the history. Push to GitHub `FartGarfunkel/DokuQuest-Rewrite`.
- Every release: merge `rewrite-2026-11-15` into `android` with `--no-ff`, push, watch the builds; the Android APK is
  attached to the release. PC and Android must play together.

## 2. Build, test, release
- Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build` (add
  `-DCMAKE_C_FLAGS=-Wno-error=incompatible-pointer-types` if SDL2's PipeWire file fails to compile).
- Tests: `./build/test_<name> --userdir <fresh dir>` (see `ls build/test_*`). Run 3–4 in parallel, but
  **`test_net` alone** (ports) and give **`test_npc_paths`** up to an hour (≈30 min alone). Use `stdbuf -oL` when
  redirecting. Headless game runs need `DISPLAY=:99` (Xvfb) — see CLAUDE.md.
- Branches: work on `moving-structures`; release branch `rewrite-2026-11-15` is only fast-forwarded from it;
  `android` gets `rewrite-2026-11-15` merged `--no-ff`. **All OpenGL ES code lives only on `android`** (phones without
  Vulkan 1.3 use it); engine fixes that touch GLES go there.
- Release: notes + bump `DQ_VERSION` in `CMakeLists.txt` → commit/push `moving-structures` → fast-forward and push
  `rewrite-2026-11-15` → tag `v1.0.0-alpha.N` and push the tag → merge into `android`, build, push → watch
  `gh run list` / `gh run watch`; check the release lists Windows zips, Linux tar.gz, AppImage, APK, SHA256SUMS.
- Traps: MSVC rejects GCC-only syntax (compound literals like `(const char*[]){...}[i]`); CI greps for functions
  declared inside a block and for `"\xB0C"`-style escapes; CI's `python3 tools/check_sounds.py` fails on any sound
  name that doesn't exist. Run these locally before tagging. A broken release: `gh release delete vX --yes
  --cleanup-tag`, fix, re-tag. Runner "lost communication" errors: re-run the job.
- Android local build is possible (`tools/android/build_apk.sh`, needs an Android SDK/NDK and JDK 17); the APK is signed
  with `android/debug.keystore`, so installs update in place and keep saves. Never uninstall to fix signing.

## 3. Just finished
- **alpha.36**: offline time catch-up for farms and machines; farm automation (water streams, piston harvests, the
  crafter, spawn towers at any height, dispenser bone meal, hopper pickup, sculk signals, farmland moisture); all skill
  perks and talent nodes do something; points for deeds (story chapters, first boss kills, hard achievements); skill
  XP from all the hands-on work; paths joined to talent trees (+ the Rogue path from the innkeeper); village life
  (trade requests, standing perks: lessons, a home, watch work, a statue; the Shrine of Renewal respec); a use for every
  drop (dragon scale sets, war horn, grains, capes); "What next" journal page, How to Play sections, first-time tips;
  better townsfolk pathing; candles/chains/rails sharing a block; pressure plates as shared parts pressed again.
- **Android patch (on alpha.36)**: older phones on OpenGL ES (e.g. an Adreno 640) drew the world grey — screen copies
  between formats ES refuses are now blitted (`gl.cpp esCopyTexSubImage2D`, android branch), float depth textures on
  ES, ImGui's desktop-only calls skipped; the game fills the screen round the camera cut-out (`values-v28` theme).
- In progress when this was written (check `git branch`/`git log` — they may have landed): **leaf litter + dappled
  forest** (branch `leaf-litter`), **progress-scaled rewards** (branch `progress-rewards`). See §4.1 and §4.2.

## 4. The queue (agreed with the owner, in rough priority)

### 4.1 Leaf litter and the Dappled Forest
- Leaf litter: a thin, walkable ground layer of fallen leaves under deciduous trees, coloured from the tree above
  (green, red, orange, yellow, brown/dead), following seasons if simple; a dry crunch underfoot; hoe/shovel picks it up.
- Dappled Forest: tall slim poplars (new log/planks/leaves, autumn yellow-orange-red), the ground carpeted in red and
  orange litter, fallen hollow logs with shelf fungus, red shrubs. **Not rare.** Gate new generation behind a new
  terrain version so old worlds don't change.

### 4.2 Rewards that grow with progress (not distance)
A progress tier from story chapters, total skill level and bosses beaten scales structure loot (more rolls, better
odds, capped, logarithmic) and nudges creature level bands, host-side, old saves fine. (Distance-based loot was
rejected: players would just move far away.)

### 4.3 Variety in existing biomes and structures (owner said **no flooded and no "chunky" trees**)
- Per-area biome traits rolled in forests: ancient (huge trees, moss), burnt (charred trunks, ash), overgrown (vines,
  flower carpets), sparse. Trees whose roots reach into the caves below. Large flower clumps that give each forest its
  own look. (308 biomes already exist under `data/worldgen/biome`, incl. many autumnal/beech/maple/aspen ones.)
- Animal variants per biome (fur colours/breeds: bears, foxes, deer, cows, sheep, squid), occasionally a rare off-biome
  one. Only 7 creature types have variants today.
- Structures: dungeons, towers and ruins built from swappable room parts (as villages already are: 161 style pools),
  wood/stone palettes taken from the surrounding biome, one signature rare reward per structure type.

### 4.4 Sculpted 3D plants (owner's request: full, thick, layered plants, not cut-outs)
Today (`src/render/mesh_plants.cpp`) flowers, short grass, ferns, cane and kelp have built shapes; everything else
(tall/dry grass, bushes, saplings, mushrooms, crops, grains) is the sprite extruded 1–3 micro-voxels and reads as a card.
Plan: grasses and grains as clumps of 8–20 tapered blades fanning out with random lean, colours sampled from the
plant's sprite (darker root, lighter tip) so packs still drive the look; grain crops as stalks with seed heads per
stage; bushes as twigs with leaf clusters; saplings/crops with stems and angled leaves; mushrooms with rounded caps;
tips sway with the existing waving setting. Keep the "3D Plants: Near You / Everywhere / Flat" setting
(`settings.plantDetail` → `WorldRenderer::wantDetail`), fewer blades with distance, and check frame rate on a phone
(OpenGL ES) and on an integrated GPU. Start with grasses.

### 4.5 New content batches (from an older mod the owner liked; make everything our own, in our style)
The owner **does not want**: landmines, poison cactus, wall clock, planters, pot of chance, dinosaurs, invisible
pressure plates, a lighter, extra coin tiers (we keep our coins).
- **A — RPG and village feel**: playable **races** (Human, Elf, Dwarf, Orc, Halfling, pig-folk) with pluses/minuses
  (no hunger-based drawback). A race must **keep the player's own skin**: change stats and build (height/stockiness via
  the existing stature scaling in `src/render/char_anim.cpp`), at most an optional overlay like elf ears; send race with
  the skin in multiplayer. Also: named elite creatures with titles ("The Angry …", "The Tainted …"); lockable chests
  with keys and lock picks; taverns with a bartender in villages; **torches lit when placed and staying lit — only
  Herobrine snuffs them now and then; relightable**; our lava boat restyled in obsidian (no second boat); foods
  (sandwiches, cheese, ham, bacon, chocolate cake, heated milk); pet name tags; display cases; presents; snow bricks
  and ice variants (fence, gate, stairs, bars); themed pressure plates (sun, moon, rain).
- **B — Frozen lands**: ice caves and ice ravines, thin ice that cracks, icicles, ice spikes; cold creatures (yeti,
  sasquatch, walrus, ice ghost, a living ice plant, an ice shrine).
- **C — The dark side**: corruption spreading from **Herobrine's realm** (corrupt grass/stone), purification to cleanse
  it (patch notes must describe it without naming him); a Dream / Nightmare dimension.
- **D — Gear and creatures**: bone, firestone (with ore), obsidian armour and tools, platinum (ore, ingot, ring);
  thrown spears, blowgun and darts, wands (fire, ice, black hole, pig); creatures: fuseling variants (camouflaged,
  ender, ice, baby + nests), wizard, mummy, orc/dwarf/elf/halfling folk, pig-folk chief, a carnivorous plant, hippo,
  bull, ram, rooster, red cow, ape, nether skeleton, obsidian golem, evil snow golem, ender ghast, jungle spider.

### 4.6 Known open issues
- Townsfolk: one jeweller can be shut in a market stall; crowd deadlocks head-on in open ground; no rescue for a sent
  NPC with a path but no progress; two unreachable beds in one test village.
- Redstone gaps: no quasi-connectivity; comparators don't read lecterns/chiseled bookshelves/respawn anchors;
  dispensers lack several behaviours; hoppers pull only from directly above; no player-driven villager breeding or
  farmable iron golems.
- Occasional streaming spikes at high render distance; the `test_net` basket check and the direct-link smooth-walk
  check flake now and then — investigate if they recur.
- On OpenGL ES 3.0 the UI library's one-time setup leaves one harmless GL error on the first frame.

## 5. How work was organised (worked well)
Parallel helper agents, one per subsystem, each in its own git worktree based on `moving-structures`, test-first,
never pushing; then merge each branch `--no-ff`, keep both sides in conflicts, rebuild, run the touched suites, then a
full run before release. Single-line JSON data files: load and re-dump in their own format; when two branches add
tiles, take one side's `tiles.json`/`items.json` and re-run the other side's generator script so its tiles get fresh
slots. Verify helpers' claims (one inventory said the talent data wouldn't ship — it is tracked in git).
