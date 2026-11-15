# Magic additions: rarity, scrolls, spellbooks, sockets

The user's ask: spell rarity, cast types, the inscription table, gem sockets; the skill trees (done: commit cf567e4,
sim/skilltree + ui/skilltree_screen, 985 nodes, six stats). Keep every way we already have to unlock spells (tomes,
schools, practice ranks I..X).

## Pieces
1. **Rarity** (five tiers) from a spell's rank: I-II Common, III-IV Uncommon, V-VI Rare, VII-VIII Epic, IX-X
   Legendary, each its colour (white, green, blue, purple, gold). Shown wherever a spell is named (spellbook
   screen, scrolls, the spellbook item, the HUD's spell).
2. **Scrolls** (`spell_scroll`, tag {spell, rank}): one cast of that spell at that rank, by anyone, no staff, no mana;
   the scroll is used up. Found in dungeon and tower loot; written at the inscription table from a spell you know (paper
   + lapis), at your rank.
3. **Spellbooks** (`apprentice_spellbook` 3 slots, `mage_spellbook` 5, `archmage_spellbook` 8; tag {spells:[{k,rank}]}):
   a book in the off hand is your loadout: cycling spells goes through its spells, and you may cast a spell from it you
   haven't learnt yourself, at the rank written in it (so books can be passed between players).
4. **Inscription table** (block + screen, full controller support): a book slot and a scroll slot. Inscribe: the scroll's
   spell into a free slot (or over the same spell at a lower rank). Extract: a slot back out as a scroll. Write: a scroll
   of a spell you know, for a paper and a lapis.
5. **Cast types**: instant (most), long (a cast time held before it fires: fireball, lightning, ice nova, guardian),
   continuous (held, mana each quarter second: flames, drain life). In magic.json `cast` / `castTime`.
6. **Gem sockets**: gear gets 0-2 sockets (by tier); gems (our existing gem items) set at the crafting grid (the gear and a
   gem together) add their stat (treeAttr names: damage, armor, mana, spell_power...). (The smithing table is a cinematic
   scene; the grid keeps it simple and works with a controller everywhere.)

## Done (2026-11-13)
- 1-4: sim/spell_items.{h,cpp}, the inscription table (ui/magic_screens.cpp InscriptionScreen, station "inscribe"),
  items spell_scroll / apprentice_ / mage_ / archmage_spellbook / inscription_table (our own pictures), scrolls in loot
  (gen/structures_common.cpp maybeScroll), the off-hand book's spells in knownSpells and its rank in castSpellKey.
- 5: SpellDef::cast / castTime (data/magic.json); a long cast winds up from one hand's plain cast (HandMods::windup), the
  magic tick casts it (MagicState::windK); a death or another spell chosen cancels it. Shown in the spellbook.
- 6: gems (GEMS in spell_items.cpp: ruby damage, sapphire spell power, emerald mana, diamond armour, amethyst mana
  regeneration, amber stamina, zanite crit chance, quartz crit damage, echo shard spell resistance, mese spell power),
  sockets by gear (1; 2 for diamond, netherite, mithril, adamantite, cinderite, dragon, archmage; a "sockets" tag
  overrides), set at the crafting grid (gear + gem, after any real recipe), summed into treeAttr / treeAttrPct.
- test_magic: 17 new checks.

## The spell effects and the casting hands (2026-11-14)
- The spell effects import (local, git-ignored): animated particle frames -> `data/local_models/tiles/isb/` (loaded into
  the atlas by `loadLocalTiles`, played over a particle's life: `Particle::frame0/frames`), its projectiles as voxel models
  (firebolt, fireball core, magic missile from their renderers' boxes; icicle and guiding bolt pictures extruded).
- Every school's sparks are joined by animated frames (fire: embers / fire / fiery smoke; frost: snowflakes; storm: electricity;
  life: wisps; arcane: violet electricity and traces; stone: fog); bolts fly as voxel projectiles with trails.
- Hands: each free hand holds its readied spell at the body's own fist (`playerPalms`, first person too); after a cast the
  arm comes up holding it a few seconds and each cast thrusts it out (`render/spell_hands.h`, `CharPose::spellR/L`).
  Two spells: each hand its own (mix and match shows both). Each hand has its own cooldown (`castCdL`); bolts leave the
  casting hand and meet your gaze 16 blocks out.
