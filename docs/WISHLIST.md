# Feature wishlist: the classic-adventure set

Written 2026-10-25 as a list of features DokuQuest lacked. Every entry says what a thing IS and DOES, in our own
words, so we design OUR OWN version in Doku style (micro-voxel tiles, our synth music).

Status marks (from a code search on 2026-11-15; check the code before building on it):
- **[in]** something by that name exists in the code or data (it may still be partial).
- **[open]** nothing found yet.

Matching rule: by meaning, not name. A close equivalent in our game means "not missing". Our game has no hunger and
no phantoms, and that stays.

---

## Decisions (user, 2026-10-25)
- Everything on this list is wanted, with these changes:
- **Seasons:** optional. Either a world setting (off by default) or kept mainly to one cherry-grove biome whose
  leaves change through the year.
- **Leading animals:** no fishing rod for it. A real lead item: tie it to an animal, it follows you, and you can tie
  it to a fence.

## 1. Biomes (Overworld)

At the time our generator had 7 base biomes (Ocean, Beach, Plains, Forest, Desert, Taiga, Mountains) with
climate-driven variants (jungle, savanna, cherry, mangrove).

Wanted:
- **Meadow** [in]: flat, flower-packed lowland, calm and bright. "Wildflower Meadow", bees and rabbits, tall flower
  patches, pink petals.
- **Boreal Forest** [in]: a warmer taiga with very tall spruce. "Highland Spruce", giant spruce plus moss and berries.
- ★ **Outback** [in]: scorched red desert with eucalyptus-style groves and spiky ground tufts, on a rich "scorched
  dirt" soil. "Redlands", dry gullies, ore-bearing scorched dirt.
- **Swamplands** [in]: algae-covered water and moss-draped trees. "Mossmire" with algae, mossy logs, fireflies at
  night.
- **Seasonal Forest**: cherry groves that turn pink in spring (ties to seasons). Our cherry grove as a lowland biome
  whose leaves change with the season.
- **Caatinga** [in]: dry shrub country with thorn trees and palm trees. "Thornscrub", cactus-like water plants.
- **Glacier** [in]: huge ice sheets and never-melting ice. A band north of taiga with blue ice spires and perma-ice.
- **Bigger biomes plus more snow biomes** (generation tweak): about twice the biome size, snow biomes more common,
  a tundra step. (Terrain version 4 made biomes bigger.)

Already covered then: Plains, Forest, Desert, Taiga, Mountains, Ocean, Beach, Jungle/Rainforest, Savanna, Cherry
grove, Mangrove shore, tundra-ish snow in taiga/mountains.

---

## 2. Blocks

### Terrain and plants
- ★ **Pumice** [in]: Nether stone that soaks up lava like a sponge, for draining lava lakes safely.
- **Molten Pumice** [in]: a recipe turns pumice back into a lava source; portable lava, usable as a fuel block.
- **Brimsand** [in]: black volcanic sand forming islands over lava.
- ★ **Rubyglass** [in]: tall glowing crystal growths, Nether scenery and machine parts; grow from "nodes", a pickaxe
  or piston breaks them; regrow slowly.
- **Cobbled / Mossy / Igneous / Polished / Smooth Netherrack plus Netherrack Bricks** (slabs and stairs) [in, cobbled
  and mossy at least]: a full stone family for the Nether; cobbled stands in for stone in recipes. Igneous burns
  forever and dries wet sponges.
- **Soul Schist / Bone Shale** [in, as ores in the older Nether]: "bone-white rock" strata.
- **Petrified Log** [in]: stone-like dead trunks standing on ash plains.
- **Scorched Dirt / Rich Scorched Dirt** [in]: parched soil; the rich kind yields raw metal when sifted (see
  Trommel).
- **Perma-Ice** [in]: ice that does not melt, in cold biomes.
- **Permafrost family** [open]: cobbled, polished and brick permafrost with slabs and stairs (we have plain
  Permafrost).
- **Nether Coal Ore** [in]: coal found in the Nether, in netherrack, basalt and gloomstone.
- **Mossy logs** [open]: moss-covered logs from swamp trees, for oak and mangrove.
- **Thorn Tree** [in]: spiny tree of the Caatinga; harmful leaves.
- **Palm Tree** [in]: coastal and dry-climate palms; coconut drop.
- **Eucalyptus-style tree** [in]: Outback grove tree with pale bark.
- **Giant rainforest tree** [in]: very rare massive trunk; climb it with rope.
- **Cacao tree** [open]: rare rainforest tree that makes cocoa farmable on a real tree.
- **Spinifex tufts / Algae / Bluebells / Marigolds** [in]: Outback tufts, swamp algae, two flowers.
- **Dried / raw Sugar Cane blocks** [open]: thatch-like cane bundles for storage and building.

