# Changelog

Release notes for the C++ versions of DokuQuest, newest first. The release workflow
(`.github/workflows/release.yml`) publishes the section whose heading starts with the tag's version as the
GitHub release notes. Keep it in step with `assets/news/news.dqn` (the launcher's news). Patch notes never
mention Herobrine and every entry ends with "Removed Herobrine."

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

**New in the world:**

- **The Nether:** Cinder Isles out on the lava sea, Emberglass Groves, the Ashen Wastes and the Sulphur Springs (sulphur and charcoal make gunpowder).
- **Seasons:** a year of four seven-day seasons in the Overworld. Grass and leaves turn gold and rust in autumn and dull in winter, snow falls on the temperate lands in winter and crops grow slowly in the cold, and each new season is announced. `/time season autumn` jumps to one; `/gamerule seasons false` turns them off.
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
- **The Wither Storm:** set a rift core (rift crystals round a nether star) in the middle of a Wither's T, between two wither skeleton skulls. It hangs high overhead, tears up the ground beneath it and grows stronger on every block it swallows, pulls you toward it, and fires from all three heads at once. When it falls, what it swallowed comes down.
- **Biome fuselings** (frost, dune and bog, each with its own burst) and **village golems** that take after their land.
- **Elytra glide like Minecraft's.** Throw an ender pearl into an End gateway to go through it.
- **Treading water:** hold jump with your head out of the water and you float at the waterline, bobbing gently, instead of popping out and splashing back.
- **Capes stream in the wind** out under the sky, gusting with the weather; indoors and in caves they hang still.
- **Ingots set down stack Vintage Story-style,** four across, each layer turned across the last.
- **A torch in hand burns,** with a thread of smoke behind you.
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
- **Elytra ride the wind.** Out under the open sky a headwind lifts you, a tailwind pushes you on, a crosswind drifts you sideways and gusts carry you up.
- **Torches burn in your hand.** A held torch has a little flame and a thread of smoke, and leaves a trail of sparks when you swing it (soul torches burn blue, redstone torches give off red dust), for you and for everyone else carrying one.
- **The End has a sound of its own:** a low, slowly breathing drone with a hollow wind through it, and now and then something far off blinking from place to place.

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
