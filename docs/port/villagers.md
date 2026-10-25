# Villagers: the people of the villages and towns

Workstream: villagers (`src/sim/villagers*.{h,cpp}`, `sim/villager_ai.cpp`, `sim/villager_quests.cpp`, `sim/villager_life.cpp`,
`sim/villager_look.cpp`, `src/render/villager_layer.*`, `src/ui/villager_screens.*`, `tools/test_villagers.cpp`, `data/villagers.json`).

## Ported modules

| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| game/villagers.js — who they are, standing, skins | sim/villagers.cpp, villager_look.cpp | NPC_JOBS/SHOPS/STANDING/ERRANDS/TALK... in `data/villagers.json`; npcName, npcSeedOf, npcVoice, standingOf, npcRep (daily caps for trade/defend); **npcSkinCanvas pixel for pixel** (same random sequence and rounding: the test compares the original's hash) |
| villagers.js — bringing a village to life (npcTick, npcSpawn, pens, "nothing spawns in a village") | villagers.cpp `npcTickVillages`, creature hook `naturalSpawn` | folk / guards (100+) / golems (200+), deaths remembered per village (someone new after 2 days, guards 1, golems 3), pens filled once, everyone goes when nobody is within r + 110 |
| villagers.js — npcPath, npcGo, doors, npcBrain, guards & golems, npcConfront/npcOffend/npcForgive, mutters | sim/villager_ai.cpp | their own A* (doors and gates, crops cost 4, water 3), opening and shutting doors behind them, the day (sleep at night in their bed, work in the morning — shopkeepers until 0.46 —, benches from 0.43, the square and the roads otherwise), greetings and waves, chats, working (hammer/hoe swings, reading, folded arms), panic, guards' watch and fights, the golem's slam |
| villagers.js — quests, the board, the shops, NpcTalk/NpcShop/QuestBoard, name tags, quest HUD | sim/villager_quests.cpp, ui/villager_screens.cpp, render/villager_layer.cpp | errands per villager per day, the board's two bounties + supplies, rewards (emeralds, xp, standing, skill), five jobs at most, 120 done kept; shops with standing tiers, Honoured/Hero discounts, 12 of each a day, restock each morning |
| towns3.js — npcStuckCheck, the gates (+ drawn shut: villager_layer) (open/shut with the evening bell, the watch opens at night, levers, using the gateway), the watch at the gates | villager_ai.cpp `npcStuckCheck`, villager_life.cpp gates, villager_ai.cpp guard posts | gates come from the structures' plan data (`gates`: cells, posts, lever...) |
| townlife.js | — | all world gen and cloth meshing (done by structures and render); nothing for the people |
| livingworld.js — raids, named foes, map marks | villager_life.cpp | raids at night (rift-scaled, rift creatures once it thins), won only when all are slain; named foes with their own numbers (a per-foe CreatureType copy), spawned near their haunt when the bounty holder comes close |
| hearth.js — the missing, a Hero's house | villager_life.cpp, villager_ai.cpp `lostAI` | the missing villager waits, waves, follows you home (teleports when left behind), is home within 14 of the square; the elder's house offer, homeSite search (the original's rings), builders retry 6 times. Armour sets / End crystals are not villager parts (not here) |
| paths.js — PathUI, Knight and Ranger talents, path told / points | villager_quests.cpp `path*`, ui PathScreen, villager_life.cpp | choosing at a guard/fletcher/librarian/elder, combat level 20, talent tiers, forgetting for 20 emeralds; the Knight's +4 max hp applied as a delta (`stats.pathHp`); Mage talents were already in sim/magic.cpp |
| riftlore.js — clues, reactions, greetings, news, the call as you walk in | villager_quests.cpp `riftAsk`, `home`, `news`; villager_life.cpp | the clue order (latest part of the story first), "ask the …" pointers to other jobs in the village |
| riftarc.js NpcTalk parts, rpg.js NpcTalk.say / scout & word quests, dungeons.js clear-the-dungeon, fairplay.js lapses | villager_quests.cpp | the elder's ★ rift button, the paper-thin voice, the elder's longer line; scout quests (structure index, ≤ 4096 regions), word to a neighbour, dungeon bounties (`info.data.dungeons[id].cleared`), lapse after 7 days |
| together.js — shared jobs, party kills | villager_quests.cpp `questTake`, villagers.cpp `hookDied` | a job taken is shared with every other player in the level; kills for a shared bounty count for partners within 64 |

## How it fits
- **VillagerSystem** (LevelSystem, host / single player only; `lv.isRemote()` skips): spawning, gates, raids, elites, the missing,
  homes, per-player checks (welcome, lapses, rift calls, paths). Saved state is the original's: `info.data.villages[vid].dead/penned`,
  `raids`, `elites`, `lost`, `homeBuilt`, `homeSites`; player `stats.vrep/quests/qdone/vstock/vcap/vseen/vtip/homes/riftClues/riftCalls/path/pathTold/pathPts/pathHp/questsDone`.
- Villagers aren't saved: the first tick after a load removes any that a save carried (`data.npc`), and they come again.
- **Creature hooks** (`creatureHooks()` in sim/creature.h, added for this): `brain` (villagers and golems), `beforeHurt` (no friendly fire,
  the missing can't be hurt, Beast Friend), `afterHurt` (panic, the guard's word / the sword, Cleaving Arc), `died` (deaths remembered,
  bounties, party kills, defend standing, Warlord, raid kills, elites), `use` (talk / sleeping / the missing / the golem), `naturalSpawn`.
- **Villages** come from the Overworld's `StructureIndex` (type key `village`, `StructurePlan::data` with the original's plan fields,
  y band-local → global) and are parsed once per plan. `addVillage(lv, planJson)` adds one by hand (tests, `/villagers`).
- **The dialogue** is computed in sim (`Talk`, `talkOpen/talkChoose/talkShopTrade/...`) so the screens are thin and the server could run it.

## APIs for other areas
- `sim/villagers.h`: `villagesNear`, `villageById`, `villageAtPos`, `villageNeighbour`, `standingOf`, `npcRep`, `repOf`, `npcStats`,
  `boardQuests`, `npcQuestFor`, `questTake/Finish/Kill/Lapse/Have/Ready`, `boardJob`, `shopLines/shopTrade`, `combatLevel`,
  `npcSkinPixels(seed, job)`, `spawnDevVillagers`, `setHouseBuilder(fn)`.
- **Combat / gameplay hooks for the Knight and Ranger** (call from sim/combat.h when it lands): `pathMeleeFactor(p)` (melee damage),
  `pathArrowFactor(p, dist)` + `pathArrowPoisons(p)` (arrows), `pathQuiverSaves(p)` (arrow not used up), `pathSpeedFactor(p)` (Fleet Foot),
  `pathCheatDeath(lv, p)` (before a killing blow: true = stood on half a heart). Cleave, Warlord and Beast Friend run from the creature hooks.
  If gameplay recomputes `maxHp` from scratch it must add `pathMaxHealthBonus(p)` and drop the `stats.pathHp` delta logic in villager_life.cpp.
- **itemUse** (priority 5): the quest board block (not sneaking) opens the board; a town gate's lever toggles the gate (and the lever flips);
  using a `gate_barrier` opens the gate. Villagers are used through the controller's `useEntity` hook → `useOnCreature` → `creatureHooks().use`.
- **Content hooks wired**: `contentHooks().villageNear`, `villageById`, `placesNear` (every structure plan near, from the band's index),
  `dungeonById` (ids `d<rx>_<rz>`).
- Events: `villagerScreen` [player, "talk"|"board"|"path"|"close", args] (the client opens screens), `questHud` [player].
- Client: `registerVillagerClient()` (GameSession constructor); screens `makeVillagerTalkScreen(npc, confront, victim, shop)`,
  `makeQuestBoardScreen(at)`, `makePathScreen(hint)` (ui/villager_screens.h).

## Differences
- Babble: each line plays the villager voice (`LevelFx::voice("villager", "say", pos, pitch)` with the villager's own pitch) instead of
  the per-letter formant babble (needs a buffer-playback API in audio/; the original's BABBLE_V table is in the data for it).
- Roads: the plan data carries `roads` as {d, L, hw} (no cells): road spots are worked out along the road with the ground found at run
  time; if the structures add `cells` ([x, z, h]) they are used. Town side streets aren't in the data, so they aren't walked.
- The fountain radius (`fr`) isn't in the plan data (defaults to 1 for the plaza spots).
- The hero's house: the default builder is a plain house of the village's wood (bed, chest of gifts, torch, windows); the original
  raised VTEMPLATES.house_m — at merge, plug the structures' `townBuildingBuild` into `setHouseBuilder` (it writes through a ChunkWriter,
  so it needs a world-writing SP or a copy-per-chunk adapter).
- Player-placed block tracking (`placedAt`) doesn't exist yet: homeSite checks portals, block entities, containers and beds only.
- Raiders "head for the square" is a velocity push from the system tick (the original steered in Mob.update).
- Multiplayer: everything runs on the host; a guest's talk/shop/board would need `lv.net->action(...)` relays (not wired; creature
  snapshots don't carry `data.npc` yet, so guests see villagers as the default rig only once creatures replicate).
- Rendering: guards' armour uses the game's armour pieces (no 3D gear look for villagers); villagers have no capes or carried tools.

## Tests
`build/test_villagers` (66 checks, headless, all pass): data and the original's numbers (npcSeedOf, npcName, standingOf, npcVoice, the skin
painter's hash), a village from plan JSON, villageNear hook, villagers coming out (folk + guard, armour, names), work in the morning,
sleeping in bed at night, using a sleeping villager, the talk screen event, talk options (wares, Mage training, goodbye), the shop
(price, can't afford, buy, Friend-only lines, sell, 12 a day, restock), standing (trade cap, Friend toast, tier wares open), errands
and the board (deterministic per day, new each morning, hunt kills counted through the died hook, hand in, lapse, five jobs), an errand
through the dialogue, the guard's word / sorry / the sword, paths (combat level, too early, Knight hearts, talent tiers, forgetting),
the missing on the board, everyone going when you leave.
Visual: `DQ_VILLAGERS=farmer,librarian,guard,elder,baker DQ_DAYTIME=0.2 ./build/dokuquest --create T --smoke-frames 300 --smoke-shot x.png`
(+ `DQ_VILLAGER_OPEN=talk|shop|board|path`). Chat commands (cheats): `/villagers [jobs]`, `/vrep [n]`, `/raid`.

## Changes outside this area
- `sim/creature.h/.cpp`, `creature_beasts.cpp`, `creature_spawn.cpp`: `CreatureHooks` + 6 call sites (brain, beforeHurt, afterHurt, died, use, naturalSpawn).
- `sim/creature.h`, `render/char_anim.h`: their identical inline `wrapAngle` wrapped in `#ifndef DQ_WRAP_ANGLE` (both are included together).
- `render/creature_layer.cpp`: villagers (`T->npc`) are skipped (drawn as people); golems stay creatures.
- `render/char_anim.h/.cpp`: CharPose `headYaw, wave, talk, fold, read, hoe` + villagers.js's avatarAnim additions.
- `sim/gameplay.h` hooks: one `useEntity` hook that calls `useOnCreature` (nothing called it yet, so feeding/taming/shearing/riding
  from creatures now work too); `ui/sheet.cpp`: its `combatLevel` removed (the one in sim/villagers.h is used — one definition).
- `core/init.h/.cpp`: `registerVillagers()`; `client/session.cpp`: `registerVillagerClient()`; `CMakeLists.txt`: `test_villagers`.
- New data: `data/villagers.json` (`python3 tools/port/jsdump.py <src> data/villagers.json "$(cat tools/port/villagers_expr.js)"`).

## Left to do
- Babble synth; the gates' movement (towns3.js gateMeshes slid the portcullis up / swung the doors): render/villager_layer draws a
  static portcullis (iron bars) or timber doors (planks, iron bands, log posts) in every cell that holds a `gate_barrier`, so a shut
  gate shows and an open one is gone — no animation in between.
- Quest map marks (livingworld.js / hearth.js mapMarkers: jobs' `at` and your houses) — maps.cpp can read `stats.quests[].at` / `stats.homes`.
- The rpg.js story line at the top of the quest list; a guest's actions over the network.