### Building and decoration
- ★ **Painted planks (16 dye colours)** plus painted doors, trapdoors, signs, buttons, pressure plates and chests [in]:
  any plank dyed with a paintbrush; one dye-able plank state per wood instead of 16 separate blocks.
- **Glass Door** [open]: see-through door and glass trapdoor.
- **Steel door / trapdoor / fence / steel-framed glass** [open]: building pieces for the steel tier (we have steel
  ingots and block).
- **Lapis / Iron / Gold / Steel Bricks** (slabs and stairs) [open]: metal brick blocks.
- **Coloured and Inverted Redstone Lamps** [in, inverted at least]: lamps that take dye, or light up when unpowered.
- **Statue** [in]: decorative carved figure of a mob or hero that entities cannot walk through.
- ★ **Jar** [in]: placeable glass jar for flowers or captured fireflies/butterflies (see Mobs).
- ★ **Brazier** [in]: a safer open fire in a metal bowl; lights without spreading.
- **Painting** [open]: wall art with gold or iron trims; voxel paintings painted from palettes (we have item frames).

### Traps and machines
- ★ **Spike Trap** [in]: floor spikes that hurt things walking onto them; items fall through; placeable without a
  block below; used in dungeons.
- **Motion Sensor** [in, in structures]: detects movement nearby and sends a redstone signal; pairs with spikes and
  locked doors.
- **Mesh Block / Gold Mesh** [open]: blocks mobs and players but items pass; gold mesh filters which items pass.
  Iron/gold grates for farm sorting.
- **Activator** [in]: when pulsed, does a right-click on the thing in front ("hand block" for automation).
- **Reinforced Piston** [in]: longer reach, can launch mobs and blocks into the air.
- **Redstone Pumpkin**: a pumpkin that works as a powered block (unchecked).
- ★ **Trommel** [in]: sifting machine that turns mud, sand, gravel, soul sand, brimsand and scorched dirt into small
  valuables (string, redstone dust, raw metal); a water-wheel-driven sieve block plus a held sieve.
- **Siphon** [in]: item mover that shows a particle while working.
- **Conduit** [in]: carries power between crystal parts; fed by redstone ore.
- **Matcher**: a crystal-tech component that compares things; only if we want a comparator variant (unchecked).

Already covered then: Basalt, Granite, Limestone, Marble, Slate (with bricks/cobbled/polished variants), Dirt Path,
Fence Gates, Moss, Rope, Paper Wall, Permafrost, Sandstone bricks/stairs, Blast Furnace, Heather, Blue Orchid, Cocoa,
Pumpkin, Sugar Cane, Redstone Lamp, Sponge, Soul Sand/Soil, Netherrack, Nether Bricks, Glowstone, Magma Block,
Banners/Flags (16 colours), Bunting, Chairs/Benches/Stools, Item Frame, Flower Pot, Weapon Rack, Conveyor Belt,
Crusher, Harvester, Waterwheel, Piston/Sticky Piston, Lever/Buttons/Pressure Plates, Note Block, Carved Pumpkin,
Hopper/Dropper/Dispenser.

---

## 3. Items

- ★ **Paintbrush** [in]: recolours placed blocks (planks, doors, chests, lamps) and can dye sheep; empties as it is
  used; tied to our 16 dyes.
- **Quiver / Golden Quiver** [in]: carries arrows; golden ones hit slightly softer but break on landing.
- **Rocket Launcher** [in]: late-game steel weapon firing explosive rockets.
- **Hand cannon** [in]: handheld blast that flings the user or targets; good for rocket-jumping.
- ★ **Guidebook / Travel Journal** [in]: opened with a key, shows recipes and block notes; entries unlock as you
  obtain items.
- **Summoning Wand** (creative) [open]: spawns mobs, even from dispensers; a dev/creative tool.
- **Held sieve** [in]: hand-held sifter.
- **Butterfly / Firefly Cluster** [in]: catchable critters kept in jars or lanterns, can be labelled; "Firefly
  Lantern".
- **Basket** [in]: carries small animals or produce.
- **Lead** [in]: a real lead item (user decision), not a fishing rod.
- **Cloth** [in]: zombie drop; beds could need cloth plus wool ("Rotten Cloth").
- **Fire Striker**: covered by our Flint and Steel.

