# The Wither Storm as Cracker's Wither Storm Mod 3.5.0.1 has it (behaviour spec)

Read from the decompiled mod (~/Documents/Projects/witherstorm, CFR) on 2026-11-09. The mod is "All rights reserved":
this is our own description of its behaviour for building ours natively; no code or art of it goes in the repo.
20 ticks = 1 s. "consumed" = the count of things it has eaten (blocks per cluster, items per stack, 1 per creature).

## Summoning
- Vanilla Wither T with a command block (any kind) in the middle of the soul sand row; the third skull completes it.
  Not on Peaceful. Pattern blocks vanish; COMMAND_BLOCK_ACTIVATES (vol 4).
- Spawn = the vanilla Wither's: invulnerable 1000 ticks (50 s, config), health from 1 filling to full, armour swirl the
  first 5 s, texture blinks, scale 1.5 -> 2.0; then a power-7 burst and the global wither-spawn sound.
- Phases 0-3 look and act like a Wither (0.9 x 3.5 box, vanilla Wither sounds, 3 Wither heads, Wither skulls, wither
  roses from kills) with command-block particles streaming into its chest (0-2); purple boss bar.

## Phases (0..7), driven only by consumed
- Evolves when consumed > T(phase): T = 100, 400, 1200, 18800, 195000, 351400, 580800, 2125000 (x evolution speed 1).
  Natural 5 -> 6 is blocked: only the Formidibomb moves it on. 6 -> 7 natural; 7 the last.
- Names: 0-3 hunchback, 4-5 destroyer, 6-7 devourer.

| phase | box W x H | max HP | armour | heads | look |
|---|---|---|---|---|---|
| 0 | 0.9 x 3.5 | 400 | 8 | 3 | the Wither + a rib cage with a command block cube in it (~3.4 tall) |
| 1 | 0.9 x 3.5 | 400 | 8 | 3 | + a lumpy hump growing 2.75 -> 3.2 blocks (sub-steps at 150 / 250 eaten) |
| 2 | 0.9 x 3.5 | 400 | 8 | 3 | middle head becomes a jawed storm head (~1 block), hump to ~4.3, tail gone (>= 800) |
| 3 | 0.9 x 3.5 | 400 | 8 | 3 | no tail, command block hidden, tilted mass 4-5 across, 3 tentacles ~9 long (2350 / 3500) |
| 4 | 10 x 30 | 1024 | 18 | 3 | Wither gone: a voxel mass ~30 x 41 x 25 + 3 storm heads, 5 tentacles ~43 long, 3 thick (later ~35 x 52, 9 tentacles) |
| 5 | 10 x 60 | 1024 | 20 | 3 | ~51 x 65 x 32, 7 tentacles ~57 long |
| 6 | 15 x 90 | 1024 | 22 | 3 + 2 segments x 3 | ~110 x 96 x 72, 9 tentacles (2 huge, ~84 long); two segment bodies split off (30 x 25 each, 3 heads) |
| 7 | 15 x 120 | 1024 | 24 | 9 | ~139 x 129 x 80; "torn": a hole in front (the way into the bowels), pulsing cubes on the mass |

- Phase 4+ heads (all ~4.5 W x 4.1 H x 5.25 L, same size in 4-7): main centred ~15 up, 8.6 forward; second ~8 to one
  side, 27 up, 15 forward; third ~12 to the other side, 25.5 up, 9 forward. Head = 12x6x12 upper jaw + 8x6x2 snout,
  hinged 12x2x12 lower jaw, 16 teeth per jaw; jaw opens to roar/eat. Tentacles: 6 chained segments 12->2 px, swaying.
- Eyes glow (emissive), off while it flickers (bowels command block hurt) or plays dead. Night "shine" quad (phase > 3).
- Phase 6 first half (consumed < 466100): only head 0 of each body works; the others wake with a roar past it.

## Flight
- 0-3: hovers ~10 above the surface under it; toward a target > 20 away at ~0.5 b/tick; else drifts to its ultimate
  target if > ~110 away. 4+: 75 above the highest solid (non-leaf) ground within width x 1.5; toward the ultimate target
  if > ~77 away at ~0.03 b/tick (+0.03 if > 205 away); "chase" when its target sits still: + min(0.4, dist x 0.001).
  Turns 0.1 deg/tick. Colliding with terrain rips out 10 clusters (radius 3-5) a tick.
- Immune: knockback, pushing, potions, mounting, fall damage (a big fall thumps + shakes screens), explosions.

## Eating
- Everything "tracked" is pulled to its middle each tick (0.5; clusters 0.125 at 0-3, 0.0625 at 4+; spiralling) and
  consumed on reaching the core: living things die outright (tamed pets kept and returned later), items 10%/tick within
  reach, never: command block book/tools, withered nether star.
