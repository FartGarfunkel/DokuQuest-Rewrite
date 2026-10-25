# DokuQuest C++ — bug and feature queue (after 1.0.0-alpha.3)

Reported by the user while testing alpha.2/alpha.3, not yet done. Work top to bottom; check each in the running game (scripted playtests, `dq-sweep`).

## Rendering
- Voxel displacement like blog.danielschroeder.me/blog/voxel-displacement-modernizing-retro-3d (the target look the user chose): continuous heights per tile → geometry displaced only in WHOLE micro-voxel steps where the height change is large (mortar, gems, bricks, chest latch), while all finer detail goes only into a smooth normal map computed from the exact heights (sub-voxel normals light the surface). Bake heights + normal maps once per tile set (resource pack) at load. Sharp block edges kept crisp. No AO/shadows needed for that look; add later.
- Teardown / Daniel Schroeder style voxel look: smooth derived height field, per-voxel normals + ambient occlusion in the terrain shader (both renderers, keep Legacy fast on HD 530), features that stick out (chest latch/corners, crafting-table tools hung on the sides) vs seams that sink; voxel spray particles for rivers, waterfalls and splashes.
- Rain puddles: flat, glossy, water-like (no relief) — if fix/render2 didn't finish it.
- Dynamic light from held items, off hand, belt hook and backpack lanterns, glowing dropped items, lit TNT, fireballs (original render/gl.js dynLight: up to 8 lights, uDyn[8]).
- Spawners show the spinning mob inside.
- Enchanted items: glint on items in hand/inventory/ground and the enchantment names in tooltips.
- Tool and weapon levelling shown like the original.
- Chimneys working like the original (smoke).
- Particles: verify torches, campfires, splash potions now that sim particles reach the client (ClientFx::particles was a stub until alpha.3).

## Items and animation
- Bow draw animation (first and third person).
- Fishing rod: better 3D model, cast/reel animation, line and bobber.
- Splash and lingering potions: throw, effect cloud, particles.
- Held items too low in the hand (first person) — check after fix/render2's scale fix.

## Characters and villagers
- 4-player split-screen (original game.js locals/ctxs, worlds.js multi-context).
- Swimming animation (if fix/chars2 didn't finish).
- Village gates swing instead of appearing/disappearing.
- Villagers' letter-by-letter babble voice.
- Villagers, dragons and the watcher synced over the network.
- Knight/Ranger talents hooked into combat.

## Systems
- Portal: confirm worlds no longer vanish after crossing (fix in alpha.3: remesh after realm change). If it still happens through Nether portals, look at band switching in render/world_renderer.cpp drawPass and portal_view.cpp.
- Block sweep extensions: use every block with every tool, check drops against the original's drop tables.

## Roadmap (after the bugs)
Start in the wild, potions stack to 16, test-build small fixes, coins/economy, roads and bridges, travellers, saddle bags, bigger ship, poses, torches on fences, chains/lanterns, rift to 10 fragments + slow pacing + lore, graphics presets, built-in shader levels, Skylands content, Shadowbloom ports, Iris/OptiFine shader packs.
