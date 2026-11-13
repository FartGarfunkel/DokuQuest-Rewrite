# Soulrend bosses, magic and skill trees: research notes (2026-11-13)

## Bosses (S = /home/matt/soulrend_extract)
- Best data-driven pilot: souls_like_bosses **Night Lord** (geo + ~150 clips, attack table as data in
  S/src/souls_like_bosses-1.0.3/com/slb/nightlord/attack/NightLordAttacks.java; 2 phases x 500 HP; tree_of_light structure).
- GeckoLib (geo.json + animation.json): Graveyard **Lich** (geo/lich.geo.json, animations/lich/lich.animation.json),
  Iron's **Dead King** (clips in animations/casting_animations.json), Mowzie's **Umvuthi**, **Sculptor**.
- Cataclysm "IA" bosses use Java AnimationDefinition keyframes (client/animation/*_Animation.java, regex-convertible):
  Maledictus, Scylla, Netherite Monstrosity, Ancient Remnant. Ignis/Ender Guardian/Leviathan are procedural (hard).
- Ours: data/mobs.json (boss:true), data/models.json rigs (uv + uvTex box-UV parts), registerRigAnim (sim/creature_rig.h),
  setCreatureHooks brains, Creature::bossAI (tele->execute->recover; hard-wired cinder golem, creature_monsters.cpp),
  keepers.cpp arena wake/clear, hud boss bar. armor3d.cpp already parses geo.json cubes.
- Shared work: geo.json -> models.json converter; animation.json clip player rig ("geo"); data-driven attack-table brain.

## Magic / skill trees
- Magic: Iron's Spells (+traveloptics, geomancy, familiars); real numbers in /home/matt/Documents/Soulrend/overrides/config/
  irons_spellbooks_spell_config/**; mana base 100, regen x0.1; scrolls -> inscription table -> spellbook (slots).
- Trees: Passive Skill Tree datapack /home/matt/Documents/Soulrend/overrides/config/paxi/datapacks/soulrend_skilltree/
  data/skilltree/{skill_trees,skills}/*.json (985 nodes: mage 493, warrior/ranger/rogue 164 each), lang in
  config/paxi/resourcepacks/soulrend_lang/assets/skilltree/lang/en_us.json. Nodes: positionX/Y, buttonSize, direct/long/
  oneWay connections, tags + skillLimitations, requirements, bonuses (attribute/grant_spell/...).
- Points: soulrend_core damage-dealt milestones (1 tree point + 1 stat point each). 6 stats (CON STR AGI INT LCK FOC) —
  ours already has them in src/sim/combat_rpg.* (no screen to spend them).
- Gems: Passive Skill Tree sockets + Apotheosis gems (data/apotheosis/gems/**).
- Ours: data/magic.json (29 spells, 10 schools, skills, paths/talents), sim/magic*.cpp, skills.cpp, rpg.cpp, combat_rpg.cpp,
  ui/magic_screens.cpp. Missing: tree screen, stat-point screen, sockets, spell rarity/levels/cast types, inscription table.
- Plan: data/skilltree.json via tools/port/soulrend_tree.py; sim/attrs (player attribute resolver); sim/skilltree +
  ui/skilltree_screen (pan/zoom canvas, controller nearest-neighbour); stat page; SpellDef rarity/levels/cast types;
  inscription table + spellbook; gems. Scale their numbers to ours.
