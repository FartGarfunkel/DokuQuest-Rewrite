# DokuQuest — the complete plan from Saturday, October 31 2026

> **Run order:** see docs/SATURDAY_PLAN.md (supersedes Part A below; Parts B and C still apply).

Everything we're adding or fixing, in the order it runs. Work starts automatically after the weekly reset (12:05 PM Toronto).
Items 1–5 are realistic for Saturday; the big updates (6–9) run into Sunday and beyond, and anything unfinished carries
over in this order. Standing rules: no hunger, no phantoms, full controller AND keyboard/mouse in every menu, patch notes
never mention Herobrine and always end with "Removed Herobrine.", resource-pack art is never committed.

Part A is the plan. Part B is the full BTA update detail. Part C is the full bug/feature queue the bug test works through.

---

# Part A — the plan

## 1. Big bug test and fixes
- Find and fix everything in the game, and make sure every system is connected and works, menus to mobs to saving.
- **No more clipping (top priority):** players and mobs must never end up inside blocks. alpha.5.1's push-out fix gets
  tested properly with an automated test that walks and knocks players and mobs into walls, ladders and path blocks.
  Known cases: clipping into the block behind a ladder while climbing, getting hung up on dirt path blocks.
- Every bug you report before Saturday.
- Optional: crows landing on scarecrows.

## 2. Game-only updates (like the HTML version / Minecraft's launcher)
- Each release also ships a small game-only package; the launcher downloads it into your DokuQuest folder (versions/).
- The launcher only updates itself when the launcher itself changes (its own version number).
- Older game versions stay installed: pick or roll back to one from the launcher. Works offline, Linux and Windows.

## 3. Performance
- **Cave culling** (like Sodium/Minecraft): chunks hidden behind solid rock are skipped even when they're in view, so
  underground only the cave you're in is drawn. Portal windows keep working.
- **Terrain memory diet:** Ultra used 4–9 GB of graphics memory in the Nether (~260 KB per chunk section vs ~80 KB in
  tests without the pack). Find what bloats the meshes (GeruDoku relief, 3D plant detail everywhere on Ultra, merging,
  fragmentation), cut it down, consider a compact vertex format and a memory budget that drops the farthest chunks first.

## 4. Feel and polish
- **Sound overhaul:** match the HTML game event by event — which sounds play, the resource-pack sounds used, the synth
  fallbacks, volumes, distance falloff, cave reverb/echo, footsteps per material, mining/breaking/placing (mining was
  faint and distant), mobs, UI clicks, ambience and music.
- **Particle overhaul:** everything the original emits — held and placed torches/lanterns/campfires (the held torch shows
  no flame or smoke and no smoke trail when you move), block breaking, footsteps, water/lava, mobs, spells, portals, weather.
