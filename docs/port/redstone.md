# Port notes: redstone, rails & minecarts, boats & ships, machines

Workstream files: `src/sim/redstone*.{h,cpp}`, `sim/rsplace.cpp`, `sim/minecart.{h,cpp}`, `sim/vehicles.{h,cpp}`,
`sim/machines.{h,cpp}`, `data/machines.json`, `tools/test_redstone.cpp`. Test: `build/test_redstone` (78 checks; the
expected values are what the original's probes `redstone06.js` / `rsplace06.js` print on 0.9.0.90, plus ship084.js,
trains, machines, TNT and save/load).

## Ported
| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| core/blocks.js T_RS/T_COND, world/shapes.js wireConnects/RAIL_EXITS/D6 tables | `sim/redstone.h` `rs::T()`, `rs::wireConnects`, `rs::RAIL_EXITS` | own copies in `namespace dq::rs` (the mesher keeps its own in world/shapes.h) |
| game/redstone.js `Redstone` | `sim/redstone.cpp` `Redstone` (a WorldSystem per realm), `RedstoneLevel` | 20 ticks/s, same scheduling, rounds, burn-out, dust networks, repeaters (delay/lock), comparators (+ measure), observers, lamps, bulbs, note blocks, bells, TNT, doors/trapdoors/gates, powered/activator rails, timers (plates, hooks + tripwire, detector rails, daylight), hoppers, dispensers/droppers |
| redstone.js pistons, PistonAnim, pistonPartBroken, dropAttached | `sim/redstone_parts.cpp`, `sim/redstone_entities.h` | `PistonAnim` entity (0.12 s, carries riders), finished before saving |
| blockfx.js PrimedTNT, explode | `PrimedTnt`, `explode()` | emits `explode` first (a handler returning false takes over) |
| game/rsplace.js | `sim/rsplace.cpp` `rsItemUse` (EventBus `itemUse`) | placement rules, using parts, carts on rails, ore glow, targetHit |
| game/minecart.js | `sim/minecart.cpp` `Minecart` | rail following, slopes, powered/activator rails, walls, bumps, hopper/chest/furnace/TNT carts, rail shaping (`railShapeFor`, `railRefreshAround`) |
| game/trains.js | `Minecart::use` (chain), `trainStep`, furnace double cart (seat, driving, coal from the seat), cargo/pets | joins saved as stable uids |
| game/vehicles.js, boats2.js, ships.js, lilypads.js | `sim/vehicles.cpp` `Boat`, `Wind`, `vehiclesAfterPlayerMove` | rowboat, sailboat (×1.6), ship (deck, helm, rails, chest), wind sailing, standing on boats / the deck, solid hulls, lily pads |
| game/mech.js, machines.js | `sim/machines.cpp` `Mechanics`/`MechLevel` | windmills, waterwheels, axles, millstone, sawmill, crusher, harvester, belts, machine output onto belts, hoppers feeding machines (`Container::Machine`) |

## APIs
- `Redstone* redstoneOf(Level&, realm)` (nullptr on a joined client): `inv/insertOne/moveOne/changedInv` (containers,
  `Container` = block entity `items` array or a cart's `data["items"]`), `schedule`, `powerAt`, `primeTnt`, `targetHit`
  (arrows: call it when an arrow hits a `target`), `daylightTick`.
- Riding (for the player controller, see `sim/vehicles.h` header comment):
  `bool vehicleInput(Level&, Player&, vec2 move, bool jump, bool sneak)` every frame — true while riding (skip walking);
  `void vehiclesAfterPlayerMove(Level&, Player&, double dt)` after the player moved (seats, boat/deck standing, solid hulls);
  `vehiclePick`, `useVehicle`, `hurtVehicle(lv, e, attacker, blast)` (combat; a plain hp drop also counts as a hit),
  `loadVehicle/unloadVehicle` (Carry On), `windOf(lv)`, `spawnBoat`, `placeCart`, `cartAt`.
- Events: `screenOpen` [player, "container", {pos:[x,y,z]} | {entity:id, name}] (hoppers, dispensers, chest carts/boats);
  `explode` [x,y,z,power,realm]; `skillGain` [owner, skill, xp] (crusher/harvester); `blockPlace` before placing.
- Sounds: the original sfx names; note blocks play `note:<material of the block below>` with pitch 2^(n/12).
- Entities: `minecart`, `boat` (data.kind), `piston_anim`, `primed_tnt` (factories in `registerRedstone()`).

## Differences
- Machines keep their turning speed/angle across the 2 s regroup (the original restarted each group from 0).
- Windmill height factor uses blocks above sea level (original y 52..86 = 4..38 above its sea level 48).
- Redstone ore fades on a 5–20 s timer (the original used random ticks); dispensed bone meal only grows crops.
- Explosion damage is a direct hp loss + knockback (until a combat system takes `explode`).
- Pressed buttons and lit ore are scheduled from `onBlockChanged`, so presses arriving over the network work too.
- Pistons don't push across a dimension band edge.

## Client side (render/vehicle_layer.cpp, registered by `registerVehicleLayers()` in client/session.cpp)
- Voxel models built from the original's box lists (minecart.js, trains.js seat/couplings, boats2.js hull and oars,
  vehicles.js mast/boom/sail/pennant, ships.js hull/mast/yard/sail/flag/helm, mech.js + machines.js parts), coloured
  per face from the tiles (pack-aware, rebuilt when the pack changes); 1/32 voxels (the ship and windmill sails 1/16).
- Animated like the original: cart wheels roll, TNT carts and lit TNT flash, the furnace glows while burning, sails
  fill and swing with the boom/yard, pennants stream downwind, oars row, axles/windmills/waterwheels/millstone/saw/
  rollers/harvester arms turn with their group's angle, belt slats run, piston heads and pushed blocks slide.
- Riding glue: `afterPlayerMove` calls `vehicleInput` from the app's input when the controller didn't, then
  `vehiclesAfterPlayerMove`. Container screens: "screenOpen" -> `openInventory` (block pos, or `InvOpen::entItems`
  for chest/hopper carts and boat chests — two fields added to sim/inv_ops InvOpen, read/written in InvSession).
- Dev aid: `DQ_DEV_RS=1` sets carts, boats, the ship and a windmill in front of you.

## Left to do
- The player controller must emit `itemUse` (gameplay) for any of the placing/using to happen in game.
- The ship/boat water mask (hide water inside hulls, a depth-only lid in boats2.js).
- Riding on joined clients (vehicles are ghosts there: forward input with `lv.net->action`), net `ride/own/unown`.
- Carry On wiring (gameplay); torch placement stays with gameplay rules.
- trains.js pets: creatures must honour `riding`/`data.mountedOn`; travel.js carriage; seas.js.
