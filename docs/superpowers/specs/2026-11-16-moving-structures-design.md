# Moving structures, chains and ropes — design

Date: 2026-11-16 · Status: agreed in conversation, awaiting review of this written spec

## Purpose

Players build things that move: big swinging doors and gates, drawbridges and hatches, sliding doors and
portcullises, lifts, moving bridges and turning platforms. They build them in survival out of craftable mechanical
blocks, and wire them into the game's redstone and the mill machinery (axles, windmills, waterwheels) so machines can
be chained together. Walled towns use the same system for their gates, drawbridges and lifts. Chains and ropes are
improved alongside: strung between any two points, drawn as real links and twisted rope, climbable, walkable as rope
bridges, and attached to moving pieces.

Success: every movement type works in survival with the new blocks; nothing a piece carries is ever lost; players and
creatures ride and are pushed, never trapped or hurt; it stays smooth in split screen, multiplayer and on low-end
graphics; existing worlds, doors, trapdoors, pistons and today's town gates keep working.

## 1. Blocks and items

| Thing | What it does | Recipe |
| --- | --- | --- |
| **Hinge** | Swings the glued piece on its face around its axis. Facing up/down: sideways swing (doors, gates). Facing sideways: up/down swing (drawbridges, hatches). Sneak-use cycles its setting: direction (clockwise / anticlockwise), angle (90°, 180°, continuous). | iron ingots, planks, redstone |
| **Slider** | Slides the glued piece in the direction it faces, 1–16 blocks (sneak-use cycles the distance). On a Lift Rail it follows the rail instead. | iron ingots, planks, redstone |
| **Lift Rail** | Stacked in a line (any direction); a Slider on it moves its piece along the whole rail, stopping at each Lift Stop. | iron ingots |
| **Lift Stop** | Placed beside a rail: a stop. A button on it calls the lift there; it gives a redstone signal while the lift is at it. | iron ingots |
| **Glue** | Used on the face between two blocks sticks them together (again: unsticks). Glued faces show as a faint outline while glue is held. | a slime ball or a honey bottle → 4 glue |

- The **piece** is the blocks glued to the hinge's or slider's moving face, and everything glued to those, up to 512
  blocks. The hinge/slider itself stays put.
- Glue is stored per block face (a small record on the block, saved with the chunk); breaking a block removes its glue.
- All new blocks and items: recipes, crafting guide, Compendium entries, controller and touch support, the game's own
  icons and names. No other game's or mod's name appears anywhere.

## 2. How a piece moves

1. **Start.** A hinge/slider told to move gathers its piece and checks the path: every block's sweep (hinge) or path
   (slider). A solid block in the way: it doesn't start (dust puff, "blocked" sound). Replaceable things (grass,
   flowers, snow layers) break as a piston breaks them.
2. **Lift out.** The blocks become a **moving body**: their ids, metas (facing, open/shut, waterlogged) and block
   entities (chests, furnaces, signs, item frames, banners: contents kept) are moved into the body and the world
   cells cleared.
3. **Move.** The body turns (hinge ~90°/s) or slides (slider and lift ~2 blocks/s); with axle drive the speed follows
   the axle's power. It is drawn with the normal block meshing, built once at the start, then only transformed.
4. **Set back.** At the end the blocks are placed back as ordinary blocks; a hinge rotates their facings (stairs, doors,
   logs, trapdoors...). The hinge/slider records that it's open; the next signal reverses.
5. **Chains of machines.** A hinge or slider inside a piece rides along and works again from its new place when the
   piece has stopped.
6. **Safety.** If a chunk the body is in unloads, the world is saved, or the game closes mid-move, the move finishes
   instantly and the blocks are placed at the end: nothing is lost or left floating. If the end cells were filled
   meanwhile, the blocks go back to the start position; anything that can't be placed drops as items (never deleted).

Follows the existing falling-tree pattern (`sim/felled.h`, `sim/creature_entities.cpp`, drawn in
`render/creature_layer.cpp`), extended with collision, riders and setting the blocks back.

## 3. What makes it move

- **Redstone.** Powered from any side: opens; unpowered: closes (levers hold it). A short pulse (buttons) toggles.
  Comparators read the piece's state: 0 shut, 15 open, in between while moving.
