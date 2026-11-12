# Part 3 — Fire and clay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Firestarter; the campfire as a TFC firepit (fuel, temperature, rain, heating); item heat; straw, scythe and
thatch; clay knapping into unfired pottery and molds; the pit kiln; the charcoal pit.

**Architecture:** Pure sim modules, each one feature: `sim/heat.{h,cpp}` (item temperature in the stack tag, cooling,
names/colours by temperature), `sim/firepit.{h,cpp}` (fuel table, the fire's temperature, lighting from sticks, ticking
inside `campingTick`), `sim/firestarter.{h,cpp}` (hold-use progress and what it lights), `sim/pit_kiln.{h,cpp}`,
`sim/charcoal_pit.{h,cpp}`, `sim/straw.{h,cpp}`. Data from `tools/port/immersive_data.py` (extended). Tests in
`tools/test_fire.cpp` (dqcore).

**Spec:** `docs/superpowers/specs/2026-11-10-immersive-progression-design.md` (Part 3)

## Global Constraints
- Time: one in-game hour = (dayMinutes + nightMinutes) × 60 / 24 seconds (`sim/rules.h` ruleNum); pit kiln 3 h,
  charcoal pit 6 h (a third of TFC's).
- Every world gets the firepit, firestarter, heat, straw, clay pottery, kilns and charcoal pits; immersive only marks the
  campfire recipe classic-only (you make a firepit from sticks).
- No hunger; controller: everything here is Use / hold Use.

## Review Focus
1. A firepit loaded from an old save (entity without fuel/temp fields) keeps working (defaults) and stays lit as before
   until its fuel logic runs out — old campfires get 1 in-game day of free burn so nobody's camp dies on load.
2. Rain on a covered firepit (a roof over it) must not put it out.
3. Pit kiln/charcoal pit validity when a neighbouring chunk isn't loaded: treat unknown as solid (never fail on load).
4. Heat on stacks: a hot ingot won't merge with a cold one (tag differs) — taking from a pile must keep heat.
5. The firestarter's hold progress resets when the target changes.

## Rulings made while planning
- R1: Heat cools in the player's inventory only (and on firepits/kilns heats); items in chests keep their temperature
  until taken (cheap; chests rarely hold hot metal).
- R2: A firepit's fuel holds up to 4 items (TFC's 4 fuel slots), added by use; logs, planks, sticks, charcoal, coal.
- R3: Charcoal from a finished pit goes into a ground-storage pile (the existing `item_pile`), not a new block.
- R4: Straw comes from any grass plant broken with a knife or scythe; by hand grass gives what it gave before.

---

### Task 1: Item heat (`sim/heat`)
`float itemHeat(const ItemStack&)` (°C, 20 if none); `void setItemHeat(ItemStack&, float)` (≤ 25: tag removed);
`bool heatable(const std::string& id)` (ingots, ores, ore pieces, heads, double ingots, sheets, blooms, unfired/ceramic
pottery, crucible); `float heatTowards(float h, float target, float dt)` (rises 60 °C/s toward a hotter target, cools
2%/s of the gap); `std::string heatName(float)` (TFC: Warming ≥ 80, Hot ≥ 210, Very Hot ≥ 480, Faint Red ≥ 580, Dark Red
≥ 730, Bright Red ≥ 930, Orange ≥ 1100, Yellow ≥ 1300, Yellow White ≥ 1400, White ≥ 1500, Brilliant White ≥ 1600);
`u32 heatGlow(float)` (rgb, 0 below 480). Player inventory cools every second (`sim/worldsys` or campingTick).
Tooltip line in `ui/item_tooltip_extra.cpp`.
- [ ] Tests: set/get round trip; 1000 °C cools below 600 within 60 s and never below 20; a hot and a cold ingot don't
  merge; names at the band edges.

