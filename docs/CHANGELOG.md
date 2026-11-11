# Changelog

Release notes for the C++ versions of DokuQuest, newest first. The release workflow
(`.github/workflows/release.yml`) publishes the section whose heading starts with the tag's version as the
GitHub release notes. Keep it in step with `assets/news/news.dqn` (the launcher's news). Patch notes never
mention Herobrine and every entry ends with "Removed Herobrine."

## Unreleased

**Weather (after the Particle Rain mod by pigcart, MIT):**
- **Rain falls round you, not in a ring at your feet:** drops come from a sphere around you, lean and stretch with the wind and with how you move, and are see-through like real rain. Thunderstorms add heavy rain. Rain and snow no longer blow through walls or into houses.
- **Snow** drifts down slowly, turning as it falls, with heavy snow in a blizzard.
- **Where the rain lands:** splashes on the ground, rings on water, drops running down stone and glass walls, steam where it hits lava or fire.
- **Sandstorms** blow dust the colour of the sand under it (red sand gives red dust), dust skittering along the ground, now and then a dead bush tumbling past, and a dusty haze.
- **Ground fog:** soft banks of fog lie in swamps and jungles, rise for a while after the rain, and come up the valleys on misty mornings. Fog also settles low in the valleys, thinning as you climb.
- **Wind** gusts differently from place to place and blows harder the higher you are; the wind's sound now follows the gusts.
- **Sounds:** snow and sandstorms have their own sound, softer and lower when you're high above them. Rain on the roof over you is a muffled patter now instead of fading away.
- **Multiplayer:** everyone in a world now has the host's weather. Players joining someone else's world used to run weather of their own.
- New **Options → Video → Weather**: Weather Particles (Off, Low, Normal, High), Weather Distance, Splashes & Ripples, Ground Fog and Height Fog. The graphics presets set them too.

**Look and feel:**
- **Brightness** works: it lights up caves and the night like Minecraft's, leaving full daylight alone.
- **Colour Style** (Options → Video): Vibrant, Natural (the new default), Classic (muted and flatter, like Minecraft Alpha and Beta) or your own Saturation and Contrast. Handy on bright OLED screens.
- New worlds start with Flat blocks and Classic lighting.

**Graphics cards and Vulkan:**
- **Graphics Card** option in the launcher: the game uses your dedicated graphics card by default instead of the one built into the processor.
- Vulkan: the sky, the land and the volumetric clouds draw properly again, the shader levels' shadows, reflections and corner shading work, and it runs over three times as fast as before. Vulkan also follows your VSync setting now.

**Creatures:**
- Wolves bark when they're wandering about and only growl when they're angry or hunting. Dolphins no longer let out their dying cry while swimming.

**Animation:**
- **Smooth changes between moves:** players no longer snap between standing, sneaking, sprinting, jumping, swimming, crawling, sitting, sleeping, climbing ladders, eating and flying; each move eases into the next. Animals and monsters ease between their moves too (sitting down, lying down, taking off, swimming).
- **New moves:** drinking tips the bottle up and the head back; a hit makes you flinch; a dying player's knees give before they fall; sleepers lie slack with the head turned; each jump leads with either leg; walking backwards looks like walking backwards; the head follows other players' looks smoothly. Mouths gasp when hit, chew, and puff on a long sprint.
- **First person:** changing items lowers the old one and brings the new one up; axes swing heavier, daggers quicker; mining chops straight down into the block; drinking tips the bottle up; the item dips when you land and lowers while you sprint, climb a ladder or swim.
- **Placing a block** gives it a little pop (Particles Off turns it off).

**Tools and weapons:** pickaxes, axes, shovels, hoes and swords have new 3D shapes: wrapped grips on sturdier handles, pickaxe points that hook down, a curved axe edge with a beard, a dished shovel blade, a broader hoe blade, wider sword blades with an upturned guard. They still show their material (wood, stone, iron, gold, diamond...) from the item's picture.
- **Swimming** is a breaststroke now: arms out ahead, a wide sweep and a pull under the chest, a frog kick, the head coming up to breathe. No more arms windmilling round.

**Placing things (after TerraFirmaCraft):**
- **Ingot piles:** sneak + use with an ingot on the ground sets it down; keep adding and they stack like TFC's, eight to a layer in two rows of four, each layer laid across the one below, 64 to a block. A full pile takes another on top. One metal to a pile; use takes one back.
- **Log piles:** sneak + use with a log on the ground starts a log pile, a crib of 16 logs. Placing logs normally (not sneaking) is unchanged.
- Fixed the see-through gap round the bottom of cacti.

**Immersive Progression (new worlds; after TerraFirmaCraft):** a new world option, on by default (Create World → Game Rules). Worlds made before it stay classic.
- **No punching trees:** logs need an axe. Leaves don't hold you up any more: you push through them, slowed, and they break your fall. Broken leaves drop sticks now and then.
- **On the ground:** loose rocks lie about everywhere, flint by the water on gravel and sand, twigs in the woods and on beaches.
- **Knapping:** hold two or more pebbles or flint and use them in the air. Chip away at the 5×5 grid until it's the shape you want: an axe, shovel, hoe, knife, hammer or javelin head, or a crude pickaxe (flint makes a proper flint pickaxe). Works with a controller.
- **Stone tools:** a head and a stick make the tool. Flint tools are better than stone ones.
- The wooden tools and the cobblestone tools can't be crafted in an immersive world.

**Fire and clay (after TerraFirmaCraft):**
- **Firestarter:** two sticks. Hold use on three sticks lying on the ground to make a firepit (straw helps it catch), or on an out fire, a pit kiln or a log pile. It takes a moment, and a few tries.
- **Campfires are firepits now:** feed them logs, planks, sticks, charcoal or coal (up to four). Each fuel burns so long at so hot; a fire with nothing to burn dies down and goes out, and rain puts out one in the open. Food cooks once the fire's hot. Set metal, ore or pottery on it to heat it; an empty hand takes it back.
- **Hot things:** metal and pottery carry their heat, glow, and cool in your bag; the tooltip says how hot ("Faint Red", "Orange"...).
- **Straw:** cut tall grass with a knife or a scythe; a scythe cuts all the grass and crops round it. Four straw make thatch.
- **Clay:** hold five clay balls and use them in the air to shape vessels, jugs, pots, bowls, an ingot mold and molds for tool heads. Fire clay (clay and gravel) shapes a crucible.
- **Pit kiln:** sneak + use to set unfired pottery down in a pit, then add eight straw and eight logs, wall it in and light it. Three in-game hours later it's fired.
- **Charcoal pit:** pack log piles together, cover every side with earth or stone, and light one: six in-game hours later they're heaps of charcoal. A pile left open burns away.

**Metal (after TerraFirmaCraft):**
- **New ores:** zinc and bismuth. In an immersive world ore breaks into ore pieces, and now and then a loose rock on the surface is a small piece of the ore below it. The furnace no longer smelts ore there.
- **Any metal, any mix:** everything metal carries what it's made of. Copper and tin in the right measure is bronze; so too brass, bismuth bronze, black bronze, rose gold and sterling silver. Any other mix works as well: its strength, its name ("Copper-Zinc-Tin Alloy") and its colour come from what's in it, all the way to the finished tool.
- **Casting:** fill a fired vessel with ore (hold the ore in your other hand and use the vessel), melt it on a charcoal fire, then use it with a fired mold in your other hand to pour. When the mold's cooled, use it to break it open: a tool head, or an ingot.
- **The forge:** charcoal in a pit walled in with stone, lit with the firestarter. Bellows beside it make it hotter. A crucible melts big batches and the hardest metals.
- **The anvil:** use an anvil with a hammer, hot metal in your other hand. Pick what to make, then work the metal to the mark with hits, draws, punches, bends, upsets and shrinks, finishing the way its plan asks. Close is good enough; spot on makes a sturdier tool. A hammer on raw stone makes a stone anvil; anvils of copper, bronze, iron, steel, mithril and adamantite work harder metals. Weld ingots into double ingots and sheets (with flux) and make armour of any metal.
- **Iron:** a bloomery under a stone-brick chimney turns iron ore and charcoal into blooms; refine them on the anvil. Steel: pig iron from the blast furnace, worked on the anvil.
- **Hot metal:** dip it in water to cool it (a quenched head makes a slightly tougher tool); hold it bare-handed and it burns you, unless you carry tongs. A prospector's pick tells you what ore is near. Hot things cool wherever they are: in your bag, on the ground, in a chest or on a pile. Drop them in water, a water cauldron or a barrel of water and they go cold at once.
- **Alloy colours:** a tool or piece of armour made of a mix of metals shows its blend, flecked with each metal by how much of it is in the mix (a 70/30 copper-zinc tool is copper with streaks of zinc). Named alloys like bronze keep their own colour. The name above the hotbar shows what it's made of ("Copper-Zinc Alloy Pickaxe").
- In an immersive world metal tools and armour are made this way, not at the crafting table.

**Crafting and materials (after TerraFirmaCraft):**
- **Saws and lumber:** a log and a saw at the crafting table make eight lumber (the saw stays, a little worn). Lumber makes planks, support beams, barrels and sluices. In an immersive world a log by hand gives only two planks.
- **The quern:** hold use on it with grain (flour), ore (powder, which holds more metal), limestone (flux), bone, cobblestone, gravel or flowers (dye).
- **Barrels and leather:** a sealable barrel holds water by the bucket and four stacks; sneak + use seals it. Lime in water makes limewater, logs steep into tannin. Raw hide (animals drop it in an immersive world) soaks in limewater, is scraped with a knife (the hide in your other hand), tans in tannin, and becomes leather.
- **Panning:** a clay pan used on gravel or sand by the water washes out ore samples now and then, and rarely a gem. A sluice in the water does it on its own.

**More like TerraFirmaCraft:**
- **Every metal tool, weapon and armour piece is procedural:** made at the crafting table (in every world), from a Tool Forge head, bought from a villager, found in a chest or dropped by a mob, its stats, name and colour come from what it's made of. Named alloys vary with their mix too: bronze near 90/10 outlasts an off-ratio bronze, and its colour shifts a little. Armour protection follows the blend, and mixed armour now counts its fire resistance.
- **No shortcuts:** in an immersive world steel blend only makes pig iron, in the blast furnace; iron ore can't be melted down (the bloomery makes iron); mithril, adamantite, thallasium and cinderite ore melt in the crucible, not the furnace; the anvil is smithed, not crafted from iron blocks; pelts are raw hide to tan, not leather; the Tool Forge cuts no metal heads.
- **Two-step refining:** a raw bloom is refined, then worked into wrought iron; pig iron becomes high-carbon steel, then steel.
- **Padded armour:** the anvil makes unfinished armour; pad it with leather, wool cloth or burlap at the crafting table to finish it.
- **A wooden bucket** from lumber, so a barrel can be filled before the iron age.
- **Knapping** is a flat slab of the stone, clay or flint, and you can hold the mouse button and drag across it to chip away every piece you pass.
- **Multiplayer:** stations a guest builds, lights or seals now work on the server (forges, kilns, bloomeries, charcoal pits, barrels), bellows pumped by a guest reach it, and lighting a firepit from sticks no longer copies the sticks.

**The world:**
- **Rock layers:** new worlds are built in layers of rock: chalk, shale, claystone, conglomerate, gneiss, schist, quartzite, gabbro, rhyolite and dacite join the stones we had, and each ore turns up in the rocks it belongs in.
- **Cave-ins:** mining out rock with nothing holding the roof up can bring it down on you. Support beams (from lumber) hold it.
- **Crops and plants:** barley, oats, rye, jute, garlic, soybeans, green beans and squash; ten berry bushes that fill again after you pick them; and fruit trees (plum, orange and more) whose leaves bear fruit.

**Life:**
- **Husbandry:** animals are male or female. Feed one each day and it grows familiar with you. Familiar animals breed more readily and familiar sheep give more wool.
- **Straw and thatch:** cut grass with a scythe or knife for straw; straw makes thatch.
- **Spinning:** hold use with a spindle and wool to spin yarn, then weave yarn into wool cloth, or jute into burlap.

**Machines and villages:**
- **Gearboxes:** a gearbox carries a windmill's or water wheel's turning round a corner to an axle at 90°. Power it with redstone and it disengages, stopping everything past it.
- **Powered querns and bellows:** run an axle into a quern and it grinds by itself; run one into bellows and they keep a forge at full heat. You can also pump the bellows by hand (use them with an empty hand).
- **Villages:** smiths' yards have a proper forge with bellows. Bakeries have a quern, and butchers and tanneries keep sealable barrels. Toolsmiths, weaponsmiths and armourers sell tongs, flux, metals and bellows. Masons sell vessels, molds, crucibles, fire clay, support beams and ore. Farmers sell the new seeds. Bakers sell flour, querns and gearboxes. Butchers sell hides and barrels. Merchants sell lumber, saws, pans and fruit.
- **Missing sounds:** cave-ins, sluices, turning spell pages and villagers cooking at a fire now make a sound; they played nothing before.

Removed Herobrine.

## 1.0.0-alpha.19 — High and Low (November 2026)

**Worlds:**
- **New land comes with new worlds:** the new Nether, End, Skylands and villages below are made in worlds you start now. A world you already have keeps its land; World Settings → Upgrade brings them to every place you haven't been yet (there can be seams where old land meets new, so make a backup first).
- **More world types** for a new world: Amplified (the hills and mountains near twice as high), Islands (the seas risen over most of the land), Floating Islands (the land in islands hanging over the void), Endless Winter (snow everywhere, the lakes frozen), Hell (a world that ended long ago, scorched, its seas turned to lava) and Flat.
- **The Drift,** as in Better than Adventure: great masses of land hanging in open sky under a golden light, where only chickens live. Build a frame of Glow Lanterns (glowstone) and set a block of bedrock inside its bottom: it fills with a silver portal you can see through and walk through like the others, and a portal home is built where you arrive. Distances there are eight times those at home. Bedrock has an item now: a diamond or steel pickaxe (or better) mines it and keeps it. `/tp drift` goes there too.

**The Skylands:**
- **Islands the Aether's way,** and three dungeons with keepers: a bronze one held by the Slider; a silver one, an angelic temple on a bed of cloud, where the Valkyrie Queen waits; and a gold one, a floating island with a hall of hellfire stone, where the Sun Spirit wakes with two fire minions.
- **New creatures and blocks:** sentries that sit on carved stone, evil whirlwinds, the slider, the sun spirit and its fire minions; angelic and hellfire stone (and lit ones), pillars and pillar tops, holystone bricks and crystal fruit leaves. Zanite and gravitite make whole sets of tools and armour now, and ambrosium ore gives an ambrosium shard that heals.
- **Waterfalls end in the clouds:** the islands' falls pour into the cloud bank below them and break into spray, and water and lava no longer leak out of the bottom of the Skylands (old floods drain away). The new land has no banks of cloud blocks (the sky's clouds are real ones now); the silver dungeon keeps its cloud bed, and clouds already there stay.

