# Alex's Mobs as our own creatures

Not a mod: the creatures are built into the game from the mod's files on this machine (Soulrend's copy), and everything
taken from it stays local and git-ignored under `data/local_models/` (the mod's art, moves and sounds are not ours to ship).

```
python3 tools/port/alexsmobs_import.py data     # bodies, motion, clips, creatures -> data/local_models/alexsmobs.json
python3 tools/port/alexsmobs_sounds.py data     # then their voices -> data/local_models/alexsmobs_sounds.{json,bin}
```

## What comes over
- **Bodies**: each Citadel model's boxes (its constructor, with `switch`/`if` on its arguments) as a Bedrock-style geometry,
  voxelised from the mod's picture a voxel a model pixel (`geo_boss_import.py`). The picture: the creature's plain coat
  (not a moss / pattern / eyes / baby / snowy layer); `<tex>_eyes` / `_glow` pictures glow.
- **Motion**: `setupAnim()` translated into a program the "am" rig (`sim/am_anim.cpp`) runs each pose: Citadel's walk,
  swing, flap, bob, chain*, progressRotation(Prev), progressPosition(Prev), faceTarget, assignments, locals, ifs, maths.
  What it asks of the entity (sitProgress, isFlying()...) is fed from our creature: a progress ramping a tick at a time
  while it sits / sleeps / flies / swims / runs / fights / grazes / rears.
- **Keyframe clips**: `animate()`'s ModelAnimator keyframes (sin-eased like Citadel), played on a blow or while grazing.
- **Segmented creatures** (cave centipede, bone serpent, void worm): head, body segments and tail joined into one
  creature; each segment runs its own legs a beat behind the one before, the chain slithers behind the head.
- **Joints** for the kinds new to us: legs and arms cut at the middle, the lower half (its own voxels: the picture is
  unchanged) folds at the knee / elbow as the limb above swings; the body breathes.
- **Creatures**: new kinds get a mobs entry (attributes, size, hostility, flyer / swimmer / amphibian / predator / neutral
  from their goals, drops from their loot tables, spawns from Soulrend's config or the mod's defaults when Soulrend switched
  them off). Kinds we already had keep their ways and wear the mod's body (variants tinted to their look, or the mod's own
  picture of them). Nether / End / cave / night spawns: `spawnDim` (`sim/creature_spawn.cpp`).
- **Voices**: every sound analysed into our synthesis profiles (no recordings kept); events `mob.am_<key>` (call),
  `_hurt`, `_death`, `_angry`. New kinds use them as their sound, ours as their voice (`mobPatch`).

Debug: `DQ_AM_STILL=1` holds every imported body at rest; `DQ_AM_DEBUG=<key>` logs that creature's pose twice a second.
The report of what was read and what was skipped: `data/local_models/alexsmobs_report.txt`.
