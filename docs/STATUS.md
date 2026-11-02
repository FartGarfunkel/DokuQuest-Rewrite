# DokuQuest C++ — what's done and what's left (updated 2026-11-01, after alpha.11)

This is the one current list. It replaces the open items in QUEUE.md (the alpha.3 queue: all done), NOTES_AUDIT.md
(the alpha.8 audit) and the bug lists in SATURDAY_PLAN / WEEKEND_PLAN. MASTER_PLAN.md keeps the phases, with a
status block at the top.

## Being worked on now (the user's list, 2026-11-01 night)
- **Gameplay:** a cast button for each hand (magic); the Story Mode animation layer; more Skylands content; villagers
  using church banners; crows landing on scarecrows.
- **BTA leftovers:** coloured block light; portals tuned with dye; a "bigger biomes" world option.
- **Art and tools:** an art pass on mob skins and textures that clash (only the ones NOT from the user's own pack, the
  DokuQuest Base pack); a creature-skin tab in the Texture Studio that saves straight into a pack; a check of every
  mob's skin; Iris/OptiFine shader packs and built-in shader levels.
- **Rendering:** distant terrain bugs and anomalies (bring it up to Cubyz's quality); a smaller vertex format and a
  memory budget for distant terrain; a particle pass.
- **Multiplayer:** dragons and the story synced for friends; split screen combined with LAN.
- **World:** steam vents in the Nether.
- **Tools:** the engine GUI with Strata; the data archive and game-only launcher updates.

## Still open after that
- Mob tweaks from BTA: creepers hiding under trapdoors, smarter squid, pigs keeping momentum on stairs; dwarf mode
  (game rule); an easy-bridge key; spawners passing items along.
- Audio: a sound-by-sound check of every event; the Sound Studio's synth sandbox.
- Tests: every block × every tool drop check; a scripted start-to-finish playthrough; Travel Journal checks at every
  story stage; the Nether-portal-invisible test.
- Multiplayer: the host deciding villager trades.
- From the notes audit: painted doors/trapdoors/signs/chests; the Seasonal Forest; igneous emberstone; chimney smoke
  on town buildings.
- Creature looks: sheared-sheep skins from the user's sheet; more looks per land fuseling; villager looks.
- Story: the player-triggered Wither Storm finale.
- Watch a portal's far side in game to confirm its creatures show up (spawning is in, not yet seen).

## On hold until the user says go
- Detail blocks / the finer 6 cm chisel (2×2, 4×4, 8×8).
- The structure refactor (per-mob files, mob skin variant folders, splitting mob code by family).
- Zig/Rust modules.

## Done (highlights; see CHANGELOG.md for every release)
- **alpha.11:** 255 creature looks for 121 creatures (tools/port/mob_variants.py), /looks, death messages name the look.
- **alpha.10:** roads, bridges, travellers, saddle bags, the galleon, start in the wild; guests use creatures and
  containers; portal chase and far-side spawns; dungeon keepers and structure masters; land fuselings and golems
  rebuilt; small bees; bows held right; footsteps on the stride; Nether places on the new Nether ground; the Wither
  Storm from a T with a command block; captions and the Accessibility page; /tp to each land; music per land and the
  dulcimer pieces; eight more discs.
- **alpha.9 and before:** the BTA 8.0 Nether (five lands, volcanoes, acid), mixed tools and armour, the Tool Forge,
  magic hands (dual casting, spell ranks), RPG levelling, cave culling, Cubyz-style distant terrain (first version),
  raised micro-voxels, signs, armour stands, lingering potions, the chisel and chiseled slabs/stairs, waterlogged
  chiseled blocks, branching trees and palms, the quiver, paintings, Music/Sound/Texture Studios, grass regrowth,
  swinging doors and gates, hardcore/adventure/spectator, achievements, colour filters, nightmares, hotbar rows,
  fishing-rod leads, sponges, jars and fireflies, sieves, the Travel Journal, the End and Skylands overhauls, shulkers,
  the Wither, lava boats, mob heads and spawn eggs, elytra, End gateways, ingot piles, held-torch light, swimming,
  crowd pushing, NPCs avoiding wells and Skylands mobs keeping off island edges, house upgrades, gold coins, the
  launcher-style Options, holiday events, the rift story's ten fragments, fog, watcher and incursions.
- **The alpha.3 queue (QUEUE.md):** all done: shelves, puddles (glossy, drying), relief displacement, dynamic lights,
  spawner mobs, glint, levelling display, chimneys, particles, the ale mug, fishing rod and line, swimming, the
  Nether portal remesh, bow animation, potions, split screen, village gates, babble, talents, sneak-use, the armour
  dupe, block sweep, the playthrough list, BTA content.
