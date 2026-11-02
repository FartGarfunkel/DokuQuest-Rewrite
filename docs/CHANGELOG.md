# Changelog

Release notes for the C++ versions of DokuQuest, newest first. The release workflow
(`.github/workflows/release.yml`) publishes the section whose heading starts with the tag's version as the
GitHub release notes. Keep it in step with `assets/news/news.dqn` (the launcher's news). Patch notes never
mention Herobrine and every entry ends with "Removed Herobrine."

## 1.0.0-alpha.13 — Undercoat (November 2026)

**Lighter on memory:** the land near you takes a fifth less graphics memory (16 bytes a corner instead of 20). Grass, leaves and water now take their colour from the climate where they stand as they're drawn, with the resource pack's grass and foliage colour maps, so the colours along the edge of the loaded land settle by themselves as the next chunks arrive.

**Sheep:** a shorn sheep keeps a short undercoat of tufts in its own colour until its wool grows back (as in Minecraft 1.21.5), from the resource pack's own undercoat picture when it has one. A pack's sheep wool picture is used now too (only its body was before).

**Creatures:** every land's fuseling comes in two more looks, grown, burnt or bleached into it by its land:
- barnacled and kelp-draped on the beaches, reed-grown and mud-caked in the bogs, cactus-spined and sun-bleached in the dunes
- icicled and snow-capped in the frost, flint-studded and cobbled on the gravel, vine-hung and blooming in the jungle
- flowering and hay-backed in the meadows, toadstool-sprouting and fern-tufted in the moss, great-capped and brown-spored among the mushrooms
- gold-seamed and terraced in the redlands, thorn-backed and scrub-tufted in the savanna, lily-backed and slime-streaked in the swamps
- cinder-cracked and obsidian-plated in the Nether, cloud-wrapped and storm-crested in the Skylands

Many of them drop a little of what grows on them.

**Villagers:** each villager has touches of their own now: freckles, an old scar, an earring, a headband or a flower behind the ear, a scarf, a satchel strap, a patched knee or elbow. Scarves, headbands, earrings and satchels stand off the body.

- Removed Herobrine.

## 1.0.0-alpha.12 — Coloured Light (November 2026)

**Light in colour:** redstone glows red, soul fire and soul lanterns blue, sea lanterns cyan, amethyst violet, the coloured lamps their own colour, and torches and fire stay warm. The colours spread through the air and mix where lights meet, on blocks, creatures and what you hold (Options → Coloured light).

**Gameplay:**
- **A cast button for each hand:** cast your right hand's spell, your left hand's, or both together (Z / mouse 4 for both, N / mouse 3 for the left; on a controller hold Sneak and press RB or LB).
- **Crows** flock over fields and village farms and settle on scarecrows (a pumpkin on a hay bale on a fence post) and on fence posts by the crops, two to a head. Walk up on them and they take off, unless you sneak.
- **Villagers go to church:** every seventh morning the bell rings, villagers fill the pews and stand at the back, and bow before the chapel's banners.
- **More in the Skylands:** driftstone, a pale lilac stone that falls *up* until it meets something; whirlwinds that wander the islands and toss whatever they touch into the air; and the Valkyrie Queen now wakes with two valkyries at her side.
- **Players move better:** a push-off and a tuck when you jump, a landing that sinks with the height of the fall, a wind-up before a swing, a bounce and a nod as you run.
- **Creatures, as in BTA:** fuselings slip under a shut trapdoor at head height (a ghoul doesn't fit); squid keep to deep water, swim in strokes, and squirt ink and dash off when you swim up close; a ridden horse or pig takes stairs and slabs at a run instead of stopping at each; and winter's long dark brings more monsters out.
- **Steam vents in the Nether:** the vents breathe out plumes of steam with a hiss now and then, in every Nether land.

**Worlds and portals:**
- **Tune a portal with dye:** use a dye on a portal's frame and it takes that colour and links only with a portal of the same colour, so many portals can stand side by side and each goes where you mean it to.
- **Bigger biomes:** a new world option makes every land three times as wide.

**Playing together:**
- **Dragons and world progress are shared:** friends see the dragons as you do, and your world's progress (eggs, the End, tuned portals, cleared dungeons, places found) reaches them too.
- **Split screen and LAN together:** couch players come along whether you host a LAN game or join someone else's: each has their own body, bags and health on the host, their skin reaches everyone, and their chat goes out under their own name.
- A friend's game no longer misses checking some of the world on a busy host (chunks the host was still making).

**Looks:**
- **Distant terrain, much closer to Cubyz:** the far land is four times finer at every distance (a far voxel stays a few pixels across instead of turning hills into giant steps at the edges of the view); every block shows its own picture at its true size (no more one picture stretched across a mountain), fading to the block's colour where a block is smaller than a pixel; the corners of its tops are shaded where higher ground stands over them, so slopes and cliffs have depth; far forests are a lumpy canopy, not a flat roof; and its water takes the colour of the water near you, darkens with depth and catches the sky at a low angle.
- **Terrain memory:** a new option caps the graphics memory the land uses; past it, the chunks farthest out are drawn as distant terrain instead. The far land itself takes a quarter less memory per vertex.
- **Particles:** smoke, breath, mist and poofs are soft and see-through now, fade in and out, and take the light of wherever they drift; your breath in the cold no longer fills your view.
- **Shaders:** built-in shader levels (Off, Low, Medium, High, Ultra): a film grade and vignette, bloom, sun rays and sharpening. And **Iris/OptiFine shader packs** can be used: drop the pack's .zip in shaderpacks/ and its after-effects (colour, bloom, tone mapping, depth of field...) run over the game's picture.
- **Creatures:** the skins the DokuQuest Base pack has none of (about a hundred creatures) now sit with the pack's own: a shade darker and less sweet, their speckle smoothed into a soft mottle, their markings kept; the pack's own creatures are untouched. Every creature's skin is checked when it loads (one without a head or a body falls back to its own instead of drawing broken).

**Tools:**
- **Texture Studio:** a new Creatures tab: open any creature's skin from your resource pack, paint it at its own size with a live preview, and save it straight into the pack (or over it, without touching the pack); "Put the pack's back" undoes it.
- **The Engine page** in the Mod Studio is now an engine editor: open and edit the engine's source, shaders and data, build it, start the game with the engine bridge and drive it live (commands, shaders reloaded as you save, screenshots, its log), and ask Strata about the open file. Like the Zed editor, it finds the models running on your computer by itself (Ollama, LM Studio, llama.cpp), so Strata shows up and is picked while it runs. Strata (or any AI that speaks MCP) can also drive the running game itself: `dokuquest-launcher --mcp` (docs/engine-bridge.md).
- **/lineup <page>** sets every creature out side by side, a page at a time.

**Downloads and updates:**
- Releases keep the game's data in one file (data.dqa) instead of nine and a half thousand; a loose file in data/ still wins, for mods and your own changes.
- Updates download just the game; the whole package only when the launcher itself has changed.
- **Pick the version to play** (Options → Updates): go back to an earlier version you have, offline too; the last three are kept.

**Fixes:**
- **The Travel Journal, maps, the places list and the quest board no longer crash later in the game** (once a dragon was slain or a great dungeon cleared: some records were saved as numbers and read as yes/no). Checked now at every point of the game's progress.
- **Placed mob heads no longer flicker:** the block under each head drew a stone box in the same place as the head, and the two fought (most easily seen on a Wither skull on soul sand or a command block).
- **Sounds that played nothing now do:** a sieve wearing out, the block placer placing and breaking blocks, a snow sentry building its snow, and the summoning wand calling a creature up. Every sound the game plays is checked now.
- **Under water you can see:** a few blocks down (in a well, a pond, the sea) it was a dark navy wall; now your eyes adjust after a moment, like Minecraft's water vision: murky blue, but you see the walls, the bottom and what's in front of you (more by day than by night).
- **/kill and /summon know creatures by name:** `/kill ghoul` or `/kill @e[type=ghoul]` clears those nearby (it used to kill *you*, whatever you typed after it), `/kill @e` every creature nearby, and the Minecraft names work too (`/summon zombie` is a ghoul, a creeper a fuseling). A name that's no creature gets a few that look like it instead of all hundred and twenty.
- **Death messages for every way to die:** acid, poison, withering, lightning, a falling block or tree and more now say what got you instead of a bare "You died".
- **Stepping through a portal mid-stride** could count as a fall from the height you left.
- **/weather works again:** it only took effect once the world was closed and opened (and saving could undo it); now the rain, snow or storm rolls in at once.
- **The death screen says what killed you** (it always said "You died").
- **/tp to a land with no ground just there** (the End's void, a gap in the Skylands) takes you to the nearest ground instead of dropping you into the void.
- **Portals into the Nether** look round for a floor before building over open air, and a portal built over a drop gets a wider landing: walking through, you used to step off its edge and fall.
- **Messages no longer show twice:** achievements, warnings and other notices each popped up two times over.
- **/locate** gives a place's height the way /tp takes it (it gave the land's own height, so copying it into /tp dropped you into another land), and names the land when the place is in another one.

- Removed Herobrine.

## 1.0.0-alpha.11 — Every Creature Its Own (November 2026)

**Every creature comes in looks of its own:** 255 of them across 121 creatures, each built a little differently and each from somewhere in the world. Where it turns up, what it carries and what it drops all come from its story.

- **Skeletons:** one shot full of arrows (it drops them), a miner with a lamp helmet and a pickaxe deep underground, mossy ones in the woods and swamps, sunken ones grown over with coral on the coasts, webbed ones in caves, a ranger with a quiver and a hat, and now and then a fallen knight.
- **Ghouls:** miners in the dark with coal and torches on them, farmhands in straw hats with hoes near villages, frozen ones crusted with ice in the snow, overgrown ones under moss and vines in the jungles and swamps, lost travellers with a pack and a bedroll (and sometimes a compass), fallen soldiers in old helmets.
- **Husks:** prospectors with wide hats and gold in their pockets, embalmed ones trailing wrappings, and ones stuck full of cactus spines. **Drowned:** barnacled, reef-grown with coral, a sailor with a fishing rod and an eyepatch, and one anchored in chains.
- **Strays** with icicles or snow on their shoulders, or carrying a lantern; **scorched skeletons** with obsidian shards or chains; **wither skeletons** with a broken crown; **pigmen** in gold plates or bandages; **endermen** grown with purpur crystals or crusted with end stone; **witches** with mushrooms on their hats, from the bog, or lit with candles; **raiders** with a banner, a spyglass or an axe; **pirates** with an eyepatch, a parrot on the arm or a deckhand's stripes; **ogre chieftains**, **mossback ogres**, scarred **werewolves**, lantern **wraiths**; **guards** who are veterans, archers or lamplighters.
- **Golems built of whatever their makers had:** copper with a lightning rod on its head, amethyst crystals in the mountains, a mushroom golem under a red cap, a cactus golem in flower, prismarine on the coasts, honeycomb in the meadows, quartz under snow, an ancient mossy stone one deep in the woods, a rusted one, and rarely gold, emerald or diamond. Village golems sometimes carry a flower.
- **Cows:** dairy, pinto, ashen, umbra, sunset, belted, a shaggy highland cow in the cold, a belled cow by villages and longhorns on the savanna. **Pigs:** piebald, pink-footed, sooty, spotted, mottled, mud-caked, and a truffle hunter with a mushroom on its head. **Sheep:** patched, rocky, fuzzy, rams with curled horns and a bellwether with a bell. **Chickens:** stormy, amber, bronzed, gold-crested, skewbald, midnight, a fluffy silkie and a scrawny one.
- **Wolves:** brown, husky, golden, black, chestnut, spotted, striped, snowy, a shepherd, a pug, and the rare scarred pack leader. Each wears its own picture when your resource pack has one.
- **Out in the wild:** draft horses with feathered hooves, painted horses, a lost warhorse in its barding, runaway pack donkeys and caravan llamas with bags (and what was in them), trader camels, longhorn and frost-bearded goats, dappled and moss-antlered deer, old tuskers, honey-pawed bears with a bee about them, calico, ginger, siamese and tuxedo cats, a ship's cat on the coast, white lions and tigers, a jaguar, silver and cross foxes, rabbits of six coats and the very rare jackalope, harvest mice, mossback crocodiles, barnacled turtles, lily-hatted frogs.
- **Crawlers and the rest:** spiders carrying egg sacs, mossback, frost and sand spiders, web-weavers, crystal cave spiders; slimes that swallowed a skull or a gold nugget; weeping ghasts; scarred old sharks; drakes with old arrows in their hides; elder fuselings of every land; and more.
- **/looks <creature>** sets every look of a creature out in a row in front of you, and **/summon <creature> [count] [look]** summons one look.
- **Your death message names the look that got you** ("You were slain by a miner skeleton").

**Smaller things:**

- **Thin things on creatures keep their shape:** leaves, ferns, cobwebs, coral fans and wings were cut down by their own edges.

- Removed Herobrine.

## 1.0.0-alpha.10 — On the Road (November 2026)

**Between the villages:**

- **Roads join neighbouring villages:** a path of the villages' own street blocks from the end of one's main street to the end of the next's, winding round hills and lakes, with lamp posts along the way.
- **Bridges** carry a road over a river or lake: planks with railings, on posts down to the bed.
- **Travellers:** by day you may meet a merchant walking a road from one village to the next. Talk to them to trade.
- **Saddle bags** (four leather, string and a chest): put them on a saddled mount of yours and sneak-use it to open 18 slots that go where it goes. If it dies, everything in them falls, and the bags too.
- **The galleon:** the sailing ship built bigger, with two masts, a bowsprit, a wide deck and a hold of 54 slots. Two sailing ships, a chest and three wool.
- **Start in the wild** (a new world's setting): begin out in the wilds, with no village within 200 blocks, instead of at a village's edge.

**Creatures:**

- **Bees are small again,** the size they were in the HTML game, several to a nest: they come out one at a time by day to work the flowers, carry pollen home to make honey, and go in at night or in the rain. Bee nests are more common near flowers, in meadows and plains most of all.
- **Every land has its own fuseling, built from the land:** creeper-shaped, with the creeper's face, and each one shaped its own way — a redlands one of terracotta flecked with gold with a little cactus on its head, a jungle one that's a bamboo stalk on a mossy foot, mossy sandstone on the beach, cactus-topped sandstone in the desert, mud with a spike in the bog, snow with two stones on its head, mossy cobble with grass tufts in the woods, a flowering azalea bush in the meadows, a stem under a wide red cap in the mushroom fields and a totem of three terracotta faces on the savanna. New: **gravel fuselings** in the mountains and **swamp fuselings** with a bone face under a leafy brim.
- **Savanna villages keep a savanna golem,** and each village golem is built from its land and shaped its own way, with no glow: shoulder plates and a cactus spine on the sandstone one, ice spikes and an icicle beard on the frost one, moss, vines and a mushroom on the mossy one, acacia horns and a sash on the savanna one.
- **Wolves are about more often** in forests, the taiga and the snow.
- **Dungeon keepers wake** when you walk into their hall, and the masters of the great places (the lich king, the pirate captain, the ember lord and the rest) when you come in: killing one clears the dungeon or the place for good, on your maps and for the quest board. One lost while nobody was about is back at its post next time.
- **Monsters after you follow you through a portal,** and the far side of a portal you're near has its own creatures, so there's life to see through it.
- **Ghasts are heard all the way out to where they shoot from:** their cry and their shot carry 64 blocks (a shot from far off used to come in silent).

**Music:**

- **Every land sounds different:** the Overworld, the Skylands, the Nether and the End each have pieces of their own.
- **New melodic pieces on a hammered dulcimer,** warm and bass-heavy at a gentle dance tempo, for the Overworld by day and night, the Skylands, the Nether and the End.
- **Eight more music discs** (Skylark, Void, Aurora, Lantern, Harvest, Frost, Dune and Crag), found in dungeon chests and by fishing. Each plays its own tune in a jukebox, the same every time.

**Playing together:**

- **A friend in your world can use your creatures:** feed, tame, saddle and ride them, lead them, put them in a basket. The host's game does it as it would for its own player.
- **Saddle bags and the chests of carts and boats open for friends too,** and what they move in or out stays put for everyone.
- **Summoning wands, glass jars, mesh filters, the matcher and painting trims** work for friends in your world.

**Accessibility (Options → Accessibility):**

- **Captions:** words at the side of the screen for the sounds around you (creatures, doors, explosions, footsteps, the Nether's volcanoes...), with an arrow for the way they come from.
- **The colour filters, view bobbing, screen shake and damage tilt** are on the same page now.

**Smaller things:**

- **The bow is held the right way round in third person** (upright, the curve away from you, the string toward you), and in first person it's the same size at rest as when you draw it.
- **Footsteps land on your steps:** the legs, the view's bob and the step sounds all follow one stride, walking or running.
- **The Nether's places stand on the new Nether's ground:** forges, bastions, spires and bone pits sat at the old Nether's heights (a forge could float in the air).
- **The Wither Storm also rises from the Wither's T with a command block as the middle block of the T's top row** and the three skulls on top.

- **Render distance goes to 32 chunks.**
- **/tp overworld, /tp skylands, /tp nether and /tp end** take you to that land's ground above where you are (or above x z).

- Removed Herobrine.

## 1.0.0-alpha.9 — The Better Nether (November 2026)

**A new Nether (new worlds):**

- **The Nether is a land you can cross now,** like Better than Adventure's: open ground under a high roof instead of a maze of caverns, lit well enough to find your way, each land with its own haze. Five lands:
  - **The Crag:** the old Nether's netherrack hills and crags, rock spires and pillars, lavafalls from the roof, melting rock and bleached bones.
  - **The Volcanic Islands:** a lava sea with islands of basalt and blackstone, and volcanoes whose craters spill lava down one side; brimstone on their flanks.
  - **The Sulfur Pools:** a flat yellow wasteland with steam vents and pools of **acid**. It burns whoever swims in it without armour, and wears the armour down; buckets can't hold it.
  - **The Crystal Forest:** a maze of huge glowing red crystals, some hanging from the roof. **Charged crystals** hold latent energy: they power redstone.
  - **The Old World:** a colossal desert of ash with smouldering embers (they burn unless you sneak), pits down to the lava, petrified trunks and ruins of scorched brick.
- Worlds made before keep the Nether they had.
- **Each land has its own life:** sulfur slimes (their touch poisons) in the Sulfur Pools, crystal wisps in the Crystal Forest, scorched skeleton archers in the Old World's ruins, magma slimes and ember fuselings by the volcanoes, zombie pigmen in the Crag and the Old World. And its own sounds: stones falling in the Crag, the ground grumbling by the volcanoes, bubbling acid, crystals chiming, wind moaning through the Old World's ruins.
- **Volcanoes erupt** now and then: a grumble, a blast you hear from far off, a fountain of lava, glowing bombs over the slopes and a column of ash up to the roof. The ground shakes if you're close.

**Tools and armour of mixed materials:**

- **Mix materials at the crafting table:** a pickaxe of iron, diamond and copper (or any tool or armour with more than one material in its pattern) is one tool, each material showing on its own part of it, with stats mixed from all of them. Bone, cinder rod or steel hafts change it too.
- **Mend a mixed tool or piece** with any of its own materials, at the crafting table or the anvil.
- **The Tool Forge sets gems:** one gem a tool. Diamond makes it tougher; emerald, amethyst or a charged crystal faster; ruby adds fire, sapphire frost; quartz and red crystal hit harder; cinderite raises its tier. The gem shows on the tool.

**Light and distance:**

- **Dreamlike lighting** (Options → Graphics → Lighting): a soft glow round bright things, golden light when the sun is low, soft focus and a warm halo round a low sun. After the Horizon project's shaders; Classic turns it off.
- **Far land fades in** instead of popping, and floating islands far off show their stone undersides.
- **Far land is made of voxels now, like Cubyz's:** textured with the blocks' own pictures (stone cliffs, snowy peaks, sand, brick builds), coarser the further it is, each coarse voxel taking the block that shows the most.
- **And it's the real world out there:** the land you haven't been to yet is made by the world generator itself in the background, nearest first, so caves and ravines, natural arches, villages and towers, lakes, ice floes and real trees show far off as they are, overhangs and all. It's kept in the world's folder, so next time it's there at once.
- **Raised micro-voxels on every textured block:** sand, dirt, terracotta, netherrack, granite and the other softer pictures stand out in relief too (they were flat).

**More from Better than Adventure:**

- **Saddle a pig and ride it** (no taming needed). Its short legs can't jump a whole block: stairs and slabs help.
- **Ice floes** drift in the seas by the glaciers (new worlds).

**Fixes from your play:**

- **Campfire logs show bark** (they showed a stony ash texture).
- **Opening a chest no longer leaves a hole** in the block beside it.
- **Doors and trapdoors keep their raised pixels while they swing.**
- **Lanterns hang on a chain to a ceiling up to eight blocks up,** or from the underside of a top half slab.
- **Skulls and heads are the creature's own head** in your hand and inventory, not helmets.
- **The Wither Storm can be raised with a command block** in the middle of the Wither's T as well as with the humming core.
- **In creative mode you can break anything,** command blocks included.
- **The Wither:** a skull you punch back hurts it, even once its bones harden; below half its strength, arrows visibly glance off it with a hint, instead of seeming to pass through.
- **On a LAN game everyone sees everyone's skin,** the host's too.
- **Less lag beside a Nether portal:** the view through it draws only what shows through the window.
- **Villagers sit on benches from any side** and give up on one they can't reach (they circled it before).
- **The Tool Forge shows in your hand,** and so do the other newer blocks.
- **The Skylands' grass, trees, stone and cloud banks, and the new flowers, use the DokuQuest Base pack's own pictures,** recoloured; berry bushes lost their black spots.

- Removed Herobrine.

## 1.0.0-alpha.8 — Far Lands (November 2026)

**Fixes from your play:**

- **Chiseled blocks and slabs are lit properly** (no more black rims on the desert roofs).
- **Torches, lanterns, carpets and pots stand on half slabs,** and torches and item frames fix to a vertical slab's inside face.
- **Clicks on chiseled blocks and slabs land on the face you aim at.**
- **Sitting works again** on chairs, benches and stools (and on slab and stair benches).
- **Jack-o'-lanterns face the street,** and chests, furnaces and work blocks in town buildings face the right way.
- **Vines, ferns and leaves are green again,** and End rods show.
- **Flags reach their poles.**
- **Portals built on the far side match the size of the one you came through,** so the window lines up.
- **Faster underground and in hilly country:** caves and rooms shut away behind rock aren't drawn any more (cave culling); down a mine shaft the game draws well under half as much.
- **Less memory, most of all in the Nether:** hanging and climbing vines are drawn as planes round the stem, ragged plants (roots, sprouts) with one thickness, and the terrain's memory grows in smaller steps; in a test of the Nether the terrain took 195 MB where it took 422 MB.
- **Settings are laid out like the launcher's Options:** Graphics, Packs, Sound, Controls and Game, in two columns, each option with a line about it.
- **Music frequency:** a new setting for how often music plays, from rarely to nearly always.
- **Leather, pelts, hides and saddles aren't paper-flat in your hand any more,** and paper, feathers and maps keep a sliver of body.
- **Switching menu tabs from the inventory (Q / LB / RB) goes to the next tab** instead of skipping one.
- **In a chest or furnace on a controller, Y quick-moves straight away again instead of closing the menu.**
- **Leaving a world with a menu open no longer crashes the game.**
- **On a controller, the selected hotbar slot has the focus when a menu opens,** so A, X and Y work on it at once, even if the mouse pointer was left over another slot.
- **Signs show what's written on them.** They were invisible boards; now each stands on its post or hangs on its wall with its four lines on it.
- **Writing on a sign and pressing Esc keeps the writing,** and typing straight away works even when the sign was opened with a controller.
- **The rain detector senses rain** (it acted like a daylight sensor), and **the party chest opens:** every party chest in the world shares the same 27 slots.
- **The Knight's and the Ranger's talents work in a fight:** Heavy Blows, Eagle Eye, Deadeye, Poisoned Tips, Quiver Master, Fleet Foot and Unbreakable.
- **Silver tools and the Watcher's Blade bite the undead,** half as hard again on ghouls, skeletons, wraiths and werewolves.
- **Armour stands work:** set one down and dress it with armour and a weapon; an empty hand takes them back, and sneaking changes its pose.
- **Every drop has a use:** pelts and crocodile scales tan into leather, shark teeth tip arrows, and glow ink makes a glow item frame.
- **Many of the original pictures are redrawn to match the DokuQuest Base pack** (ores, ingots, bricks, armour, tools and more).
- **Town gate levers work:** one pull opens the gate (it opened and shut again in the same moment).
- **On a controller, the first press of Y after using the keyboard shuts the inventory** (it shut and opened again at once).

**New in the world:**

- **The Nether:** Cinder Isles out on the lava sea, Emberglass Groves, the Ashen Wastes and the Sulphur Springs (sulphur and charcoal make gunpowder).
- **Seasons:** a year of four seven-day seasons in the Overworld. Grass and leaves turn gold and rust in autumn and dull in winter, snow falls on the temperate lands in winter and crops grow slowly in the cold, and each new season is announced. They're off unless you turn them on in World settings (or `/gamerule seasons true`); `/time season autumn` jumps to one.
- **Thornscrub:** dry plains thick with low thorn trees whose leaves prick like cactus.
- **Trees grow real branches:** slim branches that wind out of the trunk, fork and end in clumps of leaves, on great spreading oaks, dark oaks, jungle and cherry trees, and now and then on oaks and birches. Branches are blocks of their own too (two logs make four) and join up with logs, leaves and each other. Now and then a giant of the rainforest towers over the canopy. Saplings grow into the same shapes as wild trees.
- **Palm trees** on warm beaches and here and there in the thornscrub, with palm wood; a felled palm drops coconuts.
- **The outer End:** spire fields, crater barrens, violet moors of end moss, stone arches and floating shards.
- **End cities rebuilt:** a hall, a tower in storeys up to a crowned top room, bridges out to hanging towers, and the ship with the dragon's head on its bow and the wings in a frame. Shulkers keep them; their bolts lift you off your feet.
- **The Skylands:** islands that hang to points, glimmersand rims, islets drifting above, banks of cloud (soft ones break a fall, blue ones throw you up, golden ones let you float down), sunwood trees and sky blooms.

**New things to make and do:**

- **The lead:** lead animals, tie them to a fence post.
- **Glass jars:** catch fireflies over grass at night; a jar of them glows.
- **The sieve:** sift gravel, sand, dirt, mud, soul sand, ash or cinder sand for what's hidden in it.
- **Iron mesh** (items fall through, creatures don't) and the **motion sensor** (a redstone signal while something moves near it).
- **The Travel Journal:** a book and a compass. Everything you've carried is in it, with how it's made.
- **The Wither:** three wither skeleton skulls on a T of soul sand wake it. It drops a nether star.
- **The Wither Storm:** set a humming core (crystals round a nether star) in the middle of a Wither's T, between two wither skeleton skulls. It hangs high overhead, tears up the ground beneath it and grows stronger on every block it swallows, pulls you toward it, and fires from all three heads at once. When it falls, what it swallowed comes down.
- **Biome fuselings** (frost, dune and bog, each with its own burst) and **village golems** that take after their land.
- **Elytra glide like Minecraft's.** Throw an ender pearl into an End gateway to go through it.
- **Treading water:** hold jump with your head out of the water and you float at the waterline, bobbing gently, instead of popping out and splashing back.
- **Capes stream in the wind** out under the sky, gusting with the weather; indoors and in caves they hang still.
- **Ingots set down stack Vintage Story-style,** four across, each layer turned across the last.
- `/locate <structure>` finds the nearest one of a kind.
- **Chiseled blocks hold water:** slabs and stairs placed in water keep it in their carved-away part, a bucket fills or empties a chiseled block, and a notch carved under water fills.
- **Coloured lamps** in 16 colours, the **inverted lamp** (it shines while unpowered), and **iron, gold, lapis and steel bricks.**
- **Glass doors and trapdoors,** and the **steel set:** a steel door and trapdoor (opened by redstone, and no blast breaks them), a steel fence, and steel-framed glass.
- **A jar of butterflies** (an empty jar over grass on a bright day), **bluebells** under forest trees, **marigolds** in the meadows, **spinifex** in the redlands, and the **sugar cane bundle.**
- **The hand sieve:** sift gravel, sand, dirt and the rest right where it lies.
- **The quiver:** holds up to 192 arrows; your bow draws from it.
- **The golden quiver:** never runs dry, though its arrows hit a little softer and break where they land.
- **Paintings:** hang one on a wall for a picture of its own (landscapes, seascapes, snowy peaks, still lifes, colour fields); use it to turn to another.
- **Statues:** use a spawn egg on a statue plinth and that creature stands on it carved in stone, facing you.
- **The handcannon and the rocket launcher:** cannonballs burst where they land without breaking blocks (fire one at your feet to fling yourself up), rockets fly straight and blast what they hit.
- **The activator:** a redstone block that uses what it holds on the block in front, like a hand: it sets blocks down, digs with a tool, and with nothing in it swings a door, trapdoor or gate.
- **Particles:** sprinting kicks up the ground you run on, a hard landing throws up a ring of it, critical blows burst into stars, and running beasts raise dust.
- **The Music Studio** (launcher: Mod Studio → Music): write songs on a piano roll with the game's own instruments, have a piece written to any of the game's moods, read a recording (WAV, MP3 or FLAC) for its tempo, key and chords and have an original piece written from them, and bring MIDI in or send it out. Give a song a realm and it plays there now and then.
- **The Sound Studio** (launcher: Mod Studio → Sounds): hear and see every sound the game makes, shape one (pitch, gain, trim, fades, low-pass) or bring in your own recording, normalise it and save it as the game's own: it replaces that sound from the next start.
- **The Texture Studio** (launcher: Mod Studio → Textures): every block and item picture, painted pixel by pixel like the skin editor (pencil, fill, picker, eraser, mirror, grid, undo and redo, the picture's own colours as a palette) with a live block preview and PNG import. "Use in the game" lays your picture over whatever resource pack is on; grass and leaves keep their biome colours.
- **Grass grows back.** Bare dirt next to grass turns green again at Minecraft's pace (about a minute or two for a lone block), under trees too. A new game rule, randomTickSpeed, speeds it and the other slow growth up.
- **Doors, gates and trapdoors swing** open and shut on their hinges instead of snapping, village gates included.
- **Livelier people** (Story Mode-style): standing about, players and villagers now and then look round, stretch, scratch their heads, look at their hands, tap a foot or shift their weight, each in their own way; on steps and slabs their feet plant on the ground under each one, the hips dropping and a knee bending; and creatures throw their weight into an attack and look round when they stand about.
- **Elytra ride the wind.** Out under the open sky a headwind lifts you, a tailwind pushes you on, a crosswind drifts you sideways and gusts carry you up.
- **Torches burn in your hand.** A held torch has a little flame and a thread of smoke, and leaves a trail of sparks when you swing it (soul torches burn blue, redstone torches give off red dust), for you and for everyone else carrying one.
- **The End has a sound of its own:** a low, slowly breathing drone with a hollow wind through it, and now and then something far off blinking from place to place.
- **Hardcore worlds.** Pick Hardcore when you make a world: survival with one life, no commands. When you die you can stay and watch the world as a spectator, or leave.
- **Adventure and Spectator modes** (BTA): Adventure lets you explore, fight, trade and use things, but blocks can't be broken or placed; Spectator flies through anything and only watches. Pick Adventure for a new world, switch any world in World settings, or use /gamemode.
- **Skyblock worlds** (BTA): pick the Skyblock world type for a new world and you start on one small island of grass in an empty sky, with an oak tree and a chest of what you'll need (lava, ice, seeds, a sapling). The Depths, the Skylands and the End are still there to reach.
- **Sorting.** Middle-click a chest, a backpack or your bag (or click the right stick on one) and its stacks are put in order and topped up; the hotbar stays as you left it.
- **New commands:** /playsound plays any of the game's sounds (with volume, pitch and a place) and /particle makes any particle (smoke, flame, heart, firework...).
- **An emote row in the chat** (BTA): click a face to drop it into your line.
- **Nightmares** (a game rule, on by default): sleep in the dark with monsters about and one may come to your bed and wake you. Light your bedroom.
- **Hotbar rows** (BTA): Tab turns the rows of your bag round into the hotbar, like pages.
- **Lead animals on a fishing line** (BTA): cast at an animal and the hook catches; it follows you on the line (too far and it snaps), and reeling in gives it a tug toward you.
- **Sponges soak up water** when you put them down (everything within six blocks), turning into wet sponges; dry them in a furnace. A wet sponge used on dyed wool, glass, terracotta or painted planks washes the colour out (BTA).
- **Furnace minecarts burn any fuel** a furnace would, not just coal (BTA).
- **Armoured zombies and skeletons** (BTA): now and then one comes dressed in leather, copper, gold, iron or (rarely) diamond armour, and zombies carry more kinds of weapons. Armour soaks their hurt, a helmet keeps off the sun, and a piece may drop when they fall.
- **The reinforced piston** (BTA): steel-bound, it pushes up to 16 blocks and flings whatever stands in front of it.
- **Glow ink on a sign** makes its writing glow in the dark, an ink sac puts it out, and any dye colours it.
- **The Siphon:** a copper pipe that draws items one at a time out of whatever is behind it (or lying there) and pushes them into whatever is in front.
- **The Wicker Basket:** lift a chicken, rabbit, cat, duck, frog or any small animal into it, carry it off and set it down somewhere else.
- **Move the HUD** (BTA): Options → Interface → Move HUD pieces, then drag the hotbar, the hearts, the boss bar, the chat and the messages wherever you like (a controller works too).
- **Whole trees fall** is a World setting now, for those who'd rather take a tree down log by log.
- **Lingering potions:** a splash potion and ectoplasm make one that leaves a cloud of itself on the ground for ten seconds. **Potions stack to 16.**
- **More stone and wood** (BTA): permafrost breaks into cobbles that polish and lay as bricks, a mossy oak log, and mossy emberstone.
- **Highland Spruce and the tundra** (BTA): close-grown spruce woods on podzol up in the cool hills, and between the snowy woods and the plains an open, snowy tundra over permafrost.
- **Three more fuselings:** ember fuselings creep over the Depths' floors and leave fires burning, moss fuselings lurk in the woods and leave moss and poison spores, and sky fuselings in the Skylands burst in a gust that throws you skyward.
- **Zombie pigmen keep to the Depths' natural ground,** not to bricks and what you build.
- **December in the villages:** winter caps and scarves, strings of lights along the eaves, a decorated tree with a star on the square, wrapped chests, and now and then a carol (Silent Night, Jingle Bells, We Wish You a Merry Christmas, Deck the Halls). On New Year's Eve and Day the villages let off fireworks after dark.
- **More of BTA's world:** scorched dirt in the Redlands (rich scorched dirt sifts out iron, gold and copper), perma-ice deep in the glaciers that never melts, ember coal ore in the Depths (burns hotter and longer), moss-grown dark oak and mangrove logs and algae on the still water of the Mossmire, and dried sugar cane bundles (smelt a bundle).
- **More of BTA's Nether and Redlands:** cobbled and polished emberstone, soul schist low in the rock and bone shale higher up, petrified ashen trunks standing in the Ashen Wastes, and gum trees with pale bark and their own wood in the Redlands.
- **The Snow Sentry** (BTA's snowman): set a carved pumpkin or jack-o'-lantern on two stacked snow blocks and it comes alive, throwing snowballs at monsters near it (they push them back and burn the creatures of fire). It leaves a dusting of snow where it walks in the cold and melts in hot lands.
- **Stars come in colours** (BTA): mostly white, some blue, yellow, orange and red, and they twinkle a little.
- **Achievements** (BTA): 27 of them, from Getting Wood to The End., on their own tab beside the Stable. A toast when you earn one, and the date you did.
- **Colour filters** for colour blindness (Options → Graphics → Colour filter): red-blind, green-blind and blue-blind, shifting the colours you may not tell apart into ones you can.

**Gear and magic:**

- **The Tool Forge:** put in flint, an ingot, a gem, obsidian or emberglass and forge a pickaxe, axe, shovel, sword or hoe head. A head and a haft make the tool: a stick for the plain one, bone for a sturdier one, a cinder rod for a fast, hot one (its blades burn), steel for one that lasts. Ruby, sapphire, obsidian, flint and emberglass tools are new.
- **A spell in each hand.** The spellbook readies the right hand as before; L, X or right-click readies the left. While your left hand is free (empty, or holding a second staff), it casts with every cast.
- **The same spell in both hands is a dual cast:** stronger, and dearer.
- **Two different spells go off together,** a little cheaper, and some pairs fuse where you look: fire and frost make scalding steam, fire and storm plasma, frost and storm hail, life renews you, death drinks, nature snares, and two of one school resonate.
- **A sword or axe in hand:** use casts your left-hand spell, and the blade carries it for a few seconds (burning, freezing, shocking, draining or snaring blows).
- **Spells rank up from I to X with use,** each rank a little stronger and cheaper. `/spell` learns, readies and casts them.
- Removed Herobrine.

## 1.0.0-alpha.7 — Chisel (October 2026)

- **The chisel:** carve any full block into eighths with the iron chisel. Aim at a corner to carve it away; chisel the same spot again to put it back.
- **Slabs and stairs are chiseled blocks now.** The ones already in your worlds change over by themselves; make new ones with the chisel.
- **Torches, lanterns and plants** only fix to a whole face, and drop off if you carve away the face they sit on.
- **Dirt paths turn back to dirt** when you build on them, like Minecraft.
- **Blocks no longer vanish** when placed on a path or left floating, and chains show again, so hanging lanterns connect.
- **Spawn eggs for every creature,** with their own tab in the creative inventory. Use one on a monster cage to choose what it spawns.
- **Lava Boat:** crimson or warped planks with magma cream in the middle (the rowboat shape). It floats on lava as well as water, and the lava never reaches you or your pet aboard.
- **Endermen, ghasts and creepers wear the resource pack's skins.** An angry enderman lifts its head off its jagged jaw, a ghast opens its mouth before it fires, endermen stand with their legs apart, and creepers have Minecraft's proportions.
- **Holding right-click** keeps placing blocks with the arm swinging smoothly.
- **Sounds:** carving a pumpkin plays its own carving sound, and jack o'lanterns sound like wood.
- **Music:** new instruments, including dulcimer, mandolin and fiddle.
- **Art:** hundreds of updated pictures in the default art, and an end crystal picture.
- Removed Herobrine.

## 1.0.0-alpha.6.1 — Hotfix (October 2026)

- **Windows download is back** (a folder name in the new art stopped the Windows build).
- **The journal no longer crashes** once the story is finished.
- **Creeper and skeleton skins fixed:** a few mob, sign and boat pictures in the default art had picked up the wrong picture of the same name. They're the right ones now.
- Removed Herobrine.

## 1.0.0-alpha.6 — New look (October 2026)

- **New default art:** the game now ships its own texture set, DokuQuest Base, as the default look: blocks, items, mobs, chests and leather armour. Anything it doesn't cover keeps the generated pictures. Choose "none" in Options → Resource pack for the old generated look, or any other pack as before.
- The base set lives in data/resourcepacks/DokuQuest Base in the game folder, laid out like a Minecraft resource pack, so it can be edited directly.
- Removed Herobrine.

## 1.0.0-alpha.5.9 — Sounds (October 2026)

- **New sounds:** many more creature and block sounds from the new sound set.
- **Under water sounds like water:** everything is muffled under water now, instead of ringing like a cave.
- **Creature sounds sorted:** the wolf's howl no longer plays its death sound, the duck's death isn't a splash, and horses don't use ghost or undead horse sounds.
- Removed Herobrine.

## 1.0.0-alpha.5.8 — Hotfix (October 2026)

- **Glowing eyes line up:** on creatures wearing a skin, the glowing eyes sat one pixel below the skin's eyes. They sit on them now, flush with the face instead of standing out from it.
- Removed Herobrine.

## 1.0.0-alpha.5.7 — Modding (October 2026)

- **Game art laid out like a resource pack:** the built-in pictures are now one PNG each in data/textures/block, item, entity and misc, and creature skins sit in data/textures/entity/<creature>/. Edit any of them and the game uses your version (any square size).
- **Your own sounds:** data/sounds/ lists every sound in the game, sorted into blocks, mobs and effects. Drop a recording named after one (.wav, .mp3 or .flac) next to its placeholder, or into the sounds folder in your DokuQuest folder, and the game plays it instead.
- Removed Herobrine.

## 1.0.0-alpha.5.6 — Hotfix (October 2026)

- **Pumpkin creatures glow:** their head is lit like a real jack-o'-lantern.
- **Endermen's eyes glow purple** again.
- **Skins on creatures:** a dark skin's glowing eye pixels are picked out properly.
- Removed Herobrine.

## 1.0.0-alpha.5.5 — Hotfix (October 2026)

- **Pumpkin creatures behave:** they stay hidden as a pumpkin until you come close, never stack pumpkins up into towers when you run, and wear a plain jack-o'-lantern head.
- **Calmer October nights:** fewer pumpkin creatures and scarecrows, and only one ghost a night. The ghost is a proper sheet ghost now.
- **New fuseling look:** taller and narrower, with vines hanging down its sides.
- **Deleting a world no longer crashes the game.**
- Removed Herobrine.

## 1.0.0-alpha.5.4 — Hotfix (October 2026)

- **Glowing eyes fixed:** glowing white eyes show as two eyes again, like the original, instead of one solid bar across the face.
- Removed Herobrine.

## 1.0.0-alpha.5.3 — Hotfix (October 2026)

- **Ghasts face you:** a ghast keeps its face on you while it fights, backing away when you get close instead of turning its back.
- **Hit-back fireballs hurt ghasts:** a ghast shrugged off its own fireball because it doesn't burn. Now a fireball you punch back into it is the end of it.
- **Punch back zephyr gusts:** the Skylands' cloud creature's gusts can be hit back like fireballs, and one sent back into it finishes it.
- Removed Herobrine.

## 1.0.0-alpha.5.2 — Hotfix (October 2026)

- **The world no longer vanishes on high settings:** on Ultra, the Nether and deep caves could fill the graphics card's terrain memory past 4 GB in one piece, which the graphics driver can't hold, and everything near you disappeared into the fog. Terrain memory is now kept in smaller pieces, so it keeps working however much land is loaded.
- **See down into the Nether:** dig through the bottom of the Overworld and you see the Nether below through the hole again (and the Overworld above from the Nether's roof). The land next door shows near the edge between them, while the Skylands still stay out of sight from the ground.
- Removed Herobrine.

## 1.0.0-alpha.5.1 — Hotfix (October 2026)

- **No more stuck in walls:** players and creatures can no longer end up inside blocks. Anything that gets shoved into a wall, a ladder's block or a block placed where it stands is put back out the shortest free way before it moves on.
- Removed Herobrine.

## 1.0.0-alpha.5 — Hotfix (October 2026)

- **Deep underground fix:** going deep below ground (near the bottom of the Overworld) could make the world around you vanish, leaving only the sky and the distant land. The land next door below is no longer built in full while you can't see it, which keeps graphics memory well within what every card handles.
- **Quest list:** village quests on the side of the screen now sit under the story line instead of on top of it.
- Removed Herobrine.

## 1.0.0-alpha.4.10 — Windows (October 2026)

- **Windows download:** this release comes with a Windows version again: download the windows-x64 zip, unzip it anywhere and run dokuquest-launcher.exe. Your worlds live in %APPDATA%\DokuQuest, and the launcher keeps itself up to date.
- Removed Herobrine.

## 1.0.0-alpha.4.9 — Hotfix (October 2026)

- **Windows is back:** a Windows download (a zip: unzip it anywhere and run dokuquest-launcher.exe) comes with each release again, next to the Linux AppImage.
- **The rift in town hums again:** its deep, low drone is back, like the original, for anyone within about 28 blocks of it, and on its other side too.
- Removed Herobrine.

## 1.0.0-alpha.4.8 — Hotfix (October 2026)

- **Hit ghast fireballs back:** punch or swing at a ghast's fireball just before it reaches you and it flies back the way you're looking. Send it into the ghast and that's the end of the ghast.
- Removed Herobrine.

## 1.0.0-alpha.4.7 — Hotfix (October 2026)

- **Iron golems are back in the villages:** every village of seven houses or more keeps one again, guarding it alongside the village guards.
- Removed Herobrine.

## 1.0.0-alpha.4.6 — Hotfix (October 2026)

- **Ghosts look like ghosts:** a rounded sheet with a dome of a head, dark eye holes and a little round mouth, flowing out to a ragged hem that fades away.
- **Fewer scarecrows and ghosts:** only now and then, and one at a time near you.
- **The pumpkin creature hides better:** while it waits it looks just like any jack-o'-lantern, with no see-through face giving it away.
- **Every swing shows:** hitting a creature with a wall or the ground behind it now swings your arm too (the swing used to be skipped).
- Removed Herobrine.

## 1.0.0-alpha.4.5 — Hotfix (October 2026)

- **More villagers in capes for Halloween:** nearly half of the villagers without a costume now wear a black, orange or purple cape (only a few did before), and every vampire has the dark one.
- Removed Herobrine.

## 1.0.0-alpha.4.4 — More Halloween (October 2026)

- **More Halloween (October only):** three new night creatures. A pumpkin creature lies in pumpkin patches looking like any other pumpkin (watch for a faint glow in its face at night) until you come close or touch it, then rises on a body of vines. A scarecrow stands still in fields and by village farms, and only moves when nobody is looking at it. A pale ghost drifts about at night, chills you if it touches you, fades when you stare it down and keeps away from torchlight.
- **Villagers in costume:** some villagers dress up for Halloween as witches, ghosts, skeletons and vampires.
- **Bats** stay down around the treetops instead of flying up into the clouds.
- **Villager conversations look like the original again:** the talk window, shop and quest board are rebuilt after the original's, and villagers babble their lines letter by letter in their own voices, in conversation and when they mutter as you pass.
- Removed Herobrine.

## 1.0.0-alpha.4.3 — Halloween (October 2026)

- **Halloween is here (all of October):** zombies, skeletons and some villagers wear carved pumpkins and glowing jack-o'-lanterns, and a few villagers wear black, orange or purple capes. Full moons rise as a big orange harvest moon, nights are foggier with pale wisps drifting about, bats come out at night, new land grows far more pumpkin patches, and new villages put out jack-o'-lanterns. Out in a village at night, a villager may toss you a treat. The game goes by your computer's date; Options > Holiday events turns it off.
- Removed Herobrine.

## 1.0.0-alpha.4.2 — Hotfix (October 2026)

- **Every player has their own look:** Players & Skins has a tab for each of the four players, each with their own name, skin (or creator character), arms and painted cape, like the original. The skin editor, character creator and cape editor work on whichever player is chosen. Opened from a split-screen player's own pause menu, it opens on them, and players who join come in as themselves.
- **Throw eggs and slimeballs** like snowballs. An egg breaks where it lands and now and then hatches a chick; a slimeball splats.
- **Slime puddles:** slimes splat where they land, like the original, and leave a sticky puddle that slows you down (a bit like a cobweb) until it dries. Thrown slimeballs leave one too.
- **Ice you can see through,** like Minecraft's, with any resource pack.
- Removed Herobrine.

## 1.0.0-alpha.4.1 — Hotfix (October 2026)

- **Split-screen joins properly:** the first controller that moves you is yours; any other controller now joins with Start (it used to act as a second controller for player 1 and pause the game). Like the original, a spare controller no longer moves player 1.
- **Treading water:** chest-deep or standing on the bottom with your head under, you tread water with your arms out, like the original (sprint forward under water for the front crawl).
- Removed Herobrine.

## 1.0.0-alpha.4 — Split-screen (October 2026)

- **Split-screen:** up to four players on one screen. Press Start on a free controller to join.
- **Controllers in every menu:** the title screen, pause menu, options and every other menu now move with the D-pad or left stick, A to choose and B to go back. Keyboard and mouse work alongside.
- **Villager talk windows:** the highlighted answer lines up with the one you'll pick, and long lists of answers, shop rows and quests scroll with the mouse wheel, the keyboard and the controller.
- **Sounds like the original:** your own footsteps, digging, placing and breaking play at full loudness again. Mining had become a faint, muffled thud.
- **Weather and portals:** rain, snow and mist no longer fade away while a portal is in view.
- **Ledges:** in first person your hands grip the edge while you hang, move hand over hand as you shimmy and pull you up over the top.
- **Shelves** show what's on them.
- **Weapon racks** hang on walls when placed against one, and hold up to three weapons, tools, bows or shields.
- **Sneak-placing** blocks against crafting tables, furnaces, chests and other stations works again, flying in creative too.
- **First person:** the bow draws back as you pull it, and the fishing rod's float hangs from the tip until you cast it.
- **Monster cages** show the creature spinning inside.
- **Enchanted things** shimmer, and their tooltips list their enchantments.
- **Puddles dry up** after the rain, and storm snow melts.
- **Smoother blocks:** big shapes step a whole voxel at a time, fine detail is shaded smoothly, and cracks and corners get a little shadow.
- **Fixed** a crash right after a portal links.
- Removed Herobrine.

## 1.0.0-alpha.3.2 — Hotfix (October 2026)

- **No more duplicating:** equipping or taking off a backpack, cape, belt item or charm no longer copies it (and everything in the backpack). The inventory and the rest of the game now share one set of gear slots.
- **Caves aren't filled with weather:** mist, rain fog and sandstorms fade out below ground and indoors, so caves and dungeons show again instead of disappearing into storm fog.
- **Elytra work from the cape slot:** they used to be saved in the inventory's own copy, which the glide never looked at. Jump while falling to glide.
- Removed Herobrine.

## 1.0.0-alpha.3.1 — Hotfix (October 2026)

- **No more grid:** the thin dark frame drawn round every block face in 1.0.0-alpha.3 is gone.
- **Cracks stay:** a block you stop mining keeps its cracks and slowly mends, like the original.
- **Rain is heard again:** the rain's hiss, louder out in the open, quieter under a roof.
- **Creatures stay on their islands:** calm creatures stop at a deep drop instead of walking off (Skylands creatures no longer fall to the Overworld).
- **Sneaking:** the edge guard no longer pushes you into walls while you sneak along them.
- **Running dust:** a puff or two of the ground you run on, like the original, instead of a spray of particles.
- **Elytra:** they work from the cape slot as well as the chest slot (jump while falling to glide).
- Removed Herobrine.

## 1.0.0-alpha.3 — Your second playtest (October 2026)

- **Menus:** Q and E switch the menu tabs (with LB/RB still on a pad), clicking a tab works every time, and the inventory key closes the inventory again. The character creator and skin editor fit any screen, ultrawide included. NPC talk, shop and quest-board windows wrap and fit properly.
- **Capes:** a cape editor — paint your cape like your skin, wear it, export it as a PNG or import one (file chooser or drag and drop).
- **3D icons:** furniture, doors, levers, hoppers and other shaped blocks show as their real 3D shape in the inventory, the creative catalogue and the recipe book.
- **Placed items show:** items put down with sneak + use lie on the ground again; item frames show their item.
- **In your hands:** swords and tools are held bigger and angled like Minecraft in first person; held compasses and clocks show their faces in third person.
- **Blocks:** ice is see-through; chest lids open while you look inside; lecterns hold a book you can open to the page you choose; rain puddles are flat, glossy water.
- **Smoother relief:** bump mapping is smoothed — latches, tools and book spines stand out instead of sinking in, mortar stays a groove, and block edges get a slight bevel.
- **Characters and villagers:** guards' faces show under their helmets; villagers' mouths move when they talk; everyone blinks; a picked-up animal or block is shown in your arms; the elytra are wings that spread when you glide.
- **Effects:** sparks, smoke, flames and other particles from the world (fires, potions, breaking blocks) now appear; messages and screen shake from the world reach you.
- **Items:** fireworks no longer turn into a bucket and launch from the ground in a burst of colour; the gust puff and the Ember Effigy work; the Pocket Key takes you to your pocket dimension and back; stuck arrows are easier to pick up.
- **Fixes:** worlds no longer vanish after going through a portal into another realm; in creative a sword doesn't break blocks, and finishing off a creature doesn't dig into the ground behind it.
- **Testing:** every item is now used automatically on every build to catch items that do the wrong thing.
- Removed Herobrine.

## 1.0.0-alpha.2 — Fixes from the first playtest (October 2026)

- **Crashes fixed:** felling a tree no longer crashes the game, and a world with a falling tree in it opens again. Leaving a world and opening one again in the same session no longer crashes. If the game ever does crash, the reason and a trace go into `latest.log`.
- **Chests, furnaces, crafting tables and workstations open again**, with the mouse and the controller: double chests, barrels, ender chests, backpacks, anvil, grindstone, smithing table, loom, fletching table, stonecutter, millstone, sawmill, crusher, signs, the quest board, dispensers, hoppers, and cart and boat chests.
- **Controller:** the menu tabs (Inventory, Map, Journal, Skills, Spells, Stable) are back across the top with LB/RB. The d-pad reaches every button, the right stick scrolls and Y zooms the map. A stuck key or a drifting stick can no longer slide your character.
- **Characters:** a Players & skins screen with a turning 3D preview; skin import (64×64 and old 64×32, from a file chooser or by dropping the picture on the window); capes, backpacks and belt lanterns show. The inventory doll is your 3D character with show/hide buttons for each worn piece, the pause menu shows 3D heads, the right arm wears its sleeve, swords sit properly in the hand, and there are ledge, ladder and vine climbing poses and maps held up in third person.
- **Creatures:** monsters come out at night; the Nether, the Skylands and The End have their creatures; creatures topple over when they die and fade a moment later; limbs can be severed; blazes have their spinning rods; birds have feet; the compass and the recovery compass point the right way; every creature has its sounds (horses use the original game's, as the pack has none).
- **Worlds:** a Minecraft Beta-style underground with worm caves and rooms, ravines, lava lakes, ores at their usual depths and jagged bedrock; classic dungeons with monster cages and loot; a Minecraft-style Nether with big caverns, a low lava sea, glowstone and new fortresses; monster cages let out their creatures; weeping and twisting vines hang as one strand. The coordinates show Y within the world you're in, like Minecraft.
- **Villages:** gates are visible again, open at dawn and close at dusk; guards open them for you at night and the lever beside them works. Washing lines and bamboo no longer break villages.
- **Looks:** each world shows only itself (no Skylands above the Overworld's clouds, no Nether above The End); portals are see-through windows without the old swirl texture; new puffy clouds; bump-mapped ores, cobblestone and bricks that work with any resource pack; 3D grass on grass blocks (Options → 3D grass); shader packs can be switched in Options.
- **New:** villagers live in the villages, with their days, homes and jobs, shops with daily stock, errands, the quest board, guards and golems, raids and the Knight, Ranger and Mage paths. The End's dragon fight with its crystals, the exit portal, gateways and the egg; wild dragons in their lairs that you can hatch, raise and ride; the Deep Dark's sensors, shriekers and the Warden. Minecarts, boats, the ship, windmills and machines have their 3D models.
- **Dedicated server:** the launcher's server tab shows the world map with everyone on it and a live isometric view.
- **Please test:** portal windows, 3D grass and the server's map and isometric view haven't had a real play session yet. Split-screen co-op is still to come.
- Removed Herobrine.

## 1.0.0-alpha.1 — The C++ Rewrite (October 2026)

DokuQuest is being rebuilt as a native C++ game, as close as possible to Alpha 0.9.0.90, with no browser underneath.
- **One seamless column of worlds:** the Skylands, the Overworld, the Ember Depths and The End are stacked 256 blocks each, with no loading screens between them.
- **Everything is voxels:** every block is 32×32×32 little voxels coloured from its texture, so the GeruDoku resource pack (or the built-in Doku tiles) gets real depth.
- **Two renderers:** *Modern* (OpenGL 4.6) and *Legacy* (OpenGL 3.3) for older PCs. Vulkan comes later.
- **A new launcher:** the news, your worlds, your player name and Play, now as a native program. Options choose the renderer and a graphics preset; the launcher looks at your graphics card and suggests one.
- **The dedicated server lives in the launcher:** pick or make a server world, set the port, players, MOTD, mode, difficulty and password, and watch its console with TPS and players.
- **The Mod Studio is built in:** write DQ mods with templates and a syntax check, switch installed mods on and off, make data packs (blocks, items, recipes) without code, and browse the engine's source.
- **Mods, like Fabric, Forge and OptiFine together:** JS mods with the original `DQ` API, data packs, native plugins, shader packs and resource packs.
- **Updates from GitHub Releases:** the launcher checks for new versions, downloads and checks them, and keeps the previous version in case the new one won't start.
- Linux AppImage (and a tar.gz). A Windows build comes later. Your worlds are kept in the game folder.
- **In this build:** the original's terrain for all four worlds, villages, towns, dungeons, strongholds, fortresses, ancient cities and End cities; 125 creatures with their AI; survival and creative with every block rule; crafting, furnaces, chests and the creative catalogue; magic, enchanting, brewing, skills; maps, farming, fishing, camping, elytra and the stable; redstone, minecarts, boats and ships; multiplayer over LAN or the internet; the original menus with full controller support.
- **Still to come:** villagers living in the villages and towns, the End's dragon fight and the story, split-screen co-op, and the 3D models for carts, boats and machines.
- Removed Herobrine.
