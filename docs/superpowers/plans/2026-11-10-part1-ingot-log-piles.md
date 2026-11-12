# Part 1 — Ingot piles and log piles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** TFC-style ingot piles (64 per block, 8 per layer as 2 rows of 4, alternating courses, height by count, stacking
upward) and log piles (16 logs in a crossed crib, stacking upward) in every world.

**Architecture:** The rules live in a pure sim module (`src/sim/piles.{h,cpp}`): placing, adding, taking, topping up the
pile below, the block's height in its meta, and the slot layout (which render reads). Two new blocks (`ingot_pile`,
`log_pile`) hold a block entity `{type: "ingots"|"logs", item: stack}` — `spillEntity` already drops an `"item"` when the
block breaks. The controller routes sneak+use with an ingot or log to the module; the pile layer draws them from the
layout functions.

**Tech Stack:** C++17, glm, nlohmann json; headless test exe on dqcore.

**Spec:** `docs/superpowers/specs/2026-11-10-immersive-progression-design.md` (Part 1)

## Global Constraints

- One feature per file, under ~1500 lines; register through existing hooks (no static constructors).
- Full controller support: everything here is the existing Use / Sneak actions.
- Every world (not gated by the immersive rule).
- Patch notes end with "Removed Herobrine."
- Existing ground-storage piles (`item_pile`, type "pile") keep working unchanged.

## Review Focus

1. A pile entity with a missing/garbled `item` (old save, hand-edited) — must not crash; treated as empty and removed.
2. Placing an ingot pile where the block above the target is not replaceable, or on a non-solid target — nothing happens,
   the ingot stays in hand.
3. Mixed metals: a different ingot on a pile must not merge; it starts a new pile on top only if this one is full,
   otherwise refuses ("One metal to a pile").
