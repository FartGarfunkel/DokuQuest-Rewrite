# DokuQuest — the complete plan from Saturday, October 31 2026

> **Run order:** see docs/SATURDAY_PLAN.md (supersedes Part A below; Parts B and C still apply).

Everything we're adding or fixing, in the order it runs. Work starts automatically after the weekly reset (12:05 PM Toronto).
Items 1–5 are realistic for Saturday; the big updates (6–9) run into Sunday and beyond, and anything unfinished carries
over in this order. Standing rules: no hunger, no phantoms, full controller AND keyboard/mouse in every menu, patch notes
never mention Herobrine and always end with "Removed Herobrine.", resource-pack art is never committed.

Part A is the plan. Part B is the full classic-era content update detail. Part C is the full bug/feature queue the bug test works through.

---

# Part A — the plan

## 1. Big bug test and fixes
- Find and fix everything in the game, and make sure every system is connected and works, menus to mobs to saving.
- **No more clipping (top priority):** players and mobs must never end up inside blocks. alpha.5.1's push-out fix gets
  tested properly with an automated test that walks and knocks players and mobs into walls, ladders and path blocks.
  Known cases: clipping into the block behind a ladder while climbing, getting hung up on dirt path blocks.
- Every bug you report before Saturday.
- Optional: crows landing on scarecrows.

## 2. Game-only updates (like the HTML version)
- Each release also ships a small game-only package; the launcher downloads it into your DokuQuest folder (versions/).
- The launcher only updates itself when the launcher itself changes (its own version number).
- Older game versions stay installed: pick or roll back to one from the launcher. Works offline, Linux and Windows.

## 3. Performance
- **Cave culling:** chunks hidden behind solid rock are skipped even when they're in view, so
  underground only the cave you're in is drawn. Portal windows keep working.
- **Terrain memory diet:** Ultra used 4–9 GB of graphics memory in the Nether (~260 KB per chunk section vs ~80 KB in
  tests without the pack). Find what bloats the meshes (GeruDoku relief, 3D plant detail everywhere on Ultra, merging,
  fragmentation), cut it down, consider a compact vertex format and a memory budget that drops the farthest chunks first.

## 4. Feel and polish
- **Sound overhaul:** match the HTML game event by event — which sounds play, the resource-pack sounds used, the synth
  fallbacks, volumes, distance falloff, cave reverb/echo, footsteps per material, mining/breaking/placing (mining was
  faint and distant), mobs, UI clicks, ambience and music.
- **Particle overhaul:** everything the original emits — held and placed torches/lanterns/campfires (the held torch shows
  no flame or smoke and no smoke trail when you move), block breaking, footsteps, water/lava, mobs, spells, portals, weather.
