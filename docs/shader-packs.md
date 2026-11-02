# Shader packs

DokuQuest's look comes from a handful of GLSL files in `assets/shaders/` (one file per shader, both
stages in it behind `#ifdef VERTEX` / `#ifdef FRAGMENT`, GLSL 330 subset; the game puts the `#version`
line and `DQ_MODERN` in front). A shader pack replaces any of them.

## Installing one
- A shader pack is a **folder** in `<data>/shaderpacks/` (`~/.local/share/DokuQuest/shaderpacks` on Linux,
  `%APPDATA%\DokuQuest\shaderpacks` on Windows).
- Pick it in **Settings → Shader pack** in the game (it switches at once) or in the launcher's Options.
- Any file in the folder named like an engine shader is used instead of the game's own:
  `chunk.glsl` (terrain, water), `distant.glsl` (far terrain), `sky.glsl`, `clouds.glsl`, `voxel.glsl`
  (items, held things), `creature.glsl`, `particles.glsl`, `portal.glsl`, `content.glsl`, and
  `common.glsl` (lighting, fog, wind, shared by all of them). Files the pack doesn't have stay as they are.
- `#include "builtin:<file>"` always pulls in the game's own version of a file, so a pack can wrap a
  shader instead of copying all of it.
- If a pack's file doesn't compile, that one shader falls back to the game's own and the log
  (`<data>/latest.log`) says so; the log also lists which files each pack replaces when it's chosen.

## The hook for small packs: `DQ_GRADE`
Every world shader passes its final colour through `DQ_GRADE(c)` (defined in `common.glsl` as `(c)`).
A pack can define it before including the game's `common.glsl` and change the whole look in two lines.

### Example: "Warm Evening" (a complete pack)
`shaderpacks/WarmEvening/common.glsl`:
```glsl
// Warm Evening — a two-line DokuQuest shader pack: a warm, faded film grade over the whole world.
#define DQ_GRADE(c) (mix((c), vec3(dot((c), vec3(0.299, 0.587, 0.114))) * vec3(1.12, 0.97, 0.78), 0.5))
#include "builtin:common.glsl"
```
Choose "WarmEvening" in Settings → Shader pack: terrain, sky, clouds, far land, items and creatures all
take the sepia grade; choose "None" to go back.

Bigger packs copy a file (say `clouds.glsl` or `chunk.glsl`) into the pack folder and edit it; keep the
same `in`/`out`/uniform names, since the game sets those by name. (alpha.12 changed `distant.glsl`'s inputs: the tint
and shade now come packed as `aRgb` (5-6-5) and `aPk`; a pack with its own copy needs the new one's top.)

## Iris / OptiFine packs
Drop the pack's `.zip` (or its unpacked folder, the one holding `shaders/`) into `shaderpacks/` and pick it
the same way. The game runs the pack's **post passes** — `shaders/composite.fsh`, `composite1..15.fsh` and
`final.fsh` (with their `.vsh`, and `world0/` versions first) — over its own picture, so a pack's colour
grading, tone mapping, bloom, depth of field, vignettes and film looks come through. Each pass gets
`colortex0`/`gcolor` (the picture so far), `depthtex0`/`depthtex1` (the depth), and the usual uniforms
(`viewWidth`, `viewHeight`, `frameTimeCounter`, `sunPosition`, `upPosition`, `gbufferProjection(Inverse)`,
`gbufferModelView(Inverse)`, `cameraPosition`, `worldTime`, `rainStrength`, `isEyeInWater`, `near`, `far`...).
The pack's `gbuffers_*`, `shadow` and `deferred` programs are **not** run — the game's own world shaders
draw the world — so whatever a pack does with its own G-buffers (its lighting, shadows, water,
reflections) isn't there, and a pass that reads `colortex1..7` sees black. Old `#version 120` GLSL is
translated (`texture2D`, `varying`, `gl_FragData`, `gl_TexCoord`, `ftransform()`...); a pass that still
won't compile is left out and the log says why.

## The built-in shaders (Settings → Shaders)
Off, Low (a film grade and a vignette), Medium (and bloom), High (and sun rays, a little sharpening),
Ultra (stronger bloom and longer rays). They run after a shader pack's passes, so the two go together.
`assets/shaders/post.glsl` holds them; a pack can replace it like any other file.