**The Nether:**
- **Better than Adventure's own Nether** (new worlds), laid over open land you can walk across under the high roof: its lands, caves, lava floes, shelves and every one of its decorations as BTA makes them. Each land has ground of its own: gentler crags, volcanic islands half out of the lava with channels between them, crystal plains, stepped shelves, the forest, the Old World and the sulfur pools. Its structures stand on the land, not in the caves under it.
- **Its blocks:** brimstone, brimsand, gloomstone, ember, cobbled netherrack and rubyglass. The giant rubyglass crystals are clear red glass, and their bright veins glow from within.
- **Its air:** every land has its own fog, with BTA's warm, golden grade over it all (Options → Nether Color Correction) and a heat haze over the distance (Options → Heat Haze); water runs red, lava is thick to see through, and yellow sulfur fire burns on brimstone in the sulfur fields. Fog, sky and grade fade over as you pass between worlds instead of switching at once.
- **Its creatures:** zombie pigmen and ghasts come as in BTA (the pigmen almost never found ground to stand on before), and the Nether's own crowd is back, each in its land: sulfur slimes, crystal wisps, magma slimes and ember fuselings on the volcanic islands, scorched skeletons in the Old World, charred ghouls on the crags and shelves, and cinder wisps with the fortress's wither skeletons. They spawn on the ground below you even when you fly high over it. A portal or `/tp nether` sets you on real ground, never in lava or inside a structure.

