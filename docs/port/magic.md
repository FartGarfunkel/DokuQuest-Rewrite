# Magic, enchanting, brewing, skills and the RPG side

Workstream: magic (spells, mana, schools, robes), enchanting, brewing and potion effects, skills and gear levels,
the RPG long road (perks, journal, Exploring/Alchemy, Realm Fragments).

## Ported modules

| JS (0.9.0.90) | C++ | Notes |
|---|---|---|
| game/magic.js | sim/magic.cpp, magic_spells.cpp, magic_bolt.cpp | the ten first spells, mana (+ robes, staffs), learning tomes, G to change, the Alteration spells at work (witchlight, levitation, stoneskin, ward), SpellBolt |
| game/arcana.js | magic_spells.cpp, magic_bolt.cpp, effects.cpp, enchanting.cpp | six spells (ArcBolt: seeking missiles, bursting fireball), robe sets (robeBoost), Clarity/Fortune/Haste/Warding potions, the six enchantments |
| game/spellcraft.js | magic_spells.cpp (castNewSpell), ui/magic_screens.cpp | thirteen spells, five new schools, per-spell cooldowns, Z (bare hands, power 0.7), the spellbook (B / sneak + d-pad up) |
| game/paths.js (schools part, Mage talents) | magic_spells.cpp, rpg.cpp, magic.cpp | school levels (½ % a level, a sixth less mana from 50), Mage: +20 mana, a tenth back, Deep Well, Swift Casting, Elemental Focus, Mana Shield, Archmage's Flow, Overload |
| game/enchanting.js | sim/enchanting.cpp, ui/magic_screens.cpp (table), render/magic_fx.cpp (book, glyphs) | the whole table (offers, bookshelves, runes, lapis + levels), every enchantment's effect as hook functions, anvil books / grindstone, Bottle o' Enchanting |
| game/brewing.js | sim/brewing.cpp, effects.cpp, magic_bolt.cpp (PotionFlask), ui (stand) | potions + long/strong/turned/splash, the stand block entity (original JSON), drinking, throwing, filling bottles, milk |
| game/skills.js | sim/skills.cpp, ui (K) | 12 + 2 skills, the RS curve, gear levels to 50, ORE_XP, every skill hook |
| game/rpg.js | sim/rpg.cpp, ui (K perks, J journal) | perks at 30/60/90 and what they do, Exploring/Alchemy xp, Realm Fragments, the story chapters, records |
| fairplay.js (the 1/5 that always gets through: `src._raw`), deepdark.js Swift Sneak, nether2.js ghast tear, stable.js spirit wolves, monsters.js venom ticks | sim/ | only the parts these systems own |

Data: `data/magic.json` is extracted from the running original:
`python3 tools/port/jsdump.py <src> data/magic.json "$(cat tools/port/magic_expr.js)"` (SPELL_ORDER/SPELLS with schools, SCHOOLS, STAFFS, ROBES+SCHOOL_ROBES, ENCH, enchKind/enchantability of
every item, ROMAN, ENCH_RUNES, EFFECTS, POTIONS, CORRUPT, BREW, BREW_INGS, SKILLS, SKILL_XP, PERKS, ORE_XP, PATHS,
alchValue of every item, FRAG_FOES, DIM_LABEL). Items (tomes, staffs, robes, potions) were already in data/items.json.
It is read with key order kept (the enchantment picks depend on it).

## How it fits
- `MagicSystem` (a LevelSystem, `registerMagic()` from initCore) handles `itemUse` (tomes, staffs, potions, bottles,
  Bottle o' Enchanting, milk), ticks every non-ghost player (mana, cooldowns, ward, witchlight, levitation, stoneskin,
  effects, venom, Exploring, Green Thumb, the journal), and where the world is simulated (`!lv.isRemote()`): creature
  statuses (`burnT`, `poisonT`, `slowT`, `rootT` in Entity::data), spirits running out (`data.spiritT`), brewing stands
  (block entities `{type:"brew", items[5], fuel, t, ing}` in loaded chunks, ten times a second).
- Persistent player state lives in `Player::stats`: `spells`, `spell`, `spellCd` (ms), `schools`, `skills`, `fx`
  (`{effect: [secondsLeft, level]}`), `mana`, `enchSeed`, `enchanted`, `alchBank`, `explore`, `story`, `walkedKm`,
  `skillHint`, `path`. Runtime state: `MagicState` (`magicState(lv, player)`).
