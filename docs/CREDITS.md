# Credits

Third-party code and ideas used in the DokuQuest C++ rewrite, besides the libraries in `third_party/` (each with its own
licence file there).

## Horizon (MIT)

<https://github.com/horizon-rust/horizon>, Copyright (c) 2026 Ludovic Marie and the Horizon contributors, MIT License.

The dreamlike lighting (`assets/shaders/dreamlight.glsl`, `render/dreamlight_layer.cpp`, and the warm low-sun halo in
`assets/shaders/sky.glsl`) follows Horizon's Golden Hour shader pack (`crates/horizon-shader-api/packs/golden_hour`): the
golden light when the sun is low, the soft shoulder for the brightest colours, the saturation and vignette, the twelve-tap
halo round the brightest pixels, and the sky's warm halo and deeper blue overhead. Rewritten in GLSL for this engine; the
soft-focus glow and lifted shadows are our own additions.

MIT License text (as in Horizon's LICENSE): Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE
SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

## Particle Rain (MIT)

<https://github.com/PigCart/particle-rain> (version 4.0.3), by pigcart, Copyright (c) 2025, MIT License.

The weather particles follow Particle Rain: how rain, snow and dust spawn on a sphere round the camera and on the ground
(`ParticleSpawner`), the gust field of the wind (`ParticleRain.getWind` → `sim/wind_field.cpp`), the particles' motion,
fading, collision and bouncing (`CustomParticle`, `WeatherParticle`, `StreakParticle` → `render/weather_sim.cpp`), the way
rain is stretched along its motion and fog sheets fade at grazing angles (`assets/shaders/weather.glsl`), the slicing of a
pack's `environment/rain.png` / `snow.png` and the ripple rings (`TextureUtil` → `render/weather_sprites.cpp`), and the
numbers in its `particles.json`. Rewritten in C++/GLSL for this engine. Its snow and sandstorm recordings were analysed into
synthesis profiles (`data/sounds.json`, events `fx.ws_weather_*`); no audio files are shipped.

MIT License text (as in Particle Rain's LICENSE): Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The
above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Cubyz (GPL-3.0) — ideas only

<https://github.com/PixelGuys/Cubyz>. No code is taken from Cubyz (its licence is GPL). Ideas only:

- tools and armour made of several materials, each part of the crafting grid colouring its part of the item and adding to
  its stats (`core/tool_parts.*`, the mixed tools and armour);
- the distant terrain (`render/distant*.cpp`, `assets/shaders/distant.glsl`), after Cubyz's LOD chunks (`chunk.zig`
  `updateFromLowerResolution`, `renderer/mesh_storage.zig`): the far land is voxels at every distance, coarser further
  off; a coarse voxel is solid if any finer voxel under it is, and takes the block that shows the most (air round it
  beats buried); each level is made from the finer one; the voxels are drawn with the blocks' own textures, one per
  voxel face; a coarse piece gives way to the finer ones only once all of them are ready, and faces toward a coarser
  neighbour are drawn so no crack shows; and the far land is made by the world generator itself (the chunks nobody has
  been to are generated in the background for it) and kept on disk. Written for our engine (quadtree tiles over 256-high bands, voxels one block
  high so heights stay exact, runs of voxels per column instead of 32-cubed chunks).

## Better than Adventure! — ideas only

<https://www.betterthanadventure.net>. No code, art or music is taken from BTA. Its features (the Nether's five lands,
the blocks of the BTA list) are our own designs and pictures made after its changelogs.

## Better End (MIT)

<https://github.com/quiqueck/BetterEnd>, Better End 3.2.5 by Quiqueck, paulevs, Bulldog83, Edos, Yuki, Seaward and Firel
(fabric.mod.json: "MIT (CC BY-NC-SA 4.0 for some Assets)"; the upstream LICENSE.ASSETS names only gui/infusion.png,
icon_updater.png and lang/de_de.json as CC BY-NC-SA, and none of those is used here). The End band follows it:

- the End's sky (`render/be_sky.cpp`, `assets/shaders/be_sky.glsl`): BetterEndSkyRenderer's layers, colours, rotation
  axes and star sets, with its sky pictures `assets/sky/betterend/{nebula_1,nebula_2,nebula_3,stars,fog}.png`;
- the End's fog (`render/sky.cpp`): each biome's fog colour and density, and BCLib's fog distances;
- the outer End's lands, surface rules, trees and plants (`gen/theend_be.cpp`), rebuilt from its biome and feature data;
- its blocks (`tools/port/betterend_blocks.py`): Better End's block pictures, doubled to 32 px and given the fine grain of
  the DokuQuest Base pack's nearest picture (`data/textures/block/be_*.png`).

Music and sounds are not used. MIT License (Better End): Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following
conditions: The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

## Better than Adventure! and The Aether — numbers only, no art

BTA 8.0's Nether (terrain version 7: `gen/nether_bta*.cpp`, `render/nether_grade_layer.cpp`) and The Aether's sky
dimension (terrain version 7: `gen/skylands_aether.cpp`, `gen/aether_noise.*`) are rebuilt from their generators'
numbers and algorithms (The Aether's code is LGPL-3.0). Their art is not licensed for sharing (BTA: no licence; The
Aether: "Assets: All Rights Reserved"), so the shipped pictures of their blocks are the DokuQuest Base pack's own,
recoloured to the mods' average colours (`tools/port/bta_nether_blocks.py`, `tools/port/aether_blocks.py`).
`tools/port/local_mod_art.py` makes a resource pack of the mods' real pictures, fitted to the base pack, from the player's
own copies of the mods, into their game folder only: it must never be committed, shipped or shared.

## Animation and 3D item references (used with permission)
The animation and item work was guided by these, studied with the permission the project owner obtained; no files from them
are shipped:
- **AnimatedPlayer** (thehippomaster): the per-state blend timers and the sprint, walk and face numbers (render/char_blend.cpp,
  render/char_poses.cpp, render/char_anim.cpp).
- **Punched Stuffs** (modpack, MIT): Just 3D Tools / Just 3D Stuffs (WinterS_) model proportions for our procedural tools
  (assets/item_parts.cpp); the Hold My Items animation scripts in those packs and Punchy's per-kind animation sets for the
  first-person hand (render/fp_motion.cpp).
- **Animations n' Stuff** (modpack, MIT): Not Enough Animations' moves (eating, drinking, ladders, the item swap) and the
  A Good Place sample pack's pop_in placement animation (render/place_pop.cpp).
