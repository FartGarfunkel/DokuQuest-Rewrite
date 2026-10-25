# Port status: DokuQuest 0.9.0.90 (JS) → C++

Each workstream keeps its own notes in `docs/port/<area>.md` (what was ported, what differs, what's left).
| Area | JS modules | C++ | Status |
|---|---|---|---|
| Foundation | core/util.js, world/chunk.js (codec idea), core/blocks.js tables | common/, core/, world/, client/ | done (engine runs) |
| Data | core/blocks.js, items.js, recipes, tags, MOB_TYPES, MODEL_DEFS, atlas tiles | data/*.json via tools/port/extract_all.py | done |
| Terrain generation | world/worldgen.js, worldupdate.js (rivers, crust), metals.js (ores), deepdark.js (terrain), theend.js (End terrain, void crust), riftarc.js/worldgen.js (realm, pocket), distant.js (sampling); nether2/aether/seamless have no terrain | gen/ (docs/port/gen.md) | done (structure hook in gen/structures.h; structures themselves wave 2) |
| Meshing, light, shapes, water, foliage | world/mesher.js, shapes.js, plants3d.js | render/ | wave 1 |
| Resource packs, voxel items, held/dropped items | render/packs.js, items3d.js, gear3d.js (shapes), models3d.js, sculpt.js, packarmor.js, models.js itemMesh/icons, local.js hand() | assets/, render/item_layers | done (wave 1) — see docs/port/assets.md |
| Sound | game/audio.js, sfxlib, sounds2, sound3, voices3, blocksounds, ambience, music, music2 | audio/ | wave 1 |
| Player & block gameplay | game/player.js, physics, movement, local, build, inventory, blockfx, blocks2 (player/world parts), rules, commands (+WorldEdit, /weather), obtain, itemfix, furnish, furniture (sitting, shelves), crowd, fairplay (food), world/world.js fluids + random ticks, world/shapes.js boxes | sim/, world/shapes, client/play | ported (see docs/port/gameplay.md) |
| Menus, HUD, inventory, controls | ui/*.js, input/*.js, styles.css, game/menutabs.js, sheet.js | ui/, input/, sim/crafting | wave 1 |
| Launcher, updater, Mod Studio, packaging | launcher/desktop/*, dokuquest-mod-studio.html, tools/package.sh | launcher/, .github/ | done (docs/port/launcher.md) |
| Creatures (sim + voxel bodies) | game/entities.js, mobai, monsters, nightmobs, bestiary, fauna, aquatic, quarrels, husbandry, pets, render/rigs*, models, mobtex, monstex, anim | sim/, render/ | wave 2 |
| Characters & creator | render/skins.js, ui/creator.js, skineditor.js, game/charparts.js, capes, carried | assets/, render/, ui/ | wave 2 |
| Distant terrain, skies, clouds, weather | game/distant.js, skies.js, render/clouds.js, game/weather.js, particles2, ambientfx | render/ | wave 2 |
| Portals & seamless dimensions | game/portals.js, portallinks, portalview, seamless.js, worlds.js (+ theend.js portal/void parts, riftarc.js tears as virtual portals) | sim/portals, sim/seamless, render/portal_view | ported (see docs/port/portals.md) |
| Structures, villages, towns | game/structures.js, villages, villageflat, towns, towns3, dungeons, stronghold, deepdark, deephalls, seas, places | gen/ | wave 2 |
| Network, server, mods | net/*.js, game/server.js, mods.js, together.js, launcher/desktop/lan.js | net/, server/, mod/ | done: protocol, server, LAN, JS/native mods (docs/port/net.md; together.js party UI left) |
| Everything else (magic, enchanting, brewing, villagers, redstone, minecarts, ships, maps, rpg...) | game/*.js | sim/, ui/ | wave 3 |
