# Combat, RPG and loot ideas for DokuQuest (first pass, 12 Nov)

A list of features DokuQuest doesn't have yet (as of 12 Nov), with target numbers to balance against. Everything here
is written our own way.

Our base for comparison:
- skills 1–99 with perks at 30/60/90;
- three paths (knight, ranger, mage) with talents;
- gear levels (`gx`) and procedural part and metal stats;
- spells, schools, staffs and robes, enchantments, runes and charms;
- graves and waystones.

Combat today: hits, falling crits, sneak hits, knockback and sweep. There is no stamina, block, dodge, parry or
posture, and **no shield item**.

## A. Combat
| # | Feature | Target behaviour | Our version |
|---|---|---|---|
| 1 | Stamina bar | Dodges, guarding and special moves cost stamina | Bar under the hearts. Regenerates when you aren't spending it. No hunger is involved (user rule). |
| 2 | Dodge roll / sidestep | Dodges with i-frames | Double-tap a direction or press a pad button. About 0.3 s of i-frames. Costs stamina. |
| 3 | Guard and parry | Guard drains stamina per hit. A parry is a block timed just as the hit lands. | Block with a shield or weapon. A parry staggers the attacker and opens a counter. Needs a **shield item** (we have none). |
| 4 | Posture / poise | Armour scales posture | Hits fill a posture bar. When it breaks, the target is staggered and can be executed. Armour raises posture. |
| 5 | Executions / assassinations | Execution and assassination moves; a damage bonus about 0.16 | A finisher on a staggered or unaware mob. This extends our sneak hit. |
| 6 | Weapon movesets | A moveset per weapon class | Combo chains per weapon class (sword, greatsword, dagger, spear, axe, staff), plus heavy, sprint and jump attacks. We would need new weapon classes. |
| 7 | Weapon special skill | A special charged by landing hits | One special per weapon class, charged by hits. Shown as a ring on the hotbar. |
| 8 | Lock-on | Target lock | Target lock with a reticle, switched with the right stick on a pad. |
| 9 | Mobs that defend | Parry 10 %, dodge 7 %, guard 12.5 % (−30 % damage, 3 s), runners 5 % (+20 % speed) | Some mobs block, parry and dodge, with cooldowns, so fights aren't just clicking. |
| 10 | Boss rules | A damage cap per boss per 20-tick window; styled boss bars; boss music | Bosses can't be burst down in one second. Proper boss bars, phases and combat music. |
| 11 | Second chance / coyote time | Survive one lethal hit from mobs or explosions; 10-tick coyote jump | One saved death on a cooldown. Jump still works for half a second after walking off a ledge. |
| 12 | Healing flask | Heals 25 % of max HP (big flask 75 %) | Flask charges refill at a campfire. This fits "no hunger" and the campfire scene. |
| 13 | Damage numbers and mob health bars | Numbers on hits, a bar on the mob you look at | Damage numbers and a health bar shown on the mob you are looking at. We already have `floatText`. |

## B. RPG stats
| # | Feature | Target numbers | Our version |
|---|---|---|---|
| 14 | Attribute points | Per point: Constitution +1 max HP, +0.05 toughness. Strength +0.15 damage, +1.5 % armour shred. Agility +0.002 speed, +1.5 % arrow damage. Intelligence +0.5 % spell power, +1 mana. Luck +0.04 luck, +1 % XP. Focus +1 mana, +0.15 mana regen. | Points from total level (sum of skills) go into six stats. This sits on top of skills, it doesn't replace them. |
| 15 | Classes | Melee (berserker, rogue, samurai, tank, warrior), Ranged (hunter), Mage (12 schools plus healer and bard), Fusion (paladin, spellblade, reaper) | Our three paths become categories with sub-classes, plus fusion paths. |
| 16 | Passive skill tree | Up to 500 points, costs rising from 15 to about 2000 XP | A tree screen that takes in our perks and talents. |
| 17 | More attributes | | Crit chance and crit damage, armour pierce and shred, lifesteal, dodge chance, draw speed, projectile damage, spell power per school, cooldown reduction, healing received, XP gained, overheal |
| 18 | Lifesteal with a cap | heal = maxHP × lifesteal × 0.10 × log2(1 + damage / targetMaxHP), at most 15 % of max HP per hit, ignored below 0.1 | Balanced this way. |
| 19 | Mob levels | The average level of players within 60 blocks. Per level: +10 % damage, +30 % HP, +1.1 % armour, +35 % XP. The level is shown when you look at the mob. | Mobs scale with the players near them, not only with distance. |

