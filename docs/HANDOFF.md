# DokuQuest — session handoff (2026-11-02, night)

Start here in a new session. Read this file, `CLAUDE.md` (the code map), and only the files a task needs.
The full feature list is `docs/MASTER_PLAN.md` (the user's master doc, V2); the BTA detail is `docs/BTA_GAP.md`; the
Herobrine lore and ideas are `docs/HEROBRINE_LORE.md`. Third-party credits: `docs/CREDITS.md` (Horizon MIT; Cubyz and
BTA ideas only). Released: alpha.9, alpha.10 (On the Road) and alpha.11 (Every Creature Its Own) on 2026-11-01 evening;
alpha.12 (Coloured Light) on 2026-11-02 night.

## 2026-11-07 (Claude): BTA menus, Better End's End, BTA's Nether, the Aether (uncommitted, with Grok's work under it)
- **Menus like BTA 8.0:** `ui/paged.h/.cpp` is BTA's ScreenPaged kit (page list with item icons, folding categories,
  24-unit rows: name, leader line, 100-unit control, reset button, hover tooltips, Search page, LB/RB pages). Used by
  Options (`ui/options.cpp`: General, Player, Audio, Video, Controls with the keys inline, Accessibility, Asset Packs,
  Language Packs, Search), Select World / Create New World / Rename / Delete / World Settings (`ui/worlds.cpp`, BTA's
  exact button rows) and Select Server (`ui/multiplayer.cpp`: Favorites/History/Local Network/Direct Connect, Join/Edit/
  Delete column, Add/Edit/Delete Server screens). The launcher's Options tab (`launcher/options.cpp`) has its own copy
  of the same layout (the launcher has its own ImGui context and fonts).
- **The End = Better End 3.2.5 (MIT, docs/CREDITS.md):** `render/be_sky.cpp` (+ `assets/shaders/be_sky.glsl`,
  `assets/sky/betterend/`), the per-land fog colour/density in `render/sky.cpp`, `gen/theend_be.cpp` (surfaces, trees,
  plants per land; land 2 is now Foggy Mushroomland), 103 BE blocks from `tools/port/betterend_blocks.py`.
- **Terrain version 7** (new worlds; old ones keep 6 until World Settings → Upgrade): BTA's Nether climate/density
  fixes, surfaces and features (`gen/nether_bta.cpp` v7 branches, `gen/nether_bta7.cpp`), BTA fog + grade
  (`render/nether_grade_layer.cpp`, option colorCorrectionNether) for every cave Nether; the Aether's islands
  (`gen/aether_noise.*`, `gen/skylands_aether.cpp`, AETHER_DY 100).
- **Art rules:** BTA/Aether art is not shippable: `tools/port/bta_nether_blocks.py` and `aether_blocks.py` recolour the
  base pack; `tools/port/local_mod_art.py` builds a this-computer-only pack from the user's own mod copies.
- **Build here:** the toolchain is /tmp/toolchain (PATH + LD_LIBRARY_PATH to its usr/bin, usr/lib64). The old sysroot
  /tmp/dq-sysroot is gone: SDL and imgui's SDL backend can't recompile, so after any `cmake build` touch their objects
  newer than the headers. The objects record system headers as /usr/include/... (sysroot paths); drop entries for
  files that don't exist from build/**/compiler_depend.* and *.o.d before `make`, or everything rebuilds.
- Research notes with the remaining gaps: `docs/research/{better_end,bta_nether,aether}.md`.
- **Later the same day:** the game's menus use the launcher's look (`ui/paged.cpp` classicDirt / classicButton /
  pageIcon). Better End's 6 creatures (`tools/port/end_mobs.py`, spawned per land in `sim/creature_spawn.cpp`). The
  Aether's dungeons are structures now (`gen/structures_aether.cpp`, terrain 7 only, registered after the roads):
  bronze (Slider), silver (angelic temple on a cloud bed, Valkyrie Queen) and gold (floating island, hellfire hall, Sun
  Spirit + 2 fire minions woken in `sim/keepers.cpp`). New blocks: angelic/hellfire stone (+ lit), pillar, pillar top,
  holystone bricks, crystal fruit leaves; new creatures: slider, sentry (on carved stone), sun_spirit, fire_minion,
  evil_whirlwind (`tools/port/aether_mobs.py`). BTA Nether: heat haze (option heatHaze, depth copied in
  NetherGradeLayer::drawTranslucent because the held item clears depth), red water / opaque lava fog, sulfuric fire
  (yellow, on brimstone in the Sulfur Fields). Not done: Better End's NBT structures, the Aether's holiday tree, title and
  pause screens still use the pack's buttons.
