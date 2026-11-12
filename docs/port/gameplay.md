# Player & block gameplay (workstream notes)

Port of the original's player and block rules into `src/sim` (dqcore: runs in the game, on the launcher's
server and headless), with a thin client glue in `src/client/play.*`. Checks: `./build/dq-test-gameplay`
(headless, ~105 checks: movement, ledges, crawling, swimming, breath, mining speeds/tiers/drops, tree felling,
placing slabs/stairs/doors/beds/torches/chests, sleeping, falling sand, fluids, TNT, farming, saplings,
furnaces, armour, death/graves/respawn, regen, eating, lava, sitting, dropping, bow, itemUse, creative,
commands, WorldEdit, save/reopen).

## Ported modules
| JS | C++ | Notes |
|---|---|---|
| world/shapes.js (`shapeBoxes`, `worldBoxes`, stair corners, slab sink, torch set-back) + mesher.js `SHAPES`, `liquidHeight` | `world/shapes.{h,cpp}`, `data/shapes.json` | boxes for draw/collide/select; static models dumped with jsdump (`SHAPES`, incl. furniture) |
| game/physics.js | `sim/physics.{h,cpp}` | `gatherBoxes`, `collides`, `moveEntity` (step-up with lift ladder, air-step lip, scaffold one-way), `sampleContacts`, `raycastBlocks` vs real shapes, `rayAABB`, `lookVector`. Double precision inside; floats written back rounded away from contacts |
| game/player.js, game/movement.js | `sim/player.{h,cpp}` | walk 4.3 / sprint 5.7 / sneak 1.4 / swim 4.4, gravity 28, jump 8.6, water/lava drag, ladders, webs, slime bounce, soul sand/honey, ice, sneak edge-stop, crawl (C = double-tap sneak, or sneak into a 1-high gap), sprint-swimming, ledge hang/shimmy/climb, coyote time + jump buffer, view bob. Save = the original's `toJSON` keys (+ engine fields) |
| game/inventory.js | `sim/inventory.{h,cpp}` | containers on `vector<ItemStack>`, backpack contents on the stack (`c`), armour points, fire resist, charm bonus, carried light |
| game/local.js | `sim/controller.{h,cpp}` | `PlayerController::update` (= LocalPlayer.update), target, mining (hardness × 1.5/5 ÷ tool speed, tiers, needTier, leaves ×⅓ with swords, ×3 under water, tree-size factor), drops, XP, tool wear, Tree Capitator + falling trees, cracks that mend, use/attack, Carry On, ground storage piles, pick block, Q / Ctrl+Q, breath/drowning, regen, lava/fire/cactus/hot blocks/void, fall damage (beds/hay/slime/honey soften), off hand, bow & arrows |
| game/build.js, blocks2.js (player side), blockfx.js (flint, bone meal), furnish.js, furniture.js | `sim/build.cpp` | placement rules (slabs top/bottom/vertical + merge, stairs/trapdoors upside down, R mode, doors with hinges, beds, double plants, axis blocks, signs, facing blocks, double chests, ladders, torches on whole faces, lanterns, candles, cake, rods, frames, dripstone, hanging plants, supports, waterlogging), block use (doors/trapdoors/gates, beds, note blocks, bells, signs, cauldrons, pots, TNT, strip/till/path/carve, crop harvest-replant, candles, cake, composter, jukebox, ender chest, stations, lectern, frames, hives, glow berries, graves), buckets + waterlogging, seats, shelves/bookcases |
| game/blockfx.js, game.js onBlockChanged | `sim/blockfx.{h,cpp}` | `falling_block`, `tnt` entities, checkFall/checkHarden, explosions, sleeping skips the night, support/hanging/attached-block reactions |
| world/world.js fluids + randomTicks, blocks2.js growExtra/grassTick/stemFruit, game.js tickFurnaces/tickFurnace, itemfix.js smelt classes, crowd.js | `sim/worldsys.{h,cpp}` | `FluidSystem` (same levels/rates; lava 6/10 in the Ember Depths), `BlockRules` world system, random ticks (saplings, crops, cane/cactus/kelp/bamboo, vines, fire burn-out, copper weathering, amethyst, hives, grass spread/death), furnaces/smokers/blast furnaces, crowd push |
| game.js dropItem/dropBlockItems/spillEntity, entities.js DroppedItem/FallingTree, obtain.js, fairplay.js (food) | `sim/gameplay.{h,cpp}` | `item` entities (`data.stack`, pickup pull, merge, float, lava, take root, 5 min despawn except KEEP_DROPS), `falling_tree`, block drops, XP, feedback helpers, hooks, per-level gameplay system |
| local.js hurt/die/respawn, player.js hurt/wearArmor, entities.js Mob.hurt/die | `sim/combat.{h,cpp}` | armour 3.2 %/point (≤ 80 %, ≥ ⅕ of the raw blow), fire resist, invulnerability 0.6/0.45 s, knockback, crits, death causes, keepInventory/graves/death compass, bed respawn |
| game/rules.js | `sim/rules.{h,cpp}` | rule defaults, day/night lengths (`advanceDayTime`, called by `Level::tick`), daylight, graves |
| game/commands.js (+ /weather) | `sim/gamecommands.{h,cpp}` | registered in the networking registry (`sim/commands.h`): herobrine say seed gamemode time daylength gamerule tp give clear kill heal xp spawnpoint setworldspawn summon setblock fill weather + WorldEdit (wand pos1/2 hpos1/2 sel size count set replace walls faces hollow drain sphere hsphere cyl hcyl pyramid copy cut paste rotate flip move stack expand contract undo redo) |