**The End:**
- **The outer End, the Better End way** (new worlds): its lands, each with its own ground, trees and plants: the chorus forest, blossoming spires, foggy mushroomland, glowing grasslands, crystal mountains, sulphur springs, umbrella jungle, shadow forest, ice starfield, amber land, the megalake and its grove, lantern woods, painted mountains, neon oasis, dragon graveyards, dry shrubland, dust wastelands and umbra valley. Islands stack over islands, each with its land's ground. The dragon's island stays as it was.
- **Its sky, fog and buildings:** slow-turning nebulae and stars overhead, each land its own fog colour and thickness, over a hundred new blocks, and buildings standing about the lands, each land its own kinds.
- **Its creatures:** dragonflies, end fish, cubozoa, silk moths, shadow walkers and end slimes.
- **The dragon's pillars** are tall obsidian columns, here and there rimmed with crying obsidian, with the crystals on top. Thallasium makes tools and armour, ender ore gives an ender shard, and amber ore an amber gem.

**Villages:**
- **Villages built from DokuQuest's own buildings,** in 18 styles: plains, savanna, taiga, snowy and desert; beach (on stilts, thatched); jungle (stilts, leafy thatch, vines); jungle treehouses (houses on decks up the trees, a spiral stair round a post to each); swamp (stilts, mud brick, moss on the roofs); mountain (tiered slate pagoda roofs); alpine chalets (big eaves, balconies); mesa (adobe terracotta and roof beams); oasis (desert houses among palms); savanna huts (round mud huts under thatch cones); mushroom (homes in giant mushrooms); igloo (snow domes with ice windows and a tunnel); Christmas (lanterns along the eaves, wreaths, decorated trees, presents and snowmen); and Halloween (dark oak and slate, jack-o'-lanterns, cobwebs, graves, soul lanterns and a pumpkin-patch square).
- **The land picks the style:** beach villages on the coasts, mesa in the Redlands, igloos on the glaciers, swamp villages in the Mossmire, chalets in the highlands, pagodas in the mountains, jungle villages and treehouses where it's hot and wet, savanna villages and huts where it's hot and dry, snowy, igloo or Christmas villages in the cold, now and then a Halloween village in the woods and a mushroom village here and there. Villages can stand on beaches and mountains now.
- **What's in them:** houses in three sizes; a workshop for every trade; a library, a temple, a tavern, a sawmill, a builder's yard, barracks for the guards, a jeweller, an alchemist and a general store; a windmill whose great sails turn a millstone; farms, pens and a stable; market stalls; and a square with a well, a market, a great tree, an oasis, palms, a Christmas tree, a pumpkin patch or a giant mushroom. Towns are laid out in quarters: the market and the civic buildings round the square, the crafts beyond, farms, pens, the windmill and the sawmill at the edge, homes between.
- **Shops show their goods:** item frames on the walls, display tables piled with wares, the trade's own item framed over the door, weapon racks, wall shelves, dressed armour stands at the armourer's, bookcases in the library and shelves of mugs over barrels behind the tavern bar. Market stalls have tables of wares, a stocked shelf behind and a chair for the trader.
- **Furnished homes:** chairs, benches, sofas and tables, kitchen cupboards with the house's loot in them and a shelf above, bedside shelves, rugs under the tables and down the temple aisle, shutters in even pairs on every window (or none), and a hearth at the back wall whose smoke goes up the chimney.
- **Streets that follow the land:** each street finds its own level, eased so no step is more than a block, and every one-block rise is a path stair you walk up without jumping. Streets no longer run down a ravine as a staircase or sink into a trench, and houses no longer cut across them.
- **Walls, gates and water:** walled towns and cities have a gate wherever a street runs out, with a tower either side; a palisade tower no longer leaves a way in under its platform. Farms by lakes stand on dry ground, and fountains, ponds, fish tanks and irrigation channels hold exactly their water. The waystone stands on real ground by the square.
- **Town mines are real mines:** a headframe over a twelve-step drift down to a timbered chamber, then three mineshaft-style galleries with posts and plank caps, rails, torches and ore in the walls. The way out climbs to the street without a jump.

