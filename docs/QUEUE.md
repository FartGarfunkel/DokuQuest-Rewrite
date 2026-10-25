# DokuQuest C++ — bug and feature queue (after 1.0.0-alpha.3)

Reported by the user while testing alpha.2/alpha.3, not yet done. Work top to bottom; check each in the running game (scripted playtests, `dq-sweep`).

## Rendering
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

- Shader packs: the user reports they don't appear to work. Today only DokuQuest-format packs (docs/shader-packs.md: files that override assets/shaders/*.glsl) load; Minecraft Iris/OptiFine packs (shaders/*.fsh/.vsh, gbuffers_*, composite) are not supported yet — that's the roadmap's shader-pack pipeline. First check a DokuQuest-format pack end to end in Options, then plan the Iris/OptiFine loader.

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

## Next after alpha.4 (user, 2026-10-25)
- A start-to-finish playthrough check (scripted where possible): new world → village → tools/armour → Nether → fortress → End → dragon → the rift story to the end; note everything that breaks.
- The rift story: 4 → 10 Realm Fragments, each fed to the rift by hand, the rift growing each time. Slow-burn creepypasta pacing like the "I saw him in my world" stories: early signs rare and small (torches you didn't place or dimmed, a figure at the edge of fog that's gone when you look, footsteps with no source, doors left open, wrong signs, 2x2 tunnels, leafless trees), never announced. He is mostly harmless to the player directly but interacts with the world.
- Late stage (close to 10 fragments): he opens portals like the rift in town — anywhere: mines, caves, any land — walks through from his realm with corrupted hostile mobs AND corrupted ordinary animals (cows etc. that attack for him), then walks back through and the portal closes.
- Patch notes never hint at him; they still end with "Removed Herobrine."

## Roadmap (after the bugs)
Start in the wild, potions stack to 16, test-build small fixes, coins/economy, roads and bridges, travellers, saddle bags, bigger ship, poses, torches on fences, chains/lanterns, rift to 10 fragments + slow pacing + lore, graphics presets, built-in shader levels, Skylands content, Shadowbloom ports, Iris/OptiFine shader packs.