- **Mill machinery.** An axle ending at a hinge drives it: a continuous hinge turns while the axle turns, at its speed;
  a 90°/180° hinge or a slider moves at the axle's speed. The turning piece counts as load on the axle network.
- **By hand.** Using any block of a door/gate/hatch/drawbridge piece toggles it, unless a redstone signal holds it.
- **Lifts.** A button on a Lift Stop calls the lift there; using the lift's piece while standing on it sends it to
  the next stop; stops signal while the lift is at them.

## 4. Riding, pushing, saving, multiplayer, performance

- **Riding:** anything standing on a moving piece (players, villagers, animals, dropped items) moves with it and can
  walk about on it while it moves.
- **Pushing:** a swinging/sliding piece pushes players and creatures out of its way. Something trapped between the piece
  and the world stops the piece, which goes back. Pieces never hurt anyone.
- **Solid:** a moving piece is solid to stand on and bump into; it can't be built on or broken mid-move.
- **Saving:** never saved mid-move (§2.6); a save holds ordinary blocks and each hinge/slider's state.
- **Multiplayer / split screen:** the host simulates; guests receive the body once, then its angle/offset each tick.
  Split-screen views share one body.
- **Performance:** one mesh per body per move, then transforms only. Beyond the render distance, no animation: the end
  state is applied directly.

## 5. Villages and towns (new towns only)

- **Town gates:** stone walls get an iron-bar portcullis on a hidden Slider in the arch; palisades get two timber leaves
  on hidden Hinges. Guards open them at dawn, shut them at night and when raiders come, by signalling them. The
  gatehouse lever is a real lever wired to the gate.
- **Drawbridges:** a town whose gate faces a river (or a moat added to walled towns) gets a plank drawbridge on a
  sideways Hinge, hung from the gatehouse by chains, lowered by day and raised at night with the gate.
- **Lifts:** watchtowers, tall town towers and the windmill's top floor get a lift (rail, a stop and call button per
  floor); villagers call it, ride it and step off like stairs.
- Existing towns keep their current gates.

## 6. Chains and ropes

- **Strung between points:** use a chain or rope on a block, then on a second block up to 16 blocks away, at any
  angle: it strings between them with a natural sag (chains heavier, ropes lighter). One item per 2 blocks of length;
  breaking either end drops them. Ends attach to fences, walls, posts, block sides and lantern hooks. A strung
  chain/rope is a line between its ends, not grid blocks.
- **Look:** chains as interlocking 3D links, each turned a quarter from the last, in their metal; ropes as twisted
  strands, frayed at the ends. Chain and rope blocks hanging straight down (lantern chains included) get the same look.
- **Climbing:** hanging ropes and chains, and strung ones that are steep enough, climb like ladders.
- **Rope bridges:** a near-level strung rope takes slats (planks or slabs used on it) to become a walkable bridge that
  sways and dips under weight.
- **Tied to machines:** an end on a moving piece moves with it, going slack and taut. Town drawbridges hang this way;
  a lift's rope turns a wheel above it (decoration).

## 7. Build order and testing

Stages, each playable and tested before the next:

1. Core: Glue, Hinge, Slider, the moving body (lift out, move, set back), by-hand use.
2. Redstone and mill-machinery connections.
3. Riding, pushing and collision; Lift Rails, Stops and call buttons.
4. Multiplayer sync; finish-on-save/unload.
5. Chains and ropes.
6. Villages and towns: new town gates, drawbridges, lifts.

Testing at every stage:

- Automatic tests (a new `tools/test_moving.cpp`): a piece's blocks and block entities come back exactly as they were;
  rotations of facings; a blocked piece doesn't move; a lift carries a player to a stop; a save mid-move loses nothing;
  a guest sees the same angle; redstone and axle drive.
- Scripted playtests in the real game with screenshots checked: build and wire a gate, ride a lift, cross a rope
  bridge, a drawbridge on chains.
- Before done: all existing suites, Windows/Linux/Android builds, frame-time check against the current build.

No regressions: existing worlds, doors, trapdoors, pistons and current town gates keep working; only new towns get the
new gates.
