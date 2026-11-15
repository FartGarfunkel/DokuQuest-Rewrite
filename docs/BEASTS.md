# The imported creatures (the "beast" rig)

A family of creatures built into the game from locally imported model data. Everything imported stays local and
git-ignored under `data/local_models/` (the art, moves and sounds are not ours to ship); without it the game runs with
its own creatures only.

The creature import writes bodies, motion, clips and creature entries into `data/local_models/`; the creature voice
import then adds their voices as synthesis profiles (`.json` + `.bin`).

## How it works
- **Bodies**: each source model's boxes become a box geometry, voxelised from its picture a voxel a model pixel. The
  picture: the creature's plain coat (not a moss / pattern / eyes / baby / snowy layer); `<tex>_eyes` / `_glow`
  pictures glow.
- **Motion**: each model's animation code is translated into a small program the "beast" rig (`sim/beast_anim.cpp`)
  runs each pose: walk, swing, flap, bob, chain swings and waves, progressRotation(Prev), progressPosition(Prev),
  faceTarget, assignments, locals, ifs, maths. What it asks of the entity (sitProgress, isFlying()...) is fed from our
  creature: a progress ramping a tick at a time while it sits / sleeps / flies / swims / runs / fights / grazes / rears.
- **Keyframe clips**: sin-eased keyframe clips, played on a blow (role "attack") or while grazing ("eat").
- **Segmented creatures** (cave centipede, bone serpent, void worm): head, body segments and tail joined into one
  creature; each segment runs its own legs a beat behind the one before, the chain slithers behind the head.
- **Joints** for the new kinds: legs and arms cut at the middle, the lower half (its own voxels: the picture is
  unchanged) folds at the knee / elbow as the limb above swings; the body breathes.
- **Creatures**: new kinds get a mobs entry (attributes, size, hostility, flyer / swimmer / amphibian / predator /
  neutral, drops, spawns). Kinds we already had keep their ways and wear the imported body (variants tinted to their
  look, or their own picture). Nether / End / cave / night spawns: `spawnDim` (`sim/creature_spawn.cpp`).
- **Voices**: every sound analysed into our synthesis profiles (no recordings kept); events `mob.am_<key>` (call),
  `_hurt`, `_death`, `_angry`. New kinds use them as their sound, ours as their voice (`mobPatch`).

Debug: `DQ_AM_STILL=1` holds every imported body at rest; `DQ_AM_DEBUG=<key>` logs that creature's pose twice a second.
The import writes a report of what was read and what was skipped next to its data in `data/local_models/`.

## Still to do
- Watch every new kind in game for poses that read wrong (the report lists what was skipped).