### Task 2: Firepit (`sim/firepit`) and the firestarter (`sim/firestarter`)
Fuel table `FuelInfo fuelOf(id)` {peak °C, seconds}: logs/wood 650/75, planks 550/40, stick 450/20, charcoal 1350/90,
coal 1400/110. Campfire entity gains `fuel` (array ≤ 4 stacks of 1), `burn`, `temp`. Tick (host): lit → temp eases to
current fuel's peak (+15 °C/s up, −8 °C/s down with no fuel burning); burn counts down, next fuel taken when it ends; out of
fuel and temp < 80 → goes out (CAMPFIRE_OUT). Raining with sky light 15 straight above → out. Cooking needs temp ≥ 150.
Non-food heatable items can be set on the spots (use with them) and heat toward the fire's temp; taken back with use (empty
hand / sneak). Old entities without `temp`: treated as a fire burning wood with 1 in-game day of `burn`.
Firestarter: item `firestarter` (2 sticks diagonal; 8 uses as durability). Holding use on a target accumulates (use repeat
calls); after 1.0 s a roll: 30% (+40% if straw lies with the sticks) → lights: an out campfire; 3+ dropped sticks lying on a
solid top → a new lit firepit there (sticks used up, 20 s of burn); a full pit kiln; a log pile (charcoal pit). Each try
wears it 1.
- [ ] Tests: fuel times; a lit pit with 1 log reaches > 600 °C then goes out after ~75 s + cooling; rain + open sky puts it
  out, a roof doesn't; 3 sticks on the floor + firestarter held for 1.2 s with a fixed RNG → a lit campfire; an old entity
  keeps burning.

### Task 3: Straw, scythe and thatch
Items `straw`, `flint_scythe_head` (knapped: `"#####","#####","   ##"`-like distinct shape), `flint_scythe` (tool
"scythe": `knife: true`, `scythe: true`), block `thatch` (4 straw shaped 2×2; texture hay-like `hay_block` side), recipe
straw ×4 → thatch. Breaking a grass plant (tall grass, short grass, fern) with a knife or scythe drops 1 straw; a scythe
breaks the 3×3 of grass plants and crops around.
- [ ] Tests: knife on tall grass → straw; hand → not straw; scythe clears a 3×3 of grass.

### Task 4: Clay knapping and pottery
Clay patterns in `data/knapping.json` (kept cells): vessel `" ### ","#####","#####","#####"," ### "`; jug
`"  #  "," ### ","#####","#####"," ### "`; pot `"#   #","#   #","#   #","#   #","#####"`; bowl `"#   #","#   #"," ### "`; ingot
mold `"#####","#   #","#####"`; tool molds = the complement (in 5×5) of each stone head pattern placed top-left (pickaxe,
axe, shovel, hoe, knife, hammer, javelin) plus sword `"   ##","  ###"," ### ","###  ","##   "`, saw `"#    ","##   ","###  ","#### ","#####"`,
scythe `"#####","#####","   ##"` and prospector's pick `"  ###","#####","  ###"` complements. Fire clay: `fire_clay` = 4
clay_ball + gravel (shapeless); material "fireclay": crucible `"#   #","#   #","#   #","#   #"," ### "`. Items
`unfired_X` (each with prop `fires: "ceramic_X"`) and `ceramic_X`; `crucible` from `unfired_crucible`.
- [ ] Tests: every clay pattern distinct and makes a real `unfired_*`; each `unfired_*` names an existing fired item; the
  vessel pattern matches; mold of the pickaxe matches.

### Task 5: Pit kiln (`sim/pit_kiln`)
Block `pit_kiln` (render 0; shape like a pile, 2 px + straw/log height by meta), entity `{type:"kiln", items[4], straw,
logs, lit, t}`. Sneak + use with unfired pottery on a solid top → a kiln with it in the pointed quarter (or added to one).
Use with straw (≤ 8, needs ≥ 1 item), then logs (≤ 8, needs straw 8). Firestarter on a full kiln (straw 8, logs 8, solid on
4 sides and below) → lit. Lit: t counts to 3 in-game hours; a side opened → goes out, fuel lost. Done: every `unfired_X`
becomes `fires` with heat 600 °C; straw/logs 0; lit false. Use with empty hand takes an item back (not while lit). Drawn
in `render/pile_layer.cpp` (items, straw layer, log layer, flames while lit via fx particles from the tick).
- [ ] Tests: place → straw → logs → light → after 3 h (ticked) the pieces are fired; an open side stops it; lighting
  before full is refused.

### Task 6: Charcoal pit (`sim/charcoal_pit`)
Firestarter on a log pile → it and every log pile touching it (6-connected, flood ≤ 64) light (entity `lit`, `t`). Every
2 s each lit pile checks its 6 faces: another log pile, or a solid non-flammable block (opaque, not wood/leaves/wool);
an open face → that pile is lost (block → fire for a moment, then air). After 6 in-game hours each lit pile becomes an
`item_pile` holding charcoal = logs × (25–50%), at least 1.
- [ ] Tests: 4 piles in a dirt-covered pit lit → after 6 h four charcoal piles, each 4–8 charcoal from 16 logs; one pile
  with an open top → lost, the others fine.
- [ ] Full suite; CHANGELOG; ledger.