- Stacks: enchantments in `ItemStack::tag["en"]` (the original `s.en`), gear experience in `tag["gx"]`.
- Visuals travel as `LevelFx::particles` with the original spark tile names and a shape (`sim/magic_fx.h`), so a
  server's spells reach every client; the client layer `render/magic_fx.cpp` wraps the level's fx sink and draws them
  (soft additive glows, motes, bubbles, glyphs; `spellfx.glsl`). Bolts are `netLocal` on the caster's machine; every
  machine draws its own from the `cast` effect, like the original's `adv cast` message. A guest's cast is relayed by
  the host (`netAction magicCast`); a guest's hits go through `lv.net->hitEntity` (statuses via `netHit` / `magicMobset`).

## APIs for other areas
- **Combat adapter** `sim/magic_combat.h` — every hurt/heal/knock/status/xp the magic code does goes through
  `combatHurt(lv, target, amount, src, by)`, `combatHeal`, `combatKnock`, `setMobStatus/mobStatus`, `grantXP`,
  `addXP`, `spendLevels`, `spawnCreature`, `dropStack`, `giveOrDrop`. **Merge: re-point magic_combat.cpp at sim/combat.h.**
- **Player harm pipeline** (sim/magic.h): the player hurt calls `float magicIncomingDamage(lv, p, amount, src)` once
  before armour (paths talents, Surefooted, Thick Skin, `_raw`, stoneskin, ward, Defence + gear, fire resistance /
  leaping / resistance, Protection/Fire/Blast/Projectile/Feather) and `afterPlayerHurt(lv, p, took, src)` after
  (Defence xp, gear xp, Thorns, Second Wind). `magicOutgoingDamage` = Focus/Overload on spell damage.
- **Enchantments** (sim/enchanting.h): `enchMiningBonus(held)` (+eff²+1 tool speed), `enchDamageBonus(held, target)`
  (Sharpness/Smite/Bane), `enchKnockFactor`, `enchFireAspect`, `protectionFactor(p, cause, element)`,
  `enchUnbreakingSaves(stack)` (per wear point), `enchArmorWearSaves(piece)`, `enchLootingExtra(held, chance)`,
  `enchBlockDrops(held, block, out)` (Silk Touch / Fortune), `enchSmeltDrop`, `enchAquaDivisor`, `enchBreathBonus`,
  `enchMoveFactor(p, inWater, sneaking, flying)` (Depth Strider, Swiftness, Swift Sneak), bow: `enchArrowDamageFactor`,
  `enchArrowPunch`, `enchArrowFlame`, `enchInfinity`, `enchVolley`; rod: `enchLure`, `enchLuck`;
  `enchAfterMelee(lv, p, target, dealt)` (Vampirism, Frost Aspect, Bane slow, Fire Aspect), `enchXpGain` (Insight,
  Mending; already inside `grantXP`). Anvil/grindstone: `anvilEnchant(a, b)`, `grindstoneDisenchant(a, b)`.
- **Effects** (sim/effects.h): `giveEffect(lv, p, id, secs, level)`, `clearEffects`, `effectTime/Level`, `effectsOn`,
  `mobEffect`; for the controller/combat: `effectSpeedFactor`, `effectJumpFactor`, `effectDamageFactor`,
  `effectBreakDivisor`, `effectNightVision` (sky/light: minLight 0.62 × it), `effectBreathes`, `hiddenFrom` (AI).
- **Skills** (sim/skills.h): `skillGain`, `skillLv`, `skillBonus`, `gearGain`, `gearBonus`; hooks the gameplay code
  should call: `skillBreakDivisor(p, block, held)`, `onBlockBroken(lv, p, x,y,z, block, meta, harvest, placed)`,
  `skillHitFactor(p, "melee"|"archery", held)` + `onPlayerHitMob`, `onCrafted`, `onSmeltedTaken`, `onFishCaught`,
  `onBred`, `cookingHealFactor`, `smithSavesWear`. Event `skillXp [player, skill, xp, level]` (HUD pops, mods).
- **RPG** (sim/rpg.h): `hasPerk`, `onPath/hasTal/pathSkill`, `storyOf`, `journalRecords`, `setBiomeNamer(fn)` (terrain:
  name the biome at x,z for "New land"), `onGreatFoeDied` (hooked to `mobDeath`). World flags as the original's
  g.meta: `info.data.rift {stage, frags, sealed}`, `info.data.end.slain`, `info.data.dragons`, `info.data.flags.fragFoes`,
  `info.data.playTime`; player `stats.vseen/dseen/sseen/mobsKilled/questsDone/realmSeen` (villages/structures/quests set them).