## APIs for other areas
- **Player controller** (`sim/controller.h`): `PlayerController(lv, player)`; `update(dt, PlayerInput)` every frame;
  `respawn()` from the death screen; `target`/`targetEnt`/`mining` for HUD and cracks; `damageCracks(lv, w, cam, &mining)`
  for the renderer. The client glue is `client/play.h` (`localController(session)`, `clientTickLocalPlayer`,
  `clientPlayerCamera`). A server can drive remote players with the same class.
- **Screens**: the controller emits `screenOpen [player, kind, opts]` (kinds: container {pos|ender|held}, furnace {pos,kind},
  table, stonecutter, station {kind,pos}, machine {pos}, sign {pos}). Block entities are created in the original's JSON
  format by `containerEntity(w, x, y, z)`; double chests: `chestPartner()`. After editing one in place call
  `world.blockEntityChanged(x, y, z)`.
- **Combat** (`sim/combat.h`): `damagePlayer`, `damageEntity`, `registerHurtHandler(type, fn)`, `mobDeath`, `knockback`,
  `attackEntity`, `wearHeld`, `playerDie`, `respawnPlayer`, `placeAtSpawn`. On a joined client hits on ghosts go through
  `lv.net->hitEntity`.
- **Commands**: `registerGameCommands()` (from `registerCommands()` in sim/register.cpp); `resolveBlock/resolveItem`,
  `completeCommand` (Tab), `weSelection/weSetCorner` (wand outline).
- **Events** (EventBus): `itemUse [player, item|"" , x, y, z, face]` before every use (false = used up; portals,
  redstone, minecarts hook it), `blockBreak [player,x,y,z,key]` and `blockPlace [player,x,y,z,key]` before the change
  (cancellable), `pickup`, `toast`, `rumble`, `playerDeath [player, cause, level]`, `playerRespawn`, `mobDeath`,
  `morning`, `note`, `jukebox [x,y,z,disc|null]`, `foodEffect`, `weather`, `herobrine`.
- **Hooks** (`sim/gameplay.h` `hooks()`): place, useBlock, useItem, useEntity, drops, breakTime, afterBreak,
  blockChanged, growTree (the generator's treeParts), ignitePortal.
- **Per-level state**: `levelSlot<T>(lv, "name")` (freed with the level).
- `QuietEdits` (sim/worldsys.h): block changes still saved and synced but no fluid/support reactions (WorldEdit).
- Feedback through `Level::fx`: sounds use the original sfx names; particles kinds `burst {tile,speed}`, `text {text,color}`,
  `splash`, `dust`, `smoke`, `bubbles`, `explosion`, `growth`, `note`, `motes`.

## Differences from the original
- `naturalRegen` defaults ON (user rule: no hunger; 1 HP every 4 s after 8 s unhurt). Food heals half at once and the rest
  over a few seconds (fairplay.js).
- Creative placing / bone meal / buckets don't use items up (the original's base placeBlock did).
- World time is global for the stacked world (the original advanced it only in the Overworld).
- `blockBreak` / `blockPlace` are emitted before the change and are cancellable.
- Fall damage honours other systems zeroing `Entity::fallDist` (seamless crossings, updrafts).
- WorldEdit commands are also reachable as `/set` etc. (the registry strips slashes).
- Sapling growth uses `hooks().growTree` when the generator registers it; until then a simple tree of the sapling's kind.

## Networking notes
- On a joined client (`lv.isRemote()`) the gameplay system doesn't simulate the world (fluids, random ticks, furnaces,
  item pickup/merging, sleeping, crowding); the local PlayerController still moves the local player. Hits on ghost
  creatures go to the server (`lv.net->hitEntity`); the server's `netHit` handler applies them with `damageEntity`.
- Dropped items handed over by a client keep `data.stack` (or the older `data.item`) and their pickup delay.
- `tools/test_net`'s check "chunks made here from the seed match the server's" now sees a few re-requests: the server's
  random ticks really change freshly generated chunks (cave vines lighting up, kelp growing — the original's rates), so
  clients ask for those records. The check should count only checksum mismatches, not stale chunks (net/ follow-up).

## Left for later / other areas
- Reconcile with main's `sim/effects.*` and enchantment/skill hooks (speed factors in `Player::move`, mining bonus in
  `breakTime` hook, protection in `damagePlayer`, unbreaking in `wearHeld`, `onBlockBroken`); food `foodEffect` events
  should call `giveEffect`.
- Placed-block marks (fairplay.js, skills XP), redstone placement and
  use (hooks ready), camping (sleeping bags, campfires, ropes), boats/minecarts, animal feeding/shearing (useEntity hook),
  compass/clock/spyglass HUD, jukebox music, weather ticks for puddles/snow, redstone ore fading.
- The UI death screen should call `localController(s)->respawn()` (until then jump/use respawns).
- A client LevelFx sink (sounds/particles/toasts) belongs to the audio/UI work.
