# Four original path trees — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship four short original trees that grow the four paths, keep the trainer kit, and make spending a node felt.

**Architecture:** `tools/local/write_path_trees.py` writes tracked `data/skilltree.json`. `skillTrees()` reads that file. Unknown learnt ids are dropped. Toasts and the tree page name the spend. Path talents and skill perks are untouched.

**Tech Stack:** C++20, Python 3 for the JSON writer, headless `test_magic` / `test_balance` / `test_villagers`.

**Spec:** `docs/superpowers/specs/2026-11-21-path-trees-design.md`

## Global Constraints

- Do not push, tag, or release. Branch stays `moving-structures`. Commit prefix `[claude] `.
- Trainer path talents stay. Do not edit `data/magic.json` path tiers.
- Do not retune `BOSS_CAP_FRAC` (0.08), `PARRY_WINDOW` (0.18), shield keep 0.35, or 1.75 stagger.
- No bleed meter. No other-game names in game files, comments, or node titles.
- Start ids stay `warrior`, `ranger_1`, `rogue_1`, `mage`.
- Do not commit `src/sim/player.cpp` unless the only staged hunk is `treeSanitise(*this)` in `Player::load`. Prefer calling sanitise from `treeLearn` and `skilltree_screen` if that file is dirty.
- Do not commit render/mace/dragon files or `third_party`.
- Work on the live `/home/matt/Documents/DokuQuest-Rewrite-main` checkout.

## Review Focus

- A Knight who already learnt start id `warrior` keeps it after sanitise; `warrior_159` is dropped.
- Keystone limit 1: after one size-2 Keystone, `treeWhyNot` refuses the other two.
- `test_villagers` still grants the Knight start free on path pick.
- Descriptions contain no `(` so `test_balance` node-text pins stay green.
- `combatLevel` for paths is unchanged.

---

### Task 1: Four original trees ship

**Files:**
- Create: `data/skilltree.json` via `tools/local/write_path_trees.py` (already in the repo)
- Modify: `src/sim/skilltree.cpp` (`skillTrees()` reads `dataDir() / "skilltree.json"`)
- Modify: `src/sim/skilltree.h` (comment: shipped file, not the gitignored import)
- Modify: `tools/test_magic.cpp` (tree-shape pins)
- Modify: `tools/test_balance.cpp` (message when trees are missing: `data/skilltree.json`)

**Interfaces:**
- Consumes: existing `TreeNode` / `skillTrees()` / `treeLearn`
- Produces: four trees, 19 nodes each, start ids as in Global Constraints, `limits.Keystone == 1`

- [ ] **Step 1: Write the failing tests** in `tools/test_magic.cpp` inside the existing tree block. Replace `W->nodes.size() > 100` with pins:

```
check(W && W->nodes.size() >= 18 && W->nodes.size() <= 22, "the Knight tree is short");
check(start && start->id == "warrior", "Knight start id stays warrior");
int trees = 0, keystones = 0; for (const SkillTree& t : skillTrees()) { trees++; for (auto& n : t.nodes) for (auto& tg : n.tags) if (tg == "Keystone") keystones++; }
check(trees == 4, "four path trees");
check(keystones == 12, "three keystones on each tree");
```

Also: every node has a non-empty title and desc; every percentage bonus `op != 0` has `fabs(v) >= 0.04` unless the attr starts with `grant_spell.` or is a flag; start `warrior` still has `treeAttr` damage and health after learn.

- [ ] **Step 2: Run** `ninja -C /home/matt/Documents/DokuQuest-Rewrite-main/build test_magic && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_magic`

Expected: FAIL on "the Knight tree is short" while the loader still reads the 164-node import, or FAIL compile if you already switched the path and the file is missing.

- [ ] **Step 3: Run** `python3 tools/local/write_path_trees.py` from the repo root, then point `skillTrees()` at `dataDir() / "skilltree.json"`. Copy runtime data is automatic on the next ninja because `dq_runtime_data` copies `data/`.

