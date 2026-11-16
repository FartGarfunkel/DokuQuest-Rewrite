# Moving Structures — Stage 1 (core) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Glue, the Hinge and the Slider work in survival: a glued piece swings or slides smoothly as one body, drawn like its blocks, and is set back as ordinary blocks with its contents and facings; opened and closed by hand.

**Architecture:** A `MovingBodies` level system (sim/) lifts a piece's blocks (ids, metas, block entities) out of the world into a body, advances its angle/offset each tick, and places them back transformed at the end. A render layer meshes each body once with the normal section mesher and draws it with the chunk shader under a model matrix. Glue is a per-block 6-face mask kept in the block's block entity.

**Tech Stack:** C++17, the game's own World/Level/EventBus, render/mesher, OpenGL chunk shader; data added by an idempotent Python patch script.

**Spec:** `docs/superpowers/specs/2026-11-16-moving-structures-design.md` (§1, §2; §3 by-hand use only). Later stages (redstone/axles, riding/lifts, multiplayer sync, chains and ropes, towns) get their own plans.

## Global Constraints

- Never name other games, mods or their authors in code, comments, data or docs (CLAUDE.md).
- New blocks/items: recipes, crafting guide, Compendium entry, controller and touch support, the game's own pictures and names.
- Glue: "a slime ball or a honey bottle → 4 glue"; Hinge and Slider: "iron ingots, planks, redstone".
- A piece is at most 512 blocks; a hinge swings ~90°/s, a slider ~2 blocks/s; slider distance 1–16.
- Nothing a piece carries is ever lost: a move interrupted by save/unload finishes instantly; unplaceable blocks drop as items.
- No regressions: doors, trapdoors, pistons, chests and existing worlds behave exactly as before.
- One feature per file, files under ~1500 lines; register systems through a `registerXxx()` called from `initCore()` (never static constructors).
- PC and Android ship from the same commit (crossplay); Android-only code stays behind `#ifdef __ANDROID__`.

## Review Focus

- A piece that includes another Hinge/Slider (chained machines): the inner machine must move with the piece and keep its own setting and open state.
- A two-block door, a bed or a double chest inside a piece: both halves must travel and come back paired (both halves glued; a half left behind is refused, not split).
- A chest with items inside a piece: its contents and its large-chest pairing survive the move, including a save in the middle.
- A piece containing water, lava, fire or bedrock: liquids and fire can't be glued and are left in place; bedrock and unbreakable blocks refuse glue.
- The chunk holding part of a moving body unloads (player walks away): the move finishes at once and nothing floats or vanishes.

---

### Task 1: The blocks, the item, their pictures and recipes

**Files:**
- Create: `tools/port/moving_parts.py` (idempotent, like `tools/port/forging.py`: reads/writes `data/blocks.json`, `items.json`, `recipes.json`, `tiles.json`, makes pictures in `data/textures/`)
- Modify: `tools/port/data_patches.py` (run it, like the forging block at line 138)
- Create: `tools/test_moving.cpp`; Modify: `CMakeLists.txt` (add `test_moving` like `test_piles`, linked to `dqcore`)

**Interfaces:**
- Produces: block keys `hinge`, `slider` (render 1 cube, solid, opaque, `"f6": true` six-way facing set from the face clicked, `"machine": "hinge"|"slider"`, hardness 2.5, tool pickaxe); item `glue` (maxStack 64); block items `hinge`, `slider`.

- [ ] **Step 1: Write the failing test** in `tools/test_moving.cpp` (same skeleton as `tools/test_piles.cpp`: initCore with a temp userdir, a Level, a flat stone floor at `dimBase(DIM_OVERWORLD)+200`):
  `check(reg().blockId("hinge") && reg().blockId("slider"), "the hinge and slider blocks exist")`,
  `check(reg().item("glue") && reg().item("glue")->maxStack() == 64, "glue is an item")`,
  `check(recipeFor("glue") makes 4 from slime_ball and 4 from honey_bottle, ...)`, `check(recipe for hinge and slider uses iron_ingot, planks, redstone)` (use the recipe lookup test_content.cpp uses).
