# DokuQuest — session handoff (2026-11-02, night)

Start here in a new session. Read this file, `CLAUDE.md` (the code map), and only the files a task needs.
The full feature list is `docs/MASTER_PLAN.md` (the user's master doc, V2); the BTA detail is `docs/BTA_GAP.md`; the
Herobrine lore and ideas are `docs/HEROBRINE_LORE.md`. Third-party credits: `docs/CREDITS.md` (Horizon MIT; Cubyz and
BTA ideas only). Released: alpha.9, alpha.10 (On the Road) and alpha.11 (Every Creature Its Own) on 2026-11-01 evening;
alpha.12 (Coloured Light) on 2026-11-02 night.

## Morning 2026-11-02 (alpha.13 "Undercoat": what alpha.12 left open)
- **Near-terrain vertex 20 -> 16 bytes:** render/mesher.h ChunkVertex (four packed u32; 14-bit fixed point positions and
  uvs at 1/256 with +16 offset, measured ranges [-0.1, 16.1]); the tint is a kind (none/grass/foliage/water/dust+power)
  and chunk.glsl colours it: world_renderer.cpp climateFor() fills a 1024x1024 RG8 climate array (a layer per land,
  units 13) as sections upload, buildTintLut() makes the 256x256 grass/foliage/water table (unit 14, rebuilt when the
  pack's colormaps change). Greedy merging still keys on the CPU corner colour (one climate per merged face).
- **Sheared sheep:** undercoat parts (flag "undercoat", shown only while sheared: creature_rig.cpp); pack spec tries
  entity/sheep/sheep_fur, sheep/sheep_wool, sheep_wool and sheep(_wool)_undercoat.
- **Looks:** mob_variants.py LAND (28 looks); villager_look.cpp npcSkinTouches (a second random sequence; test_villagers
  keeps npcSkinPixels pixel-exact).
- **The finale (story; never in the notes):** sim/wither.cpp riftHeart + finale/hpFloor/heartT; creature.cpp hpFloor;
  achievements "storm"; rpg.cpp chapter "The Storm"; rift.cpp hint 90 s after the seal; villager_quests elder lines.

## Early morning 2026-11-02 (the check and the player's playthrough, before alpha.12 went out)
Scripted playthroughs (menus, title → worlds, village/trade, night, rain, Nether/End/Skylands, combat, magic, portals,
under water) found and fixed: every HUD notice shown twice (ClientFx + HudFx both toasted; ui/hud.cpp hudTakesMessages);
/weather never reached WeatherSystem (gamecommands.cpp now calls set()); the death screen's cause never set
(combat.cpp playerDie -> p.data.deathCause); /kill always killed the player (now creatures by name / @e[type=] / @e);
/summon takes Minecraft names (zombie = ghoul, creeper = fuseling) and says "did you mean"; /locate gave band-local y;
/tp <land> over the void finds the nearest ground; Nether arrival portals search ±8 for a floor and get a wider
landing; a portal crossing clears the fall start; water vision (render/terrain_fx.cpp: the light floor rises under
water); death messages for acid/poison/etc.; deaths and hits of 5+ are logged ("X took N (type)"); five silent sound
names; tools/check_sounds.py (in CI). The playthrough scripts live only in the session scratchpad; the useful ones to
keep are tools/playtests/*.json.

## Night 2026-11-01/02 (the user's list "Let's get these done": gameplay, BTA leftovers, art and tools, rendering, MP)
Released as alpha.12. docs/STATUS.md has the done / partly / not-yet split. Where the new pieces are:
- **Coloured light:** render/light_colour_layer.cpp (a 64x48x64 RGB volume flooded on a worker, unit 12, common.glsl
  lightingAt). **Dyed portals:** sim/portals.cpp (dyeOf/setDye, lv.info.data["portalDye"]). **Bigger biomes:** worldType
  "large_biomes" (gen/overworld.cpp climateScale x3). **Steam vents:** render/nether_fx_layer.cpp steamVents.
- **Cast per hand:** sim/magic_hands.cpp castHand; input Act::CastLeft. **Crows:** tools/port/crows.py +
  creature_beasts.cpp scarecrowPerchAI. **Church:** villager_ai.cpp npcService (every 7th day). **Skylands:**
  tools/port/skylands_more.py, blockfx.cpp floats/FallingBlock up, creature.cpp whirlwindTick, keepers.cpp valkyries.
- **MP:** server_sync.cpp "st" (world progress), server_host.cpp "sd" (per-player), split.cpp + netplay.cpp
  lanShareLocalSkin (host-side couch players in a LAN game). A guest's couch players: net/client.cpp addCouch (a "lite"
  sub-link per couch player: HELLO "sub", server_host.cpp Peer.lite skips chunks/entities/fx; tools/test_net.cpp).
- **Texture Studio Creatures tab:** launcher/texture_studio.cpp (openSkin/saveSkin; pack folder under userDir, else
  userDir/textures/entity overlay, which render/creature_models.cpp loadPackPic reads first). DQ_TS_OPEN=<rel> opens one.
- **Skin check:** creature_models.cpp (no head/body parts -> warn once, fall back).
- **Shaders:** render/post_layer.cpp (levels: settings.extra.shaderLevel 0..4, assets/shaders/post.glsl; Iris/OptiFine
  composite*/final passes translated from GLSL 120; zipped packs listed). docs/shader-packs.md.
- **Distant terrain:** distant.glsl per-block UVs (uTileWorld) + average colour past a pixel; water as chunk water
  (climateWater, fresnel). Test with DQ_ENV_TEST="x y z yaw pitch time clear" (yaw/pitch in RADIANS; it turns the
  adaptive range off, which otherwise shrinks the far land in software GL and fogs everything).
- **Terrain memory budget:** world_renderer.cpp budgetR (settings.extra.terrainMemoryMB), distant.cpp realR/chunkReal.
- **Particles:** particles.cpp soft particles (sorted, blended, faded, near-camera fade, relit as they drift).
- **Engine editor + bridge:** client/engine_bridge.cpp (--bridge, 127.0.0.1:25590, JSON lines), net/line_socket.*,
  launcher/engine_tools.cpp (edit/build/live/Strata over an OpenAI-style endpoint via curl). docs/engine-bridge.md.
  Ask Strata finds local models like Zed does (Ollama /api/tags, LM Studio and llama.cpp /v1/models, every 20 s) and
  picks any "strata" model; launcher/mcp_server.cpp (`dokuquest-launcher --mcp`) is the bridge as an MCP server.
- **Data archive:** common/fs.cpp DataArchive (installDir()/data.dqa; loose data/ files win); tools/package.sh makes it.
- **Updates:** "-game" packages (no launcher); the whole one only when launcher.version > DQ_LAUNCHER_VERSION
  (CMakeLists.txt, raise it when the launcher changes); versions.json "pinned" + Options > Updates "Version to play".
- **/lineup <page> [n]:** every creature side by side (sim/creature.cpp spawnLineup).
- **Art pass:** tools/port/mob_restyle.py (only our own creature skins; the 19 the user's pack skins are left alone).

## Evening 2026-11-01 (the user tested alpha.9: bug list + "variants for every mob, physically different, with lore")
- **alpha.10 (3670957 + 9c70365):** roads/bridges/travellers (gen/structures_roads.cpp, sim/villager_life.cpp
  travellersTick), saddle bags + galleon (tools/port/travel.py, sim/vehicles.*), start in the wild, guest use of
  creatures/containers (controller.cpp runGuestUse), portal chase + far-side spawns, ghast sound range, KeeperSystem
  (sim/keepers.cpp: dungeon keepers/structure masters wake on entry, clear records), creeper-shaped land fuselings and
  no-glow golems (tools/port/biome_mobs.py), small bees again, bow held right, footsteps on the stride, Nether places on
  the new Nether ground (TerrainQuery::openNether), Wither Storm from the T with a command block, captions +
  Accessibility page, /tp <land>. The user's Herobrine skin: assets/char_creator.cpp userHerobrineSkin() (skins/).
- **alpha.11 (e57e1cc): creature looks.** tools/port/mob_variants.py (hooked last in data_patches.py) builds 255 looks
  for 121 creatures: each a model `<mob>_<look>` = the base model + Kit parts (arrows, helmet+lamp, tool on back,
  hats, moss/snow/coral lumps, spikes, fans, drapes, bands/rings, coats, packs, saddlebags, horns, antlers, crests,
  eyepatch, beard, things inside slimes) and/or recoloured+patterned copies of its own tiles (`<tile>_<look>`, our own
  painting: tint_px matches C++ tintImg). mobs.json `variantDefs` [{model, w, label, lands, cave, surface, village,
  water, dim, held, drops}]; the base look's weight is 1.2x the others' total. sim/creature.cpp placeVariant (in
  spawnCreature) picks by place; Creature::label(); death message uses it (combat.cpp creatureLabelOf). Models carry
  packBase/packTint/packTex (creature_rig.h): with a pack (the built-in "DokuQuest Base" pack is active by default and
  has wolf/sheep/cat/horse/llama/spider/skeleton/zombie/creeper/golem skins!) a look wears its base's pack skin,
  tinted (packTint, only when the whole coat was recoloured: `ptint`), or the pack's own picture (packTex: e.g.
  entity/wolf/wolf_ashen). Patterns don't show on pack skins. Golems of other blocks use pack=False. `/looks <mob>`
  lines them up; `/summon <mob> [n] [look]`. Thin parts are cut by their face pictures only (creature_models.cpp
  voxelPart thinAxis). Not given looks: bosses, the story's figures, villagers (professions), end crystals.
- **Open from the user's bug list:** placed skulls "flickering textures around them" (not reproduced; ask which skull
  and where), portal far-side creatures not visually confirmed. Ideas for more looks: per-land fuseling looks beyond
  "elder", villager looks, sheared-sheep skin looks from the user's sheet.

