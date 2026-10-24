# Sound and music (src/audio, src/client/client_fx.*)

Every sound is generated on the fly. Nothing recorded ships: the resource-pack sounds are stored as small
synthesis **profiles** (`data/sounds.json` + `data/sounds.bin`) and resynthesised at run time; the original
game's own sounds are stored as the **recipes** its DSP kit ran (`data/sfx.json`) and rendered by a C++ port of
that kit. Data total: 1.81 MB (profiles 1.44 MB zlib + 43 KB index, recipes 330 KB).

## Ported modules
| JS | C++ | Notes |
|---|---|---|
| game/audio.js (Sfx: play, playAt, material, note, ok gaps, tone/noiseHit) | audio.cpp, dsp.cpp (`renderLive`, `renderNote`, `renderSpecial`) | all `case` sounds; note blocks by the block below |
| game/sfxlib.js (SndBuf kit, SND_MAT, SND_FX, takes, `sample` pitch/gain wobble, block → material) | dsp.cpp (`SndBuf`), sfxlib.cpp | 296 recipes, each take with the original's own numbers |
| game/sounds2.js (VOICES, CAVE_SOUNDS, WORLD_SOUNDS, voice/swell/click) | dsp.cpp (`voice`, `swell`), data `names.*.live` | WebAudio graphs rebuilt (AudioParam ramps, biquads in WebAudio's Q units) |
| game/voices3.js (throat, hiss, clack, room, shape; VOX) | dsp.cpp, data | glottal pulses + 4-formant tract + room |
| game/sound3.js (placeNode, flood-fill field, walls, room/cave reverb, cave mood, mob footsteps, doors, tree creak) | audio.cpp `place`, ambience.cpp | HRTF approximated by equal-power pan (far ear −13 dB) + duller from behind |
| game/blocksounds.js (wood/log recipes, pack wood for logs) | data + sfxlib.cpp `material` | block→material table already in data/blocks.json |
| game/ambience.js (wind, birds, crickets, drips, rumbles, Depths hiss) | ambience.cpp + live wind in the mixer | |
| game/music.js + music2.js (composePiece/2, moods + night, band, battle, fanfare, bus) | music.cpp | composer runs on the game thread, the band plays live on the audio thread |
| render/packs.js PACK_SOUND_EVENTS (pack event → our sound) | tools/port/sound_analyze.py `EXTRA` + data/sounds.json `events` | plus pack events the original never listed (anvil, fuse, thunder, cave ambience, portal, fireworks, mob steps, iron/deepslate/cobweb/vine… blocks) |
| other modules' SND_FX/VOX additions (deepdark, dragons, theend, brewing, camping, magic, riftarc, trains, aether, nether2, villagers…) | data/sfx.json | come along for free: the extractor records every recipe present after the whole game loads |

## How the original's sounds are carried over
`tools/port/sfx_extract.py` runs the whole original game in headless Chromium (like jsdump.py), wraps the
SndBuf kit, and for every take of every recipe (seeded exactly like `sndTakes`) records the kit's top-level
calls with their values (`["noise", t0, dur, "bp", f, Q, gain, {att…}]`, `["throat", t0, dur, {...}]`…). Then it
calls `sfx.play(name)` for every name the game uses (415) with the primitives patched, so the original's
dispatch chain (voices3 → sfxlib → sounds2 → audio.js) decides what each name is: a rendered recipe (255), a
live WebAudio sound (153, 3 recorded takes each), a special (`cave`, `cave_scrape`) or nothing (4). It also
stores each name's minimum gap and whether it is a creature (mob volume). The hand-written sample loops (hinge
creak, bow stave, pop sweep) are recognised and ported as ops.

Check (dq-sfx vs the original rendered in Chromium, same 64-band log-mel distance as below): rendered recipes
1.4 dB mean over 23 recipes × all takes (identical for the 5 whose random stream lines up: grass.step,
explode, levelup, cave_b, bow_draw; the rest differ only in noise realisation — the original's
own take-to-take distance is 6.8 dB); live sounds match WebAudio's OfflineAudioContext renders within their
take-to-take spread (deterministic ones: bell 0.0 dB, warn 1.3, rabbit 2.2).

