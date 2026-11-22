// sim/balance.cpp — see balance.h (all the numbers live there)
#include "sim/balance.h"
#include "sim/player.h"
#include "sim/skilltree.h"
#include "sim/skills.h"
#include "sim/deeds.h"
#include "sim/rpg.h"
#include "core/tool_parts.h"
#include "core/registry.h"
#include "sim/weapon_quality.h"
#include "sim/attack_moves.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace dq {
using namespace bal;

const TalentRule* talentRule(const std::string& attr) {
  for (const TalentRule& r : TALENTS) if (attr == r.attr) return &r;
  return nullptr;
}
float talent(const Player& p, const std::string& attr) {
  const TalentRule* r = talentRule(attr);
  const float pct = treeAttrPct(p, attr);
  if (!r) return pct;
  float v = (r->flatScale != 0 ? treeAttr(p, attr) * r->flatScale : 0.f) + pct;
  if (hasTal(p, "vanish") && attr == "stealth") v += 0.25f;
  if (hasTal(p, "cutpurse") && attr == "loot_double") v += 0.20f;
  return std::clamp(v, r->lo, r->hi);
}

int statPointsEarned(const Player& p) { return std::max(0, totalLevel(p) / STAT_EVERY) + deedStatPoints(p); }   // (+ the bosses beaten: sim/deeds.h)
static int st(int v) { return std::clamp(v, 0, STAT_CAP); }

float conStamina(const Player& p) { return st(p.statCon) * CON_STAMINA; }
float conHealth(const Player& p) { return st(p.statCon) * CON_HEALTH; }
float conReduction(const Player& p) { return std::min(CON_REDUCE_CAP, st(p.statCon) * CON_REDUCE); }
float strDamageFactor(const Player& p) { return 1 + st(p.statStr) * STR_DAMAGE; }
float critChance(const Player& p) { return std::min(CRIT_CHANCE_CAP, st(p.statLck) * LCK_CRIT + talent(p, "crit_chance") + (onPath(p, "rogue") ? ROGUE_CRIT : 0.f)); }
float critMultiplier(const Player& p) { return CRIT_MUL + talent(p, "crit_damage"); }
float moveFactor(const Player& p) {
  return std::clamp(1 + st(p.statAgi) * AGI_MOVE + talent(p, "speed") - talent(p, "weight"), MOVE_FACTOR_MIN, MOVE_FACTOR_MAX) * exploringSpeedFactor(p);   // (Pathfinder, Wayfarer: past the cap)
}
float stepBonus(const Player& p) { return talent(p, "step"); }
float dodgeCost(const Player& p) { return DODGE_COST * (1 - st(p.statAgi) * AGI_DODGE_COST) * (1 + 2 * talent(p, "weight")) * (hasTal(p, "nimble") ? ROGUE_NIMBLE : 1.f); }
float dodgeTime(const Player& p) { return DODGE_TIME * (1 + st(p.statAgi) * AGI_DODGE_TIME); }
float dodgeChance(const Player& p) { return talent(p, "dodge"); }
float attackSpeedFactor(const Player& p) { return 1 + std::clamp(st(p.statAgi) * AGI_ATTACK + talent(p, "attack_speed"), -0.2f, ATTACK_SPEED_CAP); }
float drawSpeedFactor(const Player& p) { return 1 + std::clamp(st(p.statAgi) * AGI_ATTACK + talent(p, "draw_speed"), DRAW_SPEED_MIN, DRAW_SPEED_MAX); }
float arrowDamageFactor(const Player& p) { return 1 + talent(p, "arrow_damage"); }
float arrowSpeedFactor(const Player& p) { return 1 + talent(p, "arrow_speed"); }
float armorPierce(const Player& p) { return talent(p, "armor_pierce"); }
float postureFactor(const Player& p) { return 1 + st(p.statFoc) * FOC_POSTURE + talent(p, "posture_damage"); }
float staminaRegenFactor(const Player& p) { return 1 + talent(p, "stamina_regen"); }
float knockbackFactor(const Player& p) { return 1 + talent(p, "knockback"); }
float knockResist(const Player& p) { return talent(p, "knock_resist"); }
float stunResist(const Player& p) { return talent(p, "stun_armor"); }
float spellPowerFactor(const Player& p) { return 1 + st(p.statInt) * INT_SPELL; }
float manaFactor(const Player& p) { return 1 + st(p.statInt) * INT_MANA; }
float manaRegenFactor(const Player& p) { return 1 + st(p.statInt) * INT_REGEN; }
float cooldownFactor(const Player& p) { return 1 - talent(p, "cooldown"); }
float castTimeFactor(const Player& p) { return 1 - talent(p, "cast_time"); }
float overhealShare(const Player& p) { return talent(p, "overheal"); }
float summonDamageFactor(const Player& p) { return 1 + talent(p, "summon_damage"); }
float lootLuck(const Player& p) { return st(p.statLck) * LCK_LOOT + talent(p, "luck"); }
float fishingLuck(const Player& p) { return talent(p, "fishing"); }
float noticeRange(const Player& p, float range) {
  if (!p.sneaking) return range;
  return std::max(std::min(range, SNEAK_NOTICE_MIN), range * (1 - talent(p, "stealth")));
}
float healReceivedFactor(const Player& p) { return 1 + talent(p, "heal_received"); }
float effectDurationFactor(const Player& p) { return 1 + talent(p, "effect_duration"); }
float xpGainFactor(const Player& p) { return 1 + talent(p, "xp_gain"); }
float damageTakenFactor(const Player& p) { return 1 - talent(p, "damage_reduction"); }
float jumpFactor(const Player& p) { return 1 + talent(p, "jump"); }
float useMoveFactor(const Player& p) { return 1 + talent(p, "use_move"); }
float rageFactor(const Player& p) {
  const float r = talent(p, "rage");
  if (r <= 0 || p.maxHp <= 0) return 1;
  return 1 + r * std::clamp(1 - (p.hp - 1) / std::max(1.f, p.maxHp - 1), 0.f, 1.f);   // (none at full health, all of it at the last heart)
}

