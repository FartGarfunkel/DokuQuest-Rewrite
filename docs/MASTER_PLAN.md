# DokuQuest — Master Development Plan, BTA Integration & Bug Tracker (V2)

This document consolidates all project goals, existing development schedules (`SATURDAY_PLAN.md`, `WEEKEND_PLAN.md`), feature gap analyses (`BTA_GAP.md`), engine architecture evaluations (Zig vs. C++), GUI/IDE tooling plans, and the complete inventory of reported bugs, enhancements, and manual coding additions.

---

## Phase 0: Merge Work & Code Review
* **Zip Integration:** Ingest and merge your latest zip of changes (code, `data/textures`, `data/sounds`, and the base pack) before starting runtime work.
* **Code Audit:** Rigorously review code edits during merging to catch common errors (e.g., dangling `else` statements, inverted skin checks).

---

## Phase 1: Stability & Bug Test (Top Priority)
* **Journal Crash:** Verify the fix from `alpha.6.1` (the rift's "sealed" flag) and add automated tests opening the journal at every story stage.
* **Anti-Clipping & Collision (`aitoolkit` Integration):**
  * Validate `alpha.5.1`'s push-out fix with automated walk/knock tests against walls, ladders, and path blocks (addressing ladder back-clipping and dirt path hangups).
  * Prevent players, mobs, and NPCs of any kind from clipping or standing inside each other on the same block during pathfinding.
  * Mobs/NPCs should physically push each other slightly while remaining able to slip past through tight 1-block spaces (such as doorways).
  * Integrate [`aitoolkit`](https://github.com/linkdd/aitoolkit) logic to keep town NPCs from pathfinding into wells and stop Aether/Skylands mobs from walking off island edges.
* **The Big Sweep:** Comprehensive testing across all systems: menus, saving, crafting, mobs, villages, portals, story, magic, and multiplayer.
* **User Bug Reports & Optional Polish:** Address all reported bugs; include optional behaviors like crows landing on scarecrows.

---

## Phase 2: Engine Architecture, Tooling, & Asset Foundations
* **Engine Structure & Tooling GUI:**
  * Build a front-end GUI application for the game engine to streamline file editing, file organization, and real-time visualization.
  * Hook in **Strata coding AI** to assist with live coding and real-time testing, serving future projects (Voxel Mechwarriors, Voxel MMO World of Warcraft-style game, and D&D campaign world).
* **C++ vs. Zig Architecture Evaluation:**
  * *C++ Maintenance:* Retain working C++ engine foundations while integrating optimized open-source Zig/Rust modules via clean C-compatible ABIs (`extern "C"` / `no_mangle`).
  * *Distant Horizons / Voxy Style LOD:* Implement superior distant chunk meshing and procedural world generation inspired by advanced voxel rendering techniques.
  * *Dynamic Procedural Equipment Crafting:* Replace traditional fixed tools/weapons with a procedural system where different ores mix and match to craft items with dynamic stats linked to RPG levelling, repair mechanics, and 3D slot visual rendering of different ores.
* **Per-Mob Files & Skin Variants:**
  * Move mob stats, models, sounds, spawn rules, and variant definitions to `data/mobs/<mob>.json` (retaining legacy combined file fallback).
  * Establish mob skin directories (`data/textures/entity/<mob>/`) supporting unlimited PNG variants chosen via weighted random selection (optionally filtered by biome, dimension, or season) and saved to state.
  * Standardize name endings: `_baby`, `_angry`, `_tame`, `_overlay`, `_e` (glowing).
  * Split monolithic mob code files by family.
* **Base Pack (DokuQuest Base) Fixes & Asset Audit:**
  * **Raised Micro-Voxels:** Ensure relief work applies across every block in the base pack.
  * **Skin Audit:** Validate mob skins against expected layouts; reject malformed skins automatically to prevent rendering corruption.
* **Game-Only Launcher Updates:** Streamlined game-only packages installed into `versions/` by the launcher (offline rollback supported on Linux and Windows).
* **Release Archive:** Bundle `data/` into a single read archive for faster load times while preserving loose folder overrides for development and modding.

---

## Phase 3: Immediate Gameplay, Physics, & Mechanics Queue
* **Swimming Overhaul:** Implement smoother, proper swimming mechanics (floating/paddling at the surface like Minecraft rather than thrashing/bobbing).
* **Lava Boats:** Add lava-proof craftable boats for navigating subterranean lava lakes.
* **Block Placement Animation:** Continuous holding/placing of blocks should loop smoothly like Minecraft without resetting or stopping abruptly.
* **Cloth Simulation:** Capes and cloth-like blocks/items must move freely in response to environmental wind, matching banner wind physics.
* **Mob Visual Updates & Expressions:**
  * *Creeper Scaling:* Fix creeper model proportions and scaling issues.
  * *Enderman Agro:* Implement the iconic mouth-open aggressive face state when aggravated.
  * *Ghast Attack State:* Show the firing/shooting face frame when launching a fireball.
* **Wither Skeleton Heads & Rare Mob Drops:**
  * Withers and Wither Skeletons drop heads as rare loot (matching Minecraft drop rates).
  * Introduce wearable/placeable mob heads for all mob types and spawn eggs for every mob.
* **Bees:** Fix broken bee AI, hive tracking, and flying routines.
* **Vintage Story-Style World Placement & Ingot Stacking:**
  * Allow items (e.g., metal ingots) to be placed directly on surfaces in 3D stacks.
  * Ingot stacking pattern: 4 across in one direction, next layer 4 across at 90 degrees, and so on.

---

## Phase 4: Chisel Mod & Block Mechanics
* **Chisel Bug Sweep:**
  * Resolve severe bugs introduced by the chisel system (lantern chain glitches, jack-o'-lantern texture/sound bugs, misaligned block textures such as campfires displaying cobblestone textures).
  * Add multi-resolution chisel options: **2x2, 4x4, and 8x8** grid configurations for fine detail building.
  * **Chiseled Block Rules:** Chiseled blocks natively fill stair/slab void requirements and must allow torches, lanterns, and smaller items to be mounted directly on them.
* **Path Block Interaction:**
  * Fix bug where blocks placed on top of path blocks turn invisible. Revert path blocks to dirt when built on (Minecraft style) or ensure full rendering compatibility when items are attached to them.
* **Lantern Chains:** Ensure lantern chains always connect dynamically to the block face above them instead of hovering in air over half-slabs.

---

## Phase 5: Village, Housing, & Economy Overhaul
* **Village Gates:** Fix village gates so they smoothly swing open/closed rather than snapping between visible/invisible states.
* **Villager AI & Furniture Usage:** Update villager pathfinding so NPCs actually sit on chairs, interact with benches, use church banners, and utilize placed furniture.
* **Housing Upgrades (Animal Crossing / Skyrim Style):**
  * NPC/Villager interaction system allowing players to buy structural home upgrades (expanding floor space, adding a basement, adding a 2nd floor).
  * Skyrim-style purchasable interior furnishings alongside manual block customization.
* **Economy Update:** Villagers trade in gold coins instead of emeralds, rebalancing emerald as a precious ore.

---

## Phase 6: Audio & Music Design (Yáhno & C418 Influence)
* **Music & Ambient Soundtrack:**
  * Crafted in the style of indie producer **Yáhno** (blending electronic dance music / EDM light rhythms with ancient hammered dulcimer melodies) infused with **C418**-style sparse Minecraft atmospheric synth textures.
  * Dynamic track sets for Overworld biomes, Aether, Nether, End, Herobrine's realm, caves, boss fights, and more.
* **Sound Overhaul:** Match original HTML events (volumes, falloff, cave reverb, material footsteps, mining/breaking/placing, mobs, UI, ambience, music, plus your custom recordings).
* **Sound Studio (Launcher Tool):**
  * Built-in browser for all game sounds with a modular synth sandbox to tweak/audition sounds.
  * Import WAV/MP3/FLAC files, normalize, and export directly into sound directories.

---

## Phase 7: Performance & Memory Optimization
* **Cave Culling:** Skip rendering chunks hidden behind solid rock even when within view frustum; keep portal windows functional.
* **Terrain Memory Diet:** Address Nether memory bloat (4–9 GB usage on Ultra settings). Audit relief, 3D plants, mesh merging, and fragmentation; implement compact vertex formats and distance-based memory budgeting.

---

## Phase 8: Feel, Polish, & Particle Overhaul
* **Particle Overhaul:** Replicate emitter effects: handheld and placed lights (torches with flame, smoke, and movement trails), block breaking, footsteps, water/lava, mobs, spells, portals, and weather.
* **Bows (Players & Skeletons):** Correct grip (middle grip with string facing archer), upright orientation, multi-stage draw animation (string to cheek, bow arm forward), correct scale in first and third person.
* **Elytra Gliding:** Minecraft-style pitch-based lift/drag, speed conservation from dives, upper-back positioning, and weather wind interaction (headwinds lift, tailwinds push, crosswinds drift, gusts provide updrafts).
* **End Gateways:** Functional ender pearl teleportation.

---

## Phase 9: Unified Art Style & Texture Studio
* **Fill the Gaps:** Restyle unsupported textures to 32px Doku style (matching palette, shading, outlines) based on your reference images.
* **Blend It All:** Harmonize clashing base textures across blocks, items, and mobs for a cohesive visual identity.
* **Preserve User Art:** Keep your custom reference pictures intact; adjust clashing elements to blend rather than replacing them.
* **Texture/Resource Studio (Launcher Tool):**
  * Skin-editor style interface to browse blocks, items, entities, and mob skin variants.
  * Pixel tools (pencil, fill, picker, eraser, palette, zoom, grid, mirror, undo/redo), live preview, and PNG import/export.

---

## Phase 10: RPG Levelling & Magic Overhaul
* **RPG Levelling:** Port the HTML levelling system: tools, weapons, and armor gain XP through active use (mining, striking, taking damage) and level up with stat bonuses, tooltips, particle/audio feedback, and persistent save states.
* **Magic Overhaul & Skyrim-Style Hands:**
  * Dual-wield spelling mechanics: combine identical spells, mix distinct spells, or pair weapons with off-hand spells.
  * Spells level up with active use.

---

## Phase 11: Rift Story, Fog, & The Watcher
* **Fragments & Slow Burn:** 10 Realm Fragments featuring creepypasta pacing (subtle wrong things early on scaling up).
* **Corrupted Mobs & Incursions:** Hostile mobs and animals infected with Herobrine's purple glitch aesthetic and white eyes. Late-stage portal incursions bringing corrupted entities into the world.
* **Fortress Foreshadowing & Climax:** Watcher appearance in Nether fortresses pulling Wither skeletons into portals; climax featuring the player-triggered Wither Storm boss.
* **The Fog & The Watcher Design:**
  * *Dual Distance:* Maintain far-terrain LOD ("see forever") layered with eerie early-alpha/beta thick low fog during dread moments, night, or weather.
  * *The Watcher Behavior:* Increased glitching (head twitches, jitter, flickers, dropped frames) at higher fragment stages. Ranged combat, minions, and teleportation (removing close-range brawling AI).
  * *World Marks:* 2x2 rock tunnels, perfect ocean sand pyramids, stripped tree groves, and displaced torches/signs.

---

## Phase 12: Better Than Adventure (BTA) Feature Integration
* **Content Expansion:** Implement BTA-inspired biomes (Outback/Redlands, Swamplands/Mossmire, Seasonal Forest, Glacier, Caatinga/Thornscrub), custom blocks (Pumice, Cinder Sand, Emberglass, painted planks, spike traps, trommels/sieves), and structures.
* **Mob & Boss Additions:** Biome fuselings, articulated biome iron golems with glowing eyes, player-summoned Wither boss, mob heads/skulls as drops, and spawn eggs for every mob.
* **End & Skylands Overhauls:** Proper End cities, ships, shulkers, and Nullscape-style outer End terrain. Aether-style Skylands islands, holystone, aerclouds, and dungeons.
* **Boss Music:** Restore HTML boss battle themes for dragon, keepers, and Wither fights.

---

## Phase 13: Story Mode-Style Animation & Backlog
* **Story Mode-Style Animation:** A unified shared animation layer providing fluid, expressive movement, follow-through on capes/hair/tails, anticipation and weight, head/eye tracking, foot planting on slopes, varied idles, and smooth turning.
* **Backlog:**
  * Over/Under-style original procedural music sets per biome and dimension.
  * Launcher-styled in-game Options screen.
  * PC-clock holiday events (Halloween, Christmas, etc.) with seasonal villager town decorations.