# Content: maps, instruments, farming, fishing, camping, travel, elytra, the stable, places

Test: `build/test_content` (64 headless checks). Dev aid: `DQ_DEV_ITEMS="filled_map,compass"` shows the map and a live dial in hand.

## Ported
| JS | C++ | Notes |
|---|---|---|
| game/maps.js | sim/maps.*, render/map_render.*, ui/map_screen.cpp | records `<world>/maps/<id>.json` (the original 'map~id' record: palette + RLE + base64), mapAlign grid, exploring (6 columns/tick), skip table, climate/water depth, zoom/copy crafting (`mapCraft`), pins (`LevelInfo.data.mapPins`), markers (+ waystones, places, houses, villages/dungeons via hooks), colours from the tiles/pack (mapColorOf/mapRGB, climateGrass/Water), paper + marker canvas pictures, the map in one or both hands (comes up as you look down), the map screen (laid-out maps, zoom/drag/pins, compass rose, labels) |
| game/instruments.js | render/instruments.* | painted frames (32/64/32, outlined), needle spring, death compass to `stats.grave`, clock dial; live dial drawn over the held model |
| game/farming.js | sim/farming.* | watering can (24, 0.5 s pours, 40 %/crop), rice needs water, `wildSeed()`; items/recipes/shops are data |
| game/fishing.js | sim/fishing.* (+ float/line in render/content_layers.cpp) | wind-up 0.3 s, Bobber entity (fly/float/bite/got away), real fish hooked (`data.hooked`), catch tables by sea/temperature, sizes, junk/treasure, xp, wear |
| game/camping.js | sim/camping.* (+ smoke in content_layers) | campfire out/relight/kit/cooking (24 s, 4 spots), out-fire light 0 (registry lightMeta "campfire"), burn, hearth flue, far smoke columns, rope hang/cut, grappling hook entity + anchor + hook rope, sleeping bag spawn for a night |
| game/travel.js | sim/travel.*, ui waystone screen | waystones (attune, cost, travel, own ones named, village ones via `villageNear` hook), mount measure (`mountStats`, `hsLine`, `foalStats`) |
| game/elytra.js (+ batche.js firework boost) | sim/elytra.* | glide physics, boost (gust puff 1.8 s / rocket 2.6 s), crash damage, wind sound; `hasElytra`, `Player::gliding`, `elytraWings()` |
| game/stable.js | sim/stable.*, ui stable screen | book in `LevelInfo.data.stable[owner]`, uid/generation, summon/dismiss/rename/release/★/sneak+H, rest 5 min, faces from model head tiles |
| game/places.js (finding), structures.js structTick finding, hearth.js homes, obtain.js drops | sim/places.*, farming.cpp | `stats.sseen[id] = [x, z, name, dim]`, "You have found …!", +25 xp; `homeSettle`; journal of places screen; golden feather (no fall damage, in `hurtPlayer`); `obtainExtraDrops()` |

## APIs
- **itemUse / blockBreak / netAction** are handled by one `ContentSystem` (sim/content.cpp). Screens open through event `contentScreen [player, id, args]` (ids map, waystones, stable, places); factories in ui/content_screens.h: `makeMapScreen(slot)`, `makeWaystoneScreen(here)`, `makeStableScreen()`, `makePlacesScreen()`, `canOpenMapScreen(app)` (tab strip).
- `contentHooks()` (sim/content.h): `hurtPlayer`, `grantXp`, `dropItem` (gameplay), `capeSlot` (characters; until set, elytra count in the chest slot), `placesNear`, `villageNear`, `villageById`, `dungeonById` (structures/villages).
- Gameplay: call `elytraJump()` on jump and `elytraMove()` while `p.gliding`, then set `g_elytraByController = true` (until then render/content_layers drives the local glide); call `sleepingBagSleep()` before sleeping in a bed block; `wildSeed()` / `obtainExtraDrops()` in block drops; `mapCraft()` / `mapItemInfo()` in crafting/tooltips; `heldRodModel()` / `castWindOf()` for the rod pose.
- Creatures: `stableCreatureDies(lv, e)` before drops (true = went home); entity data keys owner/uid/sgen/petName/saddled/variant/color/size/baby/growT/hs; a fish with `data.hooked` must not run its AI.
- Render: `HeldItemLayer::handsHook` / `itemHook` (render/item_layers.h); `instrumentIcon(key, tex, uv)` for UI slots; `mapTexture()`, `drawMapSheet()`, `Canvas`, `ContentDraw` (assets/shaders/content.glsl).
- Particles via `Level::fx`: campfire_smoke, smoke_puff, flame, soul_flame, ember_spark, water, splash, mana_arcane, mana_light, white, gust_ball (extra: vel, life, size, grow, grav, rise, wind, glow, shrink, ring, spread).

## Differences
- Map heights are band-local (256 tall, clamped to 255); sseen entries carry a 4th element (dim) so places show on maps of their layer.
- Multiplayer: maps are kept per device (no host sync yet); stable actions go to the host via `lv.net->action(…"content")`, its list comes back as a message of kind `stable-list` (caught by a LevelFx tap in content_layers).
- Waystone travel to an unloaded waystone trusts the record (steps out on top of it).
- Instrument dials are a picture over the voxel model's faces (not a rebuilt model).

## Left
- Carriage (travel.js boat type) — vehicles; mount speed/jump use in riding — creatures; paths.js is combat paths/magic schools (RPG area), not ported here.
- Maps in item frames and in other players' hands; map pixel/pin sync over the network.
- Explorer backpack gear straps (inventory UI); house building for village heroes (needs village templates); raids/named foes (livingworld.js).
- Elytra wings/glide pose drawing (characters), rod cast pose, campfire food drawn on the fire, dropped/framed instrument dials.

## Changes outside my area
`core/init.{h,cpp}` (registerContent), `core/registry.cpp` (1 line: lightMeta "campfire"), `sim/player.h` (`gliding`, `glideBoost`), `render/item_layers.{h,cpp}` (two hooks, ~8 lines), `client/session.cpp` (`registerContentLayers()` next to `registerItemLayers()`), `CMakeLists.txt` (test_content), `docs/PORT_STATUS.md`.
