# Part 5 — Crafting and materials Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Saw and lumber; the quern (flour, ore powder, flux, lime, dyes); sealable barrels and leather tanning; gold
panning and the sluice.

**Architecture:** `sim/quern.{h,cpp}`, `sim/barrel.{h,cpp}`, `sim/panning.{h,cpp}`; data via
`tools/port/immersive_data.py` (items, blocks, recipes, `data/quern.json`, `data/barrel.json`); crafting: a tool-kept
recipe kind (`keepTool: "saw"`: the saw stays in the grid, worn by one) and an `immersiveOnly` gate beside `classicOnly`.
Tests: `tools/test_materials.cpp` (dqcore).

**Spec:** `docs/superpowers/specs/2026-11-10-immersive-progression-design.md` (Part 5)

## Rulings made while planning
- R1: Brine, pickling and vinegar are left out: there is no food decay (the user declined it), so they'd do nothing.
- R2: One `raw_hide` item (no small/medium/large); in an immersive world animals that dropped leather drop raw hide.
- R3: Prepared hide → leather at the crafting table (2 leather), not on a knapping grid.
- R4: The TFC barrel is a new block `sealed_barrel` ("Barrel (sealable)"); our storage barrel stays as it is.
- R5: Panning: the pan (clay knapped, fired) used on gravel or sand with water beside it; 2 s of holding use; a find
  30% (an ore sample of the metals that cast or iron, weighted), 3% a small gem (a nugget of the local gems: emerald,
  ruby, sapphire, diamond rarer), else nothing; one in four tries uses up the block.
- R6: The sluice: a block in flowing water beside gravel or sand; a find into its slot every 60 s (the same table), worn
  out after 64 finds.

## Tasks
### Task 1: Saw, lumber, planks, support beams
Items `lumber`, `flint_saw_head` (knapped `"#    ","##   ","###  ","#### ","#####"`... a distinct flint shape) + `flint_saw`
(saw: true); recipes: log + any saw (keepTool) → 8 lumber; 4 lumber (2×2) → 1 planks block... (planks item); immersive:
log → 2 planks (`immersiveOnly`), the classic log → 4 planks `classicOnly`; 3 lumber → 2 `support_beam` (a block: a thick
post, solid, used by Part 6).
- [ ] Tests: log + saw → 8 lumber and the saw stays with one more wear; immersive log → 2 planks, classic → 4.

### Task 2: Quern
Block `quern` (recipe: 3 smooth stone over a `handstone`; handstone = 2 smooth stone + stick); use with an item on it,
hold use 1 s: `data/quern.json` table: wheat → flour, `<metal>_ore_piece` → 1 `<metal>_ore_powder` (30 u, so a piece
yields more), limestone → 2 flux, bone → 2 bone meal (as now) + 1 flux, cobblestone → gravel, gravel → sand, flowers →
their dye ×2. One per grind, the result dropped on top.
- [ ] Tests: each table entry; powder units 30.

### Task 3: Barrel and leather
Block `sealed_barrel` (8 lumber), entity {liquid, amount (0..10 buckets), items[4], sealed, t, recipe}. Use with a water
bucket: +1 water; an empty bucket: −1 (if water); items in by use; empty hand takes back (not sealed); sneak + use seals /
unseals. `data/barrel.json` recipes (sealed, liquid + items, hours): water + `lime` → limewater (instant); water + log →
tannin (1 h, the log used); `raw_hide` + limewater → `soaked_hide` (3 h); `scraped_hide` + tannin → `prepared_hide` (3 h).
Knife used with a soaked hide in the off hand → scraped hide. Prepared hide → 2 leather (crafting). Animals (cow, horse,
pig, sheep, goat, deer, bear, llama) drop raw hide instead of leather in an immersive world.
- [ ] Tests: limewater; soaked after 3 h sealed; not when unsealed; knife scraping; leather.

### Task 4: Panning and sluice
Clay pattern `unfired_pan` (`"#   #"," ### "`... a distinct shape) → `ceramic_pan`; `sim/panning` table; use as R5; the sluice
as R6 (block `sluice`, 6 lumber; entity {finds, out[]}; ticked from sim/content.cpp).
- [ ] Tests: panning with a fixed roll gives a sample; no water beside: nothing; a sluice in flowing water gathers a find
  after 60 s; dry: nothing.
- [ ] Full suite; CHANGELOG; ledger.
