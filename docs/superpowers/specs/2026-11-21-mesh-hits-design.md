# Mesh-following hits — design

Date: 2026-11-21. Partner: swept-mesh hitting (not full rigid-body physics). Dismemberment already aims at parts; make the **blade** pick the part, not the eye ray. Mixed-tool stats already feed `heldTool()->damage`. Blocking exists for arrows via `playerShieldBox`; melee must meet that box too.

## Intent

A swing lands if the drawn blade overlaps a drawn limb. A blocked blow lands on the shield or the weapon guard, not the body behind it. Armour on the struck place counts more. Creatures striking you use a strike volume, not a distance check alone.

## What already exists

- `bladeAt` / `bladeSweepHits`: sampled blade vs an AABB.
- `rigHitPart` / `rigLimbPart` / `tryDismember`: ray vs posed parts; eye ray today.
- `playerShieldBox` / `shieldRayHit`: arrows vs the raised shield.
- Mixed tools: `toolOf` damage already used in `attackEntity`.
- `combatRpgIncoming`: `blockT` reduces damage; it does not test whether the blade met the guard.

## Approaches

1. Full rigid-body weapons. Dropped: voxel jitter, tunneling.
2. **Chosen:** sweep the existing blade samples against posed part boxes (creatures) and limb boxes (players). Guard box intercepts first.

## Rules

- Live window only (`hitFrom`..`hitTo`). Reach from the move, as now.
- The part the blade overlaps is the limb. `src.rayO`/`rayD` follow the blade through that part so dismemberment stays on the same path.
- Player limbs: head → helmet, torso/arms → chest, legs → leggings+boots.
- If `blockT > 0` and the blade meets the shield or a two-hand guard slab, the blow is blocked (existing `guardKeep` / parry). It does not also hit the body.
- Creature melee: a short segment from the creature toward you during `swingT`; same shield/limb tests.
- Creative swords still do not break blocks (already).

## Non-goals

No bouncing swords off walls. No rewrite of `skilltree.json`. No retune of parry window / boss cap.
