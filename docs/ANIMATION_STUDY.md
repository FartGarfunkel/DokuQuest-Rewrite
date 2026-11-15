# Animation notes: what makes our movement read as fluid

Our character and mob animation is our own procedural code (render/char_anim.cpp, render/char_blend.cpp). These are
the principles it follows and the numbers it is tuned to.

## Principles
- **Links from what is on screen.** Every change of clip blends from the pose actually shown to the next clip's first
  frame (0.03-0.1 s for actions, 0.15-0.35 s for idle/walk/holding). We had this (render/char_blend.cpp).
- **Joint masks.** Upper-body actions (guard, eating, holding) are layers over the legs' own cycle, so the legs keep
  walking. Now: charBlendArmsOnly() keeps the legs live through arm-only state changes.
- **Continuous locomotion.** Walk/run/sneak cycles keep running while they blend. Now: runAmt / sneakAmt eased amounts.
- **Timing inside the clips.** Attacks start cocked, strike in 0.07-0.10 s (the blade tip at 40-85 blocks/s), the chest
  turning about 0.3 rad ahead of the arm, then hold the follow-through until the next input is taken (0.4 s, finisher
  0.6 s). Combo window 0.8 s. Now: weaponSwing() in render/char_anim.cpp (three cuts on a 0.5 s clock).
- **Hits snap, hold, release.** A short hit is in the recoil pose within about 0.08 s and holds it for the stun time.
  Now: the flinch snaps in 0.06 s, holds 0.1 s, eases out, away from where the blow came from.
- **Aiming swings.** The chest pitches with the look (pitch squared over 30 degrees, capped at 30); the head keeps the look.

## Target characteristics (our CharAngles convention, rounded)
| motion | target | ours now |
|---|---|---|
| walk arm swing | about +-0.25 rad, forearm bent 0.2-0.3 | amp x 0.42, forearm bend kept |
| walk knee fold | up to about -1.2 in the swing, after toe-off | -0.95 peak, phase +0.45 rad |
| walk hips | dip about 0.06 twice a cycle, chest twist +-0.3 | lift bob 0.05, twist 0.21 |
| run | chest leant 0.5-0.7 in, hips 0.08-0.19 lower | lean about 0.42, hips 0.05 lower |
| sneak | chest leant 0.8-1.0, deep knees | lean 0.6, knees -0.58 |
| idle armed | hips turned 0.55 (left shoulder leads), back foot out 0.48 | 0.35, back foot 0.38 |
| shield guard | body as idle, elbow low and forward, forearm level across the chest | same shape, fitted to our strapped shield |
| blocked hit | body twists a further 0.4 within 0.08 s | +0.3 twist, shield knocked in |
| sword cuts | 1 diagonal down R to L, 2 flat backhand L to R, 3 rising R low to L high | the same three shapes |
| roll | tucked somersault, about 0.63 s, 3.2 blocks | ours 0.4 s (sim timing) |

## Still to do
Weapon trail ribbon; an aim/chest-twist share for bows and crossbows; a body yaw snap at the start of an action; a
crouch anticipation into the roll and a sidestep; per-weapon cuts (greatsword, dagger, spear, axe) once the weapon
items exist; first-person versions of the cuts; mobs: a hurt flinch that peaks at the hit, level
heads on quadrupeds and birds, warped-sine gaits with phase lag, breathing, blinks, rare idle actions.
