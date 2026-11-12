# Soulrend: what it has that DokuQuest doesn't (first pass, 12 Nov)

Source: `~/Documents/Soulrend` is the CurseForge modpack Soulrend v8.5.2 (Forge 1.20.1, 485 mods). The folder holds
only the manifest, the mod list and the pack's own configs and KubeJS scripts, not the mods themselves. **Everything
below is used as ideas only and written our own way.** The pack's configs and scripts belong to its author, and each
mod has its own licence. Numbers are quoted so we can balance against them, not copied as code.

Our base for comparison:
- skills 1–99 with perks at 30/60/90;
- three paths (knight, ranger, mage) with talents;
- gear levels (`gx`) and procedural part and metal stats;
- spells, schools, staffs and robes, enchantments, runes and charms;
- graves and waystones.

Combat today: hits, falling crits, sneak hits, knockback and sweep. There is no stamina, block, dodge, parry or
posture, and **no shield item**.

## A. Combat (Epic Fight and its add-ons)
| # | Feature | What Soulrend does (file) | Our version |
|---|---|---|---|
| 1 | Stamina bar | Epic Fight: dodges, guarding and special moves cost stamina | Bar under the hearts. Regenerates when you aren't spending it. No hunger is involved (user rule). |
| 2 | Dodge roll / sidestep | Epic Fight dodge skills, with i-frames | Double-tap a direction or press a pad button. About 0.3 s of i-frames. Costs stamina. |
| 3 | Guard and parry | Guard drains stamina per hit. A parry is a block timed just as the hit lands. | Block with a shield or weapon. A parry staggers the attacker and opens a counter. Needs a **shield item** (we have none). |
| 4 | Posture / poise | `epicfight_posture_integration` (armour scales posture) | Hits fill a posture bar. When it breaks, the target is staggered and can be executed. Armour raises posture. |
| 5 | Executions / assassinations | `combat_evolution-common.toml`: execution and assassination on, Massacre 0.16 | A finisher on a staggered or unaware mob. This extends our sneak hit. |
| 6 | Weapon movesets | Epic Fight, Weapons of Miracles, Simply Swords Epicfied | Combo chains per weapon class (sword, greatsword, dagger, spear, axe, staff), plus heavy, sprint and jump attacks. We would need new weapon classes. |
| 7 | Weapon special skill | Epic Fight innate skills, charged by landing hits | One special per weapon class, charged by hits. Shown as a ring on the hotbar. |
| 8 | Lock-on | Better Lock On | Target lock with a reticle, switched with the right stick on a pad. |
| 9 | Mobs that defend | `awakenedmobs-common.toml`: parry 10 %, dodge 7 %, guard 12.5 % (−30 % damage, 3 s), runners 5 % (+20 % speed) | Some mobs block, parry and dodge, with cooldowns, so fights aren't just clicking. |
| 10 | Boss rules | `soulrend_core/boss_damage.toml`: damage cap per boss per 20-tick window; Enhanced Boss Bars; boss music | Bosses can't be burst down in one second. Proper boss bars, phases and combat music. |
| 11 | Second chance / coyote time | `secondchanceforge-common.toml`: survive one lethal hit from mobs or explosions; 10-tick coyote jump | One saved death on a cooldown. Jump still works for half a second after walking off a ledge. |
| 12 | Healing flask (Estus) | `estus_flask_healing.js`: heals 25 % of max HP (big flask 75 %) | Flask charges refill at a campfire. This fits "no hunger" and the campfire scene. |
| 13 | Damage numbers and mob health bars | JeremySeq Damage Indicators, YDM MobHealthBar | Damage numbers and a health bar shown on the mob you are looking at. We already have `floatText`. |

## B. RPG stats
| # | Feature | Soulrend | Our version |
|---|---|---|---|
| 14 | Attribute points | `soulrend_core/stats.toml`, per point: Constitution +1 max HP, +0.05 toughness. Strength +0.15 damage, +1.5 % armour shred. Agility +0.002 speed, +1.5 % arrow damage. Intelligence +0.5 % spell power, +1 mana. Luck +0.04 luck, +1 % XP. Focus +1 mana, +0.15 mana regen. | Points from total level (sum of skills) go into six stats. This sits on top of skills, it doesn't replace them. |
| 15 | Classes (Origins) | `soulrend_core/common.toml`: Melee (berserker, rogue, samurai, tank, warrior), Ranged (hunter), Mage (12 schools plus healer and bard), Fusion (paladin, spellblade, reaper) | Our three paths become categories with sub-classes, plus fusion paths. |
| 16 | Passive skill tree | `skilltree-common.toml` (Passive Skill Trees): up to 500 points, costs rising from 15 to about 2000 XP | A tree screen that takes in our perks and talents. |
| 17 | More attributes | Apothic Attributes and Additional Attributes | Crit chance and crit damage, armour pierce and shred, lifesteal, dodge chance, draw speed, projectile damage, spell power per school, cooldown reduction, healing received, XP gained, overheal |
| 18 | Lifesteal with a cap | `lifesteal.js`: heal = maxHP × lifesteal × 0.10 × log2(1 + damage / targetMaxHP), at most 15 % of max HP per hit, ignored below 0.1 | We copy the idea and balance it this way. |
| 19 | Mob levels | `soulrend_core` mobLeveling: average level of players within 60 blocks. Per level: +10 % damage, +30 % HP, +1.1 % armour, +35 % XP. The level is shown when you look at the mob. | Mobs scale with the players near them, not only with distance. |

## C. Loot (keeping our procedural stats underneath)
| # | Feature | Soulrend | Our version |
|---|---|---|---|
| 20 | Rarity and affixes | Apotheosis adventure module, Fallen Gems & Affixes | Rarity tiers roll 0–4 affixes on top of the procedural part stats. Loot beams and tooltip borders take the rarity colour. |
| 21 | Gem sockets | Skill tree: 1 socket per gear piece by default, gems drop 1 %. Fallen Gems: 20 % socketed, tiered sockets | Gems socketed at the **smithing table** scene |
| 22 | Reforging and salvage | Esoteric Reforging, Ancient Reforging, Apotheosis salvaging | Re-roll affixes or salvage gear into materials at the smithing table or grindstone |
| 23 | Unique weapons | Simply Swords uniques, Marium's Soulslike Weaponry | A few named weapons, each with its own active ability |
| 24 | Trinket slots | Curios rings and necklaces | Ring, necklace and charm slots. Our charms move into them. |
| 25 | Per-player loot chests | Lootr | Each player gets their own roll of a dungeon chest in multiplayer |
| 26 | Target dummy | MmmMmmMmmMmm | A dummy that shows DPS, for balancing and for players |

## Balance rules (proposed)
- Base HP stays at 20. Every attribute bonus is small per point and capped. Procedural part stats stay the main
  source of weapon and armour power. Affixes and gems add at most about 30 % on top.
- Stamina gates defence (rolls, blocks), not walking. Regeneration pauses for 1 s after you spend stamina.
- Every new combat input has a pad binding and shows in the HUD's pad hints (user rule: controller support everywhere).