float mobEffLevel(int lvl) { lvl = std::max(1, lvl); return lvl <= MOB_SOFT_LEVEL ? (float)lvl : MOB_SOFT_LEVEL + (lvl - MOB_SOFT_LEVEL) * MOB_SOFT_SLOPE; }
float mobLevelHp(int lvl) { return 1.f + MOB_HP_PER_LEVEL * (mobEffLevel(lvl) - 1); }
float mobLevelDmg(int lvl) { return 1.f + MOB_DMG_PER_LEVEL * (mobEffLevel(lvl) - 1); }

int progressTier(const Player& p) {
  float score = totalLevel(p) / (float)TIER_SKILL_PER;
  const json& D = p.data.contains("deeds") && p.data["deeds"].is_object() ? p.data["deeds"] : json::object();
  if (D.contains("got") && D["got"].is_object())
    for (auto it = D["got"].begin(); it != D["got"].end(); ++it) {
      if (it.key().rfind("ch:", 0) == 0) score += TIER_PER_CHAPTER;
      else if (it.key().rfind("boss:", 0) == 0) score += TIER_PER_BOSS;
    }
  return std::clamp((int)(score / TIER_DIV), 0, TIER_MAX);
}
float progressLootRolls(int tier) { return LOOT_TIER_ROLLS * std::log2(1.f + std::max(0, std::min(tier, TIER_MAX))); }
float progressLootChance(int tier) { return std::min(LOOT_TIER_CHANCE_MAX, 1.f + LOOT_TIER_CHANCE * std::log2(1.f + std::max(0, std::min(tier, TIER_MAX)))); }
int progressMobLevels(int tier) { return std::min(MOB_TIER_MAX, (int)std::lround(MOB_TIER_LEVELS * std::log2(1.f + std::max(0, std::min(tier, TIER_MAX))))); }

MobLoot mobLoot(int level, bool elite, const Player* killer) {
  MobLoot l;
  if (!killer) return l;   // (only a player's kill: a creature that fell in lava drops its plain table)
  const int up = std::max(1, level) - 1;
  const float luck = lootLuck(*killer);
  l.rolls = elite ? ELITE_ROLLS : 1;
  l.chanceMul = std::min(LOOT_CHANCE_MAX, 1 + up * LOOT_CHANCE_PER_LEVEL + luck);
  l.extra = std::min(LOOT_EXTRA_MAX, up * LOOT_EXTRA_PER_LEVEL + luck * 0.5f);
  l.twice = talent(*killer, "loot_double");
  if (elite) l.gearChance = std::min(ELITE_GEAR_MAX, ELITE_GEAR + up * ELITE_GEAR_PER_LEVEL);
  else if (level >= GEAR_FROM) l.gearChance = std::min(GEAR_CHANCE_MAX, (level - GEAR_FROM + 1) * GEAR_CHANCE_PER_LEVEL);
  if (l.gearChance > 0) l.gearChance = std::min(1.f, l.gearChance * (1 + luck));
  const int tiers = (int)(sizeof GEAR_TIERS / sizeof GEAR_TIERS[0]);
  const int fromLevel = std::min(tiers - 1, up / GEAR_LEVELS_PER_TIER + (elite ? 1 : 0));
  const int fromProgress = std::min(tiers - 1, progressTier(*killer) + (elite ? 1 : 0));
  l.gearTier = std::min(fromLevel, fromProgress);
  return l;
}