- Block clusters (floating lumps of blocks lifted whole, they tremble 1-2 s first):
  - hunchback (0-3): every max(1, 60 - 0.00375 x consumed) ticks, 1/3/9/18 single blocks by phase, within
    min(48, 12 + 0.00445 x consumed); eats cave roofs too.
  - default (4+): every 40 ticks (160 while chasing) one sphere of radius 3/3/4/5 (phases 4-7) within 80.
  - small: every 64 (0-3) / 30, 25, 20, 15 ticks (4-7), "interesting" blocks only (not stone/dirt/sand/gravel/ores...).
  - nature: plants, glass, torches, rails...: doesn't count toward growth.
  - never: wither-immune blocks, end portal, water, air, beacons of its own, its phlegm, formidibombs.
- Tractor beams (one per head): a cone from the mouth, 1.7 wide at the mouth, 7.3 at 100 blocks, stopped by blocks
  (250 long). 0-1: none; 2-3: the main head only; 4+: every working head. Pulls its target at 0.2 (players straight at the
  head), mobs in the beam too (4+). Touching the head: a player is chomped 3.5 a hit (~2 a second), a mob dies and is
  eaten. Beam block grabs every 24 (0-2) / 15 (3) / 5-24 (4) / 0-14 (6+) ticks near where it hits, cluster radius 1 ->
  up to 8 (phase 7); drains water/lava in 13^3 there. Purple (128,77,204) translucent beams.

## Attacks and targeting
- Wither skulls from heads whose beam is off: 0-3 one every 40-59 ticks; 4+ rarely. Idle 16 updates: a charged skull
  at random. Main head 0.1% charged.
- Flaming skulls at every roar (first 10-20 s after spawn, then every 20-50 s): 10 damage + Wither II 9 s, burst 5-6,
  heals the storm 10 on a kill; a shield blocks; a sword can swat one (costs it 120-259 durability).
- An injured head fires a blue flaming skull (burst 9, breaks almost anything).
- Hurt: breaks a 3x4x3 round itself a second later (as the Wither). No lightning/weather attacks.
- Each head targets its own nearest (40 at 0-3, 120 at 4+), 75% preferring players; never: endermen, wither
  skeletons, flyers, its own kind, players raising a shield, anything within 10 blocks sideways of its body (right under
  it is safe), what another head has. Loses a target unseen 4 s (0-3) / 1 s (4+).
- Distractions (4+): fireworks, "distraction" blocks near the beam end (beacons, glowstone, redstone, bricks, beds...),
  TNT going off in view, a goat horn: the beam fixes on them 6-9 s.
- Ultimate target: the nearest player. Chases one who stays put too long (30 min less with distance/phase, >= 2 min)
  or runs away 5 times; after a chase, or boating 60 s, it is "distracted" to a village/outpost/monument for 4-35 min.
- Wither sickness: ~10 min near it (+0-3) infects a player, then ~12 min (+0-5) later the effect: 1 damage and -0.5
  max HP every 7200>>amp ticks. Golden Apple Stew cures. Its death cures everyone near.

## Hurting it and beating it
- 0-3: hurt like a Wither (400 HP, armour 8) but regenerates 10 HP a second: practically untouchable; killed there it
  just dies, no drops. 4+: immune to everything except the bowels kill.
- Head injury (beam heads only): 1-2 hits (0-3) / 3-5 (4+) from a projectile into the head, a melee hit on it, or an
  explosion within 5 (0-3) / 12 (4+): the head is out 9 s / 36 s (beam off, screams, blue skull), and a pulled player
  gets 40 s of being ignored. No HP damage: it's how you escape.
- Formidibomb (Super TNT = 4 TNT + 4 gunpowder + Command Block Book; Formidibomb = Super TNT + 4 blaze powder + 4
  gunpowder; the Book drops from the Withered Symbiont, which the storm sends at a player at the end of phase 5): the
  storm (phase >= 5) sees a lit one within 170, roars, and drags it into its mouth. The blast (radius ~50) knocks it
  out of the sky: FALLING (it drops all it carries), at 10 s phase 5 becomes 6 and it SPLITS (two segments ~90 away);
  then PLAYING DEAD on its back, a rib-cage podium with a command block beside it. It revives if a player comes within
  6 of the podium (pulled in), hits it with a command-block tool or explosion, the podium goes missing, or after 60 min
  (global "reactivates", a power-16 blast without block damage, then it ignores players 3 min).
