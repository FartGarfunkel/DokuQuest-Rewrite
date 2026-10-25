# Changelog

Release notes for the C++ versions of DokuQuest, newest first. The release workflow
(`.github/workflows/release.yml`) publishes the section whose heading starts with the tag's version as the
GitHub release notes. Keep it in step with `assets/news/news.dqn` (the launcher's news). Patch notes never
mention Herobrine and every entry ends with "Removed Herobrine."

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
