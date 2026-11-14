// sim/spell_items.h — Iron's Spells' items, ours (docs/MAGIC_MERGE.md): a spell's rarity by its rank, spell scrolls
// (one cast of a spell at a rank, by anyone), spellbooks (a loadout of spells at the ranks written in them, carried in the
// off hand) and the inscription table's work on them.
#pragma once
#include "common/common.h"
#include <string>
#include <utility>
#include <vector>

namespace dq {
struct ItemStack; class Level; struct Player;

const char* spellRarityName(int rank);    // I-II Common, III-IV Uncommon, V-VI Rare, VII-VIII Epic, IX-X Legendary
u32 spellRarityColor(int rank);           // 0xRRGGBB (Iron's: white, green, blue, purple, gold)
std::string spellLabel(const std::string& k, int rank);   // "Firebolt III (Rare)"

int bookSlots(const std::string& itemId); // a spellbook's slots (0: not one)
std::vector<std::pair<std::string, int>> bookSpells(const ItemStack& book);   // its spells and their ranks, in slot order
int bookRank(const Player& p, const std::string& k);      // the rank of k in the book in the off hand (0: not there)
int effectiveSpellRank(const Player& p, const std::string& k);
bool learntSpell(const Player& p, const std::string& k);  // learnt for good (a tome), not only carried in a book   // the higher of yours and the book's

ItemStack makeScroll(const std::string& spell, int rank);
bool isScroll(const ItemStack& s);
// the inscription table ("" = done, else why not)
std::string inscribeScroll(ItemStack& book, ItemStack& scroll);           // the scroll's spell into a free slot (or over itself at a lower rank)
std::string extractSpell(ItemStack& book, int slot, ItemStack& scrollOut); // a slot back out as a scroll
std::string writeScroll(const Player& p, const std::string& k, ItemStack& paper, ItemStack& lapis, ItemStack& scrollOut);   // a spell you know, at your rank
bool readScroll(Level& lv, Player& p, ItemStack& scroll);                 // cast it once (used up)
} // namespace dq
