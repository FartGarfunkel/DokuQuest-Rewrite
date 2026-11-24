// sim/spell_items.cpp — see spell_items.h
#include "sim/spell_items.h"
#include "core/tool_parts.h"
#include "sim/magic.h"
#include "sim/magic_data.h"
#include "sim/player.h"
#include "sim/level.h"
#include "core/itemstack.h"
#include "core/registry.h"
#include "sim/balance.h"
#include <algorithm>
#include <cstdio>

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

// ---- gem sockets ----
static const GemDef GEMS[] = {
  {"ruby", "damage", 0.05f, true, "+5% damage"}, {"sapphire", "spell_power", 0.08f, true, "+8% spell power"},
  {"emerald", "mana", 10, false, "+10 mana"}, {"diamond", "armor", 1, false, "+1 armour"},
  {"amethyst_shard", "mana_regen", 0.12f, true, "+12% mana regeneration"}, {"amber_gem", "stamina", 0.10f, true, "+10% stamina"},
  {"zanite_gemstone", "crit_chance", 0.04f, true, "+4% critical chance"}, {"quartz", "crit_damage", 0.10f, true, "+10% critical damage"},
  {"echo_shard", "spell_resist", 0.08f, true, "+8% spell resistance"}, {"mese_crystal", "spell_power", 0.05f, true, "+5% spell power"},
};
const GemDef* gemOf(const std::string& id) { for (const GemDef& g : GEMS) if (id == g.item) return &g; return nullptr; }
int gearSockets(const ItemStack& s) {
  if (s.empty()) return 0;
  if (s.tag.is_object() && s.tag.contains("sockets") && s.tag["sockets"].is_number()) return std::clamp(s.tag["sockets"].get<int>(), 0, 3);
  const ItemDef* d = s.def(); if (!d) return 0;
  const bool gear = d->has("armor") || d->hasTool || d->has("weapon") || d->has("staff");
  if (!gear || (s.id.find("leather") != std::string::npos) || s.id.rfind("wooden", 0) == 0 || s.id.rfind("stone_", 0) == 0) return 0;
  for (const char* fine : {"diamond", "netherite", "mithril", "adamantite", "archmage", "cinderite", "dragon"}) if (s.id.find(fine) != std::string::npos) return 2;
  return 1;
}
std::vector<std::string> gearGems(const ItemStack& s) {
  std::vector<std::string> v;
  if (!s.empty() && s.tag.is_object() && s.tag.contains("gems") && s.tag["gems"].is_array()) for (const json& g : s.tag["gems"]) if (g.is_string() && gemOf(g)) v.push_back(g);
  return v;
}
float gemAttr(const Player& p, const std::string& attr, bool pct) {
  float s = 0;
  auto add = [&](const ItemStack& it) {
    for (const std::string& g : gearGems(it)) { const GemDef* d = gemOf(g); if (d && d->pct == pct && attr == d->attr) s += d->v; }
    if (!it.tag.is_object() || !it.tag.contains("roll") || !it.tag["roll"].is_array()) return;
    for (const json& b : it.tag["roll"]) if (b.is_object() && b.value("a", std::string()) == attr && (b.value("p", 0) != 0) == pct) s += b.value("v", 0.f);
  };
  for (const ItemStack& a : p.armor) add(a);
  if (!p.inv.empty() && p.selected >= 0 && p.selected < (int)p.inv.size()) add(p.inv[p.selected]);
  if (!p.offhand.empty()) add(p.offhand[0]);
  return s;
}
std::string gearRollLine(const ItemStack& s) {
  if (!s.tag.is_object() || !s.tag.contains("roll") || !s.tag["roll"].is_array()) return "";
  std::string o;
  for (const json& b : s.tag["roll"]) {
    if (!b.is_object()) continue;
    if (!o.empty()) o += ", ";
    if (b.value("major", 0)) o += "Major: ";
    const float v = b.value("v", 0.f);
    const std::string attr = b.value("a", std::string());
    std::string name = attr;
    for (char& c : name) if (c == '_') c = ' ';
    if (const bal::TalentRule* rule = talentRule(attr); rule && rule->what && rule->what[0]) {
      name = rule->what;
      const size_t cut = name.find(" (");
      if (cut != std::string::npos) name = name.substr(0, cut);
      if (name.size() > 2 && name.compare(name.size() - 2, 2, " %") == 0) name.erase(name.size() - 2);
    }
    char buf[96];
    if (b.value("p", 0)) std::snprintf(buf, sizeof buf, "%+.0f%% %s", v * 100.f, name.c_str());
    else std::snprintf(buf, sizeof buf, "%+.0f %s", v, name.c_str());
    o += buf;
  }
  return o;
}
std::string gemSocketsLine(const ItemStack& s) {
  int n = gearSockets(s);
  const auto g = gearGems(s);
  std::string out = "Sockets:";
  int used = 0;
  // the gem the forge station set in it (core/tool_parts.h setToolGem: tag "gem") fills a socket of its own
  if (s.tag.is_object() && s.tag.contains("gem") && s.tag["gem"].is_string()) {
    const std::string gk = s.tag["gem"]; const ItemDef* id = reg().item(gk); const char* fx = gemEffect(gk);
    out += std::string(" ") + (id ? id->name : gk) + " (" + (fx ? fx : "set") + ")";
    used = 1; n = std::max(n, 1);
  }
  if (!n) return "";
  for (int i = 0; i + used < n; i++) {
    if (used) out += ",";
    if (i < (int)g.size()) { const GemDef* d = gemOf(g[i]); const ItemDef* id = reg().item(g[i]); out += std::string(i ? ", " : " ") + (id ? id->name : g[i]) + " (" + (d ? d->what : "") + ")"; }
    else out += std::string(i ? ", " : " ") + "empty";
  }
  return out;
}

} // namespace dq