Already covered then: Steel tools/armour, Blast Furnace, Flint and Steel, Pumpkin Pie, Pumpkin Seeds, Map, Compass,
Clock, Bone Meal, Saddle, Fishing Rod, Rope, Chains, Banners, Dyes, Ale and other foods, Bed, Spyglass.

---

## 4. Mobs

Our 124 mob types already covered the classic roster.

Wanted:
- **Snow Sentry** [in]: a guard built from snow blocks and a pumpkin that throws snowballs at hostiles.
- **Butterflies and Fireflies** [in]: harmless ambient insects (butterflies by day in meadows, fireflies at night in
  swamps), catchable into jars.
- **Armoured / armed zombies**: a variant, not a new species; an armour and weapon roll for Zombie, notably in
  labyrinths (unchecked).

Cheap behaviour ideas: zombies drop cloth, skeletons visibly load their bows, fuselings path under trapdoors, squid
swim smarter, ridden pigs keep momentum on stairs, mobs do not retaliate in creative, wolves spawn more often, mobs
spawn more in winter.

Already covered then: Wolf, Ghast, Squid, Zombie, Skeleton, Spider, Slime, Zombie Pigman, Pig, Cow, Sheep, Chicken,
Bat, Bee, Iron Golem, Fuseling, Stone Mite, Husk, Stray, Drowned, Witch, plus all dragons, wardens and boss lines.

---

## 5. Structures

Nothing new wanted. Twists on what we already have:
- **Labyrinth flavour**: underground, very dark, scattered, rewarding. A darker labyrinth variant using spike traps,
  motion sensors, mesh gates and crystal light.
- **Skyblock world type** [in]: a world type, not a structure; see section 8.

Already covered then: Labyrinth, Dungeons (classic and deep), Mineshaft, Strongholds, Ancient Cities, Nether
Fortress, Ember Bastion/Spire, End Cities, Villages and Towns, Desert/Ocean/Drowned temples, Pirate Cove, Shipwrecks,
Buried Treasure, Sky temples and shrines, Skyships, Moa nests, Dragon lairs.

---

## 6. Nether

**Status: [in].** The classic Nether (terrain version 7 and up, `src/gen/nethergen/`, mapped by
`src/gen/nether_exact.cpp`) has all of these lands. Its full spec is in [research/classic_nether.md](research/classic_nether.md).

What we wanted:
- **Terrain:** the basin-and-cave Nether as one land among several, with caves; other lands add islands over lava,
  crystal fields, ash plains and pools with steam vents.
- **Lands:**
  - ★ **Volcanic Islands**: black sand islands over a lava sea, lava falls, ash plumes.
  - ★ **Sulfur Pools**: pools of boiling acid ringed by vents that shoot hot steam.
  - **Crystal Forest / Plains**: fields of towering glowing crystals.
  - **The Old World**: an ash-covered field with fossil trunks, ash drifts, bone strata.
  - **The Crag / Shelf**: the original basin look; our Ember Depths basin.
- **Blocks:** pumice and molten pumice, black sand, crystal blocks, cobbled/mossy/igneous netherrack and brick
  families, bone and soul stones, Nether coal ore, glowstone always giving 4 dust, wet sponges that dry near lava,
  doubled cobbled netherrack slabs that burn forever.
- **Mobs:** keep our richer Nether bestiary (wither skeleton, cinder golem, cinder wisp, charred ghoul, magma slime,
  Ember Lord/Drake/Dragon); add two rules: ghast fireballs respect the mob-griefing rule, and pigmen only spawn on
  natural Nether blocks (a base on placed blocks is safe).
- **Light:** coloured glow from crystals and lava (red/orange), dim ash regions for contrast; ties to our coloured
  light.
- **Sounds:** see section 7.
- **Portals:** variable portal sizes (4x3 option), frame corners must be filled, and dye-tuned links so players can
  have several Nether gates (see section 8).

Already covered then: Netherrack, Nether Bricks and fortress, Soul Sand/Soil, Basalt, Blackstone, Magma Block,
Glowstone, Nether Wart, Crimson/Warped fungi and nylium, Shroomlight, Ancient Debris, Nether Gold Ore, Ember Quartz
Ore, Emberstone family, ghasts, wither skeletons, zombie pigmen, ember lords/dragons.

---

## 7. Music / sound ideas

All our own design and our own synth.

- **Mood target:** sparse, lonely, reverb-heavy synth pads and soft plucks, long silences between tracks.
- **Per-biome cue sets:** Meadow (bright bell plucks), Swamp (low drones, frog and insect beds), Glacier (glassy
  chimes, wind), Outback (dry guitar-like pluck, heat shimmer), Caatinga (quiet hum, distant birds).
