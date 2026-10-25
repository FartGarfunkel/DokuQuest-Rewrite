# Port status: DokuQuest 0.9.0.90 (JS) → C++

Each workstream keeps its own notes in `docs/port/<area>.md` (what was ported, what differs, what's left).
| Area | JS modules | C++ | Status |
|---|---|---|---|
| Foundation | core/util.js, world/chunk.js (codec idea), core/blocks.js tables | common/, core/, world/, client/ | done (engine runs) |
| Data | core/blocks.js, items.js, recipes, tags, MOB_TYPES, MODEL_DEFS, atlas tiles | data/*.json via tools/port/extract_all.py | done |
| Terrain generation | world/worldgen.js, nether2/aether/theend generation | gen/ | wave 1 |
| Meshing, light, shapes, water, foliage | world/mesher.js, shapes.js, plants3d.js | render/ | wave 1 |
| Resource packs, voxel items, held/dropped items | render/packs.js, items3d.js, gear3d.js (shapes), models3d.js, sculpt.js, packarmor.js, models.js itemMesh/icons, local.js hand() | assets/, render/item_layers | done (wave 1) — see docs/port/assets.md |
| Sound | game/audio.js, sfxlib, sounds2, sound3, voices3, blocksounds, ambience, music, music2 | audio/ | wave 1 |
| Player & block gameplay | game/player.js, physics, local, movement, build, blockfx, blocks2 (part), rules, commands, obtain, itemfix | sim/ | wave 1 |
| Menus, HUD, inventory, controls | ui/*.js, input/*.js, styles.css, game/menutabs.js, sheet.js | ui/, input/, sim/crafting | wave 1 |
| Launcher, updater, Mod Studio, packaging | launcher/desktop/*, dokuquest-mod-studio.html, tools/package.sh | launcher/, .github/ | done (docs/port/launcher.md) |
| Creatures (sim + voxel bodies) | game/entities.js, mobai, monsters, nightmobs, bestiary, fauna, aquatic, quarrels, crowd, husbandry, pets, skymobs, dismember, render/rigs*, rigdefs*, models, packmobs, anim (creatures) | sim/creature*, mobai, mob_combat, explosion; render/creature_* | done (wave 2) — see docs/port/mobs.md |
| Characters & creator | render/skins.js, ui/creator.js, skineditor.js, game/charparts.js, capes, carried | assets/, render/, ui/ | wave 2 |
| Distant terrain, skies, clouds, weather | game/distant.js, skies.js, render/clouds.js, game/weather.js, particles2, ambientfx | render/ | wave 2 |
| Portals & seamless dimensions | game/portals.js, portallinks, portalview, seamless.js, worlds.js | render/, sim/ | wave 2 |
| Structures, villages, towns | game/structures.js, villages, villageflat, towns, towns3, dungeons, stronghold, deepdark, deephalls, seas, places | gen/ | wave 2 |
| Network, server, mods | net/*.js, game/server.js, mods.js, together.js, launcher/desktop/lan.js | net/, server/, mod/ | wave 2 |
| Everything else (magic, enchanting, brewing, villagers, redstone, minecarts, ships, maps, rpg...) | game/*.js | sim/, ui/ | wave 3 |
