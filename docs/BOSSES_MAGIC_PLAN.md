# Bosses, magic and skill trees: plan notes (2026-11-13)

## Bosses
- What we have: data/mobs.json (boss:true), data/models.json rigs (uv + uvTex box-UV parts), registerRigAnim
  (sim/creature_rig.h), setCreatureHooks brains, Creature::bossAI (tele->execute->recover; hard-wired cinder golem,
  creature_monsters.cpp), keepers.cpp arena wake/clear, the HUD boss bar. armor3d.cpp already parses geo.json cubes.
- Best first pilot: a data-driven boss with a geo body, a large clip set (~150 clips) and its attack table as data;
  2 phases x 500 HP, its own arena structure.
- More bosses to follow: a lich, an undead king caster, keyframe-animated bosses (keyframes convertible to our clips);
  procedurally animated bosses are the hard ones and come last.
- Shared work: geo.json -> models.json converter; a clip player rig ("geo") for animation.json clips; a data-driven
  attack-table brain.

## Magic / skill trees
- Magic: mana base 100, regeneration x0.1; scrolls -> inscription table -> spellbook (slots). Extra schools to think
  about: travel, earth, familiars.
- Skill trees: data/skilltree.json (985 nodes: mage 493, warrior/ranger/rogue 164 each). Nodes: positionX/Y,
  buttonSize, direct/long/one-way connections, tags + skill limitations, requirements, bonuses (attribute/grant_spell/...).
- Points: damage-dealt milestones (1 tree point + 1 stat point each). 6 stats (CON STR AGI INT LCK FOC) — ours
  already has them in src/sim/combat_rpg.* (no screen to spend them).
- Gems: sockets in gear, gems that add a stat.
- What we have: data/magic.json (29 spells, 10 schools, skills, paths/talents), sim/magic*.cpp, skills.cpp, rpg.cpp,
  combat_rpg.cpp, ui/magic_screens.cpp. Missing: tree screen, stat-point screen, sockets, spell rarity/levels/cast
  types, inscription table.
- Plan: data/skilltree.json; sim/attrs (player attribute resolver); sim/skilltree + ui/skilltree_screen (pan/zoom
  canvas, controller nearest-neighbour); stat page; SpellDef rarity/levels/cast types; inscription table + spellbook;
  gems. Numbers scaled to ours. (Done since: see MAGIC_MERGE.md.)
