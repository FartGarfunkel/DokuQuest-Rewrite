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
