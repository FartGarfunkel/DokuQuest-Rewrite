# The End, dragons, the Deep Dark's guardian, the watcher and the story

Workstream files: `src/sim/endstory.{h,cpp}` (system, events, helpers), `sim/endstory_int.h` (internal),
`sim/end_fight.cpp`, `sim/dragons.cpp`, `sim/deepdark.cpp`, `sim/haunt.cpp`, `sim/rift.cpp`,
`src/render/endstory_layer.{h,cpp}`, `tools/test_endstory.cpp`.

## Ported modules

| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| game/theend.js (gameplay) | end_fight.cpp | the fight's set-up (crystals on the pillars, the dragon roused, `end.met`), the exit portal + egg + gateway when it falls, gateways round the main island (out to the far islands and back, far ends set down once their land loads), thrown ender pearls (`ender_orb` entity), eyes of ender (fly toward the nearest `stronghold` plan, 80% drop back), chorus fruit (heal 4 + chorusTeleport), snowballs (the creatures' `snow` projectile), chorus plants coming down when cut, "a dragon keeps watch" first-visit toast. Terrain, portals, eyes in frames, void crossings and the void winds were already ported (gen/theend.cpp, sim/portals, sim/seamless). |
| game/endmobs.js | end_fight.cpp | endermen (meeting their eyes — a carved pumpkin hides you —, blinking, arrows/projectiles never land, water and rain burn, wandering, spawning: End cap 12 / 0.7, Depths 0.04, Overworld nights 0.05 in the dark), end crystals (heal the dragon within 64, the beam motes, explode on breaking, `end.crystalsGone`), the Ender Dragon (circle / strafe with void breath / charge / climb / perch on the exit pillar, unstick), called-back fights (`end.again`, hearth.js) with the smaller drop table |
| game/dragons.js | dragons.cpp | dragon kinds + breath (fire ignites, poison, frost chills/slows, lightning forks, water pushes, void), lair dragons spawned at the structures workstream's `dragon_lair` plans (sleep → takeoff → circle → breath/dive/land, waking when you come within 22 unless sneaking past 10, or when hit), `dragons.<lairId>.slain`, eggs (`eggs[]`: warmth/water within 2, 300 s, cracks, hatches a tamed baby drake for the owner), drakes (feeding speeds growth by 150 s, saddle, flying ride: forward goes where you look, jump climbs, sprint races, sneak in the air flies down), `RIG_ANIM.dragon` (wing beat with lagging fingers, banking, glide, hover, folded wings on the ground, sleeping pose, jaw), `RIG_ANIM.crystal`, the 0.9.0.76 hit volumes (`dragonRayT`, `dragonHas`), batchb.js egg scan + egg countdown tags |
| game/deepdark.js (+ deephalls.js gameplay) | deepdark.cpp | vibrations (footsteps not sneaking, landings, blocks changed near someone; the Echo Charm silences you), sensors (1 s cool-down) → shriekers (10 s), the darkness (`darkT`, 12 s per shriek, 3 s near a Warden), `deepWarn` levels and their toasts, the Warden's summoning from a summoner shrieker (meta & 1), its emerge/dig, hearing + smelling with error growing with distance and rock, anger decay, heartbeat, sonic boom through stone (spread, softened by rock), swipe, sniffing, catalyst spreading sculk where something dies; Deep Halls: dart traps, keys in gates (itemUse on `dungeon_gate`), the lever puzzle, the Gaoler (ogre brain) with the silver key, the Lich King's vault once `structs.<id>.cleared`; Twin Vault: both plates, the Sun and Moon Wardens (Cinder Golem brain) rising again after 15 s alone |
| game/haunt.js | haunt.cpp | the watcher (`watcher` entity, this machine only): far / peek (beside a trunk) / behind / cave spots, vanishing when looked at or come close (or walking off into the fog), noticing, `flags.watcherSeen`, the whispered "stop", traces (tunnels with redstone torches, signs, stripped trees, swapped torches; pocket dimension: only by its portals), the haunting game rule, boss fights keep him away; `/herobrine [trace]` is gameplay's command (sim/gamecommands.cpp, hidden, always "Removed Herobrine.") — it emits `herobrine [name, "trace"|""]`, answered by EndStory |
| game/riftarc.js | rift.cpp | story state (`rift`), stability, the catalyst (first village from `stats.vseen` / `vfound` / where someone stands; later the nearest), the tears as virtual portals (window, gateway, the realm's exit, the arena's way home after sealing), his doings (doorways + doors left open, micro rifts with him or rift creatures, phantom sounds, freezing time), keepers' fragments (`data.dungeon`) and rift crystal after the gate, using the rift with four fragments, his realm (hall crowds, him in the last hall keeping his wounds, calls at 2/3 and 1/3, blinks, rift bolts, the freezing stare), sealing (drops, xp, `villageRep` event), `/rift open|frag|gate|go|seal|reset` (cheat), the Overworld sky going wrong under 15 % stability (render/sky.cpp) |
| game/riftlore.js | rift.cpp | RIFT_LORE / REACT / GREET / NEWS / CALL as data; `riftAsk`, `riftAskLabel`, `riftGreeting`, `riftNews`, `riftElderLabel/Line`, `riftCorruptSay` for the villagers' talk screen; the village call on `villageEnter` |