- **Bows (third person, players and skeletons):** held by the middle grip with the string toward the archer, upright like
  a real bow (they're held backwards now); a proper draw — string hand to the cheek, bow arm forward, release — matching
  the original's stages; correct scale in the hand. First-person bow checked too.
- **Elytra:**
  - Minecraft-style gliding: pitch-based lift and drag, speed from diving, lost climbing, smooth turning, firework boosts.
  - Sits properly on the upper back (it's too high now): folded at rest, spread and swept back when gliding.
  - Catches our weather wind: headwinds give lift, tailwinds push you faster, crosswinds drift you, strong gusts give
    updrafts to ride; stronger up high and over open land/sea, subtle in calm weather.
- **End gateways usable:** throwing an ender pearl into a gateway takes you through, like Minecraft (the opening is one
  block tall, so walking in is awkward).

## 5. RPG levelling (port of the HTML version)
- Tools, weapons and armour gain XP from use (mining, hits, damage taken) and level up with bonuses, exactly like the
  original: tooltips, level-up toast/sound/particles, saved with the item, any player skills the original has.
- Ties into the magic overhaul (spells level up the same way).

## 6. Rift story update
- **10 Realm Fragments** (up from 4), the creepypasta slow burn: small wrong things first, more and stranger as you go.
- **Corrupted mobs:** existing hostile mobs AND ordinary animals get Herobrine's corruption — the purplish glitch look of
  his realm and white eyes, as if he's watching through them. More fragments = more of them; he also brings them over
  through his portals, like the one in town.
- **Late-stage portal incursions.**
- **Nether fortress foreshadowing:** he shows up while you're in a fortress, opens a portal and pulls a wither skeleton
  into it.
- **Climax:** he places the last wither skull and starts a **Wither Storm** (story only, our own design).

## 7. BTA update (full detail in Part B)
- Everything in the BTA gap list (Part B): biomes, blocks, items, mobs, structures, the Nether like BTA's, music ideas and
  other features, in our own Doku designs. Decisions: seasons optional or mainly in the Seasonal Forest; a real lead item
  (no fishing rod).
- **Biome fuseling variants** (our creeper), one per biome, our own designs.
- **Biome iron golems:** articulated, glowing eyes, changing with the biome like the fuselings; golems in villages.
- **Wither boss** — player-summoned only (never natural), plus the story's Wither Storm.
- **Mob heads/skulls** and **spawn eggs** for every mob.
- **End overhaul:** proper End cities (towers, bridges, end ships with elytra and dragon head, shulkers, loot — the current
  ones are awful); Nullscape-style outer End terrain in our own design (towering spires, arches, floating chunks, crater
  fields, varied end-stone biomes); End atmosphere, particles and ambience.
- **Skylands overhaul** (Aether reference: aether.wiki.gg): Aether-style island shapes, holystone, aercloud types, skyroot
  and golden oak trees, quicksoil, Aether plants, ores and dungeons (bronze/silver/gold), Aether mobs, the bright floaty
  feel — our own designs, alongside the existing Skylands content.
- **Boss battle music** from the HTML version back for every boss fight: dragon, keepers, Wither and the rest.

## 8. Story Mode-style animation
- A shared animation layer for that Minecraft Story Mode feel: fluid, expressive movement for NPCs, mobs and players.

## 9. Magic overhaul
- Skyrim-style hands: the same spell in both hands combines into a stronger one; two different spells mix; a sword in
  one hand and a spell in the other. Spells still have to be unlocked.
- Spells level up with use, like tools, weapons and armour.
- Villagers trade in **gold coins** instead of emeralds, so emeralds become a better-balanced ore.

## Backlog (after all of the above)
- Over/Under-style original synthesised music sets for every part of the world (more variety).
- An Options screen laid out like the launcher's options.
- Other holiday events from the PC clock (Christmas in December and so on), villagers decorating their towns.

---

# Part B — the BTA update in full

## BTA gap list: what "Better than Adventure!" has that DokuQuest lacks

Written 2026-10-25. Ideas only: every entry says what a thing IS and DOES, in our own words, so we can design OUR OWN version in Doku style (micro-voxel tiles, our synth music). Nothing here is BTA code, art or music, and none of it should be copied or extracted.

**Reading limits (be aware):** the wiki's Main Page loaded, but every other wiki page (Biome, Mob, Block, Structure, Labyrinth, Netherrack, ...) returned a site error for our fetcher in this session. This list therefore comes from BTA's official release posts, the GitHub release notes, the Minecraft Forum thread, an official Nether fly-by video description and a hosting-site summary. Gaps in the Mob and Structure sections are probably understated; entries marked *(unverified)* rest on a page title or a single line. If you want full coverage, paste the wiki's Mob / Biome / Structure pages in a later session.

Matching rule used: by meaning, not name. A close equivalent in our game means "not missing". Our game has no hunger and no phantoms, and that stays.

---

### Decisions (user, 2026-10-25)
- Everything on this list is wanted, with these changes:
- **Seasons:** optional. Either a world setting (off by default) or kept mainly to the Seasonal Forest biome, whose cherry groves change through the year.
- **Leading animals:** no fishing rod for it. Make a real lead item like Minecraft's: tie it to an animal, it follows you, and you can tie it to a fence.

### 1. Biomes (Overworld)

Our generator has 7 base biomes (Ocean, Beach, Plains, Forest, Desert, Taiga, Mountains) with climate-driven variants (jungle, savanna, cherry, mangrove).

Missing:
- **Meadow**: flat, flower-packed lowland, calm and bright. DQ: "Wildflower Meadow", bees and rabbits, tall flower patches, pink petals.
- **Boreal Forest**: a warmer taiga with very tall spruce. DQ: "Highland Spruce", giant spruce plus moss and berries.
- ★ **Outback**: scorched red desert with eucalyptus groves and spiky ground bushes; has a rich "scorched dirt" soil. DQ: "Redlands", eucalyptus-style trees, spinifex-like tufts, dry gullies, ore-bearing scorched dirt.
- **Swamplands**: algae-covered ground and moss-draped trees. DQ: "Mossmire" with algae floor, mossy logs, fireflies at night (we only have mangrove shores).
- **Seasonal Forest**: cherry-tree groves that turn pink in spring (ties to seasons). DQ: make our cherry grove a lowland biome whose leaves change with the season.
- **Caatinga**: dry shrub country with its own thorn trees and palm trees (added in BTA 7.2). DQ: "Thornscrub" with thorn and palm trees, cactus-like water plants.
- **Glacier**: huge ice sheets and glaciers, never-melting ice. DQ: "Glacier" band north of taiga with blue ice spires and perma-ice.
- **Bigger biomes plus more snow biomes** (generation tweak, not a biome): BTA made biomes about twice as big and snow biomes more common. DQ: widen biome scale and add a tundra step.

Already covered: Plains, Forest, Desert, Taiga, Mountains, Ocean, Beach, Jungle/Rainforest (jungle trees, rainforest litter), Savanna, Cherry grove, Mangrove shore, Tundra-ish snow in taiga/mountains.

---

### 2. Blocks

#### Terrain and plants
- ★ **Pumice**: Nether stone that soaks up lava like a sponge. DQ: "Ashstone Pumice" for draining lava lakes safely.
- **Molten Pumice**: a recipe turns pumice back into a lava source. DQ: portable lava, use as a fuel block.
- **Brimsand**: black volcanic sand forming islands over lava. DQ: "Cinder Sand" (falls like sand, immune to lava glow).
- ★ **Rubyglass**: tall glowing crystal growths used as Nether scenery and as parts of machines; grow from "nodes", pickaxe or piston breaks them. DQ: "Emberglass" crystals that regrow slowly and glow.
- **Cobbled / Mossy / Igneous / Polished / Smooth Netherrack plus Netherrack Bricks** (with slabs and stairs): full stone family for the Nether; cobbled can stand in for stone in recipes. Igneous burns forever and dries wet sponges. DQ: Emberstone already has bricks; add cobbled, mossy and igneous (burning) versions.
- **Soul Schist / Bone Shale**: two planned Nether stones, unused at 7.3. DQ: ours if we want "bone-white rock" strata in the Nether.
- **Petrified Log**: stone-like dead tree trunks standing on ash plains (removed again in 8.0.1). DQ: "Ashen Trunk" scenery.
- **Scorched Dirt / Rich Scorched Dirt**: parched soil; the rich kind yields raw metal when sifted. DQ: Redlands soil (see Trommel).
- **Perma-Ice**: ice that does not melt, spawns in cold biomes. DQ: glacier core ice.
- **Permafrost family**: cobbled, polished and brick permafrost with slabs and stairs. We only have plain Permafrost.
- **Nether Coal Ore**: coal found in the Nether. DQ: "Ember Coal" ore that burns hotter.
- **Mossy Oak Log** *(from a page title)*: moss-covered logs from swamp trees. DQ: mossy variants for oak and mangrove logs.
- **Thorn Tree** (log, leaves): spiny tree exclusive to Caatinga. DQ: "Thorn" wood, harmful leaves.
- **Palm Tree** (log, leaves): coastal and dry-climate palms. DQ: palm wood, coconut drop.
- **Eucalyptus-style tree**: Outback grove tree. DQ: "Gum" wood with pale bark.
- **Giant rainforest tree**: very rare massive trunk in rainforests. DQ: huge jungle tree you can climb with rope.
- **Cacao tree** (leaves, sapling): rare rainforest tree that makes cocoa farmable. DQ: cocoa grows on a real tree.
- **Spinifex tufts / Algae / Bluebells / Marigolds**: new plants (Outback tufts, swamp algae, two flowers). DQ: add as flower/ground-cover blocks.
- **Dried / raw Sugar Cane blocks**: bundles of cane for storage and building. DQ: thatch-like bundle blocks.

#### Building and decoration
- ★ **Painted planks (16 dye colours)** plus painted doors, trapdoors, signs, buttons, pressure plates and chests: any plank dyed with a paintbrush. DQ: one dye-able plank state per wood instead of 16 separate blocks.
- **Glass Door**: see-through door. DQ: glass door and glass trapdoor.
- **Steel door / trapdoor / fence / steel-framed glass**: set of building pieces for the steel tier. We have steel ingots and block, not these.
- **Lapis / Iron / Gold / Steel Bricks** (slabs and stairs): metal brick blocks. DQ: add all four.
- **Coloured and Inverted Redstone Lamps**: lamps that take dye, or light up when unpowered. DQ: coloured lamps, inverted lamp.
- **Statue**: decorative block that entities cannot walk through. DQ: carved figure of a mob or hero.
- ★ **Jar**: placeable glass jar that can hold flowers or captured insects. DQ: "Glass Jar" with fireflies/butterflies (see Mobs).
- ★ **Brazier**: a safer open fire in a metal bowl. DQ: lights fires without spreading them.
- **Painting**: wall art you can trim with gold or iron frames. We have item frames only. DQ: voxel paintings painted from palettes.

#### Traps and machines
- ★ **Spike Trap**: floor spikes that hurt things walking onto them; items fall through. DQ: placeable without a block below, used in dungeons.
- **Motion Sensor**: detects movement nearby and sends a redstone signal. DQ: pairs with spikes and locked doors.
- **Mesh Block / Gold Mesh**: blocks mobs and players but items pass; gold mesh filters which items pass. DQ: iron/gold grates for farm sorting.
- **Activator**: when pulsed, performs a right-click on the thing in front. DQ: "Hand block" for automation.
- **Reinforced Piston**: longer reach, can launch mobs and blocks into the air. DQ: launcher piston.
- **Redstone Pumpkin**: pumpkin that works as a powered block *(unverified detail)*.
- ★ **Trommel**: sifting machine that turns mud, sand, gravel, soul sand, brimsand and scorched dirt into small valuables (string, redstone dust, raw metal). DQ: water-wheel-driven "Sieve" block, plus a held version.
- **Siphon**: item mover that shows a particle while working *(exact use unverified)*.
- **Conduit**: carries power between crystal parts; fed by redstone ore. DQ: Emberglass conduit.
- **Matcher** *(unverified)*: a crystal-tech component that compares things. DQ: only if we want a comparator variant.

Already covered: Basalt, Granite, Limestone, Marble, Slate (with bricks/cobbled/polished variants), Dirt Path, Fence Gates, Moss, Rope, Paper Wall, Permafrost, Sandstone bricks/stairs, Blast Furnace, Heather, Blue Orchid, Cocoa, Pumpkin, Sugar Cane, Redstone Lamp, Sponge, Soul Sand/Soil, Netherrack, Nether Bricks, Glowstone, Magma Block, Banners/Flags (16 colours), Bunting, Chairs/Benches/Stools (seats), Item Frame, Flower Pot, Weapon Rack, Conveyor Belt, Crusher, Harvester, Waterwheel, Piston/Sticky Piston, Lever/Buttons/Pressure Plates, Note Block, Carved Pumpkin, Hopper/Dropper/Dispenser.

---

### 3. Items

- ★ **Paintbrush**: recolours placed blocks (planks, doors, chests, lamps) and can dye sheep; empties as it is used. DQ: "Brush and pot" tool tied to our 16 dyes.
- **Quiver / Golden Quiver**: carries arrows; golden ones hit slightly softer but break on landing. DQ: quiver as a back slot ammo store.
- **Rocket Launcher** (from steel): launches explosive rockets. We have fireworks only. DQ: "Rocket Tube" late-game weapon.
- **Handcannon**: handheld explosive that flings the user or targets like other blasts. DQ: "Powder Pistol" for rocket-jumping.
- ★ **Guidebook**: opened with a key, shows recipes and block notes; entries unlock as you obtain items. We have no in-game recipe book. DQ: "Travel Journal" that fills as you discover.
- **Summoning Wand** (creative): spawns mobs, even from dispensers. DQ: dev/creative tool.
- **Held Trommel**: hand-held sifter. DQ: "Hand Sieve".
- **Butterfly / Firefly Cluster**: catchable critters kept in jars or lanterns, can be labelled. DQ: "Firefly Lantern" (the hosting summary also mentions firefly lanterns).
- **Basket** *(unverified)*: a mob-farm basket that holds a fill amount. DQ: "Wicker Basket" for carrying small animals or produce.
- **Lead behaviour**: BTA leads animals with a fishing rod. We have no lead. DQ: a real lead item like Minecraft (user decision), not the rod.
- **Cloth**: zombie drop (replaces feathers); beds need cloth plus wool. DQ: "Rotten Cloth" if we want zombie-dependent beds.
- **Fire Striker**: renamed flint and steel. Covered by our Flint and Steel.

Already covered: Steel tools/armour, Blast Furnace, Flint and Steel, Pumpkin Pie, Pumpkin Seeds, Map, Compass, Clock, Bone Meal, Saddle, Fishing Rod, Rope, Chains, Banners, Dyes, Ale and other foods, Bed, Spyglass.

---

### 4. Mobs

BTA keeps mostly the classic Beta-era roster, so our 124 mob types already exceed it.

Missing:
- **Snowman** (wiki page exists): a snow-made guard thrown together from snow blocks and a pumpkin. DQ: "Snow Sentry" that throws snowballs at hostiles.
- **Butterflies and Fireflies**: harmless ambient insects (butterflies by day in meadows, fireflies at night in swamps). DQ: ambient-only entities, catchable into jars.
- **Armoured / armed zombies** (the hosting summary says zombies can wear armour and carry weapons, notably in labyrinths): variant, not a new species. DQ: add armour and weapon roll to Zombie.
- **New 8.0 mob** (one mob, unrelated to the Nether, unnamed in the release post): *(unverified)*; check the wiki Mob page.

Behaviour ideas from release notes (not mobs, but cheap):
- Zombies drop cloth, skeletons visibly load their bows, creepers path under trapdoors, squid get smarter swimming, ridden pigs keep momentum on stairs, mobs do not retaliate in creative, wolves spawn more often, mobs spawn more in winter.

Already covered: Wolf, Ghast, Squid, Zombie, Skeleton, Spider, Slime, Zombie Pigman, Pig, Cow, Sheep, Chicken, Bat, Bee, Iron Golem, Fuseling (our creeper), Stone Mite (our silverfish), Husk, Stray, Drowned, Witch, plus all dragons, wardens and boss lines.

---

### 5. Structures

BTA's 8.0 post says no new structures; Labyrinths (added 1.7.5) are its main one.

Missing: none confirmed.

Twists on what we already have:
- **Labyrinth flavour**: underground, very dark, scattered, rewarding. DQ: darker labyrinth variant using spike traps, motion sensors, mesh gates and crystal light.
- **Skyblock world type** (BTA 7.3) is a world type, not a structure; see section 8.

Already covered: Labyrinth, Dungeons (classic and deep), Mineshaft, Strongholds, Ancient Cities, Nether Fortress, Ember Bastion/Spire, End Cities, Villages and Towns, Desert/Ocean/Drowned temples, Pirate Cove, Shipwrecks, Buried Treasure, Sky temples and shrines, Skyships, Moa nests, Dragon lairs.

---

### 6. Nether

BTA spent about a year rebuilding Nether terrain generation. It arrived in the 8.0 release (Jul 2026) as "six new Nether biomes"; the fly-by video showed five.

**How it differs (BTA to us):**
- **Terrain:** the classic basin-and-cave Nether is now just one biome, with caves restored (7.3). Other biomes add floating islands over lava, crystal fields, ash plains and pools with steam vents. Sources name five of the six; the sixth is unnamed.
- **Biomes (named):**
  - ★ **Volcanic Island**: black sand islands floating over a lava sea. DQ: "Cinder Isles" of Cinder Sand with lava falls and ash plumes.
  - ★ **Sulfur Pool**: pools of boiling red water ringed by steam vents. DQ: "Sulphur Springs" with scalding water and vents that shoot hot steam (fog, minor damage).
  - **Crystal Forest / Plains**: fields of towering glowing crystals. DQ: "Emberglass Grove", crystals ring when walked past.
  - **The Old World**: ash-covered field of petrified tree-like structures. DQ: "Ashen Wastes", fossil trunks, ash drifts, bone strata.
  - **The Crag / Shelf**: the original look of the beta Nether. DQ: our current Ember Depths basin covers this.
  - *(sixth biome unnamed: unverified)*
- **Blocks:** pumice and molten pumice, black sand, crystal blocks, cobbled/mossy/igneous netherrack and brick families, planned bone and soul stones, Nether coal ore, glowstone always giving 4 dust, wet sponges that dry near lava, doubled cobbled netherrack slabs that burn forever.
- **Mobs:** no new Nether mobs appear in what I read; ghast fireballs respect the mob-griefing rule; pigmen only spawn on natural Nether blocks (so building a base on placed blocks is safe). We already have a richer Nether bestiary (wither skeleton, cinder golem, cinder wisp, charred ghoul, magma slime, Ember Lord/Drake/Dragon), so mostly adopt the "natural blocks only" spawn rule.
- **Light:** BTA has lightmaps and glow textures (emissive textures show up in its patch notes), so crystals and lava likely give coloured glow *(inference, unverified)*. DQ: coloured light from Emberglass (red/orange), dim ash regions for contrast; ties to our "light propagation" TODO.
- **Sounds:** nothing documented in what I could read; the fly-by used third-party ambient tracks, and texture packs can add sounds and music. DQ: see section 7 for our own ambience.
- **Portals:** variable portal sizes (4x3 option), frame corners must be filled, and link targets can be tuned with dye colours. DQ: "dye-tuned" portals so players can have multiple Nether gates (see section 8).

**Suggested move toward it, in our style:** keep Ember Depths as the basin biome, then add Cinder Isles, Sulphur Springs, Emberglass Grove and Ashen Wastes as bands by temperature/noise, with Pumice for lava control and a Sieve for Nether loot.

Already covered: Netherrack, Nether Bricks and fortress, Soul Sand/Soil, Basalt, Blackstone, Magma Block, Glowstone, Nether Wart, Crimson/Warped fungi and nylium, Shroomlight, Ancient Debris, Nether Gold Ore, Ember Quartz Ore, Emberstone family, ghasts, wither skeletons, zombie pigmen, ember lords/dragons.

---

### 7. Music / sound ideas

BTA facts: subtitles added (7.3); food makes a sound when eaten; texture packs can add and replace sounds and music; notable ambience in the Nether showcase came from third-party tracks. Everything below is our own design, not BTA material.

- **Mood target:** sparse, lonely, reverb-heavy synth pads and soft plucks, long silences between tracks (our own synth, never sampled from BTA).
- **Per-biome cue sets:** Meadow (bright bell plucks), Swamp (low drones, frog and insect beds), Glacier (glassy chimes, wind), Outback (dry guitar-like pluck, heat shimmer), Caatinga (quiet hum, distant birds).
- **Season stingers:** short motifs when seasons change (spring birds, summer cicadas, autumn leaves, winter wind).
- **Nether ambience:** steam-vent hiss with random gusts; bubbling boiling water at Sulphur Springs; Emberglass chime pitched by crystal size; ash wind and distant rumble at Cinder Isles; deep slow pulse in the Ashen Wastes.
- **Subtitles / captions** for sounds (accessibility): list the sound name when a mob or block makes a noise off screen.
- **Eating sound** for food; check whether we already have it.
- **Pack hooks:** let resource packs replace sounds and music, same as textures.
- **Sleep audio:** a faint sting when a nightmare starts (see game rules, section 8).
- Existing discs: Ember, Grove, Sunrise, Tide. New discs could match new biomes (Meadow, Glacier, Swamp, Sulphur).

---

### 8. Other features worth having

- ★ **Seasons**: four seasons with a repeating cycle (BTA shortened its length to 14 days per season in 7.2), seasonal tint of leaves and grass, snow covering newly loaded land in winter and clearing instantly in other seasons, slower crops and more mobs in winter, longer nights. DQ: tie to the day counter, add a calendar item and a sundial.
- ★ **Game rules**: keepInventory, allowSprinting, allowSleeping, doNightmares, doSeasonalGrowth, treecapitator, dwarfMode (shrink the player), mobGriefing, dayLightCycle, doWeatherCycle, doFireSpread. DQ: ship a rules panel; `doNightmares` fits our rift story.
- **Nightmares while sleeping**: sleep can trigger dreams. DQ: slow-burn rift hints through dreams (fits the queue's creepypasta pacing).
- ★ **Pick up and move containers**: sneak + empty hand lifts a chest with its contents. DQ: also barrels, furnaces and brewing stands.
- **Dye-tuned portals**: choose which portal links to which gate with dyed blocks; also allow variable portal sizes.
- **Game modes**: Hardcore, Adventure, Spectator. DQ: add Adventure (no block breaking) and Spectator.
- **Skyblock, Indev and Classic world types**: BTA offers Skyblock and retro levels. DQ: a Skyblock start fits our Skylands.
- **Achievements**: redesigned screen plus 13 new ones in 7.3. DQ: tie to biomes and Nether biomes.
- **Rainbows after rain** and **random star colours**: cosmetic sky touches. DQ: cheap sky shader additions.
- **Leading animals with a fishing rod**, **rail power running up slopes**, **furnace minecarts using any fuel**: small quality-of-life touches.
- **Sorting** (middle-click sorts inventory and chests), **rearrangeable HUD**, **in-chat emote picker**, **hotbar page cycling** (TAB), **easy bridge keybind**, **render-distance slider to 32 chunks**: UI ideas. Check against our settings.
- **Commands**: /playsound, /spawnparticle, /mobspawning, dimension teleport subcommand. DQ: add to the dev console.
- **Colourblind shaders** and **accessibility page**: DQ: pair with shader-pack work.
- **Darker caves / light**: BTA's lightmaps make unlit places truly dark. Ties to our "light propagation" next-chunk item.
- **Spawner passes items through** and **sponge un-dyes blocks**: small mechanic touches.

Already covered: Hardcore-free survival, our own commands, banners and flags, Pumpkin Pie, item frames, flower pots, Elytra, gliders and ships (BTA has none), weather and puddles, day/night.

---

### Priority list (★), the 15 most valuable

Each ★ in the sections above is one of these:
1. Seasons (section 8).
2. Volcanic Island Nether biome, our "Cinder Isles" (section 6).
3. Sulfur Pool Nether biome, our "Sulphur Springs" (section 6).
4. Pumice for lava control (section 2).
5. Rubyglass-style crystals, our "Emberglass" (section 2).
6. Trommel / sifter machine (section 2).
7. Painted planks, doors and chests in 16 colours (section 2).
8. Paintbrush (section 3).
9. Spike Trap, best paired with Motion Sensor and Mesh blocks (section 2).
10. Brazier (section 2).
11. Guidebook / Travel Journal (section 3).
12. Jar, with butterflies and fireflies (sections 2 and 4).
13. Outback biome (section 1); Swamplands and Caatinga come right after it.
14. Game rules panel, including nightmares (section 8).
15. Pick up and move containers (section 8).

Strong runners-up: Crystal Forest Nether biome, steel building set, dye-tuned portals, Motion Sensor and Mesh blocks on their own.

---

### Sources

Wiki (only the Main Page could be read; other pages listed were seen as titles only):
- https://bta.miraheze.org/wiki/Main_Page

Official site, announcements and tutorials:
- https://www.betterthanadventure.net
- https://www.betterthanadventure.net/posts/
- https://www.betterthanadventure.net/posts/8-0-update-announcement/
- https://www.betterthanadventure.net/posts/8-0-1-update-announcement/
- https://www.betterthanadventure.net/posts/7-1-update-announcement/
- https://www.betterthanadventure.net/posts/7-2-update-announcement/
- https://www.betterthanadventure.net/posts/7-3-update-announcement/
- https://www.betterthanadventure.net/posts/7-3-01-update-announcement/
- https://www.betterthanadventure.net/tutorials/

Release notes and community write-ups (text only, nothing downloaded):
- https://github.com/Better-than-Adventure/bta-download-repo/releases
- https://www.minecraftforum.net/forums/mapping-and-modding-java-edition/minecraft-mods/3106066-better-than-adventure-for-beta-1-7-3-timely
- https://apexminecrafthosting.com/guides/minecraft/modpacks/better-than-adventure/
- https://portal.yandex.com.tr/yaozet/games/better-than-adventure-minecraft-mod-overview-id2-Jiz6fQEV

Videos (descriptions only):
- https://www.youtube.com/watch?v=6PDqpT49x7Y (Nether biome fly-by, 7.4)
- https://www.youtube.com/watch?v=cOWYcxlanGY (8.0 trailer)

Wiki page titles seen in search results but not readable (for a later pass): Biome, Mob, Block, Structure, Labyrinth, Tree, Log (Mossy Oak Log), Dirt, Ice, Magma, Netherrack, Nether Coal Ore, Nether Portal, Trommel, Mesh Block, Blast Furnace, Snowman, Ember, Advanced Mechanics, Guide:Getting started, Mentioned features.

---

# Part C — the bug and feature queue (the bug test checks every item; some are already done)

## DokuQuest C++ — bug and feature queue (after 1.0.0-alpha.3)

Reported by the user while testing alpha.2/alpha.3, not yet done. Work top to bottom; check each in the running game (scripted playtests, `dq-sweep`).

### Rendering
- Shelves (standing and wall) don't show their items like the original (furnish.js / furniture shelves) — probably the same missing block-entity item drawing that item piles and frames had (render/pile_layer.cpp can draw them).
- Rain puddles never dry up after the rain stops (user, alpha.3). Check the original weather.js wet/puddle timers; also check snow cover melting and ice thawing over time.
- Voxel displacement like blog.danielschroeder.me/blog/voxel-displacement-modernizing-retro-3d (the target look the user chose): continuous heights per tile → geometry displaced only in WHOLE micro-voxel steps where the height change is large (mortar, gems, bricks, chest latch), while all finer detail goes only into a smooth normal map computed from the exact heights (sub-voxel normals light the surface). Bake heights + normal maps once per tile set (resource pack) at load. Sharp block edges kept crisp. No AO/shadows needed for that look; add later.
- References the user picked (read before starting): Daniel Schroeder's three posts — voxel-displacement-modernizing-retro-3d, voxel-displacement-where-this-goes, voxel-renderer-objects-and-animation (blog.danielschroeder.me/blog/) — and John Lin's voxel sandbox (shacknews.com/cortex/article/249/john-lins-voxel-experiments). Take from them:
  * per-voxel shading attributes (normals) with a spread of normals on thin features to fake curvature, and bent-normal-style self-shadowing in crevices (cheap stand-in for AO);
  * characters/items/creatures: voxelize each animation frame (or our rig parts) at the SAME voxel size as the world, never scaled voxels; only surface voxels kept;
  * forward lighting with a handful of point lights (our dynamic lights) + ambient; shadows later;
  * John Lin: soft, noiseless lighting (GI/path-traced look) and real-time water simulation — aim for a cheap approximation (sky-visibility AO + light bounce tint), not path tracing, given the HD 530 target.
- Teardown / Daniel Schroeder style voxel look: smooth derived height field, per-voxel normals + ambient occlusion in the terrain shader (both renderers, keep Legacy fast on HD 530), features that stick out (chest latch/corners, crafting-table tools hung on the sides) vs seams that sink; voxel spray particles for rivers, waterfalls and splashes.
- Rain puddles: flat, glossy, water-like (no relief) — if fix/render2 didn't finish it.
- Dynamic light from held items, off hand, belt hook and backpack lanterns, glowing dropped items, lit TNT, fireballs (original render/gl.js dynLight: up to 8 lights, uDyn[8]).
- Spawners show the spinning mob inside.
- Enchanted items: glint on items in hand/inventory/ground and the enchantment names in tooltips.
- Tool and weapon levelling shown like the original.
- Chimneys working like the original (smoke).
- Particles: verify torches, campfires, splash potions now that sim particles reach the client (ClientFx::particles was a stub until alpha.3).

- Shader packs: the user reports they don't appear to work. Today only DokuQuest-format packs (docs/shader-packs.md: files that override assets/shaders/*.glsl) load; Minecraft Iris/OptiFine packs (shaders/*.fsh/.vsh, gbuffers_*, composite) are not supported yet — that's the roadmap's shader-pack pipeline. First check a DokuQuest-format pack end to end in Options, then plan the Iris/OptiFine loader.

### Items and animation
- Mug of ale no longer looks like a mug (likely the alpha.3 "3D icons from block shape boxes" change in assets/item_models.cpp/icons.cpp overriding special item models — items with their own 3D model must keep it).
- Fishing rod (user): the lure hangs down in front of the rod when not cast; when cast, the line runs out to the bobber in the water until reeled back in.
- Creatures swim like Minecraft (float at the surface and paddle steadily) instead of bobbing and thrashing.
- Nether portal: the Nether (and the Overworld on the way back) stays invisible after walking through until save + reload. Not the realm-change bug fixed in alpha.3 (that was the pocket/rift). Reproduce with tools/playtests/portal.json (the scripted portal didn't light — fix the script first: flint and steel on the frame's inside bottom block).
- Bow draw animation (first and third person).
- Fishing rod: better 3D model, cast/reel animation, line and bobber.
- Splash and lingering potions: throw, effect cloud, particles.
- Held items too low in the hand (first person) — check after fix/render2's scale fix.

### Characters and villagers
- 4-player split-screen (original game.js locals/ctxs, worlds.js multi-context).
- Swimming animation (if fix/chars2 didn't finish).
- Village gates swing instead of appearing/disappearing.
- Villagers' letter-by-letter babble voice.
- Villagers, dragons and the watcher synced over the network.
- Knight/Ranger talents hooked into combat.

### Systems
- Sneak + use must work with BOTH keyboard/mouse and controller (the user uses both). dq-sweep's test (PlayerController, sneak flag) places on furnace/crafting table/barrel; check the real input paths: Shift + right mouse, and the pad's sneak (latch/tap in input.cpp) + use trigger.
- Sneak + use with a block in hand on chests, furnaces, crafting tables etc. opens them instead of placing the block (user, alpha.3). sim/controller.cpp use() has the furnish.js rule (line ~420) — check whether an earlier "itemUse" handler consumes the use, or p.sneaking isn't set at that moment.
- DUPE (high): taking armour off and putting it back in the inventory duplicates items (user, alpha.3). Not reproduced yet. Suspects: two InvSessions alive at once (the screen opened twice) writing stale back/cape/hook copies through InvSession::changed/store; the 3D doll's equip/hide buttons (ui/sheet.cpp, ui/inventory.cpp H_VIS); cape stored both in Player::cape and p.data["cape"]. Write a headless InvSession test (equip → click armour slot → click main slot → count items) and fix.
- Portal: confirm worlds no longer vanish after crossing (fix in alpha.3: remesh after realm change). If it still happens through Nether portals, look at band switching in render/world_renderer.cpp drawPass and portal_view.cpp.
- Block sweep extensions: use every block with every tool, check drops against the original's drop tables.

### Next after alpha.4 (user, 2026-10-25)
- A start-to-finish playthrough check (scripted where possible): new world → village → tools/armour → Nether → fortress → End → dragon → the rift story to the end; note everything that breaks.
- The rift story: 4 → 10 Realm Fragments, each fed to the rift by hand, the rift growing each time. Slow-burn creepypasta pacing like the "I saw him in my world" stories: early signs rare and small (torches you didn't place or dimmed, a figure at the edge of fog that's gone when you look, footsteps with no source, doors left open, wrong signs, 2x2 tunnels, leafless trees), never announced. He is mostly harmless to the player directly but interacts with the world.
- Late stage (close to 10 fragments): he opens portals like the rift in town — anywhere: mines, caves, any land — walks through from his realm with corrupted hostile mobs AND corrupted ordinary animals (cows etc. that attack for him), then walks back through and the portal closes.
- Patch notes never hint at him; they still end with "Removed Herobrine."

- Better than Adventure inspiration (user, 2026-10-25): biomes, blocks, a Nether more like BTA's, mobs we lack, music in the same mood. BTA ships as jars only (the GitHub "Source code" zip is the download repo, not the mod) and its code, art and music are theirs: build OUR versions from its public feature lists (wiki, the user's video) in DokuQuest style — Doku tiles/micro-voxels, our own synth music. Never copy or resample their files.

- More music, everywhere (user, 2026-10-25): OUR OWN procedural pieces in the mood of ShignBright's "Over / Under" (the BTA soundtrack: soft C418-style piano, warm pads, lots of space). An "over" set (bright, nostalgic, per biome: plains, forest, snow, desert, ocean, village) and an "under" set (caves, deep mines, moss/lush caves, karst, torch-lit tunnels, foggy depths), plus Nether, Skylands and End sets. Seeded variation so each world sounds different. Never copy or resample their tracks or melodies.

- In-game Options laid out like the launcher's options (user, 2026-10-25): after the weekly reset.

- Holiday events from the PC clock (user, 2026-10-25): October = Halloween (zombies and skeletons in pumpkin / jack-o'-lantern heads, a hostile pumpkin creature that stands up on a vine body, pumpkins sprouting, spooky decorations), December = Christmas, and so on for each holiday; villagers decorate their towns for the current event. Our own designs. DONE for October (common/holiday.h, DQ_EVENT=halloween forces it, Options > Holiday events / settings.json "holidayEvents" switches it off): pumpkin and jack-o'-lantern heads on ghouls, skeletons and 30% of villagers (a few villager capes), 3x pumpkin patches, jack-o'-lanterns on village lamps and porches, harvest moon, foggier nights, wisps, more bats, trick-or-treat villagers. STILL QUEUED: the hostile pumpkin creature, the scarecrow, other holidays.

- Saturday Oct 31 after the reset (user): 1) Halloween creatures and costumes, 2) a large-scale bug test, finding and fixing everything in game and making sure it's all connected and working, 3) then the BTA update.

- Biome fuseling variants (user, 2026-10-26, with the BTA update): one per biome, built from that biome's blocks (terracotta and gold, bamboo, mossy sandstone, gravel, cactus-topped sandstone, mud with a spike, snow, mossy cobble, flowering azalea, mushroom cap, terracotta totem, swamp). The user showed fan art as inspiration; make our own designs, don't copy the artist's.
- After BTA (user, 2026-10-26): a Minecraft Story Mode feel for players, NPCs and mobs. That means blended transitions, follow-through on capes, hair and tails, anticipation and weight, head and eye look-at, feet planted on slopes, varied idles, expressive talking faces and smooth turning, all as one shared animation layer over the existing rigs.

- Biome golems (user, 2026-10-26, with the biome fuselings): the village iron golem built from local materials per biome (sandstone, mossy cobble, terracotta, packed snow and ice, spruce and stone...), more articulated (elbow and knee joints, segmented limbs, hands), glowing eyes. Our own designs; the user's reference is a mod's art.

- Story beats (user, 2026-10-26):
  - Nether foreshadowing: while players are in a fortress, he appears, opens a portal, grabs a wither skeleton and pulls it into his portal.
  - The climax: we see him place the last wither skull, starting a Wither Storm. It is a campaign-only boss, never natural spawning: a giant, growing three-headed wither that tears up and pulls in blocks. Our own design, not Story Mode's model.
- With BTA (user, 2026-10-26):
  - the Wither boss, summoned by players with soul sand and 3 wither skeleton skulls, never natural;
  - mob heads/skulls, as drops, wearable and placeable;
  - spawn eggs for every creature.

- Corrupted mobs (user, 2026-10-26, story update): corrupt existing mobs, hostile and ordinary animals alike.
  - Look: his realm's purplish glitch (flickering purple/black texels, a slight jitter) and pure white eyes, as if he watches or controls them through them.
  - Slow burn tied to fragments: almost none early, then rare far-off watchers, more and bolder as fragments are fed.
  - Late stage: he brings groups of them through portals like the rift in town, then walks back and the portal closes.

- Magic overhaul (user, 2026-10-26): keep the ways to unlock spells.
  - Skyrim-style hands: a spell in either hand, the same spell in both hands to combine it into a stronger one, two different spells to mix their effects, or a weapon/tool in one hand and a spell in the off hand.
  - One button per hand, on both mouse and controller.
  - Spells level up with use, like gear, tools and weapons.
- Economy (user, 2026-10-26): villagers trade in gold coins instead of emeralds, so emerald becomes a better-balanced ore. Keep or convert old saves' emerald trades.

- Bugs (user, alpha.4.10, 2026-10-26):
  - Still clipping into blocks sometimes when climbing a ladder.
  - Getting hung up on dirt path blocks sometimes (step-up across the 15/16 path edge, diagonals?).
  - SEVERE: deep underground (Overworld band-local y≈42), the near world stops rendering. Only the sky, the far LOD land and entities show, on Legacy GL 4.6 / AMD RX 6950 XT. Debug overlay: chunks 1103, sections 1406/7669, mesh jobs 0, light 0. Not reproduced in a carved test room at y≈28 with render distance 4. Suspects: near meshes dropped (setWorld/generation counter, a portal view), the band index, the LOD hole. Ask for latest.log with DQ_DEBUG_BANDS=1.
  - Fixed: the quest tracker now sits under the story line instead of on top of it.

### Roadmap (after the bugs)
Start in the wild, potions stack to 16, test-build small fixes, coins/economy, roads and bridges, travellers, saddle bags, bigger ship, poses, torches on fences, chains/lanterns, rift to 10 fragments + slow pacing + lore, graphics presets, built-in shader levels, Skylands content, Shadowbloom ports, Iris/OptiFine shader packs.