- **Bows (third person, players and skeletons):** held by the middle grip with the string toward the archer, upright like
  a real bow (they're held backwards now); a proper draw — string hand to the cheek, bow arm forward, release — matching
  the original's stages; correct scale in the hand. First-person bow checked too.
- **Elytra:**
  - Classic gliding: pitch-based lift and drag, speed from diving, lost climbing, smooth turning, firework boosts.
  - Sits properly on the upper back (it's too high now): folded at rest, spread and swept back when gliding.
  - Catches our weather wind: headwinds give lift, tailwinds push you faster, crosswinds drift you, strong gusts give
    updrafts to ride; stronger up high and over open land/sea, subtle in calm weather.
- **End gateways usable:** throwing an ender pearl into a gateway takes you through (the opening is one
  block tall, so walking in is awkward).

## 5. RPG levelling (port of the HTML version)
- Tools, weapons and armour gain XP from use (mining, hits, damage taken) and level up with bonuses, exactly like the
  original: tooltips, level-up toast/sound/particles, saved with the item, any player skills the original has.
- Ties into the magic overhaul (spells level up the same way).

## 6. Rift story update
- **10 Realm Fragments** (up from 4), the creepypasta slow burn: small wrong things first, more and stranger as you go.
- **Corrupted mobs:** existing hostile mobs AND ordinary animals get Herobrine's corruption — the purplish glitch look of
  his realm and white eyes, as if he's watching through them. More fragments = more of them; he also brings them over
  through his portals, like the one in town.
- **Late-stage portal incursions.**
- **Nether fortress foreshadowing:** he shows up while you're in a fortress, opens a portal and pulls a wither skeleton
  into it.
- **Climax:** he places the last wither skull and starts a **Wither Storm** (story only, our own design).

## 7. Classic-era content update (full detail in Part B)
- Everything in the wishlist (Part B): biomes, blocks, items, mobs, structures, the classic Nether, music ideas and
  other features, in our own Doku designs. Decisions: seasons optional or mainly in the Seasonal Forest; a real lead item
  (no fishing rod).
- **Biome fuseling variants** (our creeper), one per biome, our own designs.
- **Biome iron golems:** articulated, glowing eyes, changing with the biome like the fuselings; golems in villages.
- **Wither boss** — player-summoned only (never natural), plus the story's Wither Storm.
- **Mob heads/skulls** and **spawn eggs** for every mob.
- **End overhaul:** proper End cities (towers, bridges, end ships with elytra and dragon head, shulkers, loot — the current
  ones are awful); outer End terrain in our own design (towering spires, arches, floating chunks, crater
  fields, varied end-stone biomes); End atmosphere, particles and ambience.
- **Skylands overhaul:** floating island shapes, holystone, aercloud types, skyroot
  and golden oak trees, quicksoil, Skylands plants, ores and dungeons (bronze/silver/gold), Skylands mobs, the bright floaty
  feel — our own designs, alongside the existing Skylands content.
- **Boss battle music** from the HTML version back for every boss fight: dragon, keepers, Wither and the rest.

## 8. Storybook-style animation
- A shared animation layer for a storybook feel: fluid, expressive movement for NPCs, mobs and players.

## 9. Magic overhaul
- Spell hands: the same spell in both hands combines into a stronger one; two different spells mix; a sword in
  one hand and a spell in the other. Spells still have to be unlocked.
- Spells level up with use, like tools, weapons and armour.
- Villagers trade in **gold coins** instead of emeralds, so emeralds become a better-balanced ore.

## Backlog (after all of the above)
- Original "over" and "under" synthesised music sets for every part of the world (more variety).
- An Options screen laid out like the launcher's options.
- Other holiday events from the PC clock (Christmas in December and so on), villagers decorating their towns.

---

# Part B — the classic-era content update in full

The full list lives in WISHLIST.md (the feature wishlist: what each thing is and does, in our own words, with its
status). Decisions (user, 2026-10-25): seasons optional, or kept mainly to the Seasonal Forest biome; a real lead item
(tie it to an animal, it follows you, tie it to a fence), not the fishing rod.

---

# Part C — the bug and feature queue (the bug test checks every item; some are already done)

## DokuQuest C++ — bug and feature queue (after 1.0.0-alpha.3)

Reported by the user while testing alpha.2/alpha.3, not yet done. Work top to bottom; check each in the running game (scripted playtests, `dq-sweep`).

### Rendering
- Shelves (standing and wall) don't show their items like the original (furnish.js / furniture shelves) — probably the same missing block-entity item drawing that item piles and frames had (render/pile_layer.cpp can draw them).
- Rain puddles never dry up after the rain stops (user, alpha.3). Check the original weather.js wet/puddle timers; also check snow cover melting and ice thawing over time.
- Voxel displacement like blog.danielschroeder.me/blog/voxel-displacement-modernizing-retro-3d (the target look the user chose): continuous heights per tile → geometry displaced only in WHOLE micro-voxel steps where the height change is large (mortar, gems, bricks, chest latch), while all finer detail goes only into a smooth normal map computed from the exact heights (sub-voxel normals light the surface). Bake heights + normal maps once per tile set (resource pack) at load. Sharp block edges kept crisp. No AO/shadows needed for that look; add later.
- References the user picked (read before starting): Daniel Schroeder's three posts — voxel-displacement-modernizing-retro-3d, voxel-displacement-where-this-goes, voxel-renderer-objects-and-animation (blog.danielschroeder.me/blog/) — and John Lin's voxel sandbox (shacknews.com/cortex/article/249/john-lins-voxel-experiments). Take from them:
  * per-voxel shading attributes (normals) with a spread of normals on thin features to fake curvature, and bent-normal-style self-shadowing in crevices (cheap stand-in for AO);
  * characters/items/creatures: voxelize each animation frame (or our rig parts) at the SAME voxel size as the world, never scaled voxels; only surface voxels kept;
  * forward lighting with a handful of point lights (our dynamic lights) + ambient; shadows later;
  * John Lin: soft, noiseless lighting (GI/path-traced look) and real-time water simulation — aim for a cheap approximation (sky-visibility AO + light bounce tint), not path tracing, given the HD 530 target.
- Teardown / Daniel Schroeder style voxel look: smooth derived height field, per-voxel normals + ambient occlusion in the terrain shader (both renderers, keep Legacy fast on HD 530), features that stick out (chest latch/corners, crafting-table tools hung on the sides) vs seams that sink; voxel spray particles for rivers, waterfalls and splashes.
- Rain puddles: flat, glossy, water-like (no relief) — if fix/render2 didn't finish it.
- Dynamic light from held items, off hand, belt hook and backpack lanterns, glowing dropped items, lit TNT, fireballs (original render/gl.js dynLight: up to 8 lights, uDyn[8]).
- Spawners show the spinning mob inside.
- Enchanted items: glint on items in hand/inventory/ground and the enchantment names in tooltips.
- Tool and weapon levelling shown like the original.
- Chimneys working like the original (smoke).
- Particles: verify torches, campfires, splash potions now that sim particles reach the client (ClientFx::particles was a stub until alpha.3).

- Shader packs: the user reports they don't appear to work. Today only DokuQuest-format packs (docs/shader-packs.md: files that override assets/shaders/*.glsl) load; full shader packs in the common pack format (shaders/*.fsh/.vsh, gbuffers_*, composite) are not supported yet — that's the roadmap's shader-pack pipeline. First check a DokuQuest-format pack end to end in Options, then plan the full-pack loader.

### Items and animation
- Mug of ale no longer looks like a mug (likely the alpha.3 "3D icons from block shape boxes" change in assets/item_models.cpp/icons.cpp overriding special item models — items with their own 3D model must keep it).
- Fishing rod (user): the lure hangs down in front of the rod when not cast; when cast, the line runs out to the bobber in the water until reeled back in.
- Creatures swim properly (float at the surface and paddle steadily) instead of bobbing and thrashing.
- Nether portal: the Nether (and the Overworld on the way back) stays invisible after walking through until save + reload. Not the realm-change bug fixed in alpha.3 (that was the pocket/rift). Reproduce with tools/playtests/portal.json (the scripted portal didn't light — fix the script first: flint and steel on the frame's inside bottom block).
- Bow draw animation (first and third person).
- Fishing rod: better 3D model, cast/reel animation, line and bobber.
- Splash and lingering potions: throw, effect cloud, particles.
- Held items too low in the hand (first person) — check after fix/render2's scale fix.

### Characters and villagers
- 4-player split-screen (original game.js locals/ctxs, worlds.js multi-context).
- Swimming animation (if fix/chars2 didn't finish).
- Village gates swing instead of appearing/disappearing.
- Villagers' letter-by-letter babble voice.
- Villagers, dragons and the watcher synced over the network.
- Knight/Ranger talents hooked into combat.

### Systems
- Sneak + use must work with BOTH keyboard/mouse and controller (the user uses both). dq-sweep's test (PlayerController, sneak flag) places on furnace/crafting table/barrel; check the real input paths: Shift + right mouse, and the pad's sneak (latch/tap in input.cpp) + use trigger.
- Sneak + use with a block in hand on chests, furnaces, crafting tables etc. opens them instead of placing the block (user, alpha.3). sim/controller.cpp use() has the furnish.js rule (line ~420) — check whether an earlier "itemUse" handler consumes the use, or p.sneaking isn't set at that moment.
- DUPE (high): taking armour off and putting it back in the inventory duplicates items (user, alpha.3). Not reproduced yet. Suspects: two InvSessions alive at once (the screen opened twice) writing stale back/cape/hook copies through InvSession::changed/store; the 3D doll's equip/hide buttons (ui/sheet.cpp, ui/inventory.cpp H_VIS); cape stored both in Player::cape and p.data["cape"]. Write a headless InvSession test (equip → click armour slot → click main slot → count items) and fix.
- Portal: confirm worlds no longer vanish after crossing (fix in alpha.3: remesh after realm change). If it still happens through Nether portals, look at band switching in render/world_renderer.cpp drawPass and portal_view.cpp.
- Block sweep extensions: use every block with every tool, check drops against the original's drop tables.

### Next after alpha.4 (user, 2026-10-25)
- A start-to-finish playthrough check (scripted where possible): new world → village → tools/armour → Nether → fortress → End → dragon → the rift story to the end; note everything that breaks.
- The rift story: 4 → 10 Realm Fragments, each fed to the rift by hand, the rift growing each time. Slow-burn creepypasta pacing like the "I saw him in my world" stories: early signs rare and small (torches you didn't place or dimmed, a figure at the edge of fog that's gone when you look, footsteps with no source, doors left open, wrong signs, 2x2 tunnels, leafless trees), never announced. He is mostly harmless to the player directly but interacts with the world.
- Late stage (close to 10 fragments): he opens portals like the rift in town — anywhere: mines, caves, any land — walks through from his realm with corrupted hostile mobs AND corrupted ordinary animals (cows etc. that attack for him), then walks back through and the portal closes.
- Patch notes never hint at him; they still end with "Removed Herobrine."

- Classic-era content (user, 2026-10-25): biomes, blocks, the classic Nether, mobs we lack, music in the same mood. Build OUR versions in DokuQuest style — Doku tiles/micro-voxels, our own synth music.

- More music, everywhere (user, 2026-10-25): OUR OWN procedural pieces: soft piano, warm pads, lots of space. An "over" set (bright, nostalgic, per biome: plains, forest, snow, desert, ocean, village) and an "under" set (caves, deep mines, moss/lush caves, karst, torch-lit tunnels, foggy depths), plus Nether, Skylands and End sets. Seeded variation so each world sounds different. Original melodies only.

- In-game Options laid out like the launcher's options (user, 2026-10-25): after the weekly reset.

- Holiday events from the PC clock (user, 2026-10-25): October = Halloween (zombies and skeletons in pumpkin / jack-o'-lantern heads, a hostile pumpkin creature that stands up on a vine body, pumpkins sprouting, spooky decorations), December = Christmas, and so on for each holiday; villagers decorate their towns for the current event. Our own designs. DONE for October (common/holiday.h, DQ_EVENT=halloween forces it, Options > Holiday events / settings.json "holidayEvents" switches it off): pumpkin and jack-o'-lantern heads on ghouls, skeletons and 30% of villagers (a few villager capes), 3x pumpkin patches, jack-o'-lanterns on village lamps and porches, harvest moon, foggier nights, wisps, more bats, trick-or-treat villagers. STILL QUEUED: the hostile pumpkin creature, the scarecrow, other holidays.

- Saturday Oct 31 after the reset (user): 1) Halloween creatures and costumes, 2) a large-scale bug test, finding and fixing everything in game and making sure it's all connected and working, 3) then the classic-era content update.

- Biome fuseling variants (user, 2026-10-26, with the classic-era content update): one per biome, built from that biome's blocks (terracotta and gold, bamboo, mossy sandstone, gravel, cactus-topped sandstone, mud with a spike, snow, mossy cobble, flowering azalea, mushroom cap, terracotta totem, swamp). Our own designs.
- After the content update (user, 2026-10-26): a storybook animation feel for players, NPCs and mobs. That means blended transitions, follow-through on capes, hair and tails, anticipation and weight, head and eye look-at, feet planted on slopes, varied idles, expressive talking faces and smooth turning, all as one shared animation layer over the existing rigs.

- Biome golems (user, 2026-10-26, with the biome fuselings): the village iron golem built from local materials per biome (sandstone, mossy cobble, terracotta, packed snow and ice, spruce and stone...), more articulated (elbow and knee joints, segmented limbs, hands), glowing eyes. Our own designs.

- Story beats (user, 2026-10-26):
  - Nether foreshadowing: while players are in a fortress, he appears, opens a portal, grabs a wither skeleton and pulls it into his portal.
  - The climax: we see him place the last wither skull, starting a Wither Storm. It is a campaign-only boss, never natural spawning: a giant, growing three-headed wither that tears up and pulls in blocks. Our own design.
- With the content update (user, 2026-10-26):
  - the Wither boss, summoned by players with soul sand and 3 wither skeleton skulls, never natural;
  - mob heads/skulls, as drops, wearable and placeable;
  - spawn eggs for every creature.

- Corrupted mobs (user, 2026-10-26, story update): corrupt existing mobs, hostile and ordinary animals alike.
  - Look: his realm's purplish glitch (flickering purple/black texels, a slight jitter) and pure white eyes, as if he watches or controls them through them.
  - Slow burn tied to fragments: almost none early, then rare far-off watchers, more and bolder as fragments are fed.
  - Late stage: he brings groups of them through portals like the rift in town, then walks back and the portal closes.

- Magic overhaul (user, 2026-10-26): keep the ways to unlock spells.
  - Spell hands: a spell in either hand, the same spell in both hands to combine it into a stronger one, two different spells to mix their effects, or a weapon/tool in one hand and a spell in the off hand.
  - One button per hand, on both mouse and controller.
  - Spells level up with use, like gear, tools and weapons.
- Economy (user, 2026-10-26): villagers trade in gold coins instead of emeralds, so emerald becomes a better-balanced ore. Keep or convert old saves' emerald trades.

- Bugs (user, alpha.4.10, 2026-10-26):
  - Still clipping into blocks sometimes when climbing a ladder.
  - Getting hung up on dirt path blocks sometimes (step-up across the 15/16 path edge, diagonals?).
  - SEVERE: deep underground (Overworld band-local y≈42), the near world stops rendering. Only the sky, the far LOD land and entities show, on Legacy GL 4.6 / AMD RX 6950 XT. Debug overlay: chunks 1103, sections 1406/7669, mesh jobs 0, light 0. Not reproduced in a carved test room at y≈28 with render distance 4. Suspects: near meshes dropped (setWorld/generation counter, a portal view), the band index, the LOD hole. Ask for latest.log with DQ_DEBUG_BANDS=1.
  - Fixed: the quest tracker now sits under the story line instead of on top of it.

### Roadmap (after the bugs)
Start in the wild, potions stack to 16, test-build small fixes, coins/economy, roads and bridges, travellers, saddle bags, bigger ship, poses, torches on fences, chains/lanterns, rift to 10 fragments + slow pacing + lore, graphics presets, built-in shader levels, Skylands content, Shadowbloom ports, full shader packs.