static bool foundGear(const ItemStack& s) {
  if (s.empty()) return false;
  if (isPartTool(s) || isPartArmor(s)) return true;
  switch (movesetOf(s.def())) {
    case Moveset::Sword: case Moveset::Dagger: case Moveset::Axe:
    case Moveset::Greatsword: case Moveset::Spear: case Moveset::Mace: return true;
    default: return false;
  }
}
// proceduralOf may already have written a qual from its own counter. A drop uses this call's r1, r2, r3.
static uint32_t lootQualSeed(const std::string& id, double r1, double r2, double r3) {
  uint32_t h = 2166136261u;
  auto mix = [&](double r) { h = (h ^ (uint32_t)std::llround(std::fabs(r) * 1000003.0)) * 16777619u; };
  mix(r1); mix(r2); mix(r3);
  for (unsigned char c : id) h = (h ^ c) * 16777619u;
  return h;
}
ItemStack mobGearRoll(const MobLoot& l, double r1, double r2, double r3) {
  const int tiers = (int)(sizeof GEAR_TIERS / sizeof GEAR_TIERS[0]), pieces = (int)(sizeof GEAR_PIECES / sizeof GEAR_PIECES[0]);
  int tier = std::clamp(l.gearTier + (r2 < 0.15 ? 1 : 0), 0, tiers - 1);   // now and then a tier better
  const std::string id0 = std::string(GEAR_TIERS[tier]) + "_" + GEAR_PIECES[std::clamp((int)(r1 * pieces), 0, pieces - 1)];
  // silver is a holy sidegrade to iron, not a rung above steel
  const std::string id = (id0.rfind("iron_", 0) == 0 && r3 < 0.25) ? ("silver_" + id0.substr(5)) : id0;
  if (!reg().item(id)) return {};
  ItemStack s = proceduralOf(id, 1);
  if (foundGear(s)) rollWeaponQuality(s, lootQualSeed(s.id, r1, r2, r3));
  if (const int max = toolMaxDurability(s); max > 0) s.d = (int)(max * (GEAR_WORN_MIN + r3 * (GEAR_WORN_MAX - GEAR_WORN_MIN)));
  // a found piece is not plain: one boon, one drawback, and rarely a major, from the talents that already exist
  struct Bit { const char* a; float v; int pct; };
  static const Bit good[] = {{"damage", 0.06f, 1}, {"armor", 1.f, 0}, {"crit_chance", 0.03f, 1}, {"stamina", 0.06f, 1}, {"armor_pierce", 0.04f, 1}};
  static const Bit bad[] = {{"speed", -0.04f, 1}, {"damage", -0.04f, 1}, {"stamina_regen", -0.08f, 1}};
  static const Bit major[] = {{"lifesteal", 0.04f, 1}, {"armor_pierce", 0.12f, 1}, {"dodge", 0.04f, 1}};
  const int gi = (int)(std::fmod(r1 * 12.9898, 1.0) * 5) % 5, bi = (int)(std::fmod(r2 * 78.233, 1.0) * 3) % 3;
  if (!s.tag.is_object()) s.tag = json::object();
  json roll = json::array({json{{"a", good[gi].a}, {"v", good[gi].v}, {"p", good[gi].pct}}, json{{"a", bad[bi].a}, {"v", bad[bi].v}, {"p", bad[bi].pct}}});
  if (std::fmod(r1 * 17.13 + r2 * 3.7 + r3 * 9.1, 1.0) < 0.08) {
    const int mi = (int)(std::fmod(r3 * 41.7, 1.0) * 3) % 3;
    roll.push_back(json{{"a", major[mi].a}, {"v", major[mi].v}, {"p", major[mi].pct}, {"major", 1}});
  }
  s.tag["roll"] = roll;
  return s;
}

} // namespace dq