- The kill: phase 7 opens a hole in its front (consumed >= 2125000, or 6-10 min after a player first carries a
  command-block tool in phase 7). Touch it (or pearl into it) -> the Bowels: a flesh dimension (fog #FF6BF8), a jigsaw of
  tunnels and themed caves round a 66 x 46 x 69 arena. The command block there (64 HP) is hurt only by command-block
  tools, a quarter per hit, 4 hits, between waves:
  hit -> its podium rises -> wave 1 (sickened mobs) -> hit -> wave 2 (+ a Withered Symbiont at half HP, tentacles curl
  round the block until it's dead) -> hit -> wave 3 (and 2 wall heads wake: pull + bite 3.5, 60 HP) -> hit 4: DEATH.
  6-11 tentacles (80 HP, swing/strangle) and 6 floating invulnerable ones guard it. Each hit injures the storm's heads
  40% and makes it drop what it carries; it flickers.
- DEATH: everything near dies, clusters fall from the ceiling 8 s, the storm takes infinite damage; everyone within 150
  is cured, pets returned, and set down ~100 blocks beside it facing it.
- Its death (4+): all heads roar, death sound (vol 20), carried things fall, 18 s death (debris raining from 6+,
  dragon-style light rays white -> magenta, dissolving), then a Withered Nether Star floats down to the nearest player,
  10000 XP, Hero of the Village V near a village.

## Around it
- Boss bar purple, darkens; hidden underground when it can't be seen. Music by stage (boss, formidibomb, bowels
  exposed, revival themes). Loops: command-block pulse (0-2), storm loop (3), close/distant/far loops by distance (4+).
  Global sounds: evolves (phase 4), splits, reactivates.
- Sky (phase 5+): within 200 blocks the sky goes (20,0,19), clouds (28,10,27), fog (133,69,62), fading out by 400;
  up to 85% from phase 6; darkens to x0.55. Debris rings round it (4+): textured cone bands radius 50-250, slow.
- Underground near a 4+ storm: rumbles every 1-3 min (shake, falling dripstone/glow berries).
- Players killed by a 4+ storm respawn 300-500 blocks away if their spawn is within 300 of it. It keeps up to 576
  chunks loaded round itself.

## Withered Symbiont (mini-boss)
- 1.8x humanoid of flesh lumps with 3 back tentacles, 60 HP (+20 per extra player), hits 8-23 and launches you.
  Sent at phase 5 end (every 10-20 min) 30 blocks from the storm toward a player without the Book.
- Loop: casts spells every 20-40 s (fangs, orbiting shulker bullets, arrow volley, leap-smash, orbiting fireballs or
  skulls, pull, splash potions, lobbed sickened creepers, a shove pulse); after 5 casts summons sickened mobs; then
  kneels VULNERABLE (up to 4 min): only hurt then, only from behind (+-40 deg), projectiles /4. Enraged <= 50%.
- Dies in 16 s; drops the Command Block Book (each helper gets one); players near are ignored by the storm 5-6 min.

## Sickened mobs and tainting
- Wither sickness death, or being killed by a sickened mob/tentacle/symbiont, turns zombies, skeletons, villagers,
  creepers, spiders, cows, pigs, chickens, bees, parrots, wolves, cats, phantoms, pillagers, vindicators, golems into
  "sickened" versions: glowing eyes, hostile to players and every unsickened mob, wither on hit, tougher (e.g. zombie
  24 HP, iron golem 60 HP / 10 dmg). Golden Apple Stew turns them back over 3-5 min. No natural spawning.
- Tainted blocks (stone/dirt/wood/glass... families, wither roses for flowers) come from the storm's debris and death
  and from potions; no passive spread.

## Withered Beacon (not a weapon)
- A beacon on a lapis/beacon pyramid with up to 4 coloured support beacons (diamond/emerald/iron/redstone bases):
  area buffs; an altar for recipes: resummon the storm (3 skulls, starts at phase 4), a symbiont, the Formidi-Blade
  (charged-hit explosions), the Eye of the Storm (spike rings).

## Sounds (all 151 non-music ones are in data/sounds.* as fx.<event>, see HANDOFF 2026-11-09 late)
Storm: ambient, loop, close/distant/far loops, roar, bite, shoot, hurt, thump, tremble, tractor_beam(_activate),
evolves, splits, reactivates, death. Command block: activates, build, cracks, damage, death, destruct, hit, power_down,
pulse_loop, summon. World: bowels_loop/mood/loud_hurt/transport/tremble, cave_wind, earth_rumble, tremble,
loud_tremble, rib_bone_crack, block_cluster_shake, whoosh, tentacle_spike_stab. Symbiont: ambient, cast/prepare spell,
death, heart_beat, hurt, launch_mob, power_down, pull, spawn, step, summon. Formidibomb explosion(_quiet), pulse_loop,
super_tnt_fuse, formidi_blade charging/decharge, withered_beacon_*, withered_phlegm_block_*, amulet_*, mob_infected/cured.
