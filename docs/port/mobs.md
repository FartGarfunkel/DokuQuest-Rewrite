# Creatures: simulation, AI, spawning, voxel bodies

Workstream: creatures (`src/sim/creature*`, `sim/mobai*`, `sim/mob_combat*`, `sim/explosion*`, `src/render/creature_*`,
`assets/shaders/creature.glsl`, `tools/test/creature_test.cpp`).

## Ported modules

| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| game/entities.js Mob (update, hurt, die, wander, idle habits, drawing state) | sim/creature.cpp | all of MOB_TYPES via `reg().mobs`; burning in daylight, lava, cactus, swimming/floating, hopping, step-up hops, drops + XP, `mobDeath` |
| entities.js Cinder Golem (telegraphed slam / charge / volley, enrage) | sim/creature_monsters.cpp `bossAI` | `summonGolem()`; decals drawn by the layer |
| entities.js DroppedItem, Projectile; blocks2.js Arrow; FallingTree | sim/creature_entities.cpp | entity types `item`, `arrow`, `projectile` (fireball/spit/snow), `hex_flask`, `falling_tree`, `gib` |
| game/mobai.js | sim/mobai.cpp | A* (step up 1, drop 3, corners, doors for small creatures, water cost, danger blocks), `navDir`, `stepAhead` |
| game/nightmobs.js | creature_monsters.cpp `nightBase`, `spiderAI` | skeleton archers (strafe, arrows), spiders on walls/ceilings + pounce, fuselings (fuse + blast), slimes (sizes, split) |
| game/monsters.js | creature_monsters.cpp `monsterAI` | husk, drowned, stray, cave spider, witch (flasks, drinking), wraith, cinder wisp, magma slime, raider bands, werewolf, ogre slam, crocodile, shark, snakes, scorpion, big cats' stalking, onHit venom/frost/fire, night crowd tables, despawning |
| game/quarrels.js | creature.cpp hurt + creature_monsters.cpp `aiTarget`, `specialAI` | infighting, family call for help, notice-then-remember (`senses`), shade seeking, wariness |
| game/crowd.js (not in gameplay's tree) | creature_spawn.cpp `crowdPush` | entity push between creatures and players |
| game/dismember.js | sim/creature_rig.cpp | ray → part, sever chances per tool, crawl/limp/drop weapon/regrab, gibs |
| game/aquatic.js | creature_beasts.cpp `strandedFlop`, creature_rig.cpp squid/fish | |
| game/bestiary.js | creature_beasts.cpp | passive/neutral/predator/flyer/swimmer/amphibian, herds, packs, prey, bats roost, parrots perch/dance, dolphins, pufferfish, axolotls play dead, frogs eat slimes; spawn tables + caps (creature_spawn.cpp) |
| game/husbandry.js, blocks2.js farm animals | creature_beasts.cpp, creature_spawn.cpp | food following, love/breeding, babies, eggs, wool regrowth, herds once per chunk (`level.data.herdChunks`), bee colonies (nests/hives, flowers, honey, pollinating crops) |
| game/pets.js | creature_beasts.cpp | taming table, follow/stay, teleport to owner, defending, saddles + riding (`MOUNTS`), no friendly fire |
| game/skymobs.js | data + creature_monsters.cpp / creature_spawn.cpp | phygs, flying cows, sheepuffs, cockatrice at night |
| game/fauna.js | render/creature_fauna.cpp | client only: butterflies, fireflies, songbirds (flocks scatter), fish |
| render/rigs.js, rigdefs*.js (data), rigs2.js, anim.js (creature part), monstex.js posts | sim/creature_rig.cpp | biped/quad/spider/bird/pod/fish/squid/bat/bee/turtle/frog + sit/ram/bear/swimTuck/fan/perch/slither/sting/drift/orbit/fuse + rigLife (tail spring, ears, turn lean, lying down, floating/paddling, shaking dry, flinch, shamble, death crumple) |
| render/models.js tilesOf, mobtex.js/monstex.js (tiles already in data/tiles.png), packmobs.js | render/creature_models.cpp | voxel bodies, pack skins box-UV mapped |
| game/blockfx.js explode | sim/explosion.cpp | gameplay's TNT did not exist in this tree |

## Agreed changes / differences

- **Voxel bodies**: each rig part is a voxel box (32 voxels/block). Colours come from the part's six face pictures
  (original tiles or the pack skin); see-through pixels are carved by a visual hull of the six pictures (skeleton
  ribs, manes stay open); a head's dark face features (eyes, nostrils) are recessed one voxel; boxes ≥10 voxels get
  rounded edges. Built once per model+pack (cache, rebuilt when tiles/pack change). Glow parts use VOXEL_GLOW.
- Shadow: a soft blob disc under each creature (translucent pass); see-through parts drawn with `creature.glsl` (voxel.glsl + `uAlpha`).
- Rendering interpolates position/yaw/age between ticks (`GameSession::tickAcc`).
- Habitat: the debug terrain has no biome, so `habitatAt` estimates it from chunk climate bytes, ground and height;
  the terrain port should call `setHabitatProvider()` with the real biome.
- Underground slime/cave-spider depth thresholds are shifted +16 (sea level 64 instead of 48).
- The Skylands use the Overworld's day cycle (one sky column) so cockatrices can appear.
- Spider jockeys: mechanics ported (jockeyOf/jockeyRider), but 0.9.0.90's spawner (monsters.js) never made them, so none spawn.
- Stone mites exist as a type but never spawn (husbandry.js removed them); `wakeMites` is a no-op hook.
- No phantoms, ever (the test checks).

## APIs for other areas

- `sim/creature.h` — `Creature` (an `Entity`; `T` = parsed MOB_TYPES entry), `creatureType(key)`, `spawnCreature(lv, realm, type, pos)`,
  `dropItem(lv, realm, stack, pos, scatter)`, `useOnCreature(lv, player, creature, sneaking)` (feed/breed/tame/milk/shear/dye/saddle/mount/stay),
  `rideControl(lv, player, fwd, strafe, jump, sprint, sneak)` (call each tick while `player.riding`), `dismount`, `petsDefend`,
  `summonGolem`, `setHabitatProvider`, `habitatAt`, `Creature::snap()/applySnap()` (network replicas: `ghost`).
- `sim/mob_combat.h` — **the adapter to re-point at combat.h**: `damageEntity(lv, target, amount, attacker, cause, DamageSrc)`,
  `knockback`, `igniteEntity`, player effects `playerEffect/setPlayerEffect` (`player.data.fx`: poison, slow, chill, burn),
  `tickPlayerEffects` (called by the creature system every tick; it also counts down `player.hurtTime`), `grantXP`.
  The player's attack should call `damageEntity(lv, creature, dmg, &player, "mob", {tool: "sword"|"axe"..., weapon: true, hasRay, rayO, rayD})`.
- `sim/creature_entities.h` — `spawnArrow` (bows, dispensers), `spawnProjectile` (fireball/spit/snow), `spawnHexFlask`,
  `spawnFallingTree` (tree felling), `spawnGib`. Arrows emit `arrowHit(blockId,x,y,z,hx,hy,hz)` for targets/buttons/TNT.
- `sim/explosion.h` — `explode(lv, realm, pos, power, {noBlocks, source, onBlock})` (TNT can use it; `onBlock` primes TNT).
- `sim/mobai.h` — `findPath(world, from, to, PathOpts)`.
- `sim/creature_rig.h` — `rigModel(key)`, `rigPose`, `creatureBase`, `rigHitPart` (characters can reuse the biped anim).
- `render/creature_models.h` — `creatureModel(key)` voxel parts; `buildTileBox`.
- Events: `mobDeath([{type,x,y,z,id}, killerName|null])` (original args), `bossDefeated`, `playerDeath` (adapter), `entitySpawn` (Level).
- LevelFx calls: `voice(name, kind, pos)` with name = T.voice or T.sound and kind `say | hurt | death | angry | howl | purr`
  (original sfx = name, name_hurt, name_death, name_angry, wolf_howl, cat_purr); `sound(name)` with the original sfx names
  (bow, hiss, explode, roar, slam, warn, charge, thud, fireball, ignite, burn, fizz, pop, flop, puff, splash, spit, arrow_hit, hit, chirp, flap, ...);
  `particles(kind, pos, n, extra)` kinds: burst (extra.tex = tile name), flame, heart, pollen, explosion, floatText (extra.text/color/player).

## Registration / changes outside the area

- `src/core/init.h` + `init.cpp`: `registerCreatures();` (entity factories for every MOB_TYPES key via the "" fallback, `item`,
  `arrow`, `projectile`, `hex_flask`, `falling_tree`, `gib`, and the `CreatureSystem` LevelSystem).
- `src/client/session.cpp`: include + `registerCreatureLayers();` next to `registerItemLayers()`.
- `CMakeLists.txt`: `dq-creature-test` executable + `add_test(creatures)`.
- New: `assets/shaders/creature.glsl`. `docs/PORT_STATUS.md` row updated.

## Dev aids & tests

- `DQ_ZOO=1` (page of 16 types, `2`, `3`, …), `DQ_ZOO=all`, or `DQ_ZOO=cow,horse:horse_dun,ghoul` — line-up on a grass floor in front
  of the player, calm (no AI, no burning), noon, spawning off; swimmers get a water pool.
- `./build/dq-creature-test` (or `ctest`): rigs for all 125 types, path finding through a gap, ghoul melee, skeleton arrows,
  cow death → drops + `mobDeath` + XP, slime split, fuseling blast breaking blocks, breeding, taming, herds by day, monsters at night, no phantoms.

## Merge notes

- When combat.h lands: re-point `damageEntity`/`knockback`/`igniteEntity`; remove the `hurtTime` countdown from
  `tickPlayerEffects` if combat counts it down too; player death handling (`player.dead`, `playerDeath`) belongs there.
- If gameplay adds its own `item` entity (DroppedItem), drop `registerEntityFactory("item", …)` here (last registration wins).
- Gameplay should call `useOnCreature` on use-on-entity, `rideControl` while riding, `petsDefend` when the player attacks.

## Left to do

- Weather (bees/rain read `level.info.data.rain`), jukeboxes (`level.info.data.jukeboxes`) — hooks only.
- Riding in boats/carts (`mountedOn`), Carry On pick-up, pet naming; network snapshots are written but not wired.
- Fireball explosions from nether2.js, dragons/endmobs/haunt/villagers (later passes).
- Fauna sounds play through `level.fx` (no-op until audio wires LevelFx).
- Pack skins only for the packmobs.js list; other creatures keep the Doku tiles under a pack.
- Model builds are synchronous on first sight (a few ms per model); move to the thread pool if it hitches.
