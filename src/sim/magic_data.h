// sim/magic_data.h — the tables of the original's magic, enchanting, brewing, skills and RPG modules
// (game/magic.js, arcana.js, spellcraft.js, paths.js, enchanting.js, brewing.js, skills.js, rpg.js),
// extracted from the running JS game by tools/port/jsdump.py into data/magic.json (see docs/port/magic.md).
#pragma once
#include "common/fs.h"
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace dq {

struct SpellDef {
  std::string key, name, col, fx, school, desc;
  int mana = 0, lvl = 1;
  bool util = false;     // a quiet spell: doesn't repeat while the button is held
  float cd = 0;          // its own cooldown in seconds (Home Teleport)
};
struct EnchDef {
  std::string key, name, group, desc;
  int max = 1, w = 1, minP = 1;
  bool treasure = false;
  std::vector<std::string> on, excl;
};
struct EffectDef { std::string key, name, col; bool bad = false; };
struct PotionFx { std::string eff; float secs = 0; int lv = 0; };   // instant ones (heal/harm/mana): secs = amount
struct PotionDef { std::string key, name, col, ing; std::vector<PotionFx> fx, lng, strong; bool hasLong = false, hasStrong = false; };
struct SkillDef { std::string key, name, col; };
struct PerkDef { int lvl = 30; std::string name, desc; };

struct MagicData {
  std::vector<std::string> spellOrder;
  std::unordered_map<std::string, SpellDef> spells;
  std::vector<std::array<std::string, 3>> schools;          // key, name, colour
  std::vector<std::string> enchOrder;
  std::unordered_map<std::string, EnchDef> ench;
  std::unordered_map<std::string, std::pair<std::string, int>> enchItems;   // item -> (kind, enchantability)
  std::vector<std::string> roman;
  std::vector<std::string> runes;                            // ENCH_RUNES, one UTF-8 glyph each
  std::vector<std::string> effectOrder;
  std::unordered_map<std::string, EffectDef> effects;
  std::vector<std::string> potionOrder;
  std::unordered_map<std::string, PotionDef> potions;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> brew;   // potion item -> ingredient -> potion item
  std::unordered_set<std::string> brewIngs;
  float brewTime = 12;
  std::vector<SkillDef> skills;
  std::vector<double> skillXp;                               // SKILL_XP[level]
  std::unordered_map<std::string, std::vector<PerkDef>> perks;
  std::unordered_map<std::string, int> oreXp;
  std::unordered_map<std::string, double> alch;              // alchValue (emeralds an item is worth)
  json robes, paths, fragFoes, dimLabel, spiritWolf;
  int pathAt = 20;
  bool loaded = false;

  const SpellDef* spell(const std::string& k) const { auto it = spells.find(k); return it == spells.end() ? nullptr : &it->second; }
  const EnchDef* enchant(const std::string& k) const { auto it = ench.find(k); return it == ench.end() ? nullptr : &it->second; }
  const EffectDef* effect(const std::string& k) const { auto it = effects.find(k); return it == effects.end() ? nullptr : &it->second; }
  std::string schoolName(const std::string& k) const { for (auto& s : schools) if (s[0] == k) return s[1]; return k; }
  std::string skillName(const std::string& k) const { for (auto& s : skills) if (s.key == k) return s.name; return k; }
};

const MagicData& magicData();                 // loads dataDir()/magic.json the first time
bool loadMagicData(const fs::path& file);     // (tests / data packs)

double mrand();   // Math.random() for the magic modules

// "#ff7a2a" -> 0xRRGGBB
u32 hexColor(const std::string& s);
inline vec3 hexRGBf(const std::string& s) { u32 c = hexColor(s); return vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.f; }

} // namespace dq
