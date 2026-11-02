# Notes audit (overnight 2026-11-01)

**Superseded by `docs/STATUS.md` (2026-11-01 night).**
Every planned item and bug in docs/ checked against the code. Items marked **[DONE tonight]** were finished in the night's work (alpha.8).

Sources read: docs/MASTER_PLAN [MP], BTA_GAP [BTA] (= WEEKEND Part B), QUEUE [Q] (= WEEKEND Part C), HANDOFF [HO],
SATURDAY_PLAN [SAT], WEEKEND_PLAN Part A [WE], PORT_STATUS [PS], plus CHANGELOG for evidence of what shipped.
Project docs (claude/*.md): no Projects tool in this session, so they were skipped. Story-secret items left out, and C418 items too.
Evidence is grep over src/, data/, tools/ (file:line where it helps).

## MISSING (not in code)
- [HO][BTA] Siphon: HANDOFF says "done", but there's no key, code or data anywhere (repo-wide grep -i siphon matches only docs)  **[DONE tonight]**
- [HO][BTA] Basket / Wicker Basket: HANDOFF says "done", but nothing in src/data/tools (grep -i basket matches only docs)  **[DONE tonight]**
- [Q] Knight/Ranger talents in combat: pathMeleeFactor/pathArrowFactor/pathArrowPoisons/pathQuiverSaves/pathSpeedFactor/pathCheatDeath are defined (sim/villager_quests.cpp ~375-390) but nothing calls them  **[DONE tonight]**
- [MP][SAT][WE] Automated walk/knock clipping test (walls, ladders, path blocks): no ladder/path/knock test in tools/*.cpp or playtests (the pushOut fix is in sim/physics.cpp:76)  **[walk over dirt paths + climb a ladder and step off on top DONE tonight (tools/playtests/clip.json, passes); knockback into walls still untested]**
- [Q] Potions stack to 16: every potion_* has maxStack 1 in data/items.json  **[DONE tonight]**
- [Q] Lingering potions + effect cloud: no "linger"/effect-cloud code (splash only, sim/brewing.cpp:114)  **[DONE tonight]**
- [Q] Block sweep extension (every block x every tool, drops vs the original's tables): tools/sweep.cpp only checks use/place/screens
- [Q] Scripted start-to-finish playthrough (village to Nether to End to dragon): no such playtest
- [MP][SAT] Base-pack skin audit (reject a mob skin that doesn't fit its layout): no validation/reject in assets/skin.cpp or the pack mob loaders
- [MP][SAT] Raised micro-voxel relief on EVERY base-pack block (some come out flat): no fix recorded; tile_relief.cpp unchanged in purpose
- [MP][SAT] Crows landing on scarecrows (optional): no crow mob (hallowmobs.cpp only counts "crows")
- [MP] Villagers use church banners: nothing in sim/villager*.cpp (seats are done)
- [HO] Horizon's caves and shaders (agreed order step 4): no Horizon-derived code; docs/CREDITS.md doesn't exist
- [SAT] Dulcimer melodic-EDM music set: MI_DULCIMER instrument exists (audio/music.cpp:235) but no mood uses it
- [MP][Q] End music set: moodNow() plays "cave2" in the End (music.cpp:620); no per-realm sets from the user's 3 examples
- [BTA] Highland Spruce (Boreal) biome and the Seasonal Forest (cherry turning with the seasons): BtaVariant enum has only REDLANDS/GLACIER/MEADOW/MOSSMIRE/THORNSCRUB  **[Highland Spruce DONE tonight (Seasonal Forest still open)]**
- [BTA] Bigger biomes + a tundra step: no biome-scale change, no "tundra"  **[tundra step DONE tonight (bigger biomes open)]**
- [BTA] Mossy and igneous (ever-burning) emberstone: only cobbled_/polished_emberstone exist  **[mossy DONE tonight (igneous open)]**
- [BTA] Permafrost family (cobbled, polished, bricks): only "permafrost"  **[DONE tonight]**
- [BTA] Cacao tree (farmable cocoa on a tree): no cacao/cocoa tree in gen/trees.cpp
- [BTA] Mossy oak log: only mossy_dark_oak_log, mossy_mangrove_log  **[DONE tonight]**
- [BTA] Gold mesh (item filter), Emberglass conduit, redstone pumpkin, matcher (the last two optional): no keys
- [BTA] Summoning wand (creative), firefly lantern, rotten cloth / zombies drop cloth (optional): no keys (only we_wand, firefly_jar)
- [BTA] Dye-tuned portals (dyed blocks pick the link): nothing in sim/portals.cpp
- [BTA] Coloured block light (red/orange Emberglass glow; darker unlit caves): only a global uBlockLightColor (world_renderer.cpp:25)
- [BTA] Nether rules: pigmen spawn only on natural blocks (creature_spawn.cpp:323 has no ground check); wet sponge dries near lava/igneous (smelt only); glowstone always 4 dust (unverified)  **[pigmen rule DONE tonight]**
- [BTA] Sulphur Springs steam vents (fog, light damage); Emberglass regrowth, crystals that ring: depths.cpp makes sulphur + magma pools only
- [BTA] Darker labyrinth variant with spike traps, motion sensors, mesh and crystal light: gen/ never uses spike_trap/motion_sensor/iron_mesh
- [BTA] Mob behaviour tweaks: creepers path under trapdoors, smarter squid, ridden pigs keep momentum on stairs, more wolves, more mobs in winter (no winter check in creature_spawn.cpp)
- [BTA] Sound ideas: cues for the new biomes (client_fx.cpp placeMood folds them into frost/savanna/swamp); Nether biome ambience (steam hiss, bubbling, chimes, ash wind; nothing in audio/ambience.cpp); subtitles/captions; a real nightmare sting (it plays "hurt", blockfx.cpp:~170); new discs (only 4)
- [BTA] UI/commands: treecapitator and dwarfMode rules (not in sim/rules.cpp); easy-bridge key; render distance to 32 (max 16, ui/options.cpp:136); dimension option for /tp (gamecommands.cpp:242); accessibility page; rail power up slopes; spawners pass items  **[tree-felling rule DONE tonight (dwarfMode, easy-bridge, 32 render distance open)]**
- [BTA] Painting gold/iron frame trims (paintings themselves are done)
- [Q] Chimneys smoke like the original: town buildings get chimney stacks (structures_townbld.cpp:97) but there's no chimney emitter in render/env_layers.cpp
- [Q] Iris/OptiFine shader-pack loader (only DokuQuest-format overrides, render/gl.cpp:14); built-in shader levels
- [Q] Roadmap items: start in the wild, roads and bridges between villages, travellers, saddle bags, bigger ship, poses, Shadowbloom ports (none found; village streets only)
- [MP] Procedural mix-and-match equipment for ARMOUR, plus ore mixing with dynamic stats (the Tool Forge covers tool heads + hafts only, tools/port/forging.py)

## PARTIAL
- [MP][HO] Bees (AI, hive tracking, flying): husbandry.js beeAI is ported (creature_beasts.cpp:211-250), but HANDOFF still lists it as "Left" and no fix is recorded
- [MP][SAT][WE] Bows: 3rd-person draw pose (char_anim.cpp:56), skeleton bow_pulling (creature_layer.cpp:289) and the 1st-person draw (fp_bow.cpp) exist; no recorded fix for middle grip / string toward archer / upright / scale
- [SAT][WE] Elytra sit on the upper back (too high): wings at body y=12 (char_layer.cpp:515); no fix recorded (glide + wind are done)  **[lowered tonight (needs your eye)]**
- [MP][SAT][WE] Particle overhaul: held torch, sprint/landing/crit/creature dust and breath are done; no event-by-event pass vs the original (placed lights, water/lava, spells, portals, weather)
- [MP][SAT][WE] Sound overhaul (event-by-event match, falloff, footsteps per material): no record of the audit
- [SAT][WE] Terrain memory diet: plant/vine savings shipped (alpha.8); no compact vertex format and no distance memory budget found
- [MP][Q] Story Mode animation layer: idles, foot planting, attack weight and look-round are done; still missing follow-through on capes/hair/tails, expressive talking faces, blended transitions, smooth turning and eye look-at as one shared layer
- [Q][SAT] Biome fuselings: 3 of the ~12 asked for (frost/dune/bog, data_patches.py:269)  **[6 now (ember, moss, sky added tonight)]**
- [Q][MP] Biome golems: 3 tinted iron_golem copies (data_patches.py:279-285); not articulated (elbow/knee joints, hands), not built from local blocks, no glowing-eyes check
- [SAT][WE] Skylands/Aether overhaul: island shapes, clouds, sunwood, blooms, glimmersand are done; no holystone/quicksoil/skyroot analogues beyond skystone, no bronze/silver/gold sky dungeons, no new Aether mobs
- [BTA] Painted planks done; painted doors, trapdoors, signs, buttons, pressure plates and chests MISSING (paintbrush only does planks/wool/glass/terracotta, usables.cpp:200)
- [BTA] Seasons rule defaults ON ("on unless turned off", sim/weather.h:99); the user said optional/off by default; no toggle in World settings (worlds.cpp), only /gamerule  **[DONE tonight: off by default, toggle in World settings]**
- [Q] Magic "one button per hand": the left-hand spell joins every cast (magic_hands.cpp:1-7); there's no separate per-hand cast button on mouse/pad
- [MP][SAT] Art pass: restyle.py covers block and item pictures; mob skins and "blend the clashing base textures" not done
- [MP][SAT] Studios (counted done) sub-gaps: Texture Studio tabs are Blocks/Items/Yours only (no entity/mob-skin browsing, no save into a pack); Sound Studio has no synth sandbox
- [MP][SAT] Journal test at every story stage: test_magic.cpp:234 checks only stage 0 to 1; no test opens the journal with the rift sealed
- [MP][Q] Ladder back-clipping / dirt-path hang-ups: alpha.5.1 push-out is in; never re-verified (see the missing test)  **[verified tonight: clip.json passes]**
- [Q] Nether portal world invisible until reload: portal.json is screenshot-only (no expect); not confirmed fixed
- [PS] Split screen: LAN + split and players in side realms not yet; together.js party UI; carriage (only sim/travel.h); map net sync
- [Q][PS] Villagers/dragons over the network: generic entity sync only; guest actions, riding on clients, Joining screen (docs/port/net.md "Left to do")
- [MP][SAT] Big sweep / user bug reports: ongoing; tools/playtests/containers.json still uses the old "open" op (stale, per HANDOFF)  **[containers passes 13/13 tonight; the full sweep is in HANDOFF]**
- [MP][HO] Chisel bug sweep: campfire with cobblestone texture and jack-o'-lantern texture never reproduced; needs the user's save/pack
- [MP][Q] Music sets: biome/cave/Nether/Skylands moods exist; Over/Under-style sets with seeded per-world variation not done
- [HO] alpha.8 release: notes drafted (CHANGELOG, news.dqn), but DQ_VERSION is still 1.0.0-alpha.7 (CMakeLists.txt:4); not released  **[DONE: alpha.8 released 2026-11-01 (AppImage, tar.gz, Windows zip)]**

## DOC STALE (done in code but not marked)
- [MP] Ph3: lava boat, placement loop, creeper/enderman/ghast faces (alpha.7); ingot stacking, capes in wind, treading water (alpha.8); creatures float and paddle (creature.cpp:639)
- [MP] Ph1: crowd push / no overlap (crowdPush, creature_spawn.cpp); NPCs avoid wells (villager_ai.cpp:73 water cost); creatures stop at island edges (alpha.3.1); done with our own code, not aitoolkit
- [MP] Ph4: path to dirt, chains/lanterns, slab/stair = chisel forms, torches on chiseled faces (alpha.7); [HO] waterlogged chiseled blocks (WLOG, alpha.8) and clicking through notches (raycast sub-box pick, physics.cpp:288) are listed open but done
- [MP][Q] Ph5: gates swing (swing_layer.cpp); villagers sit on seats (villager_ai.cpp:392); house upgrades + furnishing (villager_life.cpp HOUSE_UPS); gold coins (villagers_internal.h:115, emeralds = 4 coins)
- [MP] Ph7: cave culling (world_renderer.cpp:199). Ph8: elytra physics + wind, End gateways by pearl, held torch flame
- [MP][SAT] Ph10: RPG gear levelling (skills.cpp gx, item_tooltip_extra.cpp:51 level bar); magic hands + spell ranks I-X
- [MP][SAT][WE] Boss battle music: client_fx.cpp:100 scans for every boss:true mob (wither, wither_storm, dragons, warden...)
- [MP][Q] Ph13/backlog: in-game Options laid out like the launcher (ui/options.cpp)
- [Q] Hostile pumpkin creature + scarecrow: marked "STILL QUEUED" but shipped in alpha.4.4 (sim/hallowmobs.cpp)
- [Q] Rendering: shelves, puddles drying + glossy, voxel displacement (tile_relief.cpp), dynamic lights (render/dyn_light.cpp), spawner mob, glint + enchant tooltips, gear levelling tooltips, waterfall spray (particles.cpp:90)
- [Q] Items: ale mug model (item_parts.cpp:366), rod float/line (content_layers.cpp:188-232), 1st-person bow draw, splash potions
- [Q] Characters/systems: 4-player split screen, front crawl, babble (also in the world, alpha.4.4), sneak-use on stations, gear dupe (alpha.3.2), portal remesh (alpha.3), deep-underground vanish (alpha.5/5.2)
- [Q] Wither boss, mob heads, spawn eggs, Wither Storm; economy coins (see above)
- [BTA] Done but not ticked: pick up containers with contents (build.cpp:934 "Carry On"), game rules panel (rules.cpp + worlds.cpp), rainbows (weather.h:35), rearrangeable HUD (ui/hud_edit.cpp), emote row (hud.cpp), reinforced piston (redstone_parts.cpp:92), brazier, spike trap, molten pumice, rocket launcher, handcannon, statue, painting, ambient butterflies/fireflies (render/creature_fauna.cpp), skeletons visibly load bows, eating sound (build.cpp:531), variable portal sizes, mobs don't fight back in creative (pawnCreative), creature blasts respect mobGriefing
- [BTA] Biomes Redlands/Glacier/Meadow/Mossmire/Thornscrub, the Nether biomes, scorched dirt, perma-ice, ember coal, gum/palm/thorn, giant jungle trees, spinifex/algae/bluebells/marigold, cane bundles: all in code, the list isn't ticked
- [HO] "Left: rare skull drops, capes in the wind": both done
- [PS] Item frames (alpha.3) and in-world babble are done; the three duplicate "Everything else ... wave 3" rows are stale

## DEFERRED by user
- [HO][MP] "Detail blocks" / 6 cm voxels and 2x2/4x4/8x8 chisel resolution: stick with the carve byte
- [MP][SAT][HO] Structure refactor: per-mob data/mobs/<mob>.json, weighted skin-variant folders + name endings, splitting mob code by family
- [MP][SAT][WE] Release archive for data/; game-only launcher updates into versions/ with rollback
- [MP][HO] Engine GUI front-end with Strata AI; performance instancing
- [MP][HO] Zig/Rust modules over a C ABI: decided to stay on C++ (Cubyz ideas only)
- [MP][SAT][HO] Holidays beyond Halloween: December carols, New Year fireworks, a brighter tree, other holidays (December partly started, not in the notes)  **[carols, New Year fireworks, brighter tree DONE tonight]**
- [BTA] Seasons extras ("maybe"): calendar item, sundial, more mobs in winter, longer nights, season stingers
- Waiting on the user: wolf-howl/duck/horse sound mapping (HO, asked, no answer); campfire/jack-o'-lantern texture repro; Phase 0 zip merge is a per-session process