## APIs (sim/endstory.h)
- Story: `riftRec`, `riftStability`, `riftTier`, `riftOpenAt(lv, villageId, name, at, yaw)`, `riftOpenGate`, `riftTravel(lv, p, toRealm)`.
- Villagers (no UI here): `riftAsk(lv, asker, job, jobsInVillage)` → `{text, toast, sound}`; labels/lines above. Events the
  villagers emit: `villageEnter [playerName, villageId, nearestFolkName]`; events emitted here: `riftChanged [rift]`,
  `villageRep [playerName, villageId, 60]` (after sealing).
- End: `endRec`, `endGatewaySpot(k)`, `chorusTeleport`. Dragons: `isDragon`, `dragonRayT`, `dragonHas` (the attack ray).
- Deep Dark: `vibrate(lv, realm, pos, sourceId, "step"|"land"|"block")` for any noise.
- Watcher: `hauntDimOk`, `hauntForceSighting`, `hauntLeaveTrace`.
- Creature framework hooks (added to sim/creature.h): `setCreatureHooks(type, {brain, beforeHurt, afterHurt, onUse})`;
  `registerRigAnim(rig, fn)` (sim/creature_rig.h); `Creature::breathT/hoverT`; `RideInput::descend`; `data.noDrops`.
- Player data for the controller/HUD: `darkT`, `dreadT`, `dreadAll`, `frozenT` (> 0: ignore movement input).
- Contracts used: `itemUse` (pearls, eyes, chorus fruit, snowballs, keys in gates, the rift — any item, also "" — near
  the tear), `mobDeath`, block changes (WorldSystem hook per realm), `contentHooks().villageNear/villageById`.
  Structure plans read by key with the original's data shapes: `dragon_lair` {type, at}, `deep_halls`, `twin_vault`,
  `village` {id, name, x, y, z, blds[].door/H}, `stronghold` (centre) — y in plan data is band-local.
  Dungeon keepers must carry `data.dungeon` (the dungeon id); structure bosses set `structs.<id>.cleared`.

## Differences
- The tears are true windows into his realm (portal views) instead of a flat picture; the gateway is walked through
  seamlessly (you come out at the realm's exit tear rather than 10 blocks in). `/rift go` still teleports.
- The dragon's mouth is estimated from its size and heading (the server never poses bodies).
- Breath particles are sent as a few bursts per tick instead of one spark each.
- Freezing time holds creatures still (`calm`) and stops players' horizontal motion; entities still tick.
- The haunt runs on the host for its own (non-ghost) players; a remote client gets no sightings yet.
- The original has no endermen carrying blocks and no shulkers (endmobs.js has neither), so neither is here.
- Lair `book` (land already played on keeps no lair) is the structures workstream's.

## Left to do
- Gameplay (sim/controller.cpp already emits `itemUse`, "" for an empty hand): honour `frozenT`; use `dragonRayT` for
  attacks on boss dragons; carry the rider at the drake's `seatY`; re-point `hurtPawn` at sim/combat.h if mob_combat goes.
- Villagers: call `riftAsk` & co. from the talk screen, emit `villageEnter`, handle `villageRep`.
- Network: dragon snapshots (`dg`), the watcher and rift events for guests (host-only now).
- The Lich King / hollow archmage brain and the structure-boss spawner (structures.js / dungeons.js gameplay).
- The quest track box is drawn here with only the story's line; a shared quest-track widget would hold the rest.

## Changes outside this area
- sim/creature.{h,cpp}: `CreatureHooks` (brain / beforeHurt / afterHurt / onUse per type), `hurt` split into hooks + `hurtBase`,
  `breathT/hoverT`, `data.noDrops`; sim/creature_beasts.cpp: the onUse hook, `RideInput::descend` (sneak in the air on a
  flying mount); sim/creature_rig.{h,cpp}: `registerRigAnim`; sim/commands.{h,cpp}: `hidden` + `hideChatCommand`;
  sim/rpg.cpp: `data.dungeon`/`data.lair` may be ids (not only `true`); render/sky.cpp: the Overworld sky under 15 %
  stability; client/client_fx.cpp: sleeping / tamed bosses don't start battle music; core/init.*, client/session.cpp,
  CMakeLists.txt: registration + the test.

## Test
`./build/test_endstory` (ctest `endstory`): the fight (crystals, healing, flight, breaking crystals, slaying, exit
portal, egg, gateway + travel), endermen (arrows, stare), pearls and chorus fruit, a frost dragon waking and
breathing, eggs hatching a drake, feeding/saddling/flying it, hit volumes, the sculk's four shrieks and the Warden,
the hidden command, the whole story (rift, fragment, gate, realm, crowds, him, sealing), no phantoms.