- [ ] **Step 2:** `ninja -C build test_moving && build/test_moving` → FAIL (blocks missing).
- [ ] **Step 3:** Write `moving_parts.py`: pictures drawn in code (hinge: an iron knuckle on planks with its pin showing on the front face; slider: a planked block with an iron rail groove on the front; glue: a small pot of amber paste), the two blocks with `faces` (front = the moving face), the item, recipes `hinge` `["IPI","PRP","IPI"]` / `slider` `["PIP","IRI","PIP"]` (I iron_ingot, P planks, R redstone), glue shapeless `[slime_ball]`→4 and `[honey_bottle]`→4. Hook it into `data_patches.py`. Run `python3 tools/port/data_patches.py`.
- [ ] **Step 4:** rebuild, `build/test_moving` → PASS; `build/test_content` still all passed.
- [ ] **Step 5:** Commit (`data/` changes, script, test, CMake).

### Task 2: Turning a block's facing (shared)

**Files:**
- Create: `src/world/rotate.h`, `src/world/rotate.cpp`
- Modify: `src/sim/gamecommands.cpp:513-528` (its `rotMeta` becomes a call to `rotateMetaY`)
- Test: `tools/test_moving.cpp`

**Interfaces:**
- Produces: `u8 rotateMetaY(BlockId id, u8 meta, int quarters)` (clockwise seen from above; exactly the old `rotMeta`), `u8 rotateMetaAbout(BlockId id, u8 meta, int axis /*1 x, 2 z*/, int quarters)` (a turn about a horizontal axis: axis blocks and rods swap y↔(the other horizontal); six-way `f6` facings turn; everything else unchanged).