**Villagers:**
- **Every trade works at its own stations:** in the morning each villager walks a round of their workshop, hammering at the anvil (the smiths with a smith's hammer), brewing, reading at the lectern, drawing maps, grinding, weaving, kneading or sorting, and keeps the counter in shop hours.
- **The village works:** farmers reap, replant and carry the wheat to the mill; the miller grinds it at the windmill and the baker bakes the flour; lumberjacks fell trees and plant new ones; the miner cuts ore in the town mine, a gallery a day; fishermen work the shore; ranchers and shepherds tend their own pens; the craftsfolk turn the village's stock into goods. Every trade gets a villager before any trade gets a second. Shops sell what the village has made, what you sell to a shopkeeper goes into its stock, and the miller and the miner keep shops too.
- **Villagers repair their villages:** after an explosion the builder puts back what was lost, nearest first: the blocks, their carving, the rugs, and the goods on shelves, tables and racks (a chest comes back empty). Blocks you break yourself are yours to change.
- A villager with nothing to do strolls the streets instead of waiting at the hall door; workers in walled towns go in and out by the gates; villagers keep out of water and climb out if they fall in.

**The Wither and the Wither Storm:**
- **The Wither as Minecraft has it:** it swells for eleven seconds, untouchable, then bursts out with a cry heard across the world. The middle head hunts you while the side heads pick their own targets, and now and then a blue skull flies that breaks even obsidian. Hurt, it tears up the blocks round its body. Its skulls wither creatures as well as players, and every kill heals it. It has a new look to match the Storm's first form, and a voice of its own.
- **The Wither Storm grows:** it starts as a Wither with a command block caged in its ribs (a real command block, your pack's when one is on), grows a hump, a jawed storm head and tentacles, and becomes the destroyer, one tentacle arching over its back; evolved, its tentacles hang under it like legs. Obsidian-black, veined with purple, its eyes and mouths lit violet. Each form is bigger and tougher, and it has its own roar, bite, beam and death sounds.
- **It eats the land:** whole clumps of ground tremble, tear loose and spiral up into the head that took them, and everything it swallows makes it grow. Each head picks its own target; the middle one's tractor beam comes first and all three later, pulling you up to the mouth to bite and swallowing creatures whole, while a head without a beam spits skulls. Wound one head enough and it screams, its beam goes out, and it leaves you alone for a while.
- **Only worthy blows hurt it:** magic, arrows from an enchanted bow, and enchanted weapons (Smite most of all). Plain blows, plain arrows, fire and explosions glance off, and it mends itself, faster once grown. Its last wound brings it crashing down.
- **Super TNT** (four TNT and four gunpowder round a rift crystal) and **the Formidibomb** (Super TNT with four cinder powder and four gunpowder), lit with flint and steel. The Formidibomb pulses for 30 seconds until the nearest Storm drags it into its maw, where it goes off and knocks the Storm out of the sky: it lies on its back for 30 seconds taking double from your blows, then rises.
- It can be seen from far off, its health bar shows within 200 blocks, and a grown Storm turns the sky round it a bruised purple and the fog a dusty red. `/kill` and falling out of the world now finish anything, the Storm included.

**Creatures:**
- **Stealth:** sneak out of a monster's sight (it sees about a third of the way round) and it won't notice you until you're within four blocks; sneak in the dark and it won't notice you from more than seven away. Walking and running are heard whichever way it faces. A blow on a creature that hasn't noticed you, from behind or the side, is a **sneak attack** for 1.75 times the damage ("Sneak attack!").
- **Animals with tempers:** about one in five is curious and comes over to look at you for a while; one in four is shy and keeps its distance (a calm one just steps aside); the rest don't mind you. Tamed and penned animals pay it no heed.
- **Dragons have fuller bodies:** a deep chest, haunches and a belly round a slimmer waist, thighs, three-clawed feet, a longer tail, frills along the jaw, nostrils and fangs. The ender dragon's wings are a dark membrane with magenta veins.
- **Birds have real feet:** three toes forward, a hind toe and claws, webbed for ducks, mallards, geese and swans (they stood on flat plates).
- **The Giant:** a ghoul nearly three times your height guards some crypts. Watch for its stomp: a wind-up, then a blow that throws you.
- **Wolf armour** (BTA) in leather, chainmail, iron, gold, diamond and steel, worn on the head and body.
- **Resource pack skins:** zombie pigmen (their left arm and leg were missing) and wither skeletons wear the pack's skins, pigs, cows and chickens in cold and warm lands take the pack's cold and warm looks, and endermen's eyes are the pack's own. Villagers lost the stray dots on their faces.

**Mounts:**
- **Horse armour** in leather, chainmail, iron, gold, diamond and steel, for a grown horse you've tamed: use it on the horse (what it wore comes back to you). It adds to the horse's armour and wears out like wolf armour. Craft it from its material round a piece of leather; leather horse armour from leather alone.
- **Chests on pack animals:** use a chest on a grown donkey, mule, llama or camel you've tamed and it carries 27 slots.
- **A gear screen:** sneak and use your mount with an empty hand to see its saddle and armour slots over what it carries (shift-click puts a saddle or armour straight on). Friends in your world can open their own mounts' too, and a mount kept in the stable keeps its gear.
- **You can see the gear:** barding over the body, neck and face in the armour's metal or leather, a chest on each flank, and saddle bags as leather pouches with a brass buckle. Friends see it, and wolf armour, too. When a mount dies it drops its saddle (it used to vanish), its armour and its chest or saddle bags with everything in them.
- **Wild horses, donkeys, llamas and camels wear nothing:** armour and chests are what you put on your own. The looks that wore them are plain now (a big dark stallion, a dusty donkey, a shaggy brown llama, a pale camel), and the draft horse's feathering is its mane's colour.

**Bows and quivers:**
- **Wear a quiver** in its own slot beside the backpack. It holds four slots of any kind of arrow, and the bow draws from your off hand, then the worn quiver, then a quiver in your bag, then loose arrows, shooting whatever kind comes out. Arrows you pick up go into the worn quiver first, and the golden quiver never runs dry. In the book inventory its slots sit beside the crafting grid, and you can hide it like the rest of your gear.
- **Quivers are 3D:** a leather tube with a rolled rim, strap and buckle, slung across your back or hung on the backpack's side, with more arrows showing as it fills. Friends see your quiver and your backpack (they never saw the backpack before).
- **Bows are held as archers hold them:** drawing turns you side-on, the bow upright in your left fist and the string pulled back to your chin; carried, the bow hangs level at your side, string up. Villagers and skeletons hold theirs the same way. New arrows from BTA: golden, flaming (they light TNT and braziers) and purple.

**Magic:**
- **Every spell casts bare-handed** at a little under three-quarters of its power; a staff gives the full power and wears instead. Everyone starts knowing Firebolt and Home Teleport. A tome above your Magic can be studied once for Magic experience, and you keep it; ten starter tomes craft from a book, lapis and a reagent; the Apprentice Staff takes amethyst or lapis too; and kills with a spell train Magic.
- **Mana** is a row of ten crystals beside the hearts, draining from the left, with the spell's name over them for a moment when you change or cast. Armour moved up over the hearts, and the old bar that covered villagers' talk is gone.

**Blocks and items:**
- **The trommel** (BTA): give it up to four things to sift and some fuel, and every 2.5 seconds it throws what it finds out of its left side, into a chest there if there is one: pebbles, olivine and more, and very rarely a small slime. Make it from cobblestone round an iron mesh, over a log.
- **More of BTA's blocks:** polished stone (and its bricks, mossy bricks, slabs and stairs), polished limestone, gloomstone and netherrack, carved slabs of eight stones, cobbled and mossy cobbled basalt, granite and limestone (from pebbles), rubyglass blocks and bricks, an ember coal block, a sugar cane block (and a baked one), baked mud, steel glass, a chainlink fence, gold, iron, steel and lapis brick slabs and stairs, two-block statues, piles of autumn leaves and the soul wax candle. Loose stones are called pebbles now, and you can throw them.
- **More of BTA's items:** chainmail (scrap it back to chainlinks), steel shears, a steel fire striker, crude steel, a steel bucket that holds three bucketfuls, ice skates (glide fast on ice and still steer), an explosive charge for the handcannon, ice cream, dough, cherries, cloth, a label to name things, and a rotary calendar that shows the season and the day while you hold it.
- **Trees:** gum trees (the eucalyptus, renamed, with a sapling), thorn trees, shrubs in the thornscrub, Redlands, savanna and plains, and flowering cherry leaves that bear cherries in season. Every kind of branch makes two sticks.
- **Every material mixes with every other:** all 44 make both tools and armour, and a tool or piece all of one (a Tin Pickaxe) crafts at the crafting table. Mixed armour shows each material's colour where it went in the grid.
- **A use for everything:** a venom sac brews strong poison, dough is two flour and a water bucket, and the goat horn is a war horn: speed and strength for 10 seconds to every player within 16 blocks, once every 30 seconds. Banners stand two blocks tall when you place them, on a floor or a wall, and break as one.
- **Rugs:** a carpet laid on a floor becomes a rug, and tables, chairs and beds stand on it. Hitting a rugged floor lifts the rug first, and snow doesn't settle on it.
- **The spyglass zooms:** hold use to look through the round, brass-rimmed glass at a tenth of your view, turning slower to match (you walk at a quarter pace); others see you hold it to your eye. A Zoom key (set it in Controls) gives a quarter view without one.
- **The compass and the clock work in your hand:** the live needle and dial show on the model (you saw a painted, still one).

**Graphics:**
- **Built-in shader levels** (Options → Shaders) do much more now. Low adds the wobble under water and lava; Medium a soft bloom and eyes that adjust to the dark and the bright; High sun shadows from blocks, leaves and plants, water you can see into (murkier the deeper it is, bent by the ripples), sun rays and a lens flare; Ultra bigger, sharper shadows, reflections in the water and soft shading in the corners. Shadows and the see-through water are left out in split screen, in portal windows, with a shader pack and on Vulkan. On an Intel HD 520 at 1280×720: 51 fps off, 46 at Medium, 40 at High, 38 at Ultra.
- **Always on:** Options → Texture Filtering (4x unless you change it) keeps distant blocks sharp; far leaves and glass lose their dark rims; distant fog fades into the sky behind it; light shimmers on things under water; and the sky is smooth, without bands.
- **Volumetric clouds** (Options → Volumetric Clouds, on): real clouds that build into the storms thicker, taller and darker, lit by lightning, the sunset and the moon, and fading into the fog far off. Off: the voxel heaps as before.
- **3D minecart rails:** steel rails on wooden ties, curves and slopes too, with any resource pack. Carts ride on the rails, their wheels over them.
- **Players move better:** a real crawl (on your chest, pulling with one arm then the other, a knee drawing up); elytra that hang from your back as the classic ones do, folded in a V, spread when you glide and swept back in a dive; and capes in one piece that lift as you walk and blow in the wind, a ripple running to the hem.

**Screens:**
- **Menus in Better than Adventure's style,** with the launcher's look: Options in pages with icons, folding sections, a Search page and the keys inline under Controls (the controller's bumpers turn the pages); BTA's Select World, Create New World and World Settings; and Select Server with Favorites, History, Local Network and Direct Connect. Buttons use your resource pack's own when it has them.
- **Book screens:** the Help book (with new pages), the Journal, Skills, Spellbook, the Stable, brewing, enchanting, waystones, places and the Travel Journal open as a guidebook, with cloth ribbons for bookmarks. Turn pages with the arrows, the wheel or the d-pad.
- **Your inventory as a book** (Options → User Interface → Book Inventory, on): you, your gear and your skills on the left page, crafting and your bag on the right. Chests, furnaces, crafting tables and every station open as books too. Dark pages with light writing: Options → User Interface → Dark Book Pages, or the moon under any open book.
- **Achievements and maps are books:** achievements by world in order, with what each asks and when you earned it; the map on the right page (drag, zoom about the pointer, pins, a compass rose), every map you carry on the left. **The creative catalogue** has 16 tabs, with families kept together.
- **Photo mode** (the pause menu, or F6): the camera leaves you and flies free up to 48 blocks off; change the time of day, the season and the fog, show or hide yourself, and pick from nine filters.

**Fixes:**
- **Smoother on older laptops:** flying through new land no longer freezes the game for seconds at a time, and light, building the land's shapes, water and saving are spread over frames. On an Intel HD 520, standing in a jungle went from 44 to about 60 fps.
- **A crash in about one new world in five** is gone.
- **Clicks at a low frame rate count:** a quick click inside one frame was lost. In creative a block breaks the moment you press, then one every quarter second while you hold.
- **Grass and leaves keep their climate's colour after a reload:** land loaded from your save (and land sent to friends) all took the coldest tint.
- `/tp spawn` and respawning from the Drift or another side world no longer drop you through the world; side worlds show their own skies; and a portal's far end is built the same size as the one you went in by.
- **Water no longer floods without end** beside carved blocks and slabs.
- **No more holes:** shut trapdoors no longer let you see out of the world through the block under them; anvils turned east or west have whole tops; path stairs no longer show the sky at their foot; green and white cracks along paths and bumps round stairs are gone; and a block beside a chiseled one keeps its face.
- A campfire's smoke rises (it pooled at the fire); holding a map no longer shows a third arm; door items show the door's picture; the lightning rod is no longer invisible; a lantern under a slab stretches its chain to meet it; tall grass in the DokuQuest Base pack is the same green as the grass (it came out mint); and achievements with missing icons show them. On Linux the game is named "DokuQuest" in the system's volume mixer (under its old shared name, one mute there could silence it for good).

- Removed Herobrine.

## 1.0.0-alpha.18 — Still Green (November 2026)

**Blocks:**
- A door and a trapdoor are a real mesh, the same way a ladder is. Looking along the panel still shows the wood.
- A felled tree's leaves keep the colour of the leaves still on the standing trees. They were coming down grey.

- Removed Herobrine.

## 1.0.0-alpha.17 — Thaw (November 2026)

**On a big screen:** interface size is one setting, in the launcher and in the game. Auto is 100% on 1080p, 125% on 2K, 150% on 3K and 200% on 4K, and you can set it anywhere from 100% to 300%. The launcher lays out in design pixels and scales the whole window. The game grows its default window and zooms the HUD to match.

**Weather:**
- Snow layers, the snow sitting on a chiseled block, and plain ice melt from real heat (a torch, a campfire, a furnace that is actually burning, lava, magma and the other hot blocks), from rain, and from the sun when it is not snowing. Packed ice, blue ice and snow blocks stay. A lamp, glowstone or soul fire does not thaw what stands beside it.
- Still water beside clear ice keeps its calm face. It no longer wears the downward-flow picture just because you can see through the ice.

**Light:**
- Sun and lamps mix the Cubyz way, and the night never drops below its floor.
- Stained glass colours the light that passes through it. Wool and concrete stay opaque and do not.
- A torch in your hand lights the ground in front of you in that torch's own warm colour, even where the air is already some other colour.

**Blocks:**
- A door or a trapdoor shows the same print on both sides, handle on the same edge, and the picture stays put when it finishes opening or closing.
- A ladder is a real mesh. Looking up or down the wall still shows the rungs.
- Snow lands on each little column of a chiseled block, and water stands in the empty notches instead of floating over them.
- A felled tree keeps each log's bark and end grain, and the leaves keep their holes.

**Creatures:**
- A natural spawn is refused within 24 blocks of you in every direction, so a mob does not appear a few steps in front of your face. A ghast is a rare roll, one at a time, into a clear space out past that distance.
- A blow lands on the limb it actually meets.
- The ender dragon patrols the lair in a wide circle, and the dive and the strafe are aimed at you.

**The story:** a Realm Fragment in your pack does nothing by itself. Hold it up to the town's tear and use it (right click, or the controller's use). The tear drinks that one, opens a little wider, and he grows bolder by the same count. The tenth one steadies the tear into a gateway.

**Drawing the world:** shader packs in the Iris and OptiFine style can light the land, the water and the creatures when their programs link. Vulkan sits beside the legacy and modern renderers (`--renderer vulkan`). Looking along a ladder or a leaf no longer lets the little holes swallow the rung.

- Removed Herobrine.

## 1.0.0-alpha.16 — Port of Call (November 2026)

**Playing over the internet:**
- **Your servers:** the Multiplayer screen keeps a list of servers, as Minecraft does. Type an address, give it a name and press Save server; Join, Edit or Remove it from the list later. The last address you joined is filled in the next time.
- **VPNs:** with a VPN on (NordVPN, Proton, Mullvad…), Open to LAN and the server showed the VPN's own address instead of your home one, and the internet address it found was the VPN's, which nobody can join. Open to LAN shows your home address now. Open to LAN, the server dashboard and the server's console say when a VPN is on, that friends on the internet can't get through it, and how to let the game's port skip it (NordVPN on Linux: `nordvpn allowlist add port 25580 protocol UDP`; on Windows: Split tunneling, turn the VPN off for DokuQuest), or to turn it off while you play together. A join that can't get through says so too when a VPN is on.
- **On Windows, "Let DokuQuest through Windows Firewall"** on the Multiplayer screen: if Windows' question about networks was closed or answered Cancel, Windows quietly blocks the game from then on, so joining finds nobody. The button clears the block and lets the game through, after one Yes from Windows. (An antivirus with its own firewall needs DokuQuest allowed there too.)
- **The server's console says when someone knocks:** "Heard from …" the moment a friend's first packet arrives, before they've joined. Now you can tell "their attempt never reached me" (the router, the firewall or a VPN) from "it reached me but didn't get through". It also says which UDP port it's listening on.
- **Answers from an unexpected address:** some routers send the host's answer back from its home address instead of the internet one, or from another port (a host on both a cable and Wi-Fi can answer out of the other one), and the game threw those answers away, so the friend saw "No answer". It takes them now. Packets are a little smaller too, so they pass through VPN tunnels and PPPoE links whole.

- Removed Herobrine.

## 1.0.0-alpha.15 — Over the Hills (November 2026)

**Playing over the internet:**
- **Joining from another house could give up halfway:** joining had twelve seconds to fetch the whole first download of the world's changes. On a home network that's plenty; over a home internet upload a well-played world takes longer, so friends were turned back with "The host did not answer", whether you'd opened your world to LAN or were running a server. Joining now carries on as long as the world keeps coming.
- **The firewall help told you to open the wrong kind of port:** the game talks over UDP, and the "They can't connect?" help in Open to LAN said TCP, which let nobody in. It gives the right command now: `sudo firewall-cmd --permanent --add-port=25580-25589/udp && sudo firewall-cmd --reload`.
- **Open to LAN has a "Playing over the internet" section:** forward the UDP port on your router, let it through the firewall, and a button that finds your internet address to give your friend. The server dashboard in the launcher has the same button and firewall line.
- If nobody can reach you at all, the join screen now says what to check (the router's UDP forward and the firewall).

- Removed Herobrine.

## 1.0.0-alpha.14 — Tractor Beam (November 2026)

**The Wither Storm, as in Story Mode:** it isn't a Wither drawn five times bigger any more but the behemoth itself: a black obsidian mass with three heads (the middle one huge), glowing violet eyes, mandibles and fangs, the command block glowing in its chest, and long tentacles hanging under it that sway and coil as it drifts. It rises out of the T a third of its size and grows with everything it eats. Its violet tractor beam tears up the ground under it (the blocks fly up the beam into its maw), drags you toward it, and pulls the creatures round it in to be devoured; too near its maw, the wither sickness takes you. And it roars.

**Hit it back:** fireballs punched back now hurt the creatures of fire that threw them (the cinder wisps and the Cinder Golem shrugged them off before), and a shulker's bolt hit back flies home to its shulker and stings it. Ghast fireballs and Wither skulls go back as before.

**Villagers:** no more odd bits sticking off them: the ring some wore round their heads, the cubes on their cheeks (earrings) and the librarians' floating spectacles are painted flat on them now. Scarves and pouches still stand out.

- Removed Herobrine.

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