## C. Loot (keeping our procedural stats underneath)
| # | Feature | Target behaviour | Our version |
|---|---|---|---|
| 20 | Rarity and affixes | Rarity tiers with affixes | Rarity tiers roll 0–4 affixes on top of the procedural part stats. Loot beams and tooltip borders take the rarity colour. |
| 21 | Gem sockets | 1 socket per gear piece by default, gems drop 1 %; 20 % of found gear socketed, tiered sockets | Gems socketed at the **smithing table** scene |
| 22 | Reforging and salvage | Re-roll and salvage | Re-roll affixes or salvage gear into materials at the smithing table or grindstone |
| 23 | Unique weapons | Named weapons | A few named weapons, each with its own active ability |
| 24 | Trinket slots | Rings and necklaces | Ring, necklace and charm slots. Our charms move into them. |
| 25 | Per-player loot chests | | Each player gets their own roll of a dungeon chest in multiplayer |
| 26 | Target dummy | | A dummy that shows DPS, for balancing and for players |

## Balance rules (proposed)
- Base HP stays at 20. Every attribute bonus is small per point and capped. Procedural part stats stay the main
  source of weapon and armour power. Affixes and gems add at most about 30 % on top.
- Stamina gates defence (rolls, blocks), not walking. Regeneration pauses for 1 s after you spend stamina.
- Every new combat input has a pad binding and shows in the HUD's pad hints (user rule: controller support everywhere).

## The combat layer: what it should play like
Ideas and numbers only; we write our own moves.
- **Every weapon type has a moveset, not one swing:** 3–4 basic attacks chained by timed clicks, a dash attack (attack
  while sprinting), an air attack (attack while falling), and one special on its own button. Its idle, walk, run
  and guard stances change with it, and so do one-handed and two-handed use (what is in the off hand).
  | Type | Basic chain | Extras | Special (example) | Hitbox w×h×reach |
  |---|---|---|---|---|
  | Sword | 3 | dash, air | sweeping slash | 0.4×0.4×0.8 |
  | Longsword | 4 | comet slam | plunder (lunge) | 0.4×0.4×0.8 |
  | Greatsword | 3 + charge | dash, air slash | wind slash (wave) | 0.4×0.6×1.2 |
  | Dagger | 3 | dash, air | sweeping edge / dancing edge (two daggers) | 0.4×0.4×0.6 |
  | Spear / polearm | 4 + charged | dash | plunge | 0.6×0.6×2.0 |
  | Axe / hammer | 4 | dash, air | wind slash | 0.4×0.4×0.55 |
  | Heavy weapon | 3 + 2 charged | dash, air | steel whirlwind (spin) | 0.5×0.8×1.0 |
  | Staff | 3 | dash, ground slam | spell whirl | 0.4×0.4×0.8 |
  | Fists | 2 + kick | execute | relentless combo | 0.4×0.4×0.4 |
- **Hits land on a swept hitbox** that follows the swing's arc (sampled 5 times along it), not on a ray, so a wide
  swing hits two zombies.
- **Stiff combos:** you are committed to a swing, so you cannot walk at full speed through it.
  **Global stun:** a heavy hit staggers anything, the player too.
- **Mobs fight with movesets too:** zombies and skeletons get wind-up animations you can read, combos, guard breaks
  and knockdowns. That is what makes parrying worth doing.
- **Lock-on:** up to 40 blocks, camera pitch held between -30° and 40°, an RPG-style marker on the target, and the
  next target picked by flicking the stick.
- **Damage numbers** rise from what you hit. **Boss bars** are styled per boss, and while a boss fight is on, no other
  mobs spawn.
- **Executions:** a finishing move (with its own animation) on a staggered or guard-broken enemy.
- **Weight:** heavy armour slows attacks and rolls.
