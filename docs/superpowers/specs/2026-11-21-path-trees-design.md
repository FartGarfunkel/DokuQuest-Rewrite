# Four original path trees — design

Date: 2026-11-21. Partner: keep the six path talents at the trainer; replace the imported trees with four short original ones that grow that path. Mechanics sound and balanced; it should feel like a polished RPG.

## Intent

The trainer is who you are. The tree is how you grow it. Fourteen skills stay use-based with perks at 30/60/90.

## Approaches

1. Fold the six path talents into the tree as the first keystones. One spend screen. The guard's teaching moment becomes a book click.
2. **Chosen:** trainer keeps the six named talents. Four original trees, ~19 fat nodes each, no copy of those six or of skill perk names. Path points and tree points stay two pools.
3. Leave the imported 985-node graphs and only retune numbers. Forbidden as of this conversation.

## What stays

- Path keys knight / ranger / mage / rogue, combat level 20, six talents, forget for gold.
- Tree keys `warrior` / `ranger` / `rogue` / `mage` and start ids `warrior`, `ranger_1`, `rogue_1`, `mage` so path-pick still grants the first node free.
- TALENTS table, caps, `talentOnHit`, grant_spell, land formula `totalLevel/12 + progressMobLevels`.
- Bleed-on-hit. No bleed meter.

## What ships

Tracked file `data/skilltree.json` (copied with the rest of `data/`). Loader reads that path only. Display names: Knight, Ranger, Rogue, Mage.

Each tree: 1 start + 3 branches of 6 = 19 nodes. The last node of each branch is a Keystone. Limit `Keystone: 1` — one capstone per character. Minors and notables on every branch remain open.

Fat floors: a percentage bonus is at least 0.04; a flat bonus is at least 1, except flag attrs which are 1. No filler of the old 0.005 kind.

Descriptions are plain words, no parentheses (the balance test forbids them). No other-game names. No titles that match a path talent or a skill perk.

Start bonuses stay on the existing attrs the tests already feel: Knight start still gives flat damage and health.

## Balance budget

Trees do not raise `playerCombatLevel`. Caps in `TALENTS` still clamp a full spend. A finished Knight tree with one keystone is about +30–45% melee damage *or* +12% damage reduction *or* a real bleed/rage kit, plus the start's +2 damage and +4 health. Path Heavy Blows and skill Cleave stay on top of that, not replaced.

## Old saves

Learnt ids that are not in the new graphs are dropped. The Knight start id `warrior` is kept, so a Knight who only had the free first node keeps it. Points spent fall back to what still exists.

## Polish

- Skill-up toasts a talent point when `totalLevel/3` ticks.
- Learning a node that is not a tree start toasts its title. Starts still use `pathTreeOnStart`.
- Tree book: "Creatures measure you at Lv N".
- Help: the tree grows the path the trainer started; creatures take their level from skills slowly.

## Anti-goals

No 14 skill trees. No second class. No rewrite of path talents. No retune of boss cap 0.08, parry 0.18, shield keep 0.35, stagger 1.75. No mace/animation in these commits.

## Exact nodes

`tools/write_path_trees.py` is the node list. Running it writes `data/skilltree.json`.