## Day 2026-11-01 (the user's bug reports while testing alpha.8, then the BTA 8.0 Nether, Cubyz tools, Horizon light)
- **Terrain version 5** (LevelInfo/WorldGenOptions genVersion default 5; old saves keep theirs): `gen/nether_bta.*` is
  BTA 8.0's Nether (NetherShape: floor/roof heightfields, five lands as jittered 230-block cells blended 56 blocks:
  Crag, Volcanic Islands (cones with breached lava craters), Sulfur Pools (acid pools with rims), Crystal Forest (spires
  on a 7-block grid, hanging ones), Old World (ash dunes, pits, ruins of scorched brick)); DepthsGen routes generate/
  solidAt/terrainColumn/sample to it when `bta_` is set. `dq-gentool nether <seed>` maps the lands and finds a crater
  and a pool. Overworld v5: ice floes by glaciers (`floes`). Per-land haze + minLight 0.27 (render/sky.cpp
  btaNetherEnv, render/terrain_fx.cpp). Blocks from tools/port/nether_bta.py (acid = liquid kind 3: registry tLiquid 3,
  physics Contacts::acid also sets water; controller/creature damage; worldsys flows it; buckets refuse it).
- **Mixed tools/armour** (`core/tool_parts.*`, data/tool_materials.json + mixed_* items from tools/port/tool_parts.py):
  crafting.cpp matchPartTool (patterns, mending with own materials), tag "pt"/"pa"; toolOf()/toolMaxDurability()/
  stackName()/armorPointsOf() used by combat, wear, tooltips (ui/sheet.cpp), anvil; models keyed partToolModelKey
  ("pt:..."/"pa:...") recoloured in item_models.cpp (g_recolour hook in finishGrid); per-stack icons
  ItemIcons::dynamic (theme.cpp drawItem). The Tool Forge is a two-slot station "forge": gems (setToolGem).
