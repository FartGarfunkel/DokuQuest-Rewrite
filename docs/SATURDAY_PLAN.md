# DokuQuest: the Saturday plan (from October 31 2026), in priority order

Starts automatically after the weekly reset (12:05 PM Toronto). This is the run order: each step comes before the ones
after it because later work builds on it. It supersedes Part A of WEEKEND_PLAN.md (Part B, the BTA detail, and Part C,
the queue, still apply). Anything unfinished carries over in this order.

Standing rules: no hunger, no phantoms; full controller AND keyboard/mouse in every menu and screen; patch notes never
mention Herobrine and always end with "Removed Herobrine."; releases ship the Linux AppImage + tar.gz and the Windows zip;
pack and folder names stay plain ([A-Za-z0-9_ .-]) so Windows builds work.

---

## 0. Merge your work first
- Ask for your zip of changes (code, `data/textures`, `data/sounds`, the base pack) and merge it in before anything else.
- Review your code edits for bugs while merging. The last zip had a dangling `else` and an inverted skin check.

## 1. Stability: the bug test (top priority)
- **Journal crash:** fixed in alpha.6.1 (the rift's "sealed" flag). Verify it, and add a test that opens the journal
  at every story stage.
- **No more clipping:** players and mobs never end up inside blocks. Test alpha.5.1's push-out fix with an automated
  walk/knock test against walls, ladders and path blocks. Known cases: clipping into the block behind a ladder, getting
  hung up on dirt paths.
- **The big sweep:** find and fix everything in the game, and make sure every system is connected and works: menus,
  saving, crafting, mobs, villages, portals, story, magic, multiplayer.
- **Your reports:** every bug reported before Saturday.
- **Optional:** crows landing on scarecrows.

## 2. Foundations (later steps build on these)
**a. Per-mob files and skin variants**
- Mob data: `data/mobs/<mob>.json` holds stats, model, sounds, and spawn and variant rules. The old combined files
  still load.
- Mob skins: one folder per mob, `data/textures/entity/<mob>/`, with **any number of skins**. Every PNG there is a
  variant.
- Picking a skin: each mob picks one when it spawns (weighted random, optionally by biome, dimension or season) and keeps
  it in the save.
- Name endings: `_baby`, `_angry`, `_tame`, `_overlay`, `_e` (glowing).
- Existing variants stay (axolotl colours, bee states...). The generated skin becomes the default variant.
- Code: the big mob code files are split by mob family.

**b. Base pack fixes (DokuQuest Base)**
- **Raised micro-voxels:** make relief work on every block of the base pack (some come out flat).
- **Skin audit:** check every mob picture against the layout its mob expects. The game rejects a skin that doesn't fit
  instead of drawing it broken.

**c. Game-only updates**
- Small game-only update packages that the launcher installs into `versions/`.
- The launcher only updates itself when the launcher changes.
- Older versions stay installed: pick or roll back to one in the launcher. Works offline, on Linux and Windows.

**d. Release archive**
- The release build bundles `data/` into one archive the game reads, for faster loading.
- Loose folders still win, for development and modding.

## 3. Performance
- **Cave culling:** skip chunks hidden behind solid rock even when they're in view, so underground only your cave is
  drawn. Portal windows keep working.
- **Terrain memory diet:** Ultra used 4–9 GB of graphics memory in the Nether. Find the bloat (relief, 3D plant detail,
  merging, fragmentation), shrink it, consider a compact vertex format, and add a memory budget that drops the farthest
  chunks first.

## 4. Feel and polish
- **Sound overhaul:** match the HTML game event by event:
  - which sounds play, plus volumes, falloff and cave echo
  - footsteps per material, mining, breaking and placing
  - mobs, UI, ambience and music
  - your new sound set and data/sounds recordings stay
- **Particle overhaul:** everything the original emits. First the held torch (flame, smoke, smoke trail as you move),
  then placed lights, block breaking, footsteps, water and lava, mobs, spells, portals and weather.
- **Bows (players and skeletons):**
  - held by the middle grip, string toward the archer, upright like a real bow
  - draw: string hand to the cheek, bow arm forward, release
  - correct scale; first person checked too
- **Elytra:**
  - Minecraft-style gliding: dive for speed, climbing costs speed, smooth turns, firework boosts
  - sits properly on the upper back: folded at rest, swept back in flight
  - catches the weather wind: headwind lift, tailwind push, crosswind drift, updrafts to ride; stronger up high and
    over open land or sea
- **End gateways:** throw an ender pearl in to travel through, like Minecraft.

## 5. Art: everything in one style
- **Fill the gaps:** every texture the base pack doesn't cover gets restyled from the generated version to your style
  (32 px, your palette, shading, outlines), based on your closest picture where possible, and written into DokuQuest Base.
- **Blend it all:** where existing base textures clash, adjust them too, so blocks, items and mobs look like one game.
- **Your pictures:** stay yours. Clashing ones are adjusted to blend, not replaced.

## 6. RPG levelling (before magic)
- Port the HTML version's levelling:
  - tools, weapons and armour gain XP from use (mining, hits, damage taken) and level up with bonuses
  - tooltips, level-up effects, saved with each item, and any player skills the original has

## 7. Rift story update
- **Fragments:** 10 Realm Fragments (up from 4), with a creepypasta slow burn: small wrong things first, stranger as
  you go.
- **Corrupted mobs:** hostile mobs and animals get his corruption: a purple glitch look and white eyes, as if he's
  watching through them. They get more common with each fragment, and he also brings them through his portals.
- **Portal incursions** late in the story.
- **Fortress foreshadowing:** he appears in a Nether fortress and pulls a wither skeleton into his portal.
- **Climax:** he places the last wither skull and starts a **Wither Storm** (story only, our own design).
- **The fog and the watcher** (from the original Herobrine creepypasta: a figure standing in the deep fog of an
  early-alpha world, empty white eyes, gone when you approach; 2x2 tunnels, sand pyramids in the sea, trees stripped of
  leaves). Design notes for now, details later:
  - **Two kinds of distance:** keep the far-terrain LOD ("see forever"), but bring back early alpha/beta-style fog as an
    eerie layer. Thick low fog near the player in the story's dread moments, and at night or in certain weather, with the
    distant land fading in and out behind it. He can stand at the edge of the fog looking back, then vanish when you get
    close or look away.
  - **Him:** he glitches more as the story progresses: head twitches, jitter, flickers, frames dropping out.
  - **Combat:** less running up and punching (it doesn't suit him). Keep and build on the long-range attacks, summoning
    corrupted minions and teleporting, and add more corrupted attacks. The close-range brawling AI goes.
  - **His world-marks** in the slow burn: 2x2 tunnels in rock, small perfect sand pyramids in the ocean, groves with
    their leaves cut off, torches and signs that aren't yours.

## 8. BTA update (full detail: WEEKEND_PLAN.md Part B)
- **The BTA list:** everything on it, in our own designs: biomes, blocks, items, mobs, structures, a BTA-style Nether,
  music ideas and other features.
  - Seasons: optional, or mainly in the Seasonal Forest.
  - Leads: a real lead item, not the fishing rod.
- **Biome variants:**
  - fuselings (our creeper) in a variant per biome
  - biome iron golems: articulated, glowing eyes, and back in villages
- **Wither boss:** player-summoned only.
- **Mob heads/skulls and spawn eggs** for every mob.
- **End overhaul:**
  - proper End cities, end ships with elytra and dragon head, shulkers, loot
  - Nullscape-style outer End terrain (spires, arches, floating chunks, craters)
  - End atmosphere
- **Skylands overhaul:**
  - Aether-style islands, holystone, aerclouds, skyroot and golden oak, quicksoil
  - Aether plants, ores, dungeons and mobs
  - the bright floaty feel
- **Boss battle music** from the HTML version back for every boss fight.

## 9. Story Mode-style animation
- A shared animation layer: fluid, expressive movement for NPCs, mobs and players.

## 10. Magic overhaul (needs RPG levelling)
- **Skyrim-style hands:** the same spell in both hands combines; two different spells mix; a weapon in one hand and a
  spell in the other. Spells still have to be unlocked.
- **Spell levelling:** spells level up with use.
- **Gold coins:** villagers trade in gold coins instead of emeralds.

## 11. Launcher studios (after the file formats above are settled)
- **Sound Studio:**
  - browse every sound by category and play the game's version
  - a synth using the same building blocks as the game's sounds: load, tweak and audition any of them
  - import wav/mp3/flac, trim, normalise
  - export straight into the sounds folder
- **Texture Studio:**
  - like the skin editor: browse all block, item and entity textures and mob skin variants
  - pixel tools: pencil, fill, picker, eraser, palette, zoom, grid, mirror, undo/redo
  - a live preview
  - import/export PNG; save into a pack or straight into the game's textures

## Backlog (after all of this)
- Over/Under-style original music for every part of the world.
- An Options screen laid out like the launcher's.
- Other holiday events from the PC clock (Christmas and so on), with villagers decorating their towns.