- **Magic** (sim/magic.h): `castSpell`, `castQuick`, `cycleSpell`, `pickSpell`, `learnTome`, `activeSpell`,
  `knownSpells`, `spellRepeats(k)` (holding Use repeats bolt spells: the controller decides), `manaMax/manaRegen`,
  `magicState(lv, p).jumpHeld` (set each frame; levitation), `spawnFamiliar`, `alchValue`.
- **Screens** (ui/magic_screens.h): `makeSpellbookScreen()`, `makeSkillsScreen()`, `makeJournalScreen()`,
  `makeEnchantScreen(pos)`, `makeBrewScreen(pos)` — for the menutabs tab strip. `registerMagicClient()` is called once
  in GameSession's constructor (next to registerItemLayers). `magicItemTooltip(stack)` = the enchantment/potion/gear
  lines for the inventory tooltip.
- **Creatures**: honour `data.slowT` (half speed), `data.rootT` (held in place; the system zeroes x/z velocity),
  `data.panicT` (animals run), summons: `data.owner/tamed/spirit/guardianOf/raisedBy/spiritDmg/friendly/noDrops/hostile=false`
  (fight the owner's foes; the spirit wolf's and guardian's hunting AI is the creatures workstream's).
- Test commands (cheats): `/learn <spell|all>`, `/effect <id|clear> [s] [level]`, `/enchant <id> [level]`, `/skillxp <skill> <xp>`.

## Changes outside my area (additive)
- `core/init.{h,cpp}`: `registerMagic()` (one line each).
- `client/session.cpp`: include + `registerMagicClient()` (2 lines).
- `input/input.{h,cpp}`: actions `Spell`(G) `Spellbook`(B) `CastQuick`(Z) `Skills`(K) `Journal`(J) after `Zoom` (saved by name).
- `CMakeLists.txt`: `test_magic` target. New: `data/magic.json`, `assets/shaders/spellfx.glsl`.

## Differences from the original
- Item uses arrive through `itemUse`; a usable block under the crosshair (chest, table, station, door, bed) wins over
  the item in hand when not sneaking (the original's LocalPlayer.use order).
- Ward from the Warding potion has no timer (as the original); toasts that the original delayed (perks 1.8 s, the
  K hint 2.5 s) are immediate. The witchlight's light source is exposed via `magicState().witch/lightT` — the light
  engine should add it (14.5 level).
- Spirit wolf / skeleton / guardian are spawned through `createEntity` with the original's numbers; their AI is not here.
- Lightning spawns an entity `lightning_bolt` (weather's) and a `flash` effect.
- Rune glyphs on the table are drawn as strokes (no font carries the runic block).

## Tests
`build/test_magic` (headless, 57 checks): data, skill curve, gear levels, enchantment names/kinds/levels/groups,
**enchOffers and enchRunes compared with values taken from the original**, anvil/grindstone, protection, tomes, casting
through itemUse (mana, bolt, damage, burning, xp, wear, school), fizzle, Mend, Ward, Stoneskin, bare-handed Flames,
Home Teleport + its cooldown, effects, potions, milk, splash on undead, brewing recipes, a stand brewing in a real
chunk (fuel, 12 s, Alchemy), the table's offer applied, Mining xp (and nothing for placed blocks), the journal, /learn.
Visual: `DQ_MAGIC_TEST=fire|spellbook|skills|journal|enchant|brew ./build/dokuquest --create T --smoke-frames 300 --smoke-shot x.png`.

## Left to do
- Hooks the gameplay code must call (listed above): break time, block broken, melee/archery, crafting, furnace, fishing,
  breeding, food, arrows (Power/Punch/Flame/Infinity/Volley), Looting, Silk Touch/Fortune drops, armour wear, Thorns attacker.
- The NPC shops/loot additions (librarian/cleric tomes, robes, potions; dungeon/tower loot) belong to villagers/structures:
  the lists are in the JS (magic.js, arcana.js, spellcraft.js, brewing.js, enchanting.js).
- Paths: choosing a path at a villager (PathUI) and the Knight/Ranger talents (paths.js) — only the Mage side is in.
- Wizard hats as worn models (arcana.js hatMesh) and robe colours on the character (characters workstream; ROBES cols in data).
- Synth recipes for spell_fire/frost/heal/blink/ward/zap/fizzle/hit, enchant, book_page, glass_break, drink,
  brew_bubble, brew_done are in the JS (SND_FX) for the audio workstream.
- Quest-board scout/word quests (rpg.js boardQuests) go with the villages' quest system.
