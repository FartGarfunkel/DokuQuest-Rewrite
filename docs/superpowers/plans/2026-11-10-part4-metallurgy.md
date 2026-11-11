# Part 4 — Metallurgy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Full TFC metallurgy with our metals: zinc and bismuth; ore pieces and surface samples; every metal carrying its
makeup (named alloys or any mix) with stats, name and colour blended by proportion through our part tools; vessels melting
ore on a fire and pouring into molds; the forge and crucible; the bloomery and steel; the TFC-lite anvil; cooling,
quenching, tongs; the prospector's pick; sheets and metal armour.

**Architecture:**
- `core/metals.{h,cpp}` (data `data/metals.json`): metals, alloys, units, `Comp` (metal → fraction), naming, blending,
  `compOfStack`. Core, so `core/tool_parts` can use it.
- `core/tool_parts`: a material key `@<comp>` is a dynamic ToolMaterial (blended stats/colour/name); `toolMaterialOfStack`
  (comp-tagged stacks); more part-tool kinds (knife, hammer, saw, scythe, javelin, propick) with weights and `mixed_*`
  items. A cast/forged head `metal_<kind>_head` {comp} + a haft at the crafting table → the part tool of that makeup.
- `sim/metalwork.{h,cpp}`: vessel contents and melting (on a fire's heat step), pouring into molds, breaking molds, ore
  drops in immersive worlds, the furnace's ore gate.
- `sim/forge.{h,cpp}`, `sim/bloomery.{h,cpp}`: structures, ticks (content system), lit by the firestarter.
- `sim/anvil.{h,cpp}` (pure rules: value, moves, plans, tolerance, quality, recipes) + `ui/anvil_screen.cpp`.
- `sim/prospect.{h,cpp}`, quench/tongs/singe in `sim/heat.cpp`.
- Data: `tools/port/immersive_data.py` (items, recipes, gating), `data/metals.json`, worldgen ores in `src/gen`.
- Tests: `tools/test_metal.cpp` (dqcore).

**Spec:** `docs/superpowers/specs/2026-11-10-immersive-progression-design.md` (Part 4)

## Global Constraints
- Units: ore piece 25, small sample 10, powder 30 (Part 5), ingot 100, double ingot 200, sheet 200, double sheet 400.
- Named alloy ranges (widened TFC): bronze Cu 85–95/Sn 5–15; brass Cu 85–95/Zn 5–15; bismuth bronze Zn 20–30, Cu 50–65,
  Bi 10–20; black bronze Au 10–25, Cu 50–70, Ag 10–25; rose gold Au 70–85, Cu 15–30; sterling silver Ag 60–80, Cu 20–40.
- Anvil: target 70–119 fixed per recipe per world; moves −3 −6 −9 −15 +2 +7 +13 +16; ±3 tolerance; can't pass 150;
  working heat 60–100% of melt; welding 80–100% + flux; tiers copper 1, bronze-family 2, wrought iron 3, steel 4, mithril
  5, adamantite 6.
- Classic worlds: nothing here changes them except new items/blocks existing (ores generate everywhere in new chunks).

## Review Focus
1. A comp that sums to slightly more or less than 1 (rounding) still classifies; a comp with one metal at 100% is that
   metal (not a "mix").
2. Pouring 100 units from a vessel holding 175: 75 stay molten in it.
3. A vessel cooled below melt keeps its metal solid in it ("solid metal") and melts again on the fire.
4. Anvil value clamped at 0 and 150; tolerance ±3 inclusive; a recipe needing tier 3 on a tier-2 anvil is refused.
5. Part tools with `@comp` materials survive save/load (strings in the tag) and render (model key carries the comp).

---

### Task 1: Metals core
`data/metals.json` (metals: copper, tin, zinc, bismuth, gold, silver, iron, steel, pig_iron, mithril, adamantite,
thallasium, cinderite — name, melt °C, rgb, tier, speed, damage, durability (a pickaxe's), anvil tier, cast bool, ingot
item; alloys: the six with ranges and own stats; ores: item → metal + units). API in `core/metals.h`:
`const Metal* metalDef(key)`, `using Comp = std::map<std::string,float>`, `Comp normComp(Comp)`, `std::string
compKey(const Comp&)` ("@copper=0.88,tin=0.12", fractions to 2 places, sorted), `Comp parseCompKey(str)`,
`std::string namedAlloy(const Comp&)` ("" if none; a single metal ≥ 0.98 → that metal), `Metal blendMetal(const Comp&)`
(named alloy/metal: its own; else proportion-weighted stats, tier = min, durability ×(1 + 0.05 per metal ≥ 5% beyond the
first) × 0.85 if the biggest share < 0.8), `std::string compName(const Comp&)` ("Bronze", "Copper", "Copper-Zinc-Tin
Alloy" largest first), `Comp compOfStack(const ItemStack&)` (tag "comp", else the item's own metal), `int unitsOf(id)`.
- [ ] Tests: 0.9/0.1 Cu/Sn is bronze; 0.84/0.16 isn't (free mix); 1.0 copper is copper; key round trip; blended colour
  between the two; tier of a mix = the weaker; bronze stats better than the copper/tin average; names.

### Task 2: Dynamic materials in part tools
`toolMaterial("@…")` synthesizes (cached, mutex) a ToolMaterial {stats from blendMetal, rgb, name compName}; new kinds
knife, hammer, saw, scythe, javelin, propick (weights single-section; `mixed_<kind>` items made by the data script; model
families: knife→dagger, javelin→sword, scythe→hoe, propick→pickaxe, hammer/saw → their own simple families in
`assets/item_parts.cpp`); `toolMaterialOfStack`; crafting: one `metal_<kind>_head` {comp} + haft → `makePartTool(kind,
{compKey × sections}, haft)`; the mixed patterns accept comp-tagged ingots per section.
- [ ] Tests: bronze head + stick → a mixed pickaxe named "Bronze Pickaxe", tier 2, durability above copper's; a copper
  section + a tin section → "Copper & Tin ..."; the model key holds the comps.

### Task 3: Ores, pieces, samples, worldgen
Blocks `zinc_ore` (sphalerite), `bismuth_ore` (bismuthinite) generated like tin; items `<metal>_ore_piece` (25 u) and
`<metal>_ore_sample` (10 u) for copper, tin, zinc, bismuth, iron, gold, silver; `zinc_ingot`, `bismuth_ingot` and the
alloy ingots (`brass_ingot`, `bismuth_bronze_ingot`, `black_bronze_ingot`, `rose_gold_ingot`, `sterling_silver_ingot`,
`alloy_ingot` {comp}); ground blocks `<metal>_sample` (like loose_stones, ore tile, drop the sample) placed 1 in 3 on a
loose-rock spot with that ore within 35 blocks below; immersive: ore blocks drop 1–2 pieces, the furnace won't smelt ore.
- [ ] Tests: generated chunks hold zinc and bismuth ore; samples sit over ore; immersive mining copper ore drops pieces;
  furnace refuses copper ore in immersive, smelts it in classic.

### Task 4: Vessels, melting, casting
Ceramic vessel: holds 4 stacks of ore/metal items (tag `c`), filled by use with it in hand and ore in the other hand (or
a screen later); on a firepit/forge its heat step melts when heat ≥ the mix's melt: tag `metal` {comp, u}, contents gone.
Pour: use the molten vessel with a ceramic mold in the off hand (or the first in the inventory): 100 u (ingot mold any metal;
tool molds castable metals only) → `filled` mold (tag metal, heat). Use a filled mold once below 50% of melt → the piece
(`metal_<kind>_head` {comp} or the metal's ingot / `alloy_ingot` {comp}); 10% the mold breaks.
- [ ] Tests: vessel with 3 copper pieces + 1 tin sample → on a hot fire → molten 85 u of ~88/12 bronze; pour into ingot
  mold refused (< 100 u); 4 copper pieces → 100 u copper poured into a pickaxe mold → cooled → copper pickaxe head with
  comp copper 1.0.

### Task 5: Forge, crucible, bellows
Block `forge` (made: sneak + use charcoal on a pit ringed by stone → forge with that charcoal; up to 8 charcoal), lit by
firestarter, temp → 1300 °C (+200 with bellows block beside, 1500 max), 5 item slots heating (use / empty hand), rain puts
it out. Crucible (item) in a forge slot: holds up to 3000 u, filled like a vessel, melts anything to 1700 °C (fantasy metals
need the forge at full heat with bellows); pours like a vessel.
- [ ] Tests: forge reaches > 1200 °C with charcoal; an iron ingot heats to working heat; the crucible melts mithril only
  with bellows.

### Task 6: Anvil (rules + screen)
Stone anvil (hammer used on raw stone: the block becomes `stone_anvil`, tier 0); metal anvils (items/blocks
`<metal>_anvil`, smithed from 4 double ingots); recipes (`data/anvil.json`): ingot → each head kind (plans as TFC: pickaxe
PUNCH last, BEND not last, DRAW not last; axe PUNCH last, HIT 2nd, UPSET 3rd; shovel PUNCH last, HIT not last; hoe PUNCH
last, HIT not last, BEND not last; sword HIT last, BEND 2nd, BEND 3rd; knife HIT last, DRAW 2nd, DRAW 3rd; hammer PUNCH
last, SHRINK not last; saw HIT last, HIT 2nd; scythe HIT last, DRAW 2nd, BEND 3rd; javelin HIT last, HIT 2nd, DRAW 3rd;
propick PUNCH last, DRAW not last, DRAW not last), double ingot → sheet, bloom → wrought iron ingot (HIT ×3), pig iron →
steel ingot (HIT last, HIT 2nd, HIT 3rd... 3 times), double ingot weld (2 ingots, flux), double sheet weld, armour pieces
from double sheets, anvils from double ingots. Rules: `sim/anvil.h` pure (value, apply move, plan check, done/quality),
the screen `ui/anvil_screen.cpp` (bar, pointer, target, 8 buttons with icons/labels, plan icons, controller buttons).
Quality: exact + on plan → +15% durability; within tolerance → −10% per point off; off plan → −10%.
- [ ] Tests: move arithmetic and clamp; plan rules; target fixed per world+recipe; a cold ingot refused; tier gate; a
  finished copper pickaxe head carries the ingot's comp and its quality.

### Task 7: Bloomery and steel
Block `bloomery` (needs a chimney of stone bricks 1–3 above: 8/16/24 capacity), loaded by use with iron ore pieces and
charcoal 1:1, lit by firestarter, 5 in-game hours → `raw_bloom` {u} (dropped/stored), refined on the anvil →
`wrought_iron`… our `iron_ingot` (iron is wrought iron here). Steel: blast furnace smelts `steel_blend` into `pig_iron` in
immersive; pig iron on the anvil → `steel_ingot`.
- [ ] Tests: bloomery with 8 pieces + 8 charcoal and a 1-high chimney → after 5 h a bloom of 200 u; without a chimney it
  refuses.

### Task 8: Cooling, quenching, tongs, prospector's pick, sheets & armour, gating
Quench: use with a hot item on water (source, cauldron) → heat to 20, hiss + steam; a hot `metal_*_head` quenched gets
`q: 1` (+5% durability when made into a tool). Singe: holding > 100 °C in the main hand without tongs in the off hand → 1
damage every 3 s. Tongs: knapped (stick + stone head) / forged. Prospector's pick (part-tool kind propick): use on stone →
toast naming the most plentiful ore within 12 blocks by amount (traces < 10, small < 40, large < 120, very large).
Armour: double sheets on the anvil → `<piece>` parts → `pa` armour with comp per cell (core/tool_parts makePartArmor with
`@comp` materials). Gating: classicOnly on metal tools/armour shaped recipes and the iron/gold/copper smelting of ore in
immersive.
- [ ] Tests: quench; singe and tongs; propick near a placed vein; a bronze helmet from the anvil has bronze's protection;
  iron pickaxe recipe off in immersive.
- [ ] Full suite; CHANGELOG; ledger.