## The resource-pack profiles
`tools/port/sound_analyze.py analyze` decodes every .ogg of the pack (662 files, ffmpeg, 48 kHz mono):
* **fine loudness** every 128 samples (2.7 ms), separately below and above ~1 kHz (two tracks: attacks, clicks,
  crunches, decays; the split keeps a crackle's highs from smearing over its rumble);
* **noise bands**: 26 ERB-spaced band levels of what is left after the tones are taken out, every 15/20/40 ms
  (longer sounds get the longer hop), 2 dB steps;
* **partials**: spectral peaks (4096-point frames) followed frame to frame (McAulay–Quatieri linking, ≥ 4 frames,
  ≤ 48 tracks) as frequency (1/1024 octave) + level — harmonics of voices, rings of glass/anvils/bells, pitched pops.
Every byte run is delta coded and the blob zlib'd. Profile binary: `u32 n, u16 hop, u16 frames, u16 tracks |
fine_lo u8[n/128] | fine u8[n/128] | bands u8[26][frames] | tracks (u16 start, u16 len, u16 logf0, i8 dlogf[len-1],
u8 amp0, i8 damp[len-1])`; dB code v → −100 + v/2 dB.

`resynth.cpp` (mirrors `resynth()` in the tool): random-phase noise shaped by the bands (2048-point frames every
512 samples, both halves in one complex IFFT), the partials as oscillators with interpolated frequency/level,
then each half scaled block by block to its loudness track; peaks over 0.9 softened. Every render draws new
noise; every play varies pitch (±5–7 %) and level like the original's `sample()`, plus Minecraft's own per-entry
pitch/volume from the pack's sounds.json.

### Quality (all 662 pack sounds)
Distance = mean |difference| in dB of 64-band log-mel spectrograms (1024/256 at 48 kHz) over the cells within 60
dB of the original's loudest; env r = correlation of the 5 ms loudness curves.
* Python reference resynthesis: **3.44 dB mean** (median 3.34, p90 4.08), env r 0.983
* C++ (`dq-sfx packs` vs the .ogg files): **3.46 dB mean** (median 3.37, p90 4.13), env r 0.983
* floor — two renders of the same profile (only the noise differs): 2.89 dB mean; another take of the same event
  (stone1 vs stone2, gravel1 vs 2, cow say1 vs 2, grass1 vs 2): 5.6–8.1 dB; white noise with the right loudness every 2.7 ms: 11.8 dB.
So a resynthesised sound is about as far from its recording as two renders are from each other, and much
closer than the pack's own alternative takes are to one another. Worst: wolf/pig steps, fire loops, door
close, cave1, sand dig (≈5–6 dB).

## Which sound wins
`SfxLibrary::resolve(name)`: the pack's `mob.<name>` (creatures; the player's `hurt` → `fx.hurt`), then the
pack's `fx.<name>`, then the original's synthesis for that name. `material(mat, kind)`: the pack's
`<mat>.<kind>` (logs fall back to wood, like blocksounds.js), else SND_MAT (unknown mat → stone, unknown kind →
dig). 137 pack events are mapped (all block materials but mud/slime, which this pack lacks).

## Engine
* Game thread: API calls resolve the name, apply the original's per-name gap (`Sfx.ok`), place the sound
  (sound3.js: distance gain `1-(d-3)/(range-3)`, ×0.42 per wall up to 6 with a low-pass `max(220,1400/walls)`,
  heard-the-long-way-round through the 41³ flood-filled air field, pan + behind-dulling), pick a take.
* Worker thread: renders takes into mono 48 kHz buffers once (cache; footsteps and clicks pre-rendered at start;
  buffers not started for 30 s + their length are dropped when the cache passes 48 MB). A play waiting for its
  render starts on the next frame (dropped after 0.3 s).
* Audio thread (miniaudio): lock-free SPSC command queue, 56 voices with stealing (quietest × least remaining),
  per-voice occlusion biquad, categories (master × sfx / mobs / ambience) read live from atomics, the wind voice,
  room (0.7 s) and cave (2.5 s) reverb sends driven by sound3's field, the music bus (live band → 6.5 kHz tone →
  0.72 dry + 0.5 hall → volume×0.55 → limiter, independent of the master volume like music.js), soft knee at 0.8.
* No device (or it fails): `ok()` is false and every call returns at once; the game runs silent.
* CPU: resynthesis ≈ 40–190× real time on the worker; the music band ≈ 10× real time on one core.

## Wiring
* `GameSession` creates a `ClientFx` (client/client_fx.cpp) and sets `level->fx` (reset in the destructor):
  sounds → `playAt`, ui sounds → `play` (local player only), materials → `materialAt`, voices → `voice`;
  particles / messages / shake are TODO hooks.
* `registerAudioLayers()` (called once in `App::run`) adds the `audio` RenderLayer: the local player's footsteps
  (local.js: every 2.2 blocks on the ground, the block 0.2 under the feet), and each frame an `AudioScene`
  (world, head, sky light, dimension, env.day, creative, under water, place mood, nearest boss) for the music
  and the ambience.
* `dq-sfx` (tools/sfx_render.cpp): `list`, `render <name> <out.wav|.f32> [take] [seed]` (`orig:<name>` = the
  original's synthesis, `recipe:<key>`, `pack:<file>`), `packs <dir>`, `music <mood>[:night] <out.wav> [s]`,
  `mix <out.wav> <s> name@t[@x,y,z]…` (whole engine offline), `bench`.

## Names for other areas
`material(mat, kind)` / `materialAt(…, pos)` with the block's `sound` (stone, grass, dirt, gravel, sand, snow,
wood, log, ladder, leaves, glass, wool, metal, mud, slime) and kind `step` (walking), `dig` (each hit while
mining), `break` (the block breaks), `place`.
`playAt(name, pos)` — the original's names, e.g. click, click_off, chest, chest_close, door_open/close,
trapdoor_*, gate_*, door_iron_*, pop (pickup), hit, crit, hurt (the player), bow, bow_draw, arrow_hit, eat,
burp, drink, splash, splash_small, swim, bucket, explode, fuse, ignite, fizz, levelup, xp, equip, craft, swap,
thud (small fall), fell (big fall / tree crash), tree_creak, anvil, anvil_land, bell, portal, piston_out/in,
dispense, rail, tick, fire_crackle, lava_pop, thunder, thunder_far, glass_break, flap, frost, warn, victory,
death, enchant, book_page, brew_*, firework_*, sculk_*, warden_*, ender_*, spell_* … (`dq-sfx list` prints all 415).
`voice(creature, kind, pos)` with MOB_TYPES' `voice` (ghoul, husk, drowned, rattle, stray, spider, mite,
scorpion, snake, squish, magma, fuseling, witch, raider, ogre, werewolf, wraith, wisp, moo, oink, baa, cluck,
wolf, cat, horse, …) and kind say | hurt | death | angry | step (`step` → `mstep_<creature>`: zombie, bone,
spider). `note(blockBelowSound, 0..24, pos)` for note blocks.

## Left to do
* Biome-accurate "music of the place": the generators don't expose biomes yet, so the mood is guessed from
  the column's top block / water / height / trees; villages not detected.
* Weather gusts (wind), jukeboxes and music discs (blocks2.js `disc`), sleeping dragons in bossNear: scene
  fields exist, nobody fills them yet.
* Mob ambient calls and mob footsteps (sound3.js Mob.update) belong to the creature system: call
  `voice(...)` / `materialAt(..., "step", pos, 14, 0.35)` there.
* True HRTF; per-voice live re-panning while a long sound plays (positions are fixed at start, as in the original).
* Particles/messages/shake from `ClientFx` (TODO hooks).