4. Taking the last ingot/log removes the block; taking with a full inventory leaves the pile unchanged.
5. Tagged ingots (an alloy's makeup in its tag, Part 4) must merge only with identical tags (`canMergeStacks`).

---

### Task 1: The pile rules (pure sim module) and its test exe

**Files:**
- Create: `src/sim/piles.h`, `src/sim/piles.cpp`
- Create: `tools/test_piles.cpp`
- Modify: `CMakeLists.txt` (add `test_piles` linking dqcore, `add_test`, and add `test_piles test_anim` to the
  `dq_runtime_data` foreach at line ~170)
- Modify: `data/blocks.json` (two blocks, see Interfaces)

**Interfaces:**
- Produces (sim/piles.h):
  ```cpp
  enum class PileKind : u8 { None, Ingots, Logs };
  constexpr int INGOT_PILE_MAX = 64, LOG_PILE_MAX = 16;
  PileKind pileKindOfItem(const std::string& itemId);          // "*_ingot" -> Ingots; items in tag "#log" -> Logs
  PileKind pileKindOfBlock(BlockId id);                          // ingot_pile / log_pile / None
  int pileMax(PileKind k);
  u8 pileMeta(PileKind k, int n);                                // ingots: ceil(n/8) (1..8); logs: ceil(n/4) (1..4)
  // sneak+use with `held` on the top face of (x,y-1,z), i.e. into cell (x,y,z): tops up a not-full pile directly below
  // (same item) first, else places a new pile if the cell is free (air/replaceable) and the block under is solid or a FULL
  // pile; moves ONE item (ingots) or ONE log from held. Returns false (held untouched) if nothing was placed.
  bool pilePlace(World& w, int x, int y, int z, ItemStack& held, std::string* why = nullptr);
  // sneak+use with held on an existing pile at (x,y,z): adds one to the lowest not-full pile of the column that holds the
  // same item (so a pile below is topped up first); if all are full, places a new pile on top
  bool pileAdd(World& w, int x, int y, int z, ItemStack& held, std::string* why = nullptr);
  // use on a pile: takes one item off (returned stack, n=1); removes the block at zero. Empty stack if none.
  ItemStack pileTake(World& w, int x, int y, int z);
  // where item i (0-based, in placing order) lies in the block (block units, origin at the block's corner) and its yaw
  struct PileSlot { vec3 pos; float yaw; };
  PileSlot ingotSlot(int i);    // layer i/8 at y = layer*0.125; rows of 4 (x .125,.375,.625,.875) two deep (z .25,.75);
                                // odd layers turned 90° (x and z swapped, yaw +pi/2)
  PileSlot logSlot(int i);      // course i/4 at y = course*0.25 + 0.125; 4 logs side by side, the axis turning 90° per course
  ```
- `data/blocks.json` new entries (ids: next free; keys `ingot_pile`, `log_pile`): `render 0`, `solid true`,
  `opaque false`, `hardness 0.5` (ingots: tool null, sound "metal"; logs: tool "axe", sound "wood"), `drop null`,
  flags `"ingotPile": true` / `"logPile": true`, `name` "Ingot Pile" / "Log Pile".

- [ ] **Step 1: Write the failing tests** in `tools/test_piles.cpp` (setup copied from `tools/test_content.cpp:50-75`:
  initCore, a Level, a stone floor at `Y = dimBase(DIM_OVERWORLD) + 200`). Checks:
  - `pileKindOfItem("iron_ingot") == Ingots`, `pileKindOfItem("oak_log") == Logs`, `pileKindOfItem("stick") == None`.
  - `pileMeta(Ingots, 1) == 1`, `pileMeta(Ingots, 8) == 1`, `pileMeta(Ingots, 9) == 2`, `pileMeta(Ingots, 64) == 8`,
    `pileMeta(Logs, 16) == 4`.
  - Place: held 70 iron ingots, `pilePlace(w, 4, Y+1, 4, held)` → true, block is `ingot_pile`, entity item n == 1,
    held n == 69; 63 more `pileAdd` → n == 64, meta 8; one more `pileAdd` → a new pile at Y+2 with n == 1.
  - Top-up (TFC): with the lower pile full (64) and an upper pile of 1 on it, `pileTake` from the lower (63), then
    `pileAdd` on the upper → the lower is back to 64 and the upper stays at 1.
  - Mixed: gold ingot `pileAdd` on the iron pile → false, why == "One metal to a pile"; held unchanged.
  - Not solid below: `pilePlace` on air cell over air → false.
  - Take: `pileTake` returns an `iron_ingot` n 1 and decrements; taking all removes the block (AIR).
  - Garbled entity: set the pile's entity to `{type:"ingots"}` (no item) → `pileTake` returns empty, block becomes AIR.
  - Layout: `ingotSlot(0).pos == (0.125, 0, 0.25)`, `ingotSlot(7).pos == (0.875, 0, 0.75)`, `ingotSlot(8)` has yaw
    `pi/2` and y `0.125`; all 64 slots inside the block (0..1 on x,z; y < 1); no two slots in one layer closer than 0.2.
  - Logs: same place/add/take for `oak_log` with max 16, meta 4 when full; a different wood type doesn't merge;
    `logSlot(i)` 16 slots inside the block, courses alternating direction.
- [ ] **Step 2: Run** `ninja -C build test_piles` — Expected: compile FAIL (sim/piles.h missing).
- [ ] **Step 3: Implement** `sim/piles.{h,cpp}` and the two blocks. Log slots: course c = i/4 (y = 0.125 + c*0.25), in
  each course 4 logs side by side across the block (centres at 0.125, 0.375, 0.625, 0.875 along one axis), the axis
  turning 90° each course (a crib), yaw 0 / pi/2. Entity: `{type, item: stack json}`; the stack keeps its tag (alloys).
  Set meta via `pileMeta` after every change (`World::setMeta` or setBlock with meta) and call `blockEntityChanged`.
- [ ] **Step 4: Run** `ninja -C build test_piles dq_runtime_data && ./build/test_piles` — Expected: all ok, exit 0.
- [ ] **Step 5:** ledger line (no git in this project).

### Task 2: Collision, selection and drops

**Files:**
- Modify: `src/world/shapes.cpp` (traits read `ingotPile`/`logPile`; boxes for Collide/Select/Draw by meta)
- Modify: `src/core/registry` traits if a flag field is needed (follow `t.pile` at shapes.cpp:133)
- Test: `tools/test_piles.cpp`

**Interfaces:**
- Consumes: `pileMeta` (Task 1).
- Produces: collision box `ShapeBox(0,0,0,16, 2*meta, 16)` for ingot piles (meta 1..8 → 2..16 px), `ShapeBox(0,0,0,16,
  4*meta, 16)` for log piles; select box the same.

- [ ] **Step 1: Failing test:** after 9 ingots, `shapeBoxes(ingot_pile, meta, ..., Collide)` gives height 4 px; after 64,
  16 px; log pile with 5 logs → 8 px. Breaking the pile block via the world's break path drops its contents
  (`spillEntity` with `"item"`): count dropped item entities == 1 stack of n.
- [ ] **Step 2: Run** → FAIL (boxes are the default full cube / none).
- [ ] **Step 3: Implement** the shape cases.
- [ ] **Step 4: Run** `./build/test_piles` → all ok.
- [ ] **Step 5:** ledger line.

### Task 3: Controls

**Files:**
- Modify: `src/sim/controller.cpp` (~line 618 interaction dispatch; ~line 655 sneak+use; ~line 602 log placement)
- Modify: `src/sim/build.cpp` only if a helper is needed

**Interfaces:**
- Consumes: `pilePlace`, `pileAdd`, `pileTake`, `pileKindOfBlock` (Task 1).

- [ ] **Step 1: Failing test** through the controller (`PlayerController` on the test level, as `use(false, input)`
  with `target` set to a ray hit on the floor's top face, player sneaking): with iron ingots in hand → an ingot pile
  appears; with an oak log in hand and sneaking → a log pile (not a placed log block); not sneaking with a log → the log
  block is placed as before (classic placement untouched); use (not sneaking) on the pile with an empty hand → one ingot
  back in the inventory.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement:** in the sneak+use branch, before `putDown`: if `pileKindOfItem(held.id) != None` and the
  hit is a top face → `pilePlace` (toast `why` on failure); for logs this check comes before block placement when
  sneaking on a top face. On a hit pile block: sneaking with an item of its kind → `pileAdd`; otherwise `pileTake` and
  `giveItem` (on a full inventory put it back and toast "No room to pick that up"). Sound: `sfxMaterial(lv, "metal" or
  "wood", "place", …)`; `swingArm()`.
- [ ] **Step 4: Run** `./build/test_piles` and `./build/dq-test-gameplay` → all ok.
- [ ] **Step 5:** ledger line.

### Task 4: Drawing

**Files:**
- Modify: `src/render/pile_layer.cpp` (accept types "ingots" and "logs" at line ~61; draw from `ingotSlot`/`logSlot`)

- [ ] **Step 1:** Draw each of the n items at its slot: ingots as the item model laid flat (as the current bar code does:
  rotate -pi/2 about x, scale so one ingot is a quarter block wide and 1/8 tall); logs as the log block's model scaled to
  a quarter-block-wide, full-length log lying along the slot's yaw (`itemModel(log key)` of Block kind, scaled (1, 0.25,
  0.25) about its centre, turned by yaw).
- [ ] **Step 2:** Smoke test: `DQ_DEV_BLOCKS` can't set entities, so add to the dev aid in `render/item_layers.cpp`:
  `DQ_DEV_PILES="dx,dy,dz,item,n;..."` calls `pilePlace` then `pileAdd` n-1 times. Screenshots of 8, 20, 64 iron ingots
  and 6, 16 logs: expected brickwork courses, height growing, log crib.
- [ ] **Step 3:** Full suite: test_anim, test_weather, dq-test-gameplay, dq-creature-test, test_net, test_content,
  test_magic, test_piles; smoke run of the game.
- [ ] **Step 4:** CHANGELOG "Unreleased" lines (ends "Removed Herobrine."), ledger line.