- **Evening (second round, after the user's "Nether isn't BTA / audio broken / menus not like the launcher"):**
  - The user had been running the downloaded alpha.18, not this build: build/CMakeCache.txt said DQ_VERSION alpha.16, so
    the launcher's updater preferred the release. Fixed both: the cache is alpha.18 now, and launcher/updater.cpp runs
    the game next to a launcher that sits in a CMake build folder, whatever the versions say.
  - Audio: PipeWire had "media.name:miniaudi0" (miniaudio's shared default stream name) saved as muted. audio.cpp names
    its stream "DokuQuest" now (pulse app + stream name) and logs the device. Effects were checked by recording the game's
    own stream (parec --monitor-stream): footsteps, mining, level-up all audible.
  - Every menu uses the launcher look: theme.cpp mcButton/button -> classicButton, Panel frame, ImGui colours, slider ->
    mcSlider, col:: palette greys; pause screen BTA gradient.
  - Smoothing: sky.cpp crossfades fog/sky 0.7 s on a dimension change; the Old World fog eases; the Nether grade fades
    in/out and reads BTA's exact temperature.
  - **Terrain 7 Nether = gen/bta/** (exact port of BTA 8.0.1's generator by a subagent, validated block-for-block against
    BTA's own classes run in Java: terrain, surface, large features, biomes, all 47 decorations; Region decoration mode).
    gen/nether_exact.cpp maps its blocks to ours and serves generate/solidAt/terrainColumn/netherLandAt/temperature.
    The old v7 approximation (nether_bta7.cpp, the v7 branches in generateCave) is now unused for v7.
  - **Terrain 7 End = gen/be/** (Better End's TerrainGenerator/IslandLayer/OpenSimplex ported bit-exact; BCLib's SDF and
    hex biome map reconstructed — see gen/be/README.md). theend.cpp: island()/endBiome() read a per-chunk column cache,
    generate() fills from the 3D density (stacked islands each get their land's ground); voidRing = OUTER, the dragon's
    island stays ours. endLandAt(seed, genVersion, x, z) for the sky/spawns/particles.
  - Better End's 63 land structures: tools/port/be_building.py makes the 116 building blocks they need (cloned from our
    block kinds, BE pictures through the art pass) and data/be_structures.json; gen/end_be_structures.cpp places them
    (1 in 10 chunks, BE's lists per land, rotated). Pillars: see the night section above. Not ported: the eternal portal.
  - Textures: local_mod_art.py and betterend_blocks.py use restyle.py's colour ramp now (the pack's picture of the same
    kind in the mod's colours); packs.cpp lays "Mod Art (local)" over the chosen pack automatically.
  - Tile budget: GPUs with 2048 array layers hold 4096 tiles (2 pages). Pack animations are capped at 32 frames
    (packs.cpp) to stay under it (4024 now). Adding many more pictures needs a third page or atlas packing.

## Small hours 2026-11-08 (Claude): the open BTA Nether (terrain 8), pack looks, ores into tools (uncommitted)
  - Terrain 8 (new worlds' default; World Settings > Upgrade moves a world to it): BTA's exact Nether on version 5's open
    land (the GitHub build's walkable heightfield). bta::NetherGenOptions::shape (gen/bta, unset = BTA exactly) takes the
    column shapes; gen/nether_exact.cpp OpenShape picks version 5's land shapes (NetherShape::columnWeighted) from BTA's
    biome map every 4 blocks, blended over 14, lifted 46 so v5's lava sea (50) is BTA's (96). BTA's bedrock, surface,
    caves, floes, shelves and 47 decorations run on it. DepthsGen::spawnHeight reads the real terrain (7 and 8: down from
    the roof, never into lava): /tp nether dropped you from y ~194 before (it asked the v6 cave shape).
  - Nether spawns: the game's own Nether crowd back on terrain 6+ by BTA land (sulfur slimes, crystal wisps, magma
    slimes and ember fuselings on the volcanic islands, scorched skeletons in the Old World, charred ghouls on crags and
    shelves, cinder wisps with the fortress's wither skeletons), besides BTA's pigmen and ghasts.
  - Pack skins: zombie_pigman (the pack's zombie_pigman.png, zombie 64x64 layout) and wither_skeleton specs; cow/pig paths
    fixed for the pack's flat names (entity/pig_temperate...); climate looks pig/cow/chicken #cold/#warm (data "climate"
    set at spawn from the chunk's temperature, creature_layer modelFor). Still no pack spec: armadillo, bat, camel, cat,
    cod, dolphin, donkey, end_crystal, fox, frog, glow_squid, goat, horse, ocelot, panda, parrot, pufferfish, rabbit,
    salmon, shulker, squid, turtle, warden, wither (each needs its Minecraft UV boxes mapped to our rig's parts).
  - Enderman eyes: the glow plane takes the pack's enderman_eyes cut exactly as the head's face (pmFaces), not the
    built-in eye tile. Villagers: no freckles or cheek scar (stray dots on the voxel face); draws kept so the rest stays.
  - Menus: classicButton draws the pack's widget/button sprites (3-slice) when the pack has them (the brown boxes).
  - Clouds: no voxel wisps over the volumetric layer (the strips), mist greys far clouds instead of cutting them at 70
    blocks, the land's cover eases over 8 s, a fixed dither (no flicker), more steps toward the horizon.
  - Rubyglass: after the pack loads the crystal is ~65 % cover and the family glows from within (bright half 225, the
    rest 70 in the emissive channel), as BTA's emissive layers do (app.cpp reloadTiles).
  - Ores: zanite (gem), gravitite (plate) and thallasium (ingot) are full materials (tools/port/new_materials.py: items,
    tool and armour sets, recipes, smelting, drops, icons recoloured from the iron ones, built-in and pack-styled into
    the overlay) and forge/mixed-tool materials (forging.py, tool_parts.py: 22 now). ender_ore, amber_ore and
    ambrosium_ore drop an ender shard, an amber gem, an ambrosium shard (heals). Pack animations capped at 24 frames
    (was 32) to keep the tile count under 4096 with the new tools.
  - Armour: tools/port/armor_art.py writes worn layers into the overlay for every material (GeruDoku's iron/gold/diamond/
    netherite/chainmail/leather as they are; copper, bronze, silver, steel, mithril, adamantite, emerald, zanite,
    gravitite, thallasium recoloured from them); pack_armor.cpp reads the overlay after the chosen pack (the active
    DokuQuest Base pack has no armour layers of its own). data_patches.py runs new_materials, forging, tool_parts,
    armor_art and elytra_art at its end.

## Small hours 2026-11-08, second round (Claude): elytra, terrain 8's lands, rubyglass, Nether landings
  - Elytra: thin (the model's pixel of growth only along the panel; z scale 0.55), and the picture redrawn after the
    user's reference: a frame of bone down both long edges and the top, two ribs, a stepped trailing edge (elytra_art.py).
  - Terrain 8's shapes: NetherShape(seed, open8) gives all seven of BTA's lands their own walkable ground (floorOf;
    version 5's own shapes are floorOfV5, unchanged): gentler crags, volcanic islands half out of the lava with channels,
    crystal plains, shelf terraces (steps of 5); the forest, the Old World and the sulfur pools as version 5 makes them.
    dq-gentool nether8 <seed> prints BTA's biome map (30 % volcanic, 23 % crag, 18 % crystal forest, 15 % Old World...);
    dq-gentool ncol <seed> x z the landing and the real column.
  - Structures were built in BTA's caves under the open land (terrainExact found the ground from the bedrock up):
    terrain 8 finds it from the roof down now; locate shows them at the surface.
  - Landing: DepthsGen::spawnHeight checks the cheap terrain, then that no structure is within 8 blocks, then the
    decorated chunk; /tp with a place asked searches 96 blocks round it for ground instead of dropping you from y 160.
  - Spawns: the Nether's ground search runs from above you down 96 blocks (you could fly over everything before); ash
    layers count as ground. DQ_SPAWN_DEBUG logs why a pigman pack failed.
  - Rubyglass column and node are clear red glass too (the user wants the giant crystals see-through; BTA has them
    opaque): render 2, translucent, cullWith "rubyglass" (BlockTraits::cullGroup: one body with the crystal, no faces
    between them); after the pack loads they're ~60 % cover and their bright veins glow.

## Late night 2026-11-07 (Claude): clouds, elytra, dragons, Nether spawns, Aether falls from below (uncommitted)
  - Nether spawns: BTA 8.0.1 gives all four Nether biomes ghast 10 / zombie pigman 10; MobZombiePig.canSpawnHere needs
    air, a BlockTags.NETHER_MOBS_SPAWN block under it, block light <= 7. Our ground list missed most of the exact
    Nether's ground (brimstone, brimsand, gloomstone, ember, rubyglass, cobbled netherrack): pigmen almost never spawned
    on terrain 7. netherMobsSpawnOn() (nether_exact.cpp) is that tag on our blocks; the cave Nether tries six places a
    roll over and under you. Checked: 16 pigmen (the cap) and 1-2 ghasts within 25 s of arriving.
  - Aether falls seen from the Overworld: water/glass/ice (pass 2) and waterfall spray are drawn only in the camera's own
    land (world_renderer emitSections / waterfallBases); the land next door's solid edge still shows.
  - Volumetric clouds (render/clouds_volume.cpp, clouds_vol.glsl + clouds_comp.glsl, Options > Volumetric Clouds,
    extra "volumetricClouds", on): marched at a quarter of the screen across (legacy; half on modern), 18 steps + 2 light
    samples, laid over the picture at the cloud's own depth (gl_FragDepth) in the translucent pass. Coverage map 64 x 64
    x 48 blocks from WeatherSystem::stormAt/showerK and the zone (as the heaps used), so storms are the same clouds
    thicker, taller and darker; lightning flash, sunset, moonlight, far fade into the fog. The layer is y 720-776 (into
    the cloud bank: the Skylands' falls end in it). The 32^3 noise is built on a thread at first use. HD 520: 1.2 ms
    clear, ~2 ms in a storm. Off: the voxel heaps as before (the wisps stay either way).
  - Elytra: hung like the classic model (char_layer.cpp): 10 x 20 x 2 panels grown a pixel, hinged 5 px out at the
    top of the back 2 px behind, folded 15/15 deg (a V), gliding 90 deg out with 20 deg sweep (a steep dive folds them
    by 1 - (-dir.y)^1.5), sneaking 40/45 deg dropped 3 px. Art: tools/port/elytra_art.py draws a membrane-and-bone wing
    (no Minecraft picture) with the DokuQuest Base pack's black wool / bone block grain into the local overlay pack
    (entity/elytra.png 128 x 64 and the item icon); char_model.cpp reads the overlay's elytra first.
  - Dragons: tools/port/dragon_shape.py (run by data_patches.py, idempotent) gives all 24 dragon-rig models a chest,
    haunches and belly round a slimmer middle, thighs, feet with three claws, a fourth tail bone, jaw frills, nostrils
    and upper fangs (children of the rig's parts, sized from each model's boxes), and repaints dscale/dbelly/dwing for
    the eight kinds from their own colours (the ender wing's magenta grid is now a dark membrane with magenta veins).

## Night 2026-11-07 (Claude): performance on the user's HD 520 laptops, Aether waterfalls, Better End's pillars (uncommitted)
The user: "even on legacy opengl ... my Dell D10U 7040 and Dell 5414 rugged ... once 60fps, now lucky to stay at 10fps".
The dev machine IS the Latitude 5414 (i5-6300U, HD 520): `SDL_VIDEODRIVER=offscreen ./build/dokuquest ...` runs on the
real GPU with no window (llvmpipe under Xvfb says nothing useful about it). Profilers: `DQ_PROFILE=1` (CPU per scope,
fps, "hitch" lines over 100 ms), `=3` (GPU timestamps per scope, pipelined, plus GPU gaps), `=2` (glFinish per scope;
misleading on Intel: the GPU idles and clocks down), `DQ_PROFILE_FRAMES=n`, `DQ_PROFILE_SIM=1` (sim systems per tick),
`DQ_DEBUG_BANDS=1` (now also the quads actually drawn). Bench used: the user's New_World copied fresh each run.
  - Found and fixed (each was a stall or a per-frame cost that grew with the updates):
    * terrain arena growth copied the whole buffer into a bigger one: 0.4 s, 0.75 s, then 10.4 s stalls on shared memory.
      Now a new 64 MB page each time (world_renderer.cpp grow); the legacy path draws nearest-first across pages.
    * cave culling walked 40 sections out into a hash set: 11 ms CPU every frame. Now render distance + 3 into a flat
      grid (reachGrid): 0.37 ms, same sections drawn.
    * tile animations copied frames into the 2048-layer tile array every frame. Now every frame is already its own
      layer and a 64x64 R16UI map (tile_anim.cpp, 3 textures in turn) says which one shows; only vertices of animated
      tiles (ChunkVertex.e bit 30, set by the mesher from MeshTiles.anim) look it up (chunk.glsl animLayer).
    * light: block changes (fire, decoration spilling into loaded chunks: ~50 a frame in the old v6 Nether lands, each
      a light-15 re-flood) and new chunks' border merges ran unbounded on the main thread (100-335 ms). Now 4 ms of
      changes (deduped while queued) then 3 ms of arrivals a call (world/light.cpp flush).
    * dirty sections were all copied out for meshing in one frame (20-90 ms): now nearest first, 2.5 ms a frame
      (WorldRenderer::dirtyQ; `again` goes through it too). Chunk unloads (pack + save) 2 ms a frame (World::stream).
    * fluids scanned every waiting cell each tick (2.5 ms): now lists per due tick (worldsys.cpp buckets).
    * the far land's mask texture was re-sent every frame: now only on change, two textures in turn.
    * vines and glow lichen were voxel plates at any distance (2M of the leaves pass's quads): now only in the plant
      detail ring like other 3D plants (real ladders keep theirs). Fancy leaves skip faces 3+ leaf layers deep.
    * legacy far land refines at 3 tile widths, not 4.5 (2.5 ms instead of 3.9 ms on the HD 520; Modern keeps 4.5).
    * at most 3 frames queued on the GPU (fence after the swap, app.cpp): without it an unthrottled surface ran ahead
      until the driver stalled ~450 ms; with 2 the Intel GPU clocked down (51-57 fps), 3 gave 56-66 and no stalls.
  - Result on the HD 520 at 1920x1006, the user's settings (render 9, far 1024, fancy, waves): standing in their jungle
    44 -> ~60 fps; flying through new land no more freezes (Overworld 48-69, Nether 40-84, End 100+). Left: the GPU is
    the limit now (leaves pass 5 ms, opaque 3.6, water 3.6, far land 2.5, sky 1.5). /tp nether pauses 1.6 s (the
    command probes the exact Nether for a landing spot; portals don't).
  - Aether: liquids never leave their land through its floor (worldsys.cpp: band-local y 0 doesn't flow or spread; the
    top of a land isn't fed from the land above, so old floods drain); the falls end in the cloud bank with spray
    (mesher waterfall fx at the floor). No aercloud blocks in either Skylands generator any more (v7 chains/ribbons,
    v6 banks); the Silver dungeon keeps its cloud bed. Chunks already saved keep their clouds. lod_far.bin's tag has a
    generator revision ("/2") so the far land is made again.
  - Better End's pillars: be_building.py exports data/betterend/structures/pillars/*.nbt into be_structures.json
    ("pillars"); EndGen::bePillar builds them like its SpikeFeatureMixin (base 3 into the ground, top at ground +
    height - 64, obsidian column one narrower with crying obsidian on the rim 1 in 24). end_fight.cpp puts each crystal
    on the highest bedrock of its pillar's centre column (so old saved pillars still work). New block: flavolite_slab.
    Still not ported: Better End's eternal portal.

## Evening 2026-11-02 (alpha.15/16: the fiancée can't join over the internet)
- What the user found: the router, the host's firewall and the address are fine. Linux to Linux over mobile data by the
  external IP works. Her Windows PC from her house can't join (Windows never tested on LAN either). No VPN on the host
  (NordVPN is on another PC in the house; it was a red herring). Waiting on: what her screen says, her antivirus, her
  version, and whether the host's console shows "Heard from <her IP>" when she tries (alpha.16).
- alpha.16: net/server_host.cpp knockIntercept logs "Heard from ip:port" on the first packet of a new address (before
  the handshake) and "Listening on UDP port N". net/client.cpp hairpinIntercept accepts the host's answer from another
  address on the same port, or the same address on another port, while connecting (ENet still checks the connect id);
  ENet MTU 1200 on both ends. net/lan.cpp: adapters() (getifaddrs / GetAdaptersAddresses) -> localAddress() skips VPN
  adapters, vpnOn()/vpnAdvice() warnings (Open to LAN, server dashboard, console, join errors), firewallAllowSelf()
  (Windows: netsh via ShellExecuteEx runas; clears every rule for the exe, allows in+out) behind a Windows-only button
  on the Multiplayer screen. ui/multiplayer.cpp: saved servers (userDir()/servers.json: servers[{name,address}], last).
- Test for the port-changing router: scratchpad udp_proxy.py (client :25600 -> server :25590, answers from :25601).

## Afternoon 2026-11-02 (alpha.14 "Tractor Beam": the user's look at alpha.13)
- **Wither Storm like Story Mode:** tools/port/wither_storm.py (the model: obsidian mass, three heads with eyes, maws,
  mandibles, fangs, the command-block heart, six tentacles of four segments; rig "wither_storm"); sim/wither.cpp
  stormScale (0.35 -> 1 while it gathers, then 1 + grown/250 up to 2.6; creature_rig.cpp creatureBase scales by
  data.growS, the brain scales w/h), stormMaw, the tractor beam (magic_fx "beam" particles, now with size/width/life),
  torn blocks fly up it (blockfx.cpp spawnPulledBlock, FallingBlock "pull"), creatures are pulled in and eaten, wither
  sickness near the maw, roars; animStorm (tentacles, jaws, heads).
- **Hit back:** creature_entities.cpp deflectFireball takes any Projectile but thrown/cannon kinds, and entities with
  data.deflectable (rift bolts); punched-back fireballs land on fire-immune creatures (no fire element), shulker bolts
  go home (data.from). test_content checks wisp, Cinder Golem, shulker.
- **Villagers:** villager_look.cpp npcSkinTouches paints earrings/headbands/flowers on the base layer and flattens lone
  hat/jacket-layer pixels (glasses, monocles) until none are left; test_villagers checks 2261 skins.

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

### 2026-11-08: zombie pigman's missing leg
The pack's `entity/zombie_pigman.png` uses the pre-1.8 layout (left arm and leg blank, mirrored from the right), but the
spec read them from the 64x64 left-limb slots, so the left leg and arm drew nothing. `creature_models.cpp` now mirrors
them (like the zombie). The drowned skin does have its own left limbs, so it keeps them.

### 2026-11-08: creative clicks, the book menus
- **Lost clicks**: Input only kept "is the button down" at the end of a frame, so a click that went down and up inside
  one frame (common at low fps) never registered. `input.cpp` now latches presses (`mouseHit` / `keyHit`, cleared in
  beginFrame), so such a click counts as down for that frame. Creative also breaks the block on the press itself
  (Minecraft's startDestroyBlock), held = one per 0.25 s (`controller.cpp interact`). Test: `"hold":0` on a mouse step
  is now allowed (down and up in the same frame).
- **ui/book.h/.cpp**: BTA's guidebook in the DokuQuest Base pack's look (gui/book.png page + mirror, tab_right_*
  bookmarks, the book's arrows). Sections run on page to page; ←/→ d-pad/wheel turn, X/Tab next bookmark.
  Used by Help (`ui/help_book.cpp`, new BTA-era content), Journal / Skills / Spellbook (`magic_screens.cpp`) and the
  Stable (`content_screens.cpp`). Pixelify lacks ★ ✓ ▸: `drawStar` / `drawTick` draw them.
- **Achievements** (`achievements_screen.cpp`): an advancement-style tree per world (Overworld / Nether / The End) with
  the pack's window.png (9-sliced), tab_above_*, task/goal/challenge frames and stone/nether/end backgrounds.
  Fixed icons that pointed at missing items (wooden_* -> wood_*, dragon_egg -> dragon_egg_ender).
  Open issue: "Into Fire" asks for a blaze_rod, which no longer exists in the game, so it can't be earned.

### 2026-11-08: trommel, creative catalogue, dark pages, book inventory
- **Trommel** (BTA 8.0.1 TileEntityTrommel, decompiled): `tools/port/trommel.py` adds trommel / lit_trommel (a furnace
  kind, `"furnace": "trommel"`), its recipe (#stone_tool round iron_mesh over #log), pebble + olivine items with BTA's
  recipes, pictures from the pack, and `data/trommel.json` (BTA's weighted table, BTA items mapped to ours).
  `sim/trommel.cpp`: 4 inputs + fuel, one sift per 2.5 s round-robin, fuel x2 a furnace's, output out of the block's
  left side into a chest there or flung, 1/4000 small slime. tickFurnaces hands trommels over; inv_ops knows the 5
  slots (no output slot). Screen: `drawTrommel` in inventory.cpp (diamond of inputs round a turning drum, yields list).
- **Creative catalogue** `ui/creative_catalogue.*`: 16 tabs (all, building, colours, nature, functional, redstone,
  tools, combat, food, magic, materials, Nether, Skylands, End, eggs, forged), families kept together, hideCreative
  honoured. Classic panel: icon tabs; book inventory: bookmarks down both edges ("inv" = crafting).
- **Dark pages**: extra `bookDark` (Options > User Interface, or the moon/sun under any open book). `ui::ink` colours
  are switched by `setBookTheme`; `inkOf()` deepens UI colours only on light paper.
- **Book inventory**: extra `bookInventory` (default on). Left page: name, armour, doll, show/hide row, gear, then
  `drawSheet(..., paper=true)` (skills + stats in ink). Right: crafting + bag (survival / the "inv" bookmark), or the
  catalogue over the bag (creative). Recipe book opens on the left page. Everything still goes through the same
  hits/mouse/pad code (`finish()`), so controller focus works unchanged. Extras save at the top level of settings.json.
- **Achievements as a book** (`achievements_screen.cpp`, replaces the advancement-window tree): bookmarks Overview /
  Overworld / Nether / The End. Overview: total, a bar per world, the latest six. Each world: its achievements in
  tree order (depth-first from the roots in `places()`), the pack's task/goal/challenge frame with the icon, title,
  what it asks, "Earned <date>" or "After: <parent>".
- **Map as a book** (`map_screen.cpp`): right page = the map (drag, wheel zoom about the pointer, click pin, markers,
  compass rose, coordinates under it); left page = every map you carry (thumbnail, world, scale, explored, where it is;
  click or up/down / d-pad to switch), Zoom in/out, Find me, Whole map, pin + control notes. The other maps of the same
  world and scale are still laid out around the chosen one (MapStore::sameScale). Pad: stick pans, X/Y zoom, A find me.
- **Bookmarks as ribbons** (`drawBookTab` in book.cpp, all books + the creative bookmarks): BTA guidebook-style cloth
  ribbons with a swallowtail cut, drawn procedurally (12 cloth colours by index, weave lines, stitches — gold on the
  open one, the page's shadow where it comes out), all drawn before the pages so they come out from between them;
  the open one sits 3 units further out. The pack's advancement tab sprites are no longer used for bookmarks.
- **Elytra shape** (`tools/port/elytra_art.py` wing_face): redrawn after the reference — bone bar on top, bone down the
  hinge and outer edges, two ribs plus two cross struts (the zig-zag / diamond), and the trailing edge cut into deep
  scallops between the bone tips (fingers), feathered. The box's bottom face is now transparent: a solid one filled
  the mesh's last voxel row and joined the fingers into a straight edge (why they still read as rectangles).
- **Every inventory screen as a book** (`bookInventory`): chests / barrels / backpacks / ender chest, furnace / smoker /
  blast furnace, trommel, machines, stations, stonecutter / forge and the crafting table open in the book — the block
  on the left page (`bookPageBlock`: its name, then its own drawFurnace / drawTrommel / ... at page width; the table's
  recipe book under its 3x3), your backpack / bag / hotbar on the right (`bookPageBag`). `paper` switches those
  screens' words (status lines, "+", the trommel's finds) to the book's ink; the cutter's list is a dark card.

### 2026-11-08: the rest of BTA 8.0.1 (stage 1)
- `tools/port/bta_extras.py` (data_patches runs it after trommel.py): polished stone (+bricks, mossy, slab, stairs),
  polished limestone/gloomstone/netherrack, carved slabs of 8 stones, cobbled + mossy cobbled basalt/granite/limestone
  (from pebbles; smelt back), rubyglass block + bricks, ember coal block, sugar cane block (+baked), baked mud, steel
  glass, chainlink fence, gold/iron/steel/lapis brick slabs+stairs; chainmail (+ scrap to chainlinks), wolf armour (6),
  steel shears, steel fire striker, crude steel, golden/flaming/purple arrows, explosive charge, steel bucket, ice
  cream, rotary calendar, label, dough, cherries, cloth, ice skates; eucalyptus = gum (renamed, sapling), thorn log +
  sapling, shrub leaves + sapling, flowering cherry leaves (fruit: cherries); every *_branch -> 2 sticks.
- Behaviour: `sim/bta_items.cpp` (steel bucket charges on its tag, wolf armour use/remove/points/wear, label naming via
  `makeLabelScreen`), bow ammo kinds + arrow props (controller.cpp: grav, fire lights TNT/braziers, breaks, item back),
  explosive charge in guns.cpp (power 2.4), ice skates in player.cpp, calendar readout in hud.cpp, chainmail in the
  armoured-monster roll, wolf armour drawn in creature_layer.cpp (helmet + chestplate meshes on head/body), TREE_SHRUB
  in gen/trees.cpp (thornscrub, redlands, savanna, plains), seasonal cherries flower. stackName shows tag "name".
- Already present (no change): armoured zombies/skeletons (rollArmour), BTA's snowman = snow_sentry.
- Stage 2 (same day):
  - World types (`gen/world_types.cpp`, `OverworldGen::wtype`, ui/worlds.cpp cycle): amplified, islands, floating
    (islands over the void; session.cpp finds an island start), winter, hell, flat. Retro / Extended not done.
  - The Drift (REALM_DRIFT = 3, world type "drift"): `gen/drift.cpp` is BTA's DensityGeneratorDrift bit-exact;
    `sim/drift.cpp` the portal (glowstone frame + bedrock set on its bottom, as BTA; creative-only in survival terms),
    x8 travel, return portal built on arrival, chickens only; golden sky in render/sky.cpp.
  - Statues (shapes.json statue_<stone>_lower/_upper, two-block pair hook in bta_items.cpp), pebbles layer + throwing,
    autumn leaf piles (LeafFall system), soul wax candle.
  - Photo mode (`ui/photo_mode.cpp`, state in `client/photo_mode.h`): pause menu "Photo Mode..." or Act::PhotoMode
    (F6). Free camera (48-block leash) set in clientPlayerCamera; time-of-day swap around sky->computeFrame and
    season/fog in photoFrame (session.cpp); show player in char_layer.cpp; 9 filters in colour_filter.glsl (uPhoto,
    uAmt). Keyboard nav is off while open (Space flies). Pad: X toggles fly mode, A shoots, bumpers tilt.
  - The Giant (mobs/models "giant": ghoul model x2.7, 5.2 tall, boss): a crypt boss via the new mobs.json
    "dungeonBoss" key (structures_dungeons.cpp appends to the theme's boss list); `GiantStomp` in bta_items.cpp
    (0.75 s wind-up, 7 damage + throw within 4.8).
  - Tile budget: the set had reached 4097 (the HD 520 holds 4096). `dropUnusedFrames` (assets/packs.cpp) now drops
    built-in animation frames a pack's picture replaced or stilled, and a pack animation's frame 0 reuses the tile
    itself: 3898 tiles with DokuQuest Base (199 freed).
  - Bedrock had no item (so no creative entry, and the Drift portal couldn't be lit by hand): bta_extras.py adds one;
    mined with a tier-4 pickaxe it drops itself. `/tp drift` (driftTravel in sim/drift.cpp) goes in the portal's way;
    from the Drift `/tp <dimension>` comes back. Glowstone is named "Glow Lantern" in DokuQuest.
  - The Drift portal is a real portal now (PortalKind::Drift = 4, drift_portal "portal": 3, render 4): a live window
    and walk-through like Ember/Sky. PortalSystem::tick prelinks it to REALM_DRIFT at x8 on land (driftLanding,
    cached per window in driftLand) or back to REALM_MAIN at /8; arrivePortal builds the far glowstone frame.
    sim/drift.cpp keeps only the bedrock trigger, /tp drift and the chickens. portal.glsl uKind 4 = silver.
  - Fix: clientPlayerCamera now sets cam.realm (it stayed 0, so His realm / pocket / Drift skies never showed).
    The Drift's chunk climate is plains (204, 102) so pack grass is green, not teal.
  - Twin portals match in size (all kinds): arrivePortal reuses a nearby portal only if its opening is the same size,
    else builds beside it (clearOfPortals). The far end of a prelink is marked "made" in portalLinks; dest() queues a
    made twin of another size (legacy links: the one away from the main Overworld) and tick() rebuilds it in place
    (PortalSystem::reshape, same corner so the anchor/links stay; buildAt is arrivePortal's old build half).
  - Chunk climate was never saved: every chunk loaded from disk (and every chunk sent to a guest) had climate 0 =
    the coldest grass/foliage tint. encodeChunk now appends 'C' + 512 bytes after the coat (old readers stop
    before it); World::requestChunk regenerates climate for a loaded chunk that has none (one-time per old chunk).
  - placeAtSpawn (/tp spawn, respawn) from a side realm: requests the spawn chunks and stands on the generator's
    height when they aren't loaded (it fell through and out of the world).
  - Pebbles: the pebble item places the game's own Loose Stones (renamed "Pebbles", drop pebble; its item hidden from
    creative); the flat 'pebbles' carpet block is now a twin of it (kept so placed ones still load). Thrown in the air.
  - Rods: their pictures are Minecraft's sheets. The lightning rod now has its own Draw shape in world/shapes.cpp (tip
    4x4 + 2 px rod via tu/tv, like the end rod; it was invisible). Item models of Rod-shaped blocks come from
    shapedBlockBoxes (not itemshapes.json's plain boxes), and PxBox carries per-face fu/fv/swapUV (parts with `only`
    merged onto one box). Icon cache version dq-item-icons-8.
  - Mixed tools/armour (core/tool_parts): every ore now. tool_parts.py EXTRA adds tin, lapis, amber, quartz, amethyst,
    olivine, aether crystal, ender, mese (and chainmail, armour only) with "stats" (pickaxe tier/speed/damage/durability;
    armour as a share of iron's), colour from the item's own picture. A material with no set of its own: makePartTool
    takes its stats in proportion to iron's for the kind, makePartArmor iron's piece scaled; the crafting table makes a
    tool/piece all of it (Tin Pickaxe). data_patches runs tool_parts.py again after bta_extras (chainlink, olivine).
  - Worn mixed armour: partArmorWornLook returns the "pa:" key; armorMeshes draws iron's armour recoloured per crafting
    cell over the body (recolourWorn: grid's left column on the wearer's right; chest side columns = sleeves). The doll,
    armour stands and the first-person arm use it. Sleeve cuffs end at the wrist (body y 14): the hand is bare, no ring or plate.
  - All 44 materials make both tools and armour (anything mixes with anything): tool_parts.py EXTRA also has crude
    steel, frost (frost element), red crystal, rubyglass (fire), charged crystal, aurora, smaragdant (+shard), echo, rift,
    ambrosium, crocodile scale, dragonscale (fire; its own armour) and chainmail tools; OTHER_HALF gives wood, stone,
    flint, ruby, sapphire, obsidian, emberglass armour stats and leather tool stats. Dark item colours floored at 150.
    bta_extras.py: #planks takes every plank block (palm, gum, Better End's), #stone_tool every cobbled stone.
  - Test script: {"do":"craft","grid":[9 keys],"wear":bool} runs matchGrid and logs the stats; DQ_PHOTO front/up/pitch.
- Not done: retro grass/leaves/sapling, Retro/Extended world types.

### 2026-11-08: capes, hands, magic rework, the working village
- Capes: one solid piece (seg 0) in char_layer.cpp, lifted by walking and the wind (wloc.y blows it out, wloc.x sways
  it), pinned under a backpack. The two-part hem (the split) is gone from the draw.
- First person: HeldItemLayer::handsTaken (last frame's hook result) hides the body's left arm when a map (or anything)
  takes it: no third arm. Compass, death compass and clock lie face-up in the palm (item_layers.cpp isInstrument).
- Magic (tools/port/magic_rework.py + sim/magic*.cpp): every spell casts bare-handed at 0.7 power (spellNeedsStaff false,
  only a held staff wears); everyone gets Firebolt + Home Teleport once (stats.magicStart, MagicSystem::tickPlayer); a
  tome above your Magic is studied once for 15 xp/level (stats.studied) and kept; creative skips the gate; a spell kill
  gives Magic 6 + 3*mob xp (creature.cpp death, src.type "magic"); 10 starter tomes craft from book + lapis + a reagent;
  Apprentice Staff from amethyst or lapis too. Help and Spellbook text rewritten. tools/test_magic.cpp: 60 pass.
- Data purpose: venom sac brews strong poison; dough = 2 flour + water bucket (it was bread's recipe); goat horn is a war
  horn (bta_items.cpp: speed+strength 10 s to players within 16, 30 s cooldown). An audit (items with no recipe, smelt,
  prop, trade, material or code use) finds none left.
- Villages (sim/villager_chores.cpp, tools/port/village_rework.py):
  - The windmill is a machine (townbld.cpp bWindmill: windmill block at lz -2, axles, millstone on a loft at ly 9) with a
    miller (townBuildingPlan). Old windmills (already-generated chunks) keep the cloth sails; their miller hand-grinds.
  - parseVillage adds a builder, lumberjack, miner, fisherman (+ rancher/shepherd per pen, butcher) after the planned folk
    (VFolk.noBed: nights at the hall door). brainInner calls npcChore in work time (outdoor trades until 0.42).
  - Chores: farmers reap/replant/carry (wheat to the mill), miller loads the real millstone, baker fetches flour and
    bakes, lumberjack fells natural trees and replants, miner/fisherman gather at the nearest stone/shore (legs of 26
    blocks; unstuck when unseen), rancher/shepherd at the pens, crafts (mason, smiths, butcher, fletcher, innkeeper) turn
    stock into goods. Stock in villageRec[id].stock (seeded on first sight).
  - Shops: villageMakes items sell only from stock; villageUses items sold to shopkeepers go into it. Miller and miner
    shops added.
  - Damage: the three explode() functions record village blocks (villageDamage: key + meta); the builder puts them back
    nearest first within 6 blocks, skipping what it can't reach. (Fire never burns blocks away in this game.)
  - Gates: gate_barrier render 0 (the swinging overlay is the gate), outer gate columns swing with their leaf, E/W lintel
    axis 2.
  - Test script ops: village (report: buildings, folk, stock, sails power, millstone), tpbld, vstock. DQ_CHORE_DEBUG=1
    logs the gatherers and builder.

### 2026-11-08: the villages rebuilt from Better Village, axle icon, hub gap
- What Better Village is: its Java (Config, StructureSetMixin) only changes the village spacing/separation/salt; all of
  its look is the structure pieces, put together by Minecraft's jigsaw. So ours now does the same with its pieces.
- The pieces are game data: tools/port/village_import.py (in data_patches.py's last loop) copies its
  data/minecraft/structures/village/<style>/<houses|streets|town_centers|terminators>/*.nbt from ../bettervillage into
  data/villages/ and writes data/villages/index.json. Better Village is "All rights reserved" (jtorleon studios): don't
  commit or release data/villages without the author's leave. No mods folder is involved.
- gen/village_pieces.{h,cpp}: villagePieces() reads them once (index + readBinary, so data.dqa works): gzip + NBT
  (blocks list streamed), every Minecraft block state mapped to our block and its meta per quarter turn (stairs, slabs,
  doors, trapdoors, ladders, gates, beds, torches, lanterns, signs, banners, facing models, front-faced blocks, axes,
  crops, double plants, layers, pickles, cake, vines, waterlogging). Jigsaw blocks keep name/target/pool/final_state. Ice
  becomes packed ice (ours melts by lamplight); water and waterlogging a piece can't hold are dropped at load (the snowy
  butcher's water feature flooded the hill). The log's "village pieces: N in M pools" names any state left out (none).
- Assembly (gen/structures_jigsaw_village.cpp), Minecraft's jigsaw: a town centre at the old site, BFS over horizontal
  joints, candidates from the joint's pool (weighted shuffle, ~12% empty for houses, terminators as the streets' fallback
  and past the depth), quarter turns, joint faces opposite, no box overlap, depth 6/7/8 and reach 80/96/112 for village/
  town/city. Streets follow the final ground (bridged in planks over water; rejected when 3+ of 9 samples are wet);
  rigid pieces stand at the street joint's height (y0 = joint y - child joint y), refused over water or 7+ off the slope,
  the land levelled under each (+1, blended over 8: jigsawTownFlatten), plants/trees/snow cleared from their box, a dirt
  footing under layer 0. Our hall and windmill are pieces too (ourPiece: W x D+1 with an entrance joint in the porch row)
  placed on the first house joints (windmill not in deserts), built by townBuildingBuild.
- The plan for the villagers comes from the pieces: type/job from the name (farm, pen, stable, chapel, armorer,
  weaponsmith, toolsmith, butcher, fletcher, shepherd, mason, cartographer, librarian, fisherman, tannery -> merchant),
  beds from bed feet, door = the door nearest the entrance joint, work spot beside a job block, pens' beasts from the
  animal markers (sheep markers = sheep). The baker and merchant are "post" buildings at the hall's porch. The quest
  board is built on a post beside the centre, facing it; the bell is the centre's own (or one placed). Street ends feed
  townStreetEnds (roads) and villageStartPoint.
- Walls: towns (60%: palisade) and cities (stone) are walled 4 beyond everything, townWall/townGates as before. Street
  now carries its gate: ox|oz (lateral position), gy (ground), gate=false for the jigsaw streets' walk cells. A gate per
  side where a street block can run straight out past no building (6+ from the corners); its way out is paved
  (JigsawTown::paths) and levelled at the gate. No gate at all: no wall. The villagers' roads are the real street cells
  ("cells" [x,z,ground] in the plan; npcPickSpot and the guards' rounds use them).
- Worlds: terrain version 9 (generator.h, level.h; OverworldGen::jigsawVillages). Older saves keep the classic towns
  (their villages are half generated already). Without data/villages the classic towns are built.
- Axle/windmill/waterwheel: render/vehicle_layer.cpp hub boxes run the whole block (±0.5) so the hub meets the axle;
  item icons from village_rework.py itemshapes (icons.cpp cache "dq-item-icons-9").

### 2026-11-08 (later): villagers at work, the town mine, walkable streets, banners, bows, cape, mana HUD, smoke
- Villagers (sim/villagers.cpp parseVillage, villager_chores.cpp, villager_ai.cpp):
  - The extras (builder, lumberjack, miner, fisherman, rancher/shepherd per pen, butcher) only when no planned villager
    has the job; each works its place (miner: the mine's work spot, ranchers/shepherds: their own pen's middle) and
    sleeps in a ground-floor bed nobody planned uses (else at the hall). VBld.work parsed from the plan.
  - NpcMind.choreIdle: a long-day worker with nothing to do (no tree/stone/water found, waiting, nothing to mend) goes
    SOCIAL (strolls the streets) instead of standing at the hall door swinging.
  - findTree/findSurface: World::surfaceY (any height, not v.y-16..+28), half the looks out from the worker, the rest
    round the village edge (r*0.5 .. 1.2r+34); a leafy look finds its trunk up to 3 aside. Walled towns: workers going
    in or out walk via the nearest gate (VGate inX/inZ, 3 in/out).
  - The miner at the mine (VBld type "mine"): stands at lz 10, cuts the face (lx 2..4, ly -6..-4, lz 11) a block at a
    time into the stock (coal x2, iron_ore, copper_ore, cobblestone); when it's all cut the face is refilled at random.
- The town mine (structures_townbld.cpp bMine, townTemplateSize "mine" 7x12x6; jigsaw ourPiece after the windmill):
  headframe, a drift of cobblestone stairs 6 down, a 4-long tunnel with timber sets, torches, a rail line, a chest.
- Streets (structures_jigsaw_village.cpp): JigsawTown::street (every street column + the gate paths); halfStep puts a
  bottom slab (dirt_path_slab, or the block's own _slab) on a street block whose street neighbour is exactly one
  higher, so the climb is two half steps (step height is 0.6). dirt_path_slab: tools/port/village_rework.py.
- Converter (village_pieces.cpp): buttons/levers (floor 0, ceiling 1, wall 2 + toward-wall dir, |16 on x), tripwire
  hooks, unlit campfires (|8), banners as both halves (wall: lower one under it; standing: it becomes the lower and the
  upper goes on top). Banner meta bit 8 = flat to a wall (shapes.cpp clothBoxes shifts +z by 6.5).
- Banners placed by players (build.cpp placement): standing on a floor = lower here + upper above; on a wall (|8) or a
  ceiling = upper here + lower below; breaking one half removes the other (afterBreak). The chapel's banner was half.
- Bows: creature rigs (creature_layer bowHeldMatrix, skeletons) turned half round; villagers (villager_layer heldMat)
  had no bow case at all (sword grip): now the player's upright bow grip.
- Cape (char_layer.cpp): 8 strips (char_model capeMesh seg 10..17, 2 rows each) hung one from the other: half the lift
  at the shoulders, the rest curving down, a ripple running to the hem (more with wind/pace); never into the legs.
- Mana (ui/hud.cpp drawBottom): ten crystals right of the hearts (Minecraft's food row), draining from the left; the
  spell name over them 2.6 s after a change or a cast (and while a ward holds); armour moved over the hearts. The old
  bar (magic_fx drawHud, foreground list: it covered the villager talk) is gone.
- Smoke: env_layers' EM_CAMPFIRE no longer puffs smoke at the fire (content_layers campfireFx does it, up the chimney
  via campFlue for hearths): the houses' hearth smoke rose inside and through the roofs.
- Test ops: folk (every villager's job@pos within 200); village report has roads and wall/gates.

### 2026-11-08 (evening): Better Village's windmill works, furniture, stairs not slabs, smoke, bows, doors
- Smoke: content_layers campfireFx now starts an open fire's smoke at y + 0.98 (it began inside the campfire's block,
  which the rising particles took for a ceiling: every puff pooled at the fire, none rose). Hearths still go up the flue
  (campFlue). The env_layers puff at the fire stays gone.
- Windmills: ours is no longer a jigsaw piece. village_pieces.cpp MILLS turns plains_small_farm_1's wool pinwheel into
  our windmill on its hub (meta axis | 4 = the big sails), axles along its fence shaft, the millstone at the end; the
  piece then gets a second Bld (type windmill, job miller). render/vehicle_layer.cpp m_windmill_big: sails 7.4 out. A
  village with no windmill gets a miller post at the hall (the quern, by hand).
- Furniture (village_pieces.cpp): a fence under a pressure plate or carpet is always our <wood>_table; a lone scaffold or
  lone wooden top slab is a table when a stair-chair faces it (1-2 away); a wooden floor stair (not upside down, open
  above, not in a row of stairs) facing a table or with trapdoor/sign armrests both sides is our <wood>_chair (meta =
  the stair's rf), armrests removed. Logged: "village pieces: ... N chairs and M tables of ours" (89 and 20).
- Streets: the higher cell of a one-block rise is dirt_path_stairs (or the street block's own _stairs) rising away from
  the lower street cell (no ridges); the half slabs are gone (on a steady slope every cell got one: still a jump).
  dirt_path_stairs: tools/port/village_rework.py (4 from 6 dirt path).
- Workplaces: by the trade's own station (PREF in structures_jigsaw_village.cpp: the weaponsmith's anvil before the
  yard's grindstone...), indoors first. Anvils count as job blocks. (Better Village's weaponsmith yard has a decorative
  sharpening wheel: grindstone on a ceiling, anvil on it, observer, hoppers on cobble walls.)
- Bows: a carried "bow" (players' char_layer and villagers' villager_layer) is upright at the side, stave out, string to
  the body (bowCarryMatrix, l = +1); drawing ("bow_pulling_*") keeps the aiming grip; both at scale 1.0 (was 0.8). The
  fletcher works "idle" (hammering turned his bow flat).
- Door items: icons and held models are the door's item picture (item_models shapedBlockBoxes skips doors); icon cache
  dq-item-icons-10.
- Test ops: piece (stamp a village piece at the anchor), find (blocks of a key near you).

### 2026-11-08 (night): street levels, stable roofs, trades in houses, the bow at the side
- Streets have their own level (structures_jigsaw_village.cpp, JigsawTown::streetH): each street column's land height,
  smoothed over the street within 2, kept within (distance to the town centre) of the centre's level (the columns round
  the centre pinned to it), then eased until no column is more than a block off a neighbour (squeezed both ways: up).
  Rigid pieces are re-levelled from it (JPlaced parent/jx/jz/dy: y0 = the street's level at the joint + A.y - B.y; a
  piece on a piece: parent y0 + dy). Flatten: building boxes first, then street columns at their level, then a short
  ease beside streets (3) and the old pads/blends. Gate paths ramp from their street a block a step. Before: the street
  lay on the raw land and the houses' levelled pads cut across it (a test village had 76 steps of 2+; now 3 in 2395,
  where a house's own floor path meets a sloping street). Test op pathcheck counts them.
- Unlit campfires side by side or over nothing (Better Village's stable and snowy pen roofs) become oak slabs.
- The baker, the merchant and (with no windmill) the miller take a plain house with an oven / a counter (barrel, chest) /
  a bench (indoors first, nearest the centre), not the hall porch; only when none is free a post by the hall.
- Bows: the drawing grip for every bow again (bowHeldMatrix, scale 1.0); CharPose.bowArm (a carried bow, players and
  villagers) keeps the right arm hanging still (no talk/fold/read/idle gestures), so the bow stays upright at the side.
  First person: fp_bow sc and item_layers rest scale 0.85 -> 1.1.

### 2026-11-09: furniture in every style, benches, the weaponsmith's yard
- village_pieces.cpp furniture, now for every style's ways of making it (log line: "chairs/tables by style"):
  tables = a fence under a plate, a carpet, or (a lone post) a closed trapdoor; a lone scaffold; a lone top slab of any
  material; a closed trapdoor; a lone block of wood on a floor (the last four only when a seat faces them). Seats = any
  stairs (stone ones take the style's wood: plains oak, savanna acacia, snowy/taiga spruce, desert birch; stone ones
  only indoors and at a table); a wooden stair with its back to a two-high wall indoors is a chair too. Benches = a run
  of up to four stairs with a trapdoor or sign at both ends (indoors, yards, streets): every seat a chair, armrests gone.
  Counts now: plains 111/27, savanna 369/36, snowy 147/47, desert 18/4, taiga 10/0 (taiga sits on andesite round
  spruce-wood blocks with neighbours: left alone).
- The plains weaponsmith's yard "machine" (grindstone hung under an anvil, observer, trapdoors, hoppers on cobble walls
  with carpet: decoration) is now a grindstone on the ground with an anvil and a barrel beside it.

### 2026-11-09: the furniture pass rewritten (village_pieces.cpp), cupboards, slab staircases, house doors
- Order: (1) cupboards: unlit campfires with a door beside them (or under/over one that has) become <wood>_cabinet
  facing out through the doors (wood from the door); the doors, the trapdoors round them and the lid go (Better Village's
  2x2 pantries; villagers had taken their doors for the house's). (2) the other unlit campfires side by side / over
  nothing: oak slabs (roofs). (3) a bottom slab whose neighbour one block up is a bottom slab or a stair: a stair facing
  it (the toolsmith's slab staircase, every step a jump). (4) tables: a fence/wall/scaffold under a carpet, a plate or
  a closed trapdoor (lone posts for trapdoors); an upside-down stair with a carpet; a closed top trapdoor with open
  trapdoors for legs at its ends (the legs go); weak ones (a lone scaffold, upside-down stair, closed top trapdoor, top
  slab, block of wood) only when a seat faces them. (5) seats: a stair the right way up on a room's floor, open above,
  not a step (nothing to climb onto behind it, no stair down in front), with a table 1-2 in front, a backrest (open
  trapdoors behind at y/y+1, a sign) or armrests both sides: <wood>_chair (meta = the stair's); runs of 2-4 with
  armrests at both ends: <wood>_bench. Backrests and armrests go. Stone seats take the style's wood.
- "On a floor" = a whole block under it that isn't raised out of a floor beside it (no tables on counters).
- The plan's house door must have open space both sides (structures_jigsaw_village.cpp); else it costs +1000.
- DQ_FURN_DEBUG=1 logs seats/tables per piece; DQ_FURN_DEBUG=<piece name> logs each chair and why.
- Test ops: dump (a box's blocks layer by layer), piece "cut" (a cutaway), pathcheck.
- Now: 343 chairs and benches, 331 tables and cabinets (plains 66/78, savanna 154/183, snowy 89/62, desert 13/8, taiga 21/0).

### 2026-11-09 (later): water, the mine's exit, seats in rows, lantern chains, the fish tank
- Villagers and water (villager_ai.cpp): npcPath never steps into water unless it starts in it (it was +14 cost: the
  town-centre fountains' basins were still taken); npcPickSpot's square spots are dry ground with room (12 tries);
  npcStuckCheck: 3 s in water -> out onto the nearest dry standing spot within 6 (NpcMind.wetN).
- The mine (bMine): a stair at lz 0, ly 0 (rising out) with headroom: the drift's top step was a whole block below the
  ground. Walked out in survival without jumping (tunnel floor 577 -> street 585).
- Seats (village_pieces.cpp): one rule for rows of stairs side by side the same way round (1 = chair, 2-4 = bench):
  seating when an armrest (open trapdoor, sign) is at either end, a backrest behind one (open trapdoors, a sign, a window
  over the wall behind a wooden row), or a table 1-2 in front of one; stone rows need a table or a roof; rows over 4 need
  a table. "A step up" now means a raised floor that goes on (two blocks deep), not a lone block (a target, a barrel)
  behind it. DQ_FURN_LEFT=1 logs every seat-like stair left, with what's round it (now: door steps, eaves, ledges).
- Lantern chains (mesh_shapes.cpp): a hanging lantern under a top slab or a block carved to its upper half stretches
  its chain half a block to meet it (the hall's porch lanterns under the slab canopy).
- The fisher cottage's fish tank: the open trapdoors round indoor water become glass panes.
- Test ops: list (blocks by key in a box), lanterns (what hanging lanterns hang from); folk marks (IN WATER).

### 2026-11-09 (night): water kept as built, a water-sim bug, every style's furniture checked
- village_pieces.cpp keeps Minecraft's water with its levels (meta = level: 0 still, 1-7 running, 8 falling; ours are
  the same). The old "can it hold its water" pass is gone for buildings (it removed the taiga farms' stepped irrigation
  once the running water was dropped); streets still lose their puddles.
- sim/worldsys.cpp (all worlds, players' builds too): only still water in its own block fills a carved block's empty
  octants. Running water and waterlogged blocks did too, and each block it filled became a spring of its own: a slab
  floor beside a waterfall flooded without end (snowy butcher, taiga temple, the meeting points).
- Checked: all 44 pieces with water stamped into a flat world at ground level and run: every one holds exactly its
  water, except taiga_small_farm_1 (47 -> 95: a terrace channel Minecraft also leaves open spills outside its back
  fence). (A test stamped in the air floods everything: the flat world's top is y 576 for seed flat2.)
- Furniture: rows facing a plain block or top slab at seat height (open above, on a floor; not a container, a station,
  a bookshelf, furniture, ground, snow, bales) take it as a table; sofas = 2-4 wooden seats indoors closed in at both
  ends (not snow/grass/stairs/slabs), nothing stepped or a door behind; a table alone doesn't seat a stair whose back
  is a stair/slab or whose front is snow; slab and upside-down-stair floors count as floors (the plains small house's
  balcony sofas); leaves set on a flower pot go. Every conversion logs with DQ_FURN_ALL=1 (seat ... why ... | back
  front ends; tbl ... was ...): reviewed by pattern per style. Now 442 chairs/benches, 403 tables/cabinets.
- Test ops: list "count" with "tag".

### 2026-11-09 (late): farms by lakes, the slab-rimmed pond
- structures_jigsaw_village.cpp: a rigid piece's footprint is sampled for water every 3 blocks (not just its corners),
  and a spot whose ground is below sea level is refused (a taiga farm sat half in a lake, the lake inside it). On build,
  liquids inside a rigid piece's box are cleared and the ground under it filled (grass/sand at y0, dirt below).
- village_pieces.cpp: a building's water whose side is open at its own level gets that side closed with the block under
  it, but only where our sim does run water sideways (FluidSystem::update): not level 7, not over an open cell, not
  running water resting on water (fountain tiers). Also through a slab/stair rim beside still water: Minecraft's slab
  stays dry and holds the pond, ours is a chiseled block that takes the water into its empty half and runs it out of
  its far side (taiga_small_farm_1: 47 -> 95). Only that farm gets plugs now (2 blocks); DQ_PLUG_DEBUG=1 logs them.
- Checked: all 44 water pieces hold exactly their template water; the user's seed (-1052856227) regenerated, the taiga
  farm by the lake at 529,586,-88 is on dry ground with only its channels wet.

### 2026-11-09 (late): town wall towers at the gates
- structures_villages.cpp wallTowers: towers stand either side of each gate where its street meets the wall (r.ox / r.oz),
  for gate streets only. They were put at the square's lines (v.x / v.z), right for classic towns only; a jigsaw town's
  walks are in v.roads too (gate = false, d 0), so every walk put a pair of towers over plain wall on the north side.
  The gate banners (townLife) likewise. A tower's inner side is picked from the wall it stands on, not from the square.
- A palisade tower left the wall open under its platform (three blocks wide, a way in): the wall line runs on through
  it now, logs up to the wall's height.
- The user's "wall jogs, doubled walls, gate doors loose in the sand" (seed -404877859, Dorbridge): the world was made
  in two sessions on two builds (before and after the lake-placement change), so chunks of each followed a different
  plan (walls 2-3 blocks apart at chunk borders). A fresh world of the seed has one straight wall a side; nothing in
  the plan varies with chunk order. Changing generation mid-world does this; a new world is needed to see a fix.

### 2026-11-09 (late): the Wither as Minecraft has it; the Wither Storm mod's sounds
- sim/wither.cpp witherBrain (WitherBoss): 11 s charge (untouchable, still, swelling 0.75 -> 1 via data growS, health
  filling from a third), then a power-7 burst (mobGriefing) and fx.wither_spawn heard everywhere. Mends 1 HP/s. Three
  heads: the middle one at its mark (the player within 40, else the nearest living non-undead within 20; a skull every
  2 s within 20 it can see, blue 1 in 1000); the side heads each pick a random living thing within 20 (8 up/down) and
  fire every 2-3 s while they see it within 30; a side head idle 15 updates fires a blue skull at a random spot. Keeps
  5 over its mark, comes down to it once armoured (<= half: arrows glance), closes to 3. Hurt (not by wither, fall,
  drowning, walls), a second later it breaks every block in 3x4x3 round its body (not bedrock/hardness >= 50).
  Rig anim "wither": each head turns to its own aim (data aim0..2). Test op `mob {entity, hp?}` logs/sets a creature.
- Wither skull (creature_entities.cpp): hits creatures too (8, creature witherT 10 s = 1 HP/s, can kill; wither
  skeletons/Wither/Storm immune) and players (8 + Wither II 10 s); a kill heals the Wither 5; power 1 burst. Blue
  skull (data blue): speed 6, 12 s life, ExplodeOpts::tough (breaks obsidian, not bedrock: explosion.cpp caps at 50).
  Drawn as the wither_skeleton_skull item model, face first (creature_layer.cpp).
- Its voice: tools/port/wither_sounds.py (data_patches.py after wither_storm.py): vx.wither/_hurt/_death,
  fx.wither_spawn/_shoot/_break synth recipes; mobs.json wither voice "wither". (The pack has no wither sounds.)
- The Wither Storm mod's own recordings (Cracker's Wither Storm Mod 3.5.0.1, ~/Documents/Projects/witherstorm,
  All rights reserved: don't release without leave): tools/port/sound_analyze.py `merge <mod assets/witherstormmod> ws
  data/sounds.json data/sounds.bin` analysed its 151 non-music sounds into profiles "ws/..." and 61 events fx.<event>
  (fx.wither_storm_roar, _tractor_beam, _bite, _shoot, _thump, _evolves, _death, fx.command_block_*, fx.formidibomb_*,
  fx.withered_symbiont_*, fx.ws_tremble ...). Run it again after any `analyze` of the pack. No ffmpeg here: decode()
  falls back to soundfile (pip) + numpy FFT resampling. Its music (themes) isn't imported (ours is composed scores).
- Storm spec from the mod: docs/wither_storm_mod_spec.md (phases, eating, beams, defeat, look, symbiont, bowels).

### 2026-11-09 (late): the story's end: the Storm in his realm, his escape, the signs after (story; never in the notes)
- sim/rift.cpp: in his last hall, below half health Herobrine raises the Wither Storm himself (heroRaiseStorm, rift
  record "storm": 0 not yet, 1 raising, 2 awake, 3 broken): he steps before a cleared spot at the hall's middle
  (x (HALLS-1)*HALL+48, z 0) and over ~7 s sets soul sand, a command block at the T's heart (fx.command_block_summon),
  two skulls, then the last skull on the command block (fx.command_block_activates) -> tryWitherSummon -> the Storm,
  flagged finale (1.5x HP, can't go below a quarter till its heart shows). While it lives he's warded
  (riftHeroWarded, creature_monsters.cpp realmPreHurt) and only watches from the hall's far end, blinking away from
  anyone within 10. A scene cut short by a reload, or a Storm gone some other way, makes him raise it again. Broken
  (wither.cpp stormFalls finale: storm 3, stormBroken), his ward goes and he fights on.
- Beaten, he doesn't die: riftSeal opens a torn window where he stood showing the first player's home (2.4 s), removes
  him, and seals. "the watching is over" is gone. The rift-core finale (player-built) is gone: a rift core or command
  block T makes an ordinary Storm. Journal: "The Storm" (break the one he raises) now comes before "The Seal"; the
  elder's sealed line no longer sends you to build one. `/rift go` opens the gateway first if needed (debug).
- sim/haunt.cpp afterSealTick: after the seal, no sightings or tunnels; signs only (AFTER_SIGNS: "I'm still here",
  "I was here first", "This isn't over", "Did you miss me?", "You shut a door. Not me.", "Welcome home", "Still here",
  "I can wait"): one by your home (bed, else world spawn) when you come back to it from 96+ away (50%, at most every
  20-40 min of play, first one 10-20 min after the seal), one out in the world every 40-80 min. "haunting" rule off:
  none. flags.afterSigns counts them.
- The Storm's sounds are the mod's now (roar/ambient, bite, shoot, tractor beam, death; the global wither_spawn cry
  when it bursts out). In his realm it tears at the roof and walls, never the floor (three blocks over the void).

### 2026-11-09 (late): the Story Mode Wither Storm (sim/wither_storm.cpp; spec docs/wither_storm_mod_spec.md)
- The Storm left sim/wither.cpp for its own file; it's a CreatureHooks brain + beforeHurt now ("wither_storm"), and
  creature.cpp moves it without collisions (it flies through what it eats). Phases 0-5 by what it has eaten (data
  eaten; GROW 60/240/700/2000/6000; in his realm every eaten thing counts thrice): each phase its own model through
  Creature::variant (wither_storm_p0..p3 from tools/port/wither_storm.py young(): the Wither + a caged command block,
  hump, a jawed storm head, tentacles; then the destroyer "wither_storm"), size (data growS, eased), box and hover height.
  12 s charge, power-7 burst, the Wither's cry; young ones use the Wither's voice and mend 10 HP/s (400 HP), grown ones
  take nothing.
- Eating: block clusters ("block_cluster" entities: blocks torn out whole round a spot, tremble 0.6-1.4 s, spiral into
  the head that took them, swallowed = eaten += blocks; let go (knock-down/death/40 s) they fall apart into falling
  blocks). Single blocks every max(0.25, 3 - eaten x 0.004) s (1/3/6/10 by phase) while young, radius 3 clumps every 2 s
  grown; at most 120 in the air. In his realm it eats up from the floor (pillars, crystals, the roof), never the floor.
- Heads (data tgt/aim/mouth/inj/hits 0..2): each its own mark (players 75%), never anything under it (10 blocks
  sideways); tractor beams on the middle head from phase 2, all three from 4: pulls up to the mouth, chomps players 3.5
  per 0.5 s, swallows creatures; the beam tears clusters where it bites. Beamless heads spit skulls. A blow counts on
  the head nearest the attacker (explosions only within 5/12): 2 hits (young) / 4 (grown) injure it 9/36 s (beam off,
  scream, a blue skull, its mark ignored 40 s).
- Super TNT (4 TNT + 4 gunpowder round a rift crystal) and the Formidibomb (Super TNT + 4 cinder powder + 4
  gunpowder): blocks from tools/port/wither_storm.py bombs(), lit with flint and steel (build.cpp useFlint ->
  primeFormidi). The Formidibomb pulses 30 s; the nearest awake Storm within 170 drags it into its maw, where it goes
  off in 2.5 s: knockDown -> FALLING -> lands (thump, shake) -> DOWN on its back, a podium of bone beside it with its
  command block ("storm_core" creature: 4 HP, any player hand blow = 1, nothing else) -> 4 blows -> DYING 6 s -> dies
  (stormFalls; his realm's one breaks Herobrine's ward). Left 120 s it wakes (reactivates sound, power-8 no-block burst)
  and rises. Test op `formidi {x,y,z,key?}`.
- Render: falling blocks are drawn now (they weren't drawn at all) and clusters (creature_layer.cpp); the Storm is seen
  from 480 away; its boss bar shows within 200 (hud.cpp); a grown one (phase 4+) tints the sky (20,0,19)/fog
  (133,69,62) within 200 fading by 400, 60% / 85% (sky.cpp).
- Not done from the mod: phases 6-7 (splitting, the Bowels), the Withered Symbiont, sickened mobs, the beacons, the
  Command Block Book (Super TNT uses a rift crystal instead), flaming skulls, distractions, chase/distracted travel.

### 2026-11-09 (later still): the mod's own looks; beaten with worthy blows (user: "doesn't look like the one in my files")
- tools/port/wither_storm_models.py runs the mod's model builders (a little interpreter for its decompiled Java:
  CFR into the scratchpad, `java -jar /tmp/cfr.jar --outputdir <dir> <the jar's .class files>`) and writes
  wither_storm_p0..p3, wither_storm (the destroyer) and wither_storm_p5 into data/models.json (model scale 2, the
  mod's renderer's; each piece's own scale baked in: mass x10, heads x0.7/x3, tentacles x0.5/x2/x3), and copies its
  texture to data/textures/entity/wither_storm.png with the emissive eyes painted in (third-party art, local only).
  Minecraft model space -> ours: x and y flip, rotations re-solved for our Y-X-Z order; head roots rest turned pi (the
  mod's HeadModel). Names by path: center_head/right_head/left_head (the Wither's), head0..2 (storm heads; the
  destroyer's middle one is head1), "headN/lowerJaw", "tentacleN/base/segment1/...". wither_storm.py and
  data_patches.py leave these models alone once they exist (uvTex).
- Engine: a model may name its own picture (uvTex, uvTexW) and its parts take faces from it Minecraft-fashion (part uv
  [u,v,w,h,d], mirror) through the pack-skin face cutter; a part may be a voxel list (vox {n, c: [[x,y,z,w,h,d,rgb]]};
  [] = pivot only) and choose its voxels per block (res). creature_models.cpp / creature_rig.*.
- sim/wither_storm.cpp: LOOK by phase (box, HP 400/400/500/600/1024/1500, hover), MOUTH table from the models; the
  animation is the mod's (tentacle sway formula, heads aim, storm heads' pitch inverted, jaws gape on a roar).
  Hurting it: stormWorth: magic (spells, splash healing on the undead) and arrows full; a player's enchanted hand
  weapon 0.6 + 0.15/level + 0.25/Smite level (max x3); plain blows, fire and explosions nothing (a "glance off" hint
  every 20 s). It mends 1 HP/s young, 2 grown. Its last blow: it falls ("final"), lands, DYING 6 s, dies. The
  Formidibomb is optional now: knocked down it lies 30 s, everything (but explosions) x2, then rises. The podium and its
  command block creature (storm_core) are gone: it couldn't be hit (the Storm's box took every blow) and could end up
  in the ground under it.
- Fixed after the user saw it: the tentacles stood up into the sky. The converter read the mod's tentacle angles only
  when written as plain numbers; several are (float)Math.toRadians(90.0), which it dropped as 0 (now evaluated), and the
  sideways curl goes on segments 2-6, not only 2. Checked: a tentacle tip computed through the mod's own transforms and
  through our converted model (the engine's Y-X-Z order) lands on the same point. The evolved destroyer's tentacles hang
  under it like legs; the destroyer keeps one arching over its back (the mod's own pose).
- Arrows: only an enchanted bow's hurt it (user). controller.cpp stores the bow's enchantment levels on the arrow (data
  ench) -> DamageSource/DamageSrc ench -> stormWorth 0.6 + 0.15/level (max x3); plain arrows bounce off. Test op hurt
  takes "ench".
- /kill didn't kill the Storm (user): it hurts with type "void", which stormWorth turned aside. Creature::hurt/hurtBase
  now let "void" (/kill, falling out of the world) past every creature's own filters (CreatureHooks beforeHurt,
  realmPreHurt: the Storm's worthy-blows rule, a charging Wither, Herobrine's ward, endermen...), invulnerable moments and
  an old save's hpFloor.

### 2026-11-09 (late): quivers worn, beside the backpack (user)
- Player::quiver (1 slot, saved as inv.quiver). sim/quiver.h/.cpp: a quiver holds four slots of arrows of any kind in
  its stack's tag "c" (quiverLoad/quiverSave; an older quiver's "arrows" count comes out as stacks of plain arrows); the
  golden quiver has no slots and never runs dry. Bow (controller.cpp): off hand, then the worn quiver, then one carried
  in the bag, then loose arrows; it shoots whatever kind came out (quiverTake returns it). Player::give puts arrows into
  the worn quiver first (quiverStow). In the hand the old use stays: gather loose arrows in, crouch to tip them out.
- inv_ops: Grp::Quiver ("quiver", only quivers) and Grp::Qv ("qv", the worn one's arrows, only arrows), stored like the
  backpack and its contents; shift-click puts a quiver on and arrows into it. Book inventory: a Quiver slot beside Pack
  (2-col gear grid, 6) and a Quiver row under Backpack; classic: a Quiver block at the foot of the top section under the
  sheet (slot + 4). Show/hide gains a 7th toggle (hideGear[6]: sheet.cpp GEAR_VIS, char_layer gearHidden).
- Drawn (char_layer.cpp): slung across the back (mouth over the shoulder) or hung on the backpack's side opposite the
  lantern, three fletchings when it holds arrows; CharWear quiver/quiverFull. Net: a player's snapshot carries bk (their
  backpack, which other players never saw before) and qv/qf, applied to the replica's data back/quiver/quiverFull.
- Test ops: wear {item: "quiver", arrows: n}; report shows "quiver <id> <arrows>".

### 2026-11-09 (night): 3D quivers that fill, the book page fits, bows held as archers hold them (user)
- Quiver look (assets/quiver3d.h quiverShape, one shape for all): a leather tube, open rolled rim dark inside, base cap,
  band, strap and buckle; fill 0..4 stands 0/2/4/6/8 arrows (shaft + crossed white fletchings) in its mouth. Held/dropped:
  item_parts.cpp (empty; the golden one full). Worn: char_model.cpp quiverWornMesh(key, fill, ts) (0.25-px voxels, cached
  per fill), drawn by char_layer.cpp slung hip-to-shoulder (strap to the back) or upright on the backpack's side pocket.
  sim/quiver.h quiverFill(q): 0 empty, else 1 + (n-1)/64 capped at 4 (golden 4). CharWear.quiverFill; net qf = the level
  (replica data "quiverFill").
- Book inventory, crafting page: the quiver's 4 slots stand 2x2 LEFT of the 2x2 crafting grid (caption "Quiver" under
  them) — no extra row, so a pack + quiver no longer runs off the page. If the page would still overflow (explorer pack,
  3 rows) the recipe-book button moves beside the result slot. The block-screen bag page keeps its Quiver row (room).
- Bow, third person (render/char_anim.cpp bowDrawArms + char_layer.cpp drawnBow): drawing turns the body side-on
  (0.5..1.0 rad, the legs half as far) and two-bone IK reaches the left fist out along the aim (pitch from headPitch) and
  the right hand to just in front of the chin; the bow is then hung on the real hand positions: grip in the left fist,
  upright (canted 0.26 toward the draw hand), belly to the target, string bent to the right hand (bow_pq0..8 chosen from
  the hands' distance, ~6/8 at full draw — the arms' reach limits it). BOW_SCALE 0.85 / BOW_FIST 6.3 in char_anim.h.
- Bow carried (player, villagers, creatures): held as it's shot with the arm lowered — level at the side, limbs fore and
  aft, belly down, string on top (user: "flipped at the grip"); the carrying arm hangs nearly straight so it lies level.
  The grip is the true stave middle (-0.235, 0.235); it was (-0.2, 0.15), 1.5 px out of the fist. Skeletons aiming
  (creature_layer.cpp bowDrawnMatrix): upright across the raised arm, belly out, fist on the grip (it lay along the arm).
- Crash fix (sim/gameplay.cpp GameplaySystem::tick): the once-a-second random ticks walked lv.entities() while a random
  tick could add an entity (vector realloc -> garbage pointer, SIGSEGV, ~1 in 5 fresh worlds). Players collected first.
- Dev switches: DQ_VIEW=3 (front camera), DQ_DEV_TURN=<rad> (turn the model to see it from the side), DQ_DEV_PULL=<0..1>
  with DQ_DEV_POSE=bow (a held draw). Test op wear {item:"none", slot:"back"|"quiver"|"cape"} takes it off; wearing a
  backpack now replaces Player::back.

### 2026-11-09 (night): a real crawl; holes and lines along village paths (user)
- Crawl (render/char_anim.cpp crawlLimbs): CharPose.crawl/crawlPh/crawlMv (char_layer: crawling no longer sets swim;
  crawlPh advances with ground speed, ~1.4 blocks a stroke). charRig lays the body on its chest (2.3 px up, no pitch);
  reachArm puts each hand beside the head, alternately pulled to the chin and reached to the brow (the moving one lifts),
  elbows out; the knee across from the reaching hand draws up and out, the other leg lies straight; head lifted 1.2 rad.
  Every "lying down" test in char_layer/char_anim (body yaw, maps, carried things, ladders, fp torso) counts crawl too.
  DQ_DEV_POSE=crawl shows it.
- See-through at path stairs (assets/shaders/chunk.glsl): the pack's dirt_path_side has its top 1/16 clear (paths are
  15/16 tall); on a path stair's riser the relief march ran off the face's bottom edge, wrapped to those clear rows and
  the cutout pass discarded them — sky through the foot of every path stair. Now only the face's own texel can be a hole.
- render/mesher.cpp: a grassy block's side (bottom tile dirt) against a Path-shaped neighbour draws dirt — the grass /
  snow band of its top 1/16 read as a green or white crack along streets. A top under a solid shaped block standing on
  all of it (stairs, bottom slab, path, snow layer; not scaffold/rail/translucent) is culled (shapeStandsOnAll): its
  relief poked teal bumps out round the stair's foot.
- render/mesh_shapes.cpp emitBox: a shaped block's face beside a CHISELED neighbour tested one octant on the far side and
  culled when it was carved away (inverted): now culled only when every touching octant under the face is present.
- Test: dump now prints a chiseled block's carve mask (key/meta^carve).
- Path stairs and slabs (world/shapes.cpp pathTopped / isPathTopped: any "dirt_path*" block that isn't the Path shape)
  top out at 15/16 like the path, for every purpose (draw, collide, select): no 1/16 lip and no see-through slot where a
  stair's top meets the path beside it (their step-box sides used to reach the pack picture's clear top rows). The
  grassy-side-shows-dirt rule (mesher.cpp) counts them as paths too.

### 2026-11-09 (night): our own Wither Storm picture; the Wither in its likeness (user)
- tools/port/wither_storm_texture.py paints data/textures/entity/wither_storm.png from nothing (no mod pixels): it
  reads only the box layouts (uv) of the Storm models in data/models.json and gives each face its stuff — obsidian-black
  mass and tentacles with a few purple veins, dark bone, skulls with deep sockets and purple-lit eyes, the insides of the
  mouths glowing purple, pale teeth, an amber command block with a steel plate. The voxel masses get the body colour.
  Run by data_patches.py (after wither_storm.py) and by wither_storm_models.py (which no longer saves the mod's picture).
- The Wither (wither_like_storm in the same tool): models['wither'] = the Storm's first form (wither_storm_p0) without
  its cage of ribs and its command block, the same picture, scale 2, heads renamed head/headL/headR for animWither.
  No growing (that's the Storm's rig). witherHead() moved to the new heads (3.0 up; 2.62 up and 1.12 aside); mobs.json
  wither h 3.5.
- Still from Cracker's Wither Storm Mod (All rights reserved): the converted model shapes and the "ws/..." sound
  profiles in data/sounds.json/.bin; Better Village's pieces in data/villages. See the push notes.
- The command block in the Storm's cage is our real block: a rig part may carry "item" (RigPart::item, creature_rig.cpp);
  render/creature_layer.cpp draws itemModel(item) filling the part's box (min..max) at the part's matrix, after the
  parts. wither_storm_texture.py turns each Storm model's "block" part into vox [] + item "command_block" (it was 4x4
  texels of the Storm's picture). The pack's command block shows when a pack is on.
- Faces (face_of): two eyes on every head, set in from the sides with a gap between them (the 6-wide side heads had both
  1-px eyes side by side: one wide eye, a cyclops), the hot centre in each, a toothed mouth under them.

### 2026-11-09/10: our own village buildings in 18 styles (replacing Better Village's; user)
- Better Village (All Rights Reserved, its own repo's license.txt) is gone from the game: tools/port/village_import.py is
  out of data_patches.py; tools/port/village_builder.py builds every piece (data_patches.py runs it last). Its rules came
  from studying Better Village's pieces and ChoiceTheorem's Overhauled Village (CC BY-NC-ND; studied only, nothing
  copied): sizes, materials per style, building types, street joints. Every piece is generated from a seed (its name).
- Styles (STYLES; data/villages/<style>/, listed in data/villages/styles.json for the game): plains, savanna, taiga,
  snowy, desert (the five), beach (stilts, thatch), jungle (stilts, leafy thatch, vines), jungle_tree (houses on decks up
  jungle trees, a spiral stair round a post to each), swamp (stilts, mud brick, moss on the roofs), mountain (tiered
  slate pagoda roofs), alpine (chalets: big eaves, balconies), mesa (adobe terracotta, vigas), oasis (desert + palms),
  savanna_hut (round mud huts, thatch cones), mushroom (homes in giant mushrooms), igloo (snow domes, ice windows, a
  tunnel), christmas (lanterns along the eaves, wreaths, decorated trees, presents, snowmen), halloween (dark oak and
  slate, jack o'lanterns, cobwebs, graves, soul lanterns, a pumpkin-patch square).
- Each style: 6 small, 3 medium, 2 big houses; workshops for every trade (armorer, weaponsmith, toolsmith, butcher,
  fletcher, shepherd, mason, cartographer, fisher, tannery/merchant, bakery), library, temple (cleric), tavern
  (innkeeper), sawmill (lumberjack), builders_yard (builder), barracks (guards), jeweller, a windmill (great sails on a
  hub, axles to a millstone: the miller's), 3 farms, 2 pens (one sheep), a stable, market stalls for the merchant,
  baker, butcher, fisherman, farmer and jeweller; 6 straights, 2 corners, 2 crossroads, 2 T-junctions, 3 town squares
  (well, market, great tree / oasis / palms / Christmas tree / pumpkin patch / giant mushroom), 2 street ends. 1062.
- Game: gen/structures_townbld.cpp vstyle() reads the five from VSTYLE and the rest from data/villages/styles.json
  (VStyle.base: the base style it's a kind of; vbase()); the old VS_DESERT/VS_SNOWY ground checks use vbase(); the
  village record carries "base" (sim: the golem, the builders' wood/stone). Terrain version 10 (generator.h, level.h):
  gen/structures_villages.cpp villageStyle10 picks by the land (beach biome, Redlands -> mesa, Glacier -> igloo,
  Mossmire -> swamp, Highlands/cold mountains -> alpine, mountains -> mountain, hot and wet -> jungle/treehouses, hot
  and dry -> savanna/huts, cold -> snowy/igloo/christmas, forest -> a share halloween, a few mushroom); beach and
  mountain sites allowed (looser wet/relief limits for beach and swamp, relief for mountains). DQ_VILLAGE_STYLE=<key>
  forces every village's style (tests). Old worlds keep version 9 (the five) until upgraded.
- gen/structures_jigsaw_village.cpp: pieceRole knows market_stall_<job> (type "stall"), sawmill, builders_yard,
  barracks/watchtower (guards), windmill (by its millstone); PREF work blocks for baker, jeweller, lumberjack, builder,
  merchant's barrel; a village takes trades it hasn't got before more of one (J->trades). village_pieces.cpp: the
  hard-coded Better Village mill fix-up is gone (it hung sails over our plains farm); axles and sails take "axis" (and
  "big") from a piece; crafting_table counts as a work block.
- Test ops: tpvillage {off} (to the nearest village, anchors there); dump prints carve masks.
- Towns in quarters (structures_jigsaw_village.cpp quarterOf/zone): within 0.28 of the town's reach the market and
  civic buildings (stalls, bakery, tavern, jeweller, alchemist, general store, butcher; temple, library), a wide craft
  side (smiths, mason, fletcher, tannery, cartographer, builder), past 0.66 the edge (farms, pens, stable, windmill,
  sawmill, shepherd, fisher, barracks), homes between. One building per trade in a village (two in towns and cities),
  one stall of each kind, stalls only within 2 joints of the square; nothing more than 8/10/12 blocks off the square's
  level (the streets had climbed a peak, terracing stalls up it).
- Shops show their goods (village_builder.py show_goods/GOODS): item frames on the walls (VPK_FRAME, "items" prop on
  item_frame), a display table (our <wood>_table) with an item pile on it, and the trade's item framed over the door.
  Stalls: our tables with item piles of the stall's wares (WARES), a stocked standing shelf behind, our chair for the
  trader (VPK_PILE / VPK_SHELF: "items" props become pile/shelf entities). New: alchemist (cleric), general store
  (merchant). Street pieces keep to one-block decorations (they follow the land column by column: wells and trees on
  them were torn apart); benches, pews and armchairs have armrests/backrests so they become our seats.
- Waystone (sim/travel.cpp waySiteFor): only on real ground within a block of the square's level, open sky above it, up
  to 22 out (it stood on a stall's awning).
- Test op tpbld {type, job, look, off}: stand off a trade's work spot looking at it.
- Decor fixes (user): item frames had no facing rule in village_pieces.cpp (all hung one way, off their walls): Frame
  meta = (facing + 2) & 3, the wall's side. Windows: shutters (open trapdoors, facing = the wall's outward side) either
  side, no closed top-half trapdoors or leaf boxes hanging under them. Porch roofs a block higher, the lantern at the
  porch's front edge above head height. Turf roofs' gable-end fences (stood clear of the roof) gone. Benches in squares,
  pews and the tavern's are our benches (facing = where the sitters look), the bar our tables; the tavern and library
  furnish the floor the house stands on (stilted styles). tools/port/village_check.py scans every piece for frames,
  torches, shutters, lanterns, fences or blocks touching nothing, and anything in a doorway: all 1098 pass.
- Shutters (user: "unsymmetrical"): window_row records a pair per window or run of windows side by side (Piece.pairs);
  Piece.template() puts them up last, a pair whole or not at all, never against a door; a house has them on every window
  or on none (Pal.shutters). All 1139 shuttered walls are even.
- Brewing stands stand on a table (the alchemist's, the temple altar's); structures_jigsaw_village.cpp's work spot for a
  station on furniture is beside the furniture (the cleric stood nowhere).
- Chimneys work again: the hearth is a lit campfire set into the back wall under the stack, stone round it
  (sim/camping.cpp campFlue sends its smoke up the stack); render/content_layers.cpp campfireFx gives a flue a fuller,
  steadier plume (more, bigger, longer-lived puffs, grey).
- Shelves and cupboards (user: "shelves would make more sense"): show_goods puts racked goods on wall weapon racks,
  hung ones in item frames, everything else on our wall shelves (3 items, `<wood>_shelf`, "items" prop → shelf entity);
  wall_cells only takes positive wall materials (frames had been hung on table tops). cupboard(): a `<wood>_cabinet`
  (container 9, the house's loot packed in) with a stocked shelf over it; furnish() adds a kitchen cupboard (KITCHEN,
  KSHELF) and a bedside shelf (BEDSIDE). Libraries: `<wood>_bookcase` of books; the tavern's back bar is shelves of mugs
  over barrels (the old tavern's look).
- Streets (user: a stair down a ravine): per street the slope is at most max(2, len/4) and a joint meets its
  neighbour within 2 (structures_jigsaw_village.cpp). Villages already generated keep their streets.
- Bird feet: tools/port/bird_feet.py (run from data_patches) puts feet on legR/legL of every "bird" rig model, webbed
  for duck, mallard, goose and swan; remade each run.
- Book UI: ui/book.h bookPagesBegin/bookPagesRight/bookPagesEnd/bookPageTitle draw the open book; the brewing, enchanting,
  waystone, places and travel journal screens use it. The villager talk, shop, quest board and path screens are still
  the wooden cards.
- Rugs (user: carpet you can stand furniture on): world/rug.h. A rug is the floor block's coat byte, bit 7 | code
  (1..16 the colours in order, 17 moss); render/mesher.cpp draws the carpet in the cell above when that cell isn't
  opaque, so tables, chairs and beds stand on it. Placing a carpet (sim/build.cpp) lays a rug on the top clicked, under
  the furniture clicked, or under the cell it would fill (a normal carpet only where there's no whole floor); hitting a
  rugged floor lifts the rug first (sim/controller.cpp breakBlock, at carpet speed). Generation: GenBuf.coat,
  ChunkWriter::rug; village pieces carry a "rug" property on floor blocks (Piece.rug in village_builder.py: under the
  room tables, down the temple aisle) → VPState.rug. Weather leaves rug coats alone (no snow melt or settle on them).
  Test op expect {rugAt:[x,y,z], rug:"red_carpet"}.
- village_pieces.cpp addState de-duplicated a piece's states without the wares, the banner or the rug: every shelf, pile
  or bookcase of one facing in a piece showed the first one's goods, and rugged floors merged into plain ones. The key
  now has them all. (Also: a header edited mid-build left structures_jigsaw_village.cpp.o on the old VPState layout,
  a segfault in jigsawTownBuild; touch the users of a changed header if a build was running when it changed.)
- Anvils turned east/west had holes in their tops: anvil_top is painted down its middle 10 columns only (clear margins)
  and render/mesh_shapes.cpp addShapeBox didn't turn top/bottom pictures with a facingModel's rotation. Now they turn
  with the model (Minecraft's y-rotation), unless the box has its own grain rule.
- Pale tall grass: the base pack's short_grass.png was twice as bright as grass.png/fern.png (grey 210 vs ~100), so the
  biome tint came out mint. Remapped into the same range (original kept nowhere: it's our art, regenerate if needed).
- Sunken streets (a ridge across one, the street in a trench): the jigsaw planner's wet() was jsY <= JS_SEA, too coarse
  for the 256 band (dry land at band 64-65 counted as water), so those street columns got no level of their own and a
  house's levelled margin pulled them up. wet() is now "the terrain has water over it" (terrainColumn().water >= 0).
  DQ_VDEBUG=1 logs every planned piece ("vplan").
- Trades work rounds: gen/structures_jigsaw_village.cpp stationKind/tradeRound collect each trade's stations in its
  building (forge, anvil, tub, stand, rack, grindstone, smithing, lectern, books, brew, cutter, loom, fletching, maps,
  stores, shelf, table) with a stand spot each -> Bld.tasks -> the plan's work {at, face, tasks:[[x,y,z,f,kind]]} ->
  VFolk.tasks. sim/villager_ai.cpp: in the morning's work (ph < 0.3) they walk the round, 16-30 s a stop, holding and
  doing what data/villagers.json jobs.<job>.tasks.<kind> says (do: hammer, tend, dip, fit, grind, brew, read, draw,
  shelve, sort, chop, knead, cut, weave, work; held: the tool). The shop hours keep the counter. New item smith_hammer
  (data/textures/item/smith_hammer.png, tiles.json, packmap, itemTiles); the smiths hold it. Armourers' shops have two
  dressed armour stands (village_builder.py ARMOUR_SETS; "items" on armor_stand -> a "stand" block entity).
- Town mine (gen/town_mine.h shared by bMine and the miner's chores): a 12-step drift under the headframe to a timbered
  chamber, three mineshaft-style galleries (16-18 long, posts and plank caps every 4, rail, torches, ore in the walls,
  a side cut halfway, an ore face at the end), shored with stone/planks wherever open. A gallery stops short where the
  land comes within 7 of its roof. JigsawTown.mineBox widens the town's bounds and the chunks that build the mine. The
  miner works one gallery a day: the first step with rock (pickaxe/shovel blocks) in its 3x3, never one with the surface
  close over its face; a cut-out face is made good with fresh ore.
- Terrain version 11 (gen/generator.h, sim/level.h): the village plan's fixes are gated so worlds already made keep
  their plans (built chunks and villagers' spots must keep matching): Town.planV / Bld.planV / VBld.planV. 11: wet() as
  real water, the deep town mine (10 keeps bMineV10 and its miner chore), every trade staffed before a second of one
  (townPlanData: first job of each kind first; two bakers and two clerics had left an armourer's shop with nobody).
  Ungated (no change to what's built): trade rounds, the anvil top, the grass picture, armour stands in new chunks.
- Bird feet, again (user: "weird flat rectangles"): tools/port/bird_feet.py had put a flat plate of the leg picture under
  each leg (its clear texels notched it), on top of render/creature_models.cpp's own small toes. Now each foot is a
  voxel part (vox) in the leg picture's mean colour: three toes forward, the outer two slanting out, a hind toe, a
  knuckle, tapering to darker claws; webbed between the front toes for duck/mallard/goose/swan. creature_models.cpp
  leaves a leg with a foot part alone (no toes of its own, no pack 3x3 leg).
- Shut trapdoors culled the top of the block under them (mesher.cpp shapeStandsOnAll, the path-seam rule): their
  picture has a window, so you saw out of the world through it. Trapdoors no longer count as covering.
- Compass and clock in hand: the live dial (render/instruments.cpp drawInstrumentFace) went at depth + 0.012, inside
  the 3D dial model (item_parts "dial" is thicker): you saw its painted needle, still. It now sits on the model's own
  front and back faces, measured from its grid (frame.origin.z .. + size.z * voxel).
- Spyglass: Player::scoping (sim/controller.cpp updateHeldTools: a spyglass held, use held; you walk at a quarter);
  client/play.cpp narrows the view to a tenth (the Zoom key, if bound, a quarter) and slows the look to match; ui/hud.cpp
  draws the round glass (black round it, a brass rim); no first-person hand while scoping. Third person:
  char_anim.cpp pose.scope (the right forearm folded in to the eye, DQ_DEV_SCOPE tunes it), char_layer.cpp
  spyglassAtEye draws it from the head at the right eye along the look (DQ_DEV_SPY). DQ_DEV_POSE=scope to see it.
  (Remote players' poses aren't synced, as with the bow.)
- Test op lookAt {entity, eh}: aim that far up it (0: its feet).
- Wild mounts wear nothing (user: armour and chests are what players put on their own): tools/port/mob_variants.py's
  horse "warhorse" (iron ring and helm), donkey "packed", llama "caravan" and camel "trader" (barrel saddlebags) are
  now plain looks under the same keys (saves keep them): a big dark stallion, a dusty donkey, a shaggy brown llama, a
  pale camel; the draft horse's feathering is its mane's colour, not white wool. mob_variants.py skips parts with no
  picture (the birds' voxel feet). Running it alone: run bird_feet.py after it, and keep the *_drake_scarred models
  (their scars follow bodies a later patch changes; restored from the previous models.json this time).
  Players can saddle tamed horses; horse armour and donkey/llama chests don't exist yet.
- Village repair (the builder mends what explosions take; blocks you break are yours to change): a damage record is now
  [x,y,z,block,meta, carve, coat, goods] when there's more than the block: its chiselling (the counters' half planks),
  its rug, and a decor block's goods (pile, shelf, bookcase, frame, rack, stand, pot, lectern), kept with the record
  and not scattered (a chest's still spill and it comes back empty: no free loot). The builder puts all of it back.
  Its "someone's standing there" check counted the blast's scattered items too: every spot of a crater was "taken" and
  it went round them for ever (2 of 69 mended); now only players and creatures. Test ops: explode {at:"found"|x,y,z,
  power}, vdamage {max}, find shows a block's items (/itemsN).
- From ourCraft (MIT; studied, nothing copied as is), the visual round. Always on: anisotropic filtering on the block
  pictures (App::applyTextureFiltering, Options > Texture Filtering, extra "anisotropic", default 4x; Vulkan ignores the
  parameter); cutout tiles' clear texels take their nearest painted neighbour's colour before the mipmaps (no dark rims
  on far leaves/glass; app.cpp, alpha stays clear); far fog fades into the sky behind it where there's an open sky
  (common.glsl fogSkyColour/uFogSky, FrameCtx::apply uSkyHorizon/uSkyGlow); the sky dithered (no banding); caustics on
  faces with water in front of them (mesher.cpp MeshCtx::wet -> ChunkVertex.e bit 31 -> chunk.glsl vLight.w; part of
  the greedy key, bit 48).
- Shader levels (post.glsl/GradeLayer, all screen passes): Low adds the wobble under water/lava; Medium a 3-level bloom
  chain (13-tap down, tent up, additive) and eyes that adjust (log brightness at 64x64, mips to 1x1, a 1x1 exposure
  ping-pong eased each frame on the GPU: no readback); High 5 levels, sun rays, a lens flare (ghosts along the
  sun-centre line, visible as bright as the bloom is at the sun); a little noise against banding at every level.
- World passes (render/world_fx.cpp, High/Ultra; off in split screen, portals, with an Iris pack, on Vulkan): a sun
  shadow map (High 1536 / 28 blocks; Ultra 2048 / 48; leaves and plants cast at both: trunk-only shadows looked wrong) kept between redraws (redrawn when you
  leave a grid cell of a quarter of its size, every 1.5 s, or the sun moves; its VP is in the camera-block space it was
  drawn in and shifted when bound). It's drawn by WorldRenderer::drawShadowMap with the terrain shader itself
  (uShadowPass: alpha test only). chunk.glsl sunShadow: 8 taps on a turning spiral, normal offset, a face turned from the
  sun in its own shade; it scales the sun channels only (sky light stays). After the opaque layers the depth and then the
  colour are copied (glCopyTexSubImage2D, as the pack path does); Ultra lays world_ao.glsl's corner shading over the
  solid scene first. Water (pass 2) refracts that copy by the ripples, murkier with depth (gone opaque: it composites
  itself), and at Ultra traces reflections across it (28 steps, falling back to the sky colour). Units 8/9/10 always
  get stand-in textures (the samplers must not share unit 0 with the tile array).
  Cost on the HD 520 (1280x720 offscreen, render distance 9, seed hello): Off 51 fps, Medium 46, High 41 (40 with leaves and plants casting), Ultra 38
  (before the shadow map was kept between frames: High 21, Ultra 13; its draw was 22-45 ms).
- Stealth (creature_monsters.cpp notices/unaware): a sneaking player out of a monster's 120 degree view, or in the dark
  (light < 5) beyond 7 blocks, isn't noticed past 4 blocks; walking about is heard as before. combat.cpp: a blow on a
  creature that hasn't caught on (not hunting, its back or side to you) is a sneak attack, x1.75 ("Sneak attack!").
- Animal tempers (creature.cpp temper(), data "tp"): 20% curious (come over to look at you, lose interest after 10-18 s),
  25% shy (keep further off; a calm kind just steps aside at 3.5 blocks), the rest easy-going. Kept (tamed/penned) ones
  ignore it.
- Not taken from ourCraft: the flow-field pathfinding (mob A* isn't a cost yet: the sim tick is ~1.5-3 ms), the
  inventory revision (ours exists: server_host invSeq / the client's "base").
- Rails are 3D (render/mesh_plants.cpp railPlate, used by mesh_shapes.cpp emitRail): the picture as a height field, as
  ladders are voxel plates. In the straight picture the rails are the outermost two runs of columns painted >= 90% of
  the way along; the curve's texels go with the 7 nearest colours of the straight picture (a lone one is a spike head).
  Ties 2/32, a rail's foot 3/32, its grey head 5/32; slopes get their underside too. Any pack works; the flat picture is
  only the fallback without tiles. Carts ride on the heads now (sim/minecart.h RAIL_Y 5/32, was 1/16; test_redstone
  checks RAIL_Y) with the wheels over the rails (vehicle_layer.cpp, +-0.3 across, were +-0.42).
- Mount gear (sim/mount_gear.h/.cpp). Horse armour in the wolf armour's six materials (tools/port/horse_gear.py, hooked
  into data_patches.py: our own painted icons in data/textures/item and the DokuQuest Base pack; packmap points at a
  pack's leather/iron/golden/diamond_horse_armor; crafted X X / XLX / X X with leather, the leather one X X / XXX / X X).
  On a grown tamed horse: data "horseArmor" + "horseArmorD", points add to Creature::armourPoints and it wears out like
  wolf armour. A chest on a grown tamed donkey/mule/llama/camel: data "bags" (27) + "pack" "chest"; saddle bags now set
  "pack" "bags" (18; old saves without "pack" still read as bags). Sneak-use with an empty hand on your mount opens its
  gear screen: the inventory's container mode with InvOpen gearGet/gearSet/gearFits/gearHint (Grp::Mount: one item a
  slot, shift-click puts a saddle or armour on first), saddle + armour slots over what it carries (ui/inventory.cpp
  drawMount, book and classic). Joined players: the host sends "gear" with the screen, the guest sends ACT "entGear"
  (owner, realm, < 8 blocks). Death drops the saddle (it used to vanish), the armour and the chest/bags with their
  contents; the stable's book keeps MOUNT_GEAR_KEYS. Creature::snap sends "ha"/"wa"/"pk" (so guests now see wolf
  armour too). Drawn in render/creature_layer.cpp: barding = boxes of iron/gold/diamond/steel block or saddle leather
  (chainmail: iron tinted) over the body (top 70%), neck and the top of the face; a chest item model on each flank;
  saddle bags as leather pouches with a brass buckle.
- tools/test_content.cpp: the mixed helmet's worn look is its own "pa:helmet:..." key now (the old check expected a
  single metal's helmet).