- [ ] **Step 4: Run** `ninja -C /home/matt/Documents/DokuQuest-Rewrite-main/build test_magic test_balance test_villagers && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_magic && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_balance && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_villagers`

Expected: all three exit 0.

- [ ] **Step 5: Commit** `data/skilltree.json`, `tools/local/write_path_trees.py`, `src/sim/skilltree.cpp`, `src/sim/skilltree.h`, `tools/test_magic.cpp`, `tools/test_balance.cpp`.

```
git commit -m "[claude] Four short trees grow the paths"
```

---

### Task 2: Old node ids are dropped

**Files:**
- Modify: `src/sim/skilltree.h` (declare `int treeSanitise(Player& p);`)
- Modify: `src/sim/skilltree.cpp` (drop learnt ids that `SkillTree::find` never sees)
- Modify: `src/ui/skilltree_screen.cpp` (call it at the start of `draw`)
- Modify: `tools/test_magic.cpp` (a player with `warrior` and `warrior_159` keeps only `warrior`)

**Interfaces:**
- Consumes: `skillTrees()`, learnt ids in `stats["tree"]["nodes"]`
- Produces: `int treeSanitise(Player& p)` returns how many ids were dropped; mutates the learnt list; `recalcMax` after a drop

- [ ] **Step 1: Test** in `tools/test_magic.cpp` after the tree block's reset:

```
treeReset(p);
p.stats["tree"]["nodes"] = json::array({"warrior", "warrior_159"});
check(treeSanitise(p) == 1 && treeHas(p, "warrior") && !treeHas(p, "warrior_159"), "an old import id is dropped; the Knight start stays");
```

- [ ] **Step 2: Fail** on missing `treeSanitise`.

- [ ] **Step 3: Implement** `treeSanitise`. Walk learnt ids; keep those that exist on any of the four trees; if the list changed, write it back and `p.recalcMax()`. Call it from `treeLearn` before the rest, and from `SkillTreeScreen::draw`.

- [ ] **Step 4: Pass** `ninja -C /home/matt/Documents/DokuQuest-Rewrite-main/build test_magic && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_magic`

- [ ] **Step 5: Commit** `[claude] Old talent nodes fall off the new trees`

---

### Task 3: Spending is named out loud

**Files:**
- Modify: `src/sim/skilltree.h` / `src/sim/skilltree.cpp` (`treeNodeGainLine`)
- Modify: `src/ui/skilltree_screen.cpp` (`learn` toast; land level under tree points)
- Modify: `src/sim/skills.cpp` (talent-point toast next to the stat-point toast)
- Modify: `src/ui/help_book.cpp` (skill-tree paragraph)
- Modify: `tools/test_magic.cpp` / `tools/test_balance.cpp` (gain-line and talent-point pins)

**Interfaces:**
- Produces: `std::string treeNodeGainLine(const TreeNode& n)` returns `n.title` if non-empty, else `"Learnt"`

- [ ] **Step 1: Tests** `treeNodeGainLine` title-or-Learnt in `test_magic`. In `test_balance` two-kinds-of-level block, after mining 1→5, `fx.heard("a talent point to spend")`.

- [ ] **Step 2: Fail.**

- [ ] **Step 3: Implement.** `learn`: start nodes still only `pathTreeOnStart`; other nodes `a.toast(treeNodeGainLine(...), "good")`. Under tree points: `ImGui::TextDisabled("Creatures measure you at Lv %d", playerCombatLevel(*p));` include `sim/combat_rpg.h`. `skillGain`: if `totalAfter / 3 > totalBefore / 3`, toast `"Skill total N: a talent point to spend (Your Path)"`. Help paragraph appends: ` The tree grows the path a trainer started. Creatures take their level from your skills slowly, so the tree is how you pull ahead of the land.`

- [ ] **Step 4: Pass** `ninja -C /home/matt/Documents/DokuQuest-Rewrite-main/build test_magic test_balance dokuquest && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_magic && /home/matt/Documents/DokuQuest-Rewrite-main/build/test_balance`

- [ ] **Step 5: Commit** `[claude] Talent points and learnt nodes say what they gave you`
