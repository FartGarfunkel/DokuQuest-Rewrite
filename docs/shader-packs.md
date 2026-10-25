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
same `in`/`out`/uniform names, since the game sets those by name.
