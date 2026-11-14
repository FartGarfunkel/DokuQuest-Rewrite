# Magic merge: Iron's Spells (Soulrend) into ours

The user's ask: spell rarity, cast types, the inscription table, gem sockets; the skill trees (done: commit cf567e4,
sim/skilltree + ui/skilltree_screen, 985 nodes, six stats). Keep every way we already have to unlock spells (tomes,
schools, practice ranks I..X).

## Pieces
1. **Rarity** (Iron's five tiers) from a spell's rank: I-II Common, III-IV Uncommon, V-VI Rare, VII-VIII Epic, IX-X
   Legendary, each its colour (Iron's: white, green, blue, purple, gold). Shown wherever a spell is named (spellbook
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
6. **Gem sockets**: gear gets 0-3 sockets (by tier); gems (existing amber, zanite + Apotheosis-style cut gems) socketed at
   the smithing table add their stat (treeAttr names: damage, armor, health, spell_power...).