- **Fixes:** campfire (tileset.cpp unpackCampfireLog + shapes.json z/fit/grain on model boxes), open chest hole
  (mesher hideAt), door/trapdoor swing relief voxels (swing_layer leafModel), lantern chains to ceilings (meta bits
  2..6 = extra half blocks), skull item models (render/creature_models.cpp creatureHeadGrid), Wither Storm from a
  command block, creative breaks hardness -1, Wither skull hit-back + arrows glance, LAN skins (net SKINS message,
  netSkinHook, "net:<hash>" refs, assets registerNetSkin), portal view culled/scissored to its screen rect, villagers
  sit from any side / badSeat, newer cube blocks drawn as cubes in hand, saddled pigs ridden.
- **Light/distance:** render/dreamlight_layer.cpp + assets/shaders/dreamlight.glsl (Options → Lighting, extra
  "dreamLight"), sky.glsl warm halo (uDream); distant tiles dither-fade (Tile::readyAt, uFadeIn/uFadeOut).
- **Later the same day (the user: "keep working, nothing left unfinished"):**
  - Raised micro-voxels: soft pictures (sand, dirt, terracotta, netherrack, granite...) stayed under the relief's
    geometry threshold and drew flat; assets/tileset.cpp deriveMicroVoxelHeights now stretches plain surfaces to their
    own contrast (3211 of 3753 tiles have whole-voxel relief, was 2687).
  - Nether lands: creatures per land in sim/creature_spawn.cpp (landAt via netherBiomeMix; new mobs sulfur_slime,
    crystal_wisp, scorched_skeleton from tools/port/nether_bta.py; brains in creature_monsters.cpp), sounds per land
    (audio/nether_sfx.cpp live-synth sounds; AudioScene::netherLand set in client_fx.cpp; ambience.cpp per-land wind and
    timers), volcano eruptions (gen netherVolcanoes() + render/nether_fx_layer.cpp: clock from world time so shared
    worlds agree; DQ_ERUPT=1 makes every volcano erupt every 20 s for testing), embers/fumes/glints per land.
  - Distant terrain rebuilt after Cubyz's LOD (the user asked for Cubyz's code to be studied; read in a scratch clone,
    ideas only): render/distant_build.cpp — ChunkSummary = a chunk brought down to 2x2-block voxels (runs of block ids
    per cell; Cubyz's pick: solid if any, the most-exposed block wins; hollows without daylight under 6 tall filled),
    coarser levels merge the four quarter points (TileInput::sums has 4 per cell past L0), the generator's ColumnSample
    (+ roof, trees) fills unvisited land; meshing = run tops/bottoms merged greedily + walls = runs minus the neighbour's
    solid spans (skirt at tile edges). LodVertex is 16 bytes with the face's tile layer; distant.glsl textures each voxel
    face (uCell blocks a texture), grass/leaf/water tints as chunk.glsl, leaves drawn in their average colour (flag 8).
    DistantTerrain::draw binds TileTextures at units 5..10.
  - Then (the user: "as good as Cubyz") the far land is generated for real: DistantTerrain::genFar generates chunks nobody
    has loaded with DimGenerator::generate on a worker (priority 1000, 4 a job, 1-2 jobs), nearest first in square rings
    out to min(range, 1024), summarizes them (summarizeChunk: sky flood instead of light for unlit chunks; buried rock
    collapsed to the band's deep block). Summaries capped by trimSummaries (16000, farthest go, may be made again).
    Generated ones (ChunkSummary::generated) are kept in <world>/lod_far.bin (zlib, block keys palette, tag = version/
    genVersion/seed) and loaded on a worker before generating more. Big tiles rebuild at most every 6 s (Tile::madeAt).
  - Skylands trees looked at from outside the cloud bank (land on an island at -40,60 seed hello): cloud leaves render
    like every other leaf block (white, from the pack's oak leaves; trunks are the pack's dark birch). Nothing visibly
    broken; asked the user what they saw.
- **Still open:** BTA sounds list (biome moods, discs, jukebox playback); alpha.9 release on Nov 2 after full checks.

## Overnight 2026-11-01 (the user: "work overnight, game-wide playtest, optimizing pass, check all the notes")
- **Playtests:** `tools/playtests/run_all.sh out.txt [names]` runs every playtest in a fresh world on a copy of the
  binary (`build/dokuquest-pt`), `persist` reopening the world `containers` saved. Test op `give` takes `clear` (empty
  bag first) and `hand` (into the held slot) so a test doesn't depend on what the spawn put in the bag (a pitcher plant
  near seed hello's spawn breaks and gets picked up). `look` pitch is in degrees, NEGATIVE = down. `all.json` was a
  stale duplicate of the menu tests and is gone. New: `bta_leftovers.json` (reinforced piston, party chest, sign text,
  HUD editor), `siphon_basket.json`.
- **Found broken and fixed:** signs were invisible (render NONE with no layer): `render/sign_layer.cpp` draws board,
  post and the four lines (pixel font from the ImGui atlas, 128x64 texture per sign, cached); Esc in an ImGui text box
  reverted the line (sign_screen keeps `keep[i]`); typed letters with no focused line go on the first line. The rain
  detector acted as a daylight sensor (redstone.cpp daylightTick: rain branch). The party chest didn't open (now an
  entItems container on `lv.info.data["partyChest"]`, ui_main.cpp). Knight/Ranger talents were defined but never
  called (combat.cpp, controller.cpp arrows, player.cpp speed). Silver (`holy`) did nothing (combat.cpp,
  `creatureIsCursed` in mob_combat.cpp). Armour stands had an item but no block (`armor_stand` block, render NONE,
  block entity "stand" items[5] + pose; sim/build.cpp; render/stand_layer.cpp maps the players' armour meshes onto a
  Minecraft-sized wooden figure). HANDOFF said the Siphon and the Wicker Basket were done: they weren't; both added
  (sim/basket.cpp: basket hooks useEntity first, releases via useItem + useBlock; siphon = Redstone ticker kind 6).
- **New:** armoured/armed undead (Creature::rollArmour, data["armor"], armourPoints soak, 8.5% drops, helmet = no sun
  burn; drawn in creature_layer by stretching the char armour meshes onto rig parts; DQ_ARMOUR=1 dresses all), the
  reinforced piston (blocks reinforced_piston(+_extended) with "launcher": push 16, fling entities; steel tiles from the
  pack's piston pictures), the movable HUD (ui/hud_edit.cpp; hud.cpp shifts each piece's vertices by
  settings.extra["hudLayout"][piece]), glow ink / ink sac / dye on signs, recipes so every drop has a use, the
  art restyle (tools/port/restyle.py + code_tiles.txt + restyle_manifest.json; never the user's edited pictures),
  tools/port/audit.py (broken links, unobtainable, unused), World settings "Seasons" (now OFF unless on) and "Whole trees
  fall" (rule treeFelling).
- **Optimizing:** DQ_PROFILE=1 logs CPU ms per layer/pass every 600 frames. On llvmpipe the world cutout pass (plants,
  leaves) and clouds dominate (fill-bound there); sections already draw nearest first; clouds are instanced and culled.
  The icon cache PNG is now written on a thread (its compression took longer than drawing the 2162 icons).
- **Later the same night:** lingering potions (data_patches: lingering_* from every splash_*, splash + ectoplasm;
  PotionFlask::smash spawns a PotionCloud in sim/magic_bolt.cpp; splashPotionAt takes radius and scale), potions stack
  to 16 (brewing stand takes one bottle a slot, cauldron fill keeps the stack), permafrost family + mossy oak log +
  mossy emberstone (bta_blocks.py), Highland Spruce (BV_HIGHLANDS) and the tundra step (BV_TUNDRA) in
  gen/overworld.h/.cpp + trees.cpp, ember/moss/sky fuselings (fuseBurst in creature_monsters.cpp; spawns: Depths
  floor, forest nights, Skylands grass), zombie pigmen only on natural Depths ground, December carols (audio/music.cpp
  carol(): four public-domain melodies), New Year fireworks over villages (sim/usables.cpp), a brighter village tree,
  elytra hung 1.5 px lower. alpha.8 = DQ_VERSION bumped; released from the commit the final sweep passed on.
- **Final sweep fixes (test harness, 05:00):** the virtual pad's triggers were "released" to axis 0, which SDL reads
  as half pulled, past the 25% let-go mark, so LT/RT stayed held after every test press (furnace/containers/split
  failures: the next press never counted and a held use kept placing blocks). test_script.cpp now rests triggers at
  SDL_JOYSTICK_AXIS_MIN (and `padaxis` lt/rt maps 0..1 over the whole axis). `mouse` with x/y also warps the real
  pointer: ImGui's SDL backend reads the system pointer every frame while no button is down, so clicks on the menu
  tabs landed where the Xvfb pointer sat. New expect ops: `pos` [x,y,z] + `within`, `minY` (both take `rel`). New
  playtest `clip.json`: walk stone → dirt path → grass → path into a ladder, climb 7 and step off on top (the
  walk/knock/ladder check the notes asked for). Real game fix found by `rodlead`: the fishing line lobbed over an
  animal three blocks away; a cast aimed at a creature now flies flat at it (sim/fishing.cpp castLine) and the
  hook's catch box is a bit more generous. `puddle` gets 1800 s in run_all.sh (4000 frames on llvmpipe).
- **Headless test programs** (all in build/, all pass now: gameplay 103, magic 57, content 64, endstory 73,
  villagers 66, redstone 81, creature, net 59). Real bugs they found: **town gate levers did nothing** since
  alpha.4.9 (villagers.cpp's itemUse handler AND rsplace.cpp's useRedstone both toggled the gate; now only
  rsplace gateLever, which calls the villagers' gateUse first so their dusk/dawn logic respects the pull), and **a
  controller's first press after the keyboard counted twice** (Input::setSources gave a newly claimed pad's frame no
  history, so the still-held Y read as a fresh press next frame: the inventory shut and reopened; a new source's
  frame is now a copy of this frame's merged view fr[0], so the claiming press counts once and is held after). Stale tests fixed: slabs/stairs are chiseled forms (test_gameplay), villagers pay
  in gold coins (test_villagers), the net test looks for the cow by Alice (the server's herds are about too) and
  counts real checksum mismatches apart from re-asked chunks (NetClient::Stats::mismatches). Nightmares are off in
  the bed check (a 30% wake in the dark made it flaky). `dq-sweep --items`: 6 notes, all expected (bucket/bottle
  need a liquid, discs need a jukebox, salad leaves a bowl). Two checks failed once in five runs and passed on
  rerun: dq-creature-test "herds came with the land" (generation wait is time-bound: 8 s) and test_endstory "before
  the rift: all quiet" (something earlier in that test can stir the rift); look at them if they keep turning up.
- **Notes audit** (/tmp/pt/notes_gaps.md, regenerate with an agent if needed): still open and worth doing next: painted
  doors/trapdoors/signs/chests, the Seasonal Forest, igneous emberstone, chimney smoke check on town buildings (campfires
  in the hearths already smoke through campFlue), the Nether's own ambience and steam vents,
  dye-tuned portals (probably skip), coloured block light, a scripted start-to-finish playthrough test, crows on
  scarecrows, church banners, more biome fuselings (6 now), articulated biome golems, Story Mode follow-through on
  capes/hair and talking faces, render distance above 16. Docs MASTER_PLAN/BTA_GAP still list many done things as
  open (see the audit's DOC STALE list).

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
- **Branches:** latest release is **alpha.8** (2026-11-01; tag at e8663de, which passed the full playtest sweep, all
  headless tests and CI; its Windows link failed, so the assets were rebuilt from 0820251 = e8663de + declarations
  moved to namespace scope, re-verified, and uploaded onto the same release: AppImage, tar.gz, Windows zip). main =
  `chisel` at 0820251. Next release is **alpha.9**, no earlier than Nov 2 (one per day). Work goes on `chisel`.
- **Build:** a fresh session has no deps or ccache: `bash tools/fetch_deps.sh`, then `cmake -S . -B build -G Ninja
  -DCMAKE_BUILD_TYPE=Release` and `ninja -C build -j2 dokuquest` in the background (~25 min first time, 2 cores).
  Never edit a .cpp while it's being compiled; touch edited files and rebuild if unsure.
- **Headless test:**
  - Run Xvfb on :90 if it isn't up: `Xvfb :90 -screen 0 1280x720x24 &`
  - `DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 ./build/dokuquest --userdir /tmp/x --create T --seed hello --smoke-script s.json`
  - Ops: wait, cmd, anchor, fill {rel,from,to,key,meta}, setBlock, tp, lookAt {rel x,y,z | entity}, give, select,
    mouse {button,hold,x,y}, expect {block,is | entity,near | pos,within | minY | screen | inv | count | container},
    hurt {entity}, key, pad, padaxis, shot, report, quit. The first pad press right after the virtual pad attaches
    can be lost: start with `{"do":"padaxis","axis":"rightx","value":0}` and a few frames' wait.
  - Gotchas: `/summon` puts creatures 3 blocks ahead; use reach is ~4.5 blocks; `/kill` kills the PLAYER; tp rel
    first so the player stands at a block centre.
  - Playtests in `tools/playtests/`: chisel, eggs, lavaboat, heads, pumice, paint, slabtorch, lead, jar, sensor,
    sit, sieve, journal, portal_size. Slow on llvmpipe: run long ones with `run_in_background` and a 300-400 s
    timeout (a script hit by timeout prints nothing). `/clear` after `fill` (fills drop picked-up plants); time
    names: `/time set midnight|noon`. `/locate <structure>` prints coordinates (e.g. end_city).
  - Data is copied into build/ by ninja (`dq_runtime_data`): rerun ninja after changing data/ before a test.
  - Block keys are the game's own, e.g. oak planks = `planks`.
  - Gameplay harness: `dq-test-gameplay`. Every headless test program: dq-test-gameplay, test_magic, test_content,
    test_endstory, test_villagers, test_redstone, dq-creature-test, test_net (+ `dq-sweep --items`); build them by
    name with ninja and run each from build/. Run them before a release along with the playtest sweep.
- **Releases:**
  1. Bump `DQ_VERSION` in CMakeLists.txt.
  2. Write notes in `docs/CHANGELOG.md` and `assets/news/news.dqn`.
  3. Push main and wait for CI on the SHA (ci.yml).
  4. `git push -f origin SHA:refs/heads/release`. That runs release.yml, which publishes; publish re-runs now
     upload onto an existing release.
  5. If it fails, `gh run rerun --failed` once.
  - Versions: alpha.7 next, then alpha.7.1, ... Never alpha.N.10, because GitHub sorts it below .9.
  - Session limits: we can't delete or edit releases from here. GraphQL is blocked (use the REST `gh api`).
  - CI (ci.yml) builds Linux only; the first Windows build is the release's. **Never declare a function inside a
    block** (`{ void registerX(); registerX(); }`): MSVC gives it the global namespace and the Windows link fails
    (alpha.8's first release run lost its Windows zip to 25 of these). Declare it at namespace scope; ci.yml now
    fails on that pattern. A release re-run uploads onto the existing release with --clobber.
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
- BTA batch: hotbar rows on Tab; fishing-rod leads (fishing.h Bobber::Hooked + mob; reel = tug, > 14 snaps;
  playtest rodlead.json); sponges soak water on placing (usables.cpp blockPlace hook -> next tick, 6 steps / 65 blocks)
  -> wet_sponge (bta_blocks.py; smelts back); a wet sponge used on dyed wool/glass/terracotta/painted planks washes
  it; furnace carts take any fuel (fuelValue x 22.5 s); coloured twinkling stars (sky.glsl); achievements
  (sim/achievements.cpp list + checks once a second from data["found"]/place/state + mobDeath/fishCaught/morning;
  ui/achievements_screen.cpp, menu tab "achievements" last); colour-blind filters (render/colour_filter_layer.cpp,
  colour_filter.glsl, Settings::colorFilter, Options -> Graphics); Adventure/Spectator modes (controller interact,
  build placeBlock, player.cpp no-clip, combat damagePlayer; New World "Adventure", World settings all four).
- Fixed: tab switching from the inventory skipped a tab (ui_main.cpp g_tabSwitchFrame); pad Y in a container closed
  it (inventory.cpp: the toggle is for the plain inventory only). No spawn egg for wither_storm (spawn_eggs.py SKIP).
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
