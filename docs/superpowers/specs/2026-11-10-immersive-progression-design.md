# Immersive progression (TerraFirmaCraft-style early game) — design

Date: 2026-11-10. Status: approved in conversation, awaiting written-spec review.
Reference: TerraFirmaCraft 1.7.10 source (`~/Documents/TFC-1.7.10-master`), used for mechanics and numbers.

## Intent

"A good mix of both. Not hard survival but more immersive." The early game moves from punching trees and wooden tools to
gathering, knapping, fire, clay and metallurgy, after TFC, with the waits shortened and the punishing edges taken off. Our
procedural mixed-material tools stay at the heart of it: whatever metals you melt together, in whatever proportions,
decide a tool's stats and its colouring.

Project rules hold throughout: no hunger; full controller support in every new screen; one feature per file under about
1500 lines; `registerXxx()` registration; no audio files; patch notes end with "Removed Herobrine."

## The world option

- Create World gets **Progression: Immersive / Classic**, Immersive by default. It is a game rule, `immersive`.
- Saves made before this change have no such rule and load as **Classic**: nothing in an existing world changes.
- Everything below marked *(immersive)* applies only when the rule is on; the rest applies to every world.

## Part 1 — Placeables (every world)

**Ingot piles.** Sneak + use with an ingot on a solid top places an `ingot_pile` block (separate from general ground
storage). Layout as TFC: 8 ingots per layer as 2 rows of 4 (each a quarter of the block wide), layers 1/8 block tall, each
layer turned 90° from the one below (brickwork), 64 per block. Its collision height is ceil(n/8)/8. One metal per pile.
Sneak + use with the same ingot adds one; use with an empty hand takes one. A full pile accepts a new pile on top; placing
on a pile tops up the one below first. Existing ground-storage piles keep working; ingots already in them stay.

**Log piles.** Sneak + use with a log places a `log_pile`: 16 logs, a 4×4 crib (two logs per course, courses crossed),
drawn with the logs' own pictures. Sneak + use adds, use takes. A full pile accepts another on top. Log piles are the
charcoal pit's fuel (Part 3).

**Cactus floor fix** (done): the ground under a cactus kept its top face hidden although the cactus body is thinner than
the block, leaving a see-through ring. Only solid boxes now hide the face under them.

## Part 2 — Stone age (immersive)

- **Trees:** logs need an axe (any tier). Bare-handed or with another tool: a "You need an axe" hint, nothing breaks. Tree
  felling keeps working. Leaves drop a stick about 25% of the time.
- **Leaves don't hold you up** (TFC): no collision; moving through them is slowed (about half speed) and a fall through
  them is braked.
- **On the ground** (worldgen, all surface chunks except ocean): about 8 loose rocks per chunk (the local stone: our
  existing `loose_stones`, denser), flint on gravel, beaches and river banks, and twigs (a new `twigs` ground block)
  under trees and on beaches, about 5 per chunk. One hit or a use picks them up (rock → `pebble`, flint → `flint`, twigs →
  `stick`).