- **Season stingers:** short motifs when seasons change (spring birds, summer cicadas, autumn leaves, winter wind).
- **Nether ambience:** steam-vent hiss with random gusts; bubbling acid at the Sulfur Pools; crystal chime pitched by
  crystal size; ash wind and distant rumble on the Volcanic Islands; deep slow pulse in the Old World.
- **Subtitles / captions** [in]: name the sound when a mob or block makes a noise off screen.
- **Eating sound** for food; check whether we already have it.
- **Pack hooks:** let resource packs replace sounds and music, same as textures.
- **Sleep audio:** a faint sting when a nightmare starts (see game rules, section 8).
- Existing discs: Ember, Grove, Sunrise, Tide. New discs could match new biomes (Meadow, Glacier, Swamp, Sulphur).

---

## 8. Other features worth having

- ★ **Seasons** [in]: four seasons in a repeating cycle (14 days per season), seasonal tint of leaves and grass, snow
  covering newly loaded land in winter and clearing in other seasons, slower crops and more mobs in winter, longer
  nights. Tie to the day counter; a calendar item [in] and a sundial [open].
- ★ **Game rules** [in]: keepInventory, allowSprinting, allowSleeping, doNightmares, doSeasonalGrowth, tree felling,
  dwarfMode (shrink the player), mobGriefing, dayLightCycle, doWeatherCycle, doFireSpread. A rules panel;
  `doNightmares` fits our rift story.
- **Nightmares while sleeping** [in]: slow-burn rift hints through dreams (fits the queue's creepypasta pacing).
- ★ **Pick up and move containers**: sneak + empty hand lifts a chest with its contents; also barrels, furnaces and
  brewing stands (mentioned in the help book; check the code).
- **Dye-tuned portals**: choose which portal links to which gate with dyed blocks; variable portal sizes (unchecked).
- **Game modes**: Hardcore, Adventure (no block breaking), Spectator [in, spectator at least].
- **Skyblock, Indev and Classic world types** [in, Skyblock]: a Skyblock start fits our Skylands.
- **Achievements** [in]: a redesigned screen; tie new ones to biomes and Nether lands.
- **Rainbows after rain** [in] and **random star colours** [open]: cheap sky touches.
- **Rail power running up slopes**, **furnace minecarts using any fuel**: small quality-of-life touches.
- **Sorting** [in] (middle-click sorts inventory and chests), **rearrangeable HUD**, **in-chat emote picker**,
  **hotbar page cycling** (TAB), **easy bridge keybind**, **render-distance slider to 32 chunks**: UI ideas; check
  against our settings.
- **Commands**: /playsound, /spawnparticle, /mobspawning, dimension teleport subcommand, in the dev console.
- **Colourblind shaders** [in] and an **accessibility page**: pair with shader-pack work.
- **Darker caves / light**: unlit places truly dark; ties to our light propagation.
- **Spawner passes items through** and **sponge un-dyes blocks**: small mechanic touches.

Already covered then: survival without forced hardcore, our own commands, banners and flags, Pumpkin Pie, item
frames, flower pots, Elytra, gliders and ships, weather and puddles, day/night.

---

## Priority list (★), the 15 most valuable

1. Seasons (section 8).
2. Volcanic Islands Nether land (section 6).
3. Sulfur Pools Nether land (section 6).
4. Pumice for lava control (section 2).
5. Rubyglass crystals (section 2).
6. Trommel / sieve machine (section 2).
7. Painted planks, doors and chests in 16 colours (section 2).
8. Paintbrush (section 3).
9. Spike Trap, best paired with Motion Sensor and Mesh blocks (section 2).
10. Brazier (section 2).
11. Guidebook / Travel Journal (section 3).
12. Jar, with butterflies and fireflies (sections 2 and 4).
13. Outback biome (section 1); Swamplands and Caatinga right after it.
14. Game rules panel, including nightmares (section 8).
15. Pick up and move containers (section 8).

Strong runners-up: Crystal Forest Nether land, steel building set, dye-tuned portals, Motion Sensor and Mesh blocks
on their own.

## What is still open (from the marks above)
Permafrost family, mossy logs, cacao tree, cane bundles, glass and steel doors/trapdoors/fences, metal bricks,
paintings, mesh/grate blocks, summoning wand, sundial, random star colours, dye-tuned portals, container carrying
(verify), armoured zombies (verify), the UI ideas and commands in section 8.