- [ ] **Step 1: Failing tests:** stairs facing north turned 1 quarter → east; a log on axis y turned about x → axis z; a dispenser (`f6`) facing up turned about x by 1 → facing south; `/rotate` command output unchanged (run the same turn through `rotMeta`'s old cases: stairs, slab, torch, log).
- [ ] **Step 2:** run → FAIL (no `rotateMetaY`).
- [ ] **Step 3:** move the body of `rotMeta` into `rotateMetaY`; write `rotateMetaAbout`.
- [ ] **Step 4:** run → PASS; `build/test_world` and `build/dq-test-gameplay` all passed.
- [ ] **Step 5:** Commit.

### Task 3: Glue

**Files:**
- Create: `src/sim/glue.h`, `src/sim/glue.cpp`
- Modify: `src/core/init.cpp` (call `registerGlue()`), `src/render/` outline (Task 7 draws it)
- Test: `tools/test_moving.cpp`

**Interfaces:**
- Produces: `u8 glueMask(const World& w, ivec3 p)` (bit f = face f, faces in the game's 0..5 order: +x −x +y −y +z −z); `bool glued(const World& w, ivec3 a, int face)`; `bool setGlue(World& w, ivec3 a, int face, bool on)` (sets both sides; false if either block can't take glue); `bool glueable(BlockId id)` (false for air, liquids, fire, bedrock and blocks with hardness < 0); `void registerGlue()` (an `itemUse` handler: glue in hand on a block face toggles that face's glue with the neighbour, uses one glue when sticking, gives it back when unsticking, sound "slime" place).
- Storage: key `"glue"` (int mask) inside the block's block entity json; a block with no entity gets `{"glue": mask}`; mask 0 removes the key (and the entity when nothing else is in it). Changing the block clears it (World already drops a changed block's entity).

- [ ] **Step 1: Failing tests:** glue stone A to stone B (east face): `glued(A,east)` and `glued(B,west)`; ungluing clears both; a chest keeps its items when glued (entity merged, `"type":"chest"` intact); glue on water → false; breaking B clears A's east bit when A is next read (`glued` checks both sides).
- [ ] **Step 2:** run → FAIL.
- [ ] **Step 3:** implement; `glued(a,f)` is true only if both a's bit f and the neighbour's opposite bit are set.
- [ ] **Step 4:** run → PASS.
- [ ] **Step 5:** Commit.

### Task 4: The piece and its path

**Files:**
- Create: `src/sim/moving_piece.h`, `src/sim/moving_piece.cpp`
- Test: `tools/test_moving.cpp`

**Interfaces:**
- Consumes: `glued`, `rotateMetaY`, `rotateMetaAbout`.
- Produces:
  ```cpp
  struct Motion { int kind = 0;            // 0 hinge, 1 slider
                  ivec3 machine{0}; int face = 2;   // the machine block and its moving face (0..5)
                  int quarters = 1;        // hinge: signed quarter turns (+ clockwise about the face's axis)
                  int distance = 1; };     // slider: blocks along the face
  struct Piece { std::vector<ivec3> cells; };   // world cells, in gather order
  bool gatherPiece(const World& w, const Motion& m, Piece& out, std::string* why);   // false: nothing glued, > 512, or a two-block thing half outside
  ivec3 movedCell(const Motion& m, ivec3 cell, float fraction = 1.f);   // where a cell ends (fraction 1)
  bool pathClear(const World& w, const Motion& m, const Piece& p, std::vector<ivec3>* breakable, ivec3* blockedAt);
  ```
- Gathering: BFS from the cell on the machine's moving face, through glued faces only, never into the machine itself, cap 512. A door/bed/double-chest half whose partner isn't in the piece → `why = "glue both halves"`.
- Hinge axis = the machine's facing direction through the machine's centre; cells turn about it in whole quarter turns (integer rotation of the offset from the machine).
- Path: a cell is blocked if a solid block not in the piece sits where any piece cell passes (hinge: the arc sampled every 15° per cell; slider: every cell along the line). Replaceable blocks (`reg().block(id).replaceable`) are listed in `breakable` instead.

- [ ] **Step 1: Failing tests:** a 3-tall plank door glued to a hinge facing up gathers 3 cells; ungluing the middle splits it (1 cell); 600 glued blocks → false "too big"; `movedCell` for a hinge facing up, cell (machine+1 east, +1 up), 1 quarter → (machine+1 south, +1 up); a stone in the swing → `pathClear` false with `blockedAt` that stone; tall grass in the swing → true and listed in `breakable`; slider facing east, distance 3: a cell moves +3 x.
- [ ] **Step 2:** run → FAIL. **Step 3:** implement. **Step 4:** run → PASS. **Step 5:** Commit.

### Task 5: Moving bodies (lift out, move, set back)

**Files:**
- Create: `src/sim/moving.h`, `src/sim/moving.cpp` (LevelSystem `MovingBodies`)
- Modify: `src/core/init.cpp` (`registerMovingBodies()`), `src/sim/level.cpp` only if a pre-save hook is missing (use `LevelSystem::save` to finish moves)
- Test: `tools/test_moving.cpp`

**Interfaces:**
- Consumes: Task 4's `Motion`, `Piece`, `gatherPiece`, `movedCell`, `pathClear`; Task 2's rotations.
- Produces:
  ```cpp
  struct BodyBlock { ivec3 rel; BlockId id; u8 meta; json entity; };   // rel: offset from the machine
  struct Body { u32 id; int realm; Motion m; bool opening; float t = 0, dur; std::vector<BodyBlock> blocks;
                mat4 transform() const;          // the piece's placement now, relative to the machine block's corner
                float fraction() const; };
  MovingBodies* movingBodies(Level& lv);
  bool startMove(Level& lv, int realm, ivec3 machine, std::string* why);   // toggles: opens if shut, shuts if open
  bool machineOpen(const World& w, ivec3 machine);        // the machine's block entity {"type":"machine_state","open":bool,"quarters","distance"}
  void finishAll(Level& lv);                              // every body placed at its end now
  const std::vector<Body>& movingBodiesList(Level& lv);   // for the render layer
  ```
- Duration: hinge `|quarters| * 1.0 s`, slider `distance / 2.0 s`. Opening moves by +quarters/+distance; shutting moves back.
- Placing: each block at `movedCell`, meta through `rotateMetaY` (vertical axis) or `rotateMetaAbout` (horizontal); entities moved with it (chest items, signs); a target cell now solid → that block goes back to its start cell; if that is taken too, it drops as items (`dropItem`).
- Finish early: in `save()`, and when any body cell's chunk is unloading (`WorldSystem::onChunkUnloading`).

- [ ] **Step 1: Failing tests:** open a 2×3 plank gate (hinge facing up): after ticking 1.1 s the blocks stand turned a quarter, the start cells are air, `machineOpen` true; open again → shuts, blocks back where they were; a glued chest with 5 cobblestone keeps them after open+shut; stairs in the gate come back facing as before; a stone in the way → `startMove` false, nothing moved; `finishAll` mid-move places everything at the end; `Level::save` mid-move then reload: blocks at the end, none missing (compare a count of planks before/after).
- [ ] **Step 2:** run → FAIL. **Step 3:** implement. **Step 4:** run → PASS; all existing suites pass. **Step 5:** Commit.

### Task 6: By hand (use, sneak-use settings)

**Files:**
- Create: `src/sim/moving_use.cpp` (the `itemUse` handler; register from `registerMovingBodies()`)
- Test: `tools/test_moving.cpp`

**Interfaces:**
- Consumes: `startMove`, `gatherPiece`, the machine state entity.
- Produces: sneak-use on a hinge cycles its setting through `+1, -1, +2, continuous(0)` quarter turns (toast "Hinge: clockwise / anticlockwise / half turn / turning"); sneak-use on a slider cycles distance 1→16→1 (toast "Slider: 3 blocks"); using a block of a shut or open piece (not holding glue, not sneaking) toggles it (finds the machine by gathering back through glue from the used block: the BFS stops at a machine whose moving face is glued to the component). Continuous is stored but only turns once axles drive it (stage 2); by hand it acts as one quarter turn.
- Controller and touch: these are ordinary use / sneak-use, so they already work by pad and touch; no new buttons.

- [ ] **Step 1: Failing tests:** sneak-use cycles the hinge setting and the stored value; use on the gate's middle block toggles it open; use while holding glue does not toggle (glue path takes it).
- [ ] **Step 2:** run → FAIL. **Step 3:** implement. **Step 4:** run → PASS. **Step 5:** Commit.

### Task 7: Drawing bodies and glue

**Files:**
- Create: `src/render/moving_layer.cpp` (a RenderLayer registered like the others)
- Modify: `assets/shaders/chunk.glsl` (vertex: `uniform mat4 uModel; uniform int uUseModel;` — when set, `rel = (uModel * vec4(p, 1.0)).xyz + origin`), `src/render/world_renderer.{h,cpp}` (`void drawLoose(const FrameCtx&, int pass, GLuint vao, int quads, vec3 origin, const mat4& model)` using the existing shader setup of `drawPass`)
- Test: `tools/test_moving.cpp` (mesh non-empty) + a playtest script `tools/playtests/moving_gate.json`

**Interfaces:**
- Consumes: `movingBodiesList`, `Body::transform`, `buildSectionMesh`.
- Mesh: per body, fill one or more `MeshInput` (cx/cz/sy chosen so the body's cells sit inside; neighbours air; light copied from the machine's cell), run `buildSectionMesh` once at the body's start, upload to its own VAO; drawn in opaque/cutout/translucent passes with `model = translate(machine corner) * body.transform()`; freed when the body ends.
- Glue outline: while the held item is `glue`, glued faces within 8 blocks get a faint amber outline (the target outline layer's line drawing).

- [ ] **Step 1:** playtest `moving_gate.json`: build a gate on a hinge, glue it, use it, screenshots at 0.5 s and at the end; expect TEST PASS for the end-state block checks (`expect block`), and look at the screenshots (a turning gate drawn as planks, not boxes; nothing drawn at the old place).
- [ ] **Step 2:** run → FAIL (nothing drawn mid-move). **Step 3:** implement. **Step 4:** run → PASS; screenshots checked on Modern, Legacy and Vulkan. **Step 5:** Commit.

### Task 8: Crafting guide, Compendium, notes

**Files:**
- Modify: the crafting guide and Compendium data sources the other blocks use (`data/compendium.json` entries for hinge, slider, glue), `docs/CHANGELOG.md` (next version's section: a bullet for moving gates), `assets/news/news.dqn`
- Test: `build/test_content` (every craftable item listed in the guide)

- [ ] **Step 1:** add the entries; **Step 2:** `build/test_content` all passed; full suite run; **Step 3:** Commit.

---

Multiplayer note for this stage: guests see the blocks leave and arrive through the normal block updates (no animation) until stage 4 adds body sync; nothing breaks crossplay because the protocol is unchanged.