- **Knapping:** with 2 or more pebbles or flint in hand, use on air opens a 5×5 knapping grid. Each click chips a cell
  away; the first chip uses up one stone. The grid is matched after every chip against patterns in
  `data/knapping.json` (smaller patterns at any offset, mirrored too), TFC's shapes: axe, shovel, hoe, knife (×1 and ×2),
  hammer and javelin heads, plus our **crude flint pickaxe** head (our addition, so the start isn't hard). Taking the
  result closes it. Controller: the d-pad or stick moves a cursor, A chips, B closes.
- **Stone tools:** head + stick (shapeless) → stone axe, shovel, hoe, knife, hammer, javelin, crude pickaxe. Tier 1,
  durability 50–70 (TFC's 50/55/60/70 by stone kind, flint 65), speed about 6.5. The crude pickaxe mines stone, coal and
  copper/tin/zinc/bismuth ore (tier 1), nothing harder, durability 40.
- **Recipes:** wooden tools and the classic stone/iron/gold/diamond tool recipes are off; tools already made still work.
  Planks by hand and the crafting table stay. The iron Tool Forge stays (gems, fancy heads).

## Part 3 — Fire and clay

- **Firestarter** *(item, every world)*: 2 sticks diagonal. Hold use for about 1 s; about 30% success per try (70% with
  straw on the spot), 8 uses. Lights firepits, forges, pit kilns, charcoal pits.
- **Firepit** *(every world; your campfire)*: our campfire becomes a TFC firepit, keeping its 4 cooking spots. Made by
  dropping 3 sticks on the ground and using the firestarter on them (immersive), or crafted as now (classic). A fuel slot
  takes logs, sticks, charcoal, coal; each fuel has a peak temperature and burn time (TFC: wood 600–760 °C for 1000–2250
  ticks, charcoal 1350 °C/1800, coal 1400 °C/2200). The fire's temperature climbs toward its fuel's peak and falls without
  fuel; it goes out when the fuel is gone; rain puts out an uncovered one. It cooks food as now and heats what's on it.
- **Item heat** *(every world)*: metal, ore and pottery stacks carry a temperature (tag `heat`) that eases toward their
  surroundings over time. It shows as a glow colour on the model and in the tooltip, and pouring and smithing check it.
- **Straw, scythe and thatch** *(every world)*: cutting tall grass with a knife or scythe gives straw (pit kiln fuel,
  thatch blocks and roofs, straw beds). The scythe (knapped, cast or forged) cuts a 3×3 patch of grass and crops.
- **Clay knapping:** the same screen with 5 clay balls; 5 are used when a shape is made. Shapes (TFC): vessel, jug, pot,
  bowl, ingot mold, and tool-head molds for pickaxe, axe, shovel, hoe, sword, knife, hammer, saw; fire clay (4 clay balls +
  1 gravel, crafted) makes the crucible. All are unfired.
- **Pit kiln:** sneak-place up to 4 unfired pieces on a solid top (one per quarter, as TFC). Use with straw adds straw
  (8; wheat or hay); then logs (8). Light with the firestarter; needs solid sides and floor. After about 3
  in-game hours each piece is fired. Losing the fire or a side loses the fuel.
- **Charcoal pit:** log piles touching each other, every exposed face against dirt, stone or another pile (nothing that
  burns). Light one: it spreads to the touching piles; after about 6 in-game hours each pile becomes charcoal, 25–50% of
  its logs. A face left open burns.

## Part 4 — Metallurgy (full TFC, our metals)

**New ores:** zinc (sphalerite) and bismuth (bismuthinite), generated like tin and copper; ore pieces, ingots, surface
samples.

**Ore** *(immersive)*: ore blocks drop ore pieces worth 25 units (100 units = 1 ingot); 1 in 3 surface rocks above an ore
body is a small sample worth 10 units (a prospecting hint, and enough for a first tool). The furnace no longer smelts ore
(it keeps food, glass, bricks).

**Every metal carries its makeup.** Liquid metal, ingots, heads and tools carry `comp` (metal → fraction).
- **Named alloys** (TFC's ranges, widened a little): bronze Cu 85–95 / Sn 5–15; brass Cu 85–95 / Zn 5–15; bismuth bronze
  Zn 20–30, Cu 50–65, Bi 10–20; black bronze Au 10–25, Cu 50–70, Ag 10–25; rose gold Au 70–85, Cu 15–30; sterling silver
  Ag 60–80, Cu 20–40. Each has its own ingot and stats, better than the average of its parts. Our bronze ingot becomes the
  bronze alloy; steel stays the iron route's.
- **Any other mix** is allowed: stats are the proportion-weighted blend of its metals' stats (tier = the weakest metal's),
  plus a small toughness bonus per extra metal and a penalty for heavy impurity; named "Copper-Zinc-Tin alloy" etc.
- **Colour** is the proportion-weighted blend of its metals' colours, over the sprite's shading: ingots, poured heads and
  finished tools.
- **Into our part tools:** a cast or forged head becomes that tool's material; heads of different makeups still combine
  across a tool's sections at the crafting table (core/tool_parts). Each section keeps its makeup; stats and per-section
  colours follow it.
- Melting point is the weighted blend of its metals'; the anvil tier needed is its strongest metal's.

**Melting and casting.** A fired vessel takes 4 slots of ore; heated in a firepit or forge above the melt point its
contents become liquid metal with their makeup (lost if it cools for long: about 11 in-game hours of pour time, as TFC).
Use the hot vessel on a fired mold to pour (1 unit per tick, 100 units fill a mold): copper, bronze and the other copper
alloys, gold and silver for tool heads; any metal for an ingot mold. Break the mold for the piece (molds can break, 10%).

**Cooling and quenching.** Hot metal cools toward the air; dipped in water (a source, a cauldron or a barrel) it cools
fast with a hiss and steam. Quenching a finished head while hot gives it a small toughness bonus (+5% durability, once).
Holding something above about 100 °C bare-handed singes you a little now and then (never more than half a heart a few
seconds apart); **tongs** (knapped wood-and-stone, or forged) carry hot things without it.

**Prospector's pick.** Cast or forged. Use on rock: a message naming the most plentiful ore within 12 blocks and roughly
how much ("traces of", "a small/large amount of", "a very large amount of"), or "nothing of note". Uses wear it.

**Forge and crucible.** Charcoal (a full charcoal pile) in a pit ringed by stone, lit with the firestarter: up to about
1500 °C, more with bellows; it heats items in its slots and stops in the rain. A fire-clay crucible on it melts larger
batches (up to 30 ingots) and the fantasy metals (mithril, adamantite, thallasium, cinderite) at high heat.

**Iron.** A bloomery (TFC structure: the bloomery block in a stone chimney; 8/16/24 ore by height) with ore and charcoal
1:1 for about 5 in-game hours gives a bloom; refining it on the anvil (hit ×3) gives wrought iron. **Steel:** our blast
furnace takes iron and charcoal to pig iron; working pig iron on the anvil gives steel.

**Anvil (TFC-lite).** A stone anvil is a hammer used on raw stone (tier 0: soft metals, double ingots and sheets, and the
first anvil). Metal anvils are forged from double ingots on a lower anvil; tiers copper 1, bronze-family 2, wrought iron 3,
steel 4, mithril 5, adamantite 6. Smithing: the piece must be at working heat (60–100% of its melt point). Pick the piece
to make; a 0–150 bar shows the target (70–119, fixed per recipe per world). Eight moves shift your pointer: light, medium,
heavy hit −3/−6/−9, draw −15, punch +2, bend +7, upset +13, shrink +16 (each costs hammer wear). The plan's last three
steps show as icons. Forgiving: finishing within ±3 of the target works; full quality only exact and on plan (a
durability bonus, as TFC's skill buffs), less quality otherwise; the pointer can't pass 150 (no ruined metal). Every move
has a controller button. It makes tool heads (all eight kinds, plus saw, scythe, tongs and prospector's pick), double ingots, sheets and double
sheets, anvils and refined bloom; heads plus a stick make tools through the part-tool system.

**Sheets and metal armour.** Double sheets (two sheets welded at working heat, with flux) are smithed into helmet,
chestplate, leggings and boots pieces (TFC's unfinished/finished steps), then fitted with padding (cloth or leather) at the
crafting table. Every metal and mix works, through our mixed-armour system (core/tool_parts `pa`): each piece keeps its
makeup, its protection and wear blend by proportion, and its colour is the blend.

## Part 5 — Crafting and materials

- **Saw and lumber:** a saw (knapped flint saw, cast or forged) on a log, or log + saw at the crafting table: 8 lumber
  (immersive; the saw wears). Lumber → planks (4 → 1 block), support beams, barrels, tool handles. By hand a log still
  makes planks, but only 2 (immersive) instead of 4.
- **Quern and flux:** a quern (a stone base + a handstone, both knapped from rock/smoothed stone) turned by holding use:
  grain → flour; ore pieces → ore powder (worth 30 units instead of 25); limestone, borax or bone → flux (needed to weld
  on the anvil); plants → dyes.
- **Barrels and leather:** a barrel (8 lumber) holds 10 buckets of a liquid or 4 stacks of items, and can be sealed. Sealed
  recipes over time: hide + limewater (lime from the quern + water) → soaked hide; scraped with a knife → scraped hide; +
  tannin (bark in water) → prepared hide → leather (on the knapping grid). Brine (salt + water) pickles; vinegar from
  fruit/alcohol. Animals drop raw hides by size.
- **Gold panning and sluice:** a fired clay pan used on gravel or sand in a river: a few seconds of swirling, a chance of
  small ore pieces (copper, tin, gold, the local ores) or small gems. A wooden sluice set in flowing water does it on its
  own slowly (one find a minute or so), wearing out after a while.

## Part 6 — The world

- **Rock layers and geology:** the Overworld's stone in layers by depth and region, TFC's four groups: igneous intrusive
  (granite, diorite, gabbro), igneous extrusive (basalt, andesite, rhyolite, dacite), sedimentary (limestone, shale,
  claystone, chalk, sandstone...) and metamorphic (marble, slate, schist, gneiss, quartzite). Each ore comes in its own
  rocks (TFC's tables: e.g. native copper in igneous extrusive, cassiterite in igneous intrusive, sphalerite in
  metamorphic, bismuthinite in sedimentary...), as veins and clusters. Loose rocks and the knapped stone tools take the
  local rock. Existing worlds keep their old land; new chunks in old worlds are untouched (the rule is new worlds only).
  Uses the rock blocks we have where they exist, adds the missing ones.
- **Cave-ins and supports** *(immersive)*: mining rock where a wide span over you has nothing holding it up can shake
  loose a little rubble (a few cobble falling from the roof, about 1% per block mined in an unsupported span wider than 5
  blocks; a single falling block does at most a heart and a half). Wooden support beams (lumber) within 4 blocks make a
  span safe. Never chains, never buries you.
- **More crops and plants:** TFC's crops we don't have yet (barley, oat, rye, rice, maize, onion, garlic, cabbage, soybean,
  squash, tomato, bell peppers, green beans, jute, sugarcane where missing), its fruit trees (apple, banana, cherry, green
  apple, lemon, olive, orange, peach, plum: seasonal flowering and fruit), berry bushes (wintergreen, blueberry,
  raspberry, strawberry, blackberry, bunchberry, cranberry, snowberry, elderberry, gooseberry, cloudberry) and wild plants,
  each in its climate. Jute makes rope and sacks. No soil nutrients.

## Part 7 — Life

- **Animal husbandry:** animals are male or female (shown on look), born young and growing up over in-game days.
  Feeding one its food daily raises familiarity (a heart meter on look); familiar adults breed (one male, one female),
  give more (milk, wool, eggs) and don't flee. Neglected ones slowly forget. Wild animals stay wild.
- **Spindle and cloth:** wool → a spindle (a clay whorl + a stick) spun by holding use → yarn; yarn on our loom → wool
  cloth; jute → burlap; cloth for armour padding, sails, beds, banners.

## Part 8 — Machines and villages (added 2026-11-10 at the user's request)

- **Angled shafts:** our mechanical parts (windmills, water wheels and their shafts) get a gearbox block that turns a
  shaft through 90° (and splits it), so power can be brought anywhere.
- **Powered machines:** a shaft into a quern grinds whatever is set on it (a hopper above feeds it, the output drops or
  goes into a container below); a shaft into bellows works them (the forge beside gets its bellows heat without anyone);
  likewise any machine the shafts reach. Redstone can switch a gearbox (a powered gearbox disengages), so machines can be
  turned on and off with levers, timers and the rest.
- **Villages:** the professions take up the new ways: smiths work at a forge and anvils (and trade worked heads, double
  ingots, tongs, anvils), potters/masons fire pit kilns (vessels, molds, crucibles), farmers grind at querns (flour), tanners
  (leather workers) keep sealed barrels (hides, leather), carpenters saw lumber; village buildings get the new blocks
  (forges, anvils, querns, barrels, log and ingot piles) where those professions work, and their trades offer them.

## Pacing

About a third of TFC's: pit kiln ~3 in-game hours, bloomery ~5 h, charcoal pit ~6 h.

## Testing

Each part has headless tests (dqcore or dqclient test exes): pile capacity/layout/stacking; tree harvest rules per mode;
knapping pattern matching (offsets, mirroring, cost); recipe gating per mode; fire temperature curves and fuel; heat
cooling; kiln and charcoal pit outcomes and their validity checks; alloy classification at range edges and free-mix
stats/colour; casting units; anvil value arithmetic, tolerance and plan rules; classic worlds unchanged. Smoke test with
screenshots for each new render (piles, ground items, knapping and anvil screens, firepit, kiln).

## Order

Part 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8, each its own plan, built and checked before the next. (Straw and the scythe come in
Part 3, where the pit kiln needs them.)
