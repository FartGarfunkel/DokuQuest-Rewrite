// sim/spell_items.cpp — see spell_items.h
#include "sim/spell_items.h"
#include "sim/magic.h"
#include "sim/magic_data.h"
#include "sim/player.h"
#include "sim/level.h"
#include "core/itemstack.h"
#include <algorithm>

namespace dq {

static const char* ROMANS[11] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
const char* spellRarityName(int r) { return r >= 9 ? "Legendary" : r >= 7 ? "Epic" : r >= 5 ? "Rare" : r >= 3 ? "Uncommon" : "Common"; }
u32 spellRarityColor(int r) { return r >= 9 ? 0xFFB72E : r >= 7 ? 0xB060FF : r >= 5 ? 0x4C9BFF : r >= 3 ? 0x5AD45A : 0xE8E8E8; }
std::string spellLabel(const std::string& k, int rank) {
  const SpellDef* S = magicData().spell(k);
  rank = std::clamp(rank, 1, 10);
  return (S ? S->name : k) + " " + ROMANS[rank] + " (" + spellRarityName(rank) + ")";
}

int bookSlots(const std::string& id) { return id == "apprentice_spellbook" ? 3 : id == "mage_spellbook" ? 5 : id == "archmage_spellbook" ? 8 : 0; }
std::vector<std::pair<std::string, int>> bookSpells(const ItemStack& b) {
  std::vector<std::pair<std::string, int>> out;
  if (b.empty() || !bookSlots(b.id) || !b.tag.is_object() || !b.tag.contains("spells") || !b.tag["spells"].is_array()) return out;
  for (const json& e : b.tag["spells"]) if (e.is_object() && e.contains("k") && e["k"].is_string() && magicData().spell(e["k"].get<std::string>()))
    out.push_back({e["k"].get<std::string>(), std::clamp(e.value("r", 1), 1, 10)});
  return out;
}
int bookRank(const Player& p, const std::string& k) {
  if (p.offhand.empty()) return 0;
  for (auto& [s, r] : bookSpells(p.offhand[0])) if (s == k) return r;
  return 0;
}
bool learntSpell(const Player& p, const std::string& k) {
  if (!p.stats.contains("spells") || !p.stats["spells"].is_array()) return false;
  for (const json& s : p.stats["spells"]) if (s.is_string() && s.get<std::string>() == k) return true;
  return false;
}
int effectiveSpellRank(const Player& p, const std::string& k) { return std::max(spellRank(p, k), bookRank(p, k)); }

ItemStack makeScroll(const std::string& spell, int rank) {
  ItemStack s("spell_scroll", 1);
  rank = std::clamp(rank, 1, 10);
  s.tag = json{{"spell", spell}, {"rank", rank}, {"name", "Scroll of " + spellLabel(spell, rank)}};
  return s;
}
bool isScroll(const ItemStack& s) { return !s.empty() && s.id == "spell_scroll" && s.tag.is_object() && s.tag.contains("spell") && magicData().spell(s.tag.value("spell", std::string())); }

std::string inscribeScroll(ItemStack& book, ItemStack& scroll) {
  const int slots = bookSlots(book.id);
  if (!slots) return "Put a spellbook in the book slot";
  if (!isScroll(scroll)) return "Put a spell scroll in the scroll slot";
  const std::string k = scroll.tag["spell"]; const int r = std::clamp(scroll.tag.value("rank", 1), 1, 10);
  if (!book.tag.is_object()) book.tag = json::object();
  json& sp = book.tag["spells"]; if (!sp.is_array()) sp = json::array();
  for (json& e : sp) if (e.is_object() && e.value("k", std::string()) == k) {
    if (e.value("r", 1) >= r) return "The book already holds " + spellLabel(k, e.value("r", 1));
    e["r"] = r; if (--scroll.n <= 0) scroll.clear(); return "";
  }
  if ((int)sp.size() >= slots) return "Every slot of this book is written: take a spell out first";
  sp.push_back(json{{"k", k}, {"r", r}});
  if (--scroll.n <= 0) scroll.clear();
  return "";
}
std::string extractSpell(ItemStack& book, int slot, ItemStack& out) {
  if (!bookSlots(book.id) || !book.tag.is_object() || !book.tag.contains("spells") || !book.tag["spells"].is_array()) return "Nothing is written in it";
  json& sp = book.tag["spells"];
  if (slot < 0 || slot >= (int)sp.size()) return "That slot is empty";
  const json e = sp[slot];
  sp.erase(sp.begin() + slot);
  out = makeScroll(e.value("k", std::string()), e.value("r", 1));
  return "";
}
std::string writeScroll(const Player& p, const std::string& k, ItemStack& paper, ItemStack& lapis, ItemStack& out) {
  const SpellDef* S = magicData().spell(k);
  if (!S) return "No such spell";
  if (!learntSpell(p, k)) return "You can only write a spell you have learnt";
  if (!p.creative()) {
    if (paper.empty() || paper.id != "paper") return "A scroll is written on paper";
    if (lapis.empty() || lapis.id != "lapis_lazuli") return "and with lapis for its ink";
    if (--paper.n <= 0) paper.clear();
    if (--lapis.n <= 0) lapis.clear();
  }
  out = makeScroll(k, spellRank(p, k));
  return "";
}
bool readScroll(Level& lv, Player& p, ItemStack& scroll) {
  if (!isScroll(scroll)) return false;
  const std::string k = scroll.tag["spell"]; const int r = std::clamp(scroll.tag.value("rank", 1), 1, 10);
  MagicState& m = magicState(lv, p);
  const bool bare0 = m.bare; m.bare = true;
  HandMods& H = handMods(); H = {};
  H.mana = 0;                                                         // (the scroll holds the spell's power: no mana)
  H.power = (1 + 0.05f * (r - 1)) / (1 + 0.05f * (effectiveSpellRank(p, k) - 1));   // (cast at the scroll's rank, not yours)
  H.noStaff = true;
  castSpellKey(lv, p, k);
  const bool ok = H.ok;
  H = {}; m.bare = bare0;
  if (ok && !p.creative() && --scroll.n <= 0) scroll.clear();
  return ok;
}

} // namespace dq
