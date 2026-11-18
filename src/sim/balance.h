// sim/balance.h — every RPG number in one place: what a stat point is worth, what each talent attribute (the skill
// trees' and gems' bonuses) does and where it stops, how creature levels and elites scale, and what they drop.
// Tune here; the systems only call the helpers at the bottom. tools/test_balance.cpp checks the outcome stays sane
// (time to kill, time to die, nothing immune) for gear tier x skill level x creature level x elite.
#pragma once
#include "common/common.h"
#include <string>

namespace dq {
class Player;
class Entity;
struct ItemStack;

namespace bal {
// ---- the six stats ----------------------------------------------------------------------------------------------------
// A point every STAT_EVERY total skill levels (14 skills, 14..1386: up to 277 points). STAT_CAP a stat: 6 x 50 = 300, so
// every point earned up to the last skill level still has somewhere to go; per-point values are small to match.
constexpr int STAT_EVERY = 5;
constexpr int STAT_CAP = 50;
// Constitution: stamina and toughness
constexpr float CON_STAMINA = 1.0f;        // max stamina a point
constexpr float CON_HEALTH = 0.2f;         // max health a point (a heart every ten)
constexpr float CON_REDUCE = 0.003f;       // less harm taken a point (after armour)...
constexpr float CON_REDUCE_CAP = 0.15f;    // ...at most
// Strength: melee damage
constexpr float STR_DAMAGE = 0.006f;       // +0.6% a point (+30% at 50)
// Agility: quicker on your feet
constexpr float AGI_MOVE = 0.002f;         // walking/running speed a point (+10% at 50)
constexpr float AGI_DODGE_COST = 0.006f;   // a roll's stamina cost, less a point (-30% at 50)
constexpr float AGI_DODGE_TIME = 0.004f;   // a roll's safe moment, longer a point (+20% at 50)
constexpr float AGI_ATTACK = 0.004f;       // swing recovery and bow draw, faster a point (+20% at 50)
// Intelligence: spell power, mana
constexpr float INT_SPELL = 0.006f;        // spell power a point (+30% at 50)
constexpr float INT_MANA = 0.01f;          // max mana a point (+50% at 50)
constexpr float INT_REGEN = 0.006f;        // mana regeneration a point (+30% at 50)
// Luck: critical hits, drops
constexpr float LCK_CRIT = 0.003f;         // crit chance a point (15% at 50)
constexpr float LCK_LOOT = 0.004f;         // loot luck a point (+20% at 50)
// Focus: posture damage
constexpr float FOC_POSTURE = 0.01f;       // posture damage a point (+50% at 50)
// the Rogue's path (data/magic.json paths): its crit chance, and Nimble's rolls
constexpr float ROGUE_CRIT = 0.05f;
constexpr float ROGUE_NIMBLE = 0.75f;      // a roll's stamina cost with Nimble
constexpr float ROGUE_BACKSTAB = 1.3f;     // Backstab: a sneaking melee blow
constexpr float ROGUE_VENOM = 3.f;         // Envenomed Edge: seconds of poison a melee blow

// ---- the combat base numbers that the stats scale ----------------------------------------------------------------------
constexpr float STAMINA_MAX = 100, STAMINA_REGEN = 28, DODGE_COST = 30, DODGE_TIME = 0.40f;
constexpr float POSTURE_PER_DMG = 2.f;     // a blow's posture damage: its damage x this
constexpr float CRIT_MUL = 1.5f;
constexpr float CRIT_CHANCE_CAP = 0.5f;    // stats + talents + gems together
constexpr float CRIT_DAMAGE_CAP = 1.5f;    // extra crit multiplier from talents (1.5 -> at most 3.0)
constexpr float MOVE_FACTOR_MIN = 0.85f, MOVE_FACTOR_MAX = 1.25f;   // agility + speed talents - load
constexpr float ATTACK_SPEED_CAP = 0.35f;  // agility + attack speed talents
constexpr float DRAW_SPEED_MIN = -0.4f, DRAW_SPEED_MAX = 0.6f;

// ---- talent attributes (skill tree nodes and gems) ----------------------------------------------------------------------
// value = flat x flatScale + pct, clamped to [lo, hi]. flatScale 0: the flat part is used elsewhere as it is (health, armour,
// mana, stamina, damage add their flat amount directly) and only the percentage is capped here.
struct TalentRule { const char* attr; float flatScale, lo, hi; const char* what; };
inline constexpr TalentRule TALENTS[] = {
    {"damage", 0, -0.5f, 1.0f, "melee damage %"},
    {"health", 0, -0.5f, 1.0f, "max health %"},
    {"stamina", 0, -0.5f, 1.0f, "max stamina %"},
    {"crit_chance", 1, 0, CRIT_CHANCE_CAP, "crit chance"},
    {"crit_damage", 1, 0, CRIT_DAMAGE_CAP, "crit damage"},
    {"armor_pierce", 0.025f, 0, 0.5f, "share of a target's armour ignored (a flat point = 2.5%)"},
    {"arrow_damage", 0, -0.3f, 0.6f, "arrow damage"},
    {"arrow_speed", 0, -0.3f, 0.4f, "arrow speed"},
    {"draw_speed", 0, DRAW_SPEED_MIN, DRAW_SPEED_MAX, "bow draw speed"},
    {"attack_speed", 0.1f, -0.2f, ATTACK_SPEED_CAP, "melee attack speed (a flat point = 10%)"},
    {"posture_damage", 0.04f, 0, 0.6f, "posture damage (a flat point = 4%)"},
    {"dodge", 1, 0, 0.2f, "chance to evade a blow outright"},
    {"speed", 0, -0.1f, 0.15f, "movement speed"},
    {"weight", 0.0025f, -0.1f, 0.1f, "load: slows you and makes rolls cost more (negative = lighter; a flat point = 0.25%)"},
    {"step", 0.5f, 0, 0.5f, "step height in blocks (a flat point = half a block)"},
    {"stamina_regen", 0, -0.3f, 0.6f, "stamina regeneration"},
    {"knockback", 0.1f, 0, 0.6f, "knockback dealt (a flat point = 10%)"},
    {"knock_resist", 1, 0, 0.6f, "knockback resisted"},
    {"stun_armor", 0.02f, 0, 0.5f, "stagger resistance: shorter slows and chills, cheaper blocks (a flat point = 2%)"},
    {"cooldown", 0, 0, 0.4f, "spell cooldown reduction"},
    {"cast_time", 0, 0, 0.4f, "spell wind-up reduction"},
    {"overheal", 1, 0, 0.3f, "healing past full kept as a ward, up to this share of max health"},
    {"summon_damage", 0, 0, 0.5f, "summoned allies' damage"},
    {"luck", 0.05f, 0, 0.3f, "loot luck (a flat point = 5%)"},
    {"fishing", 0.1f, 0, 0.5f, "fishing luck (a flat point = 10%)"},
};

// ---- creature levels and elites ---------------------------------------------------------------------------------------------
constexpr float MOB_HP_PER_LEVEL = 0.08f, MOB_DMG_PER_LEVEL = 0.05f, MOB_XP_PER_LEVEL = 0.06f;
// past MOB_SOFT_LEVEL a level counts MOB_SOFT_SLOPE of one toward health, damage and posture (experience keeps the whole):
// skill totals reach creature level 173, and +5% damage a level unchecked outran every armour (a ghoul hit for 29)
constexpr int MOB_SOFT_LEVEL = 20; constexpr float MOB_SOFT_SLOPE = 0.4f;
constexpr float MOB_POSTURE_BASE = 40, MOB_POSTURE_PER_LEVEL = 2;
constexpr float ELITE_CHANCE = 1.f / 14, ELITE_HP = 1.6f, ELITE_DMG = 1.25f, ELITE_XP = 2.f, ELITE_POSTURE = 1.4f;
constexpr int ELITE_LEVELS = 3;
// what they drop (a player's kill): each entry's chance x (1 + level bonus + luck), and a chance of one more of it
constexpr float LOOT_CHANCE_PER_LEVEL = 0.03f, LOOT_CHANCE_MAX = 2.0f;   // a multiplier on each entry's chance, at most x2
constexpr float LOOT_EXTRA_PER_LEVEL = 0.015f, LOOT_EXTRA_MAX = 0.5f;    // chance of +1 of an entry that dropped
constexpr int ELITE_ROLLS = 2;                                          // an elite rolls its table twice
// a piece of gear: an elite's chance, and a high-level creature's (from level GEAR_FROM)
constexpr float ELITE_GEAR = 0.35f, ELITE_GEAR_PER_LEVEL = 0.005f, ELITE_GEAR_MAX = 0.6f;
constexpr int GEAR_FROM = 12; constexpr float GEAR_CHANCE_PER_LEVEL = 0.004f, GEAR_CHANCE_MAX = 0.12f;
constexpr int GEAR_LEVELS_PER_TIER = 6;                                  // copper 1-6, bronze 7-12, iron 13-18... (+1 tier for an elite)
inline constexpr const char* GEAR_TIERS[] = {"copper", "bronze", "iron", "steel", "diamond", "mithril", "adamantite"};
inline constexpr const char* GEAR_PIECES[] = {"sword", "axe", "helmet", "chestplate", "leggings", "boots"};
constexpr float GEAR_WORN_MIN = 0.2f, GEAR_WORN_MAX = 0.6f;              // dropped gear comes this worn
} // namespace bal

// ---- helpers (sim/balance.cpp) --------------------------------------------------------------------------------------------
float talent(const Player& p, const std::string& attr);   // the capped talent value (see TALENTS)
const bal::TalentRule* talentRule(const std::string& attr);
int statPointsEarned(const Player& p);
float conStamina(const Player& p);        // flat max stamina from Constitution
float conHealth(const Player& p);         // flat max health from Constitution
float conReduction(const Player& p);      // share of harm taken away
float strDamageFactor(const Player& p);
float critChance(const Player& p);
float critMultiplier(const Player& p);
float moveFactor(const Player& p);        // agility + speed - load
float stepBonus(const Player& p);
float dodgeCost(const Player& p);
float dodgeTime(const Player& p);
float dodgeChance(const Player& p);
float attackSpeedFactor(const Player& p); // divide a swing's recovery by this
float drawSpeedFactor(const Player& p);   // the bow draws this much faster
float arrowDamageFactor(const Player& p);
float arrowSpeedFactor(const Player& p);
float armorPierce(const Player& p);
float postureFactor(const Player& p);
float staminaRegenFactor(const Player& p);
float knockbackFactor(const Player& p);
float knockResist(const Player& p);
float stunResist(const Player& p);
float spellPowerFactor(const Player& p);  // Intelligence (the trees' spell power is added where spells are cast)
float manaFactor(const Player& p);
float manaRegenFactor(const Player& p);
float cooldownFactor(const Player& p);    // multiply a cooldown by this
float castTimeFactor(const Player& p);
float overhealShare(const Player& p);
float summonDamageFactor(const Player& p);
float lootLuck(const Player& p);
float fishingLuck(const Player& p);

// creature levels
float mobEffLevel(int lvl);   // the level its health, damage and posture scale by (soft past MOB_SOFT_LEVEL)
float mobLevelHp(int lvl);
float mobLevelDmg(int lvl);
// what a creature's death adds to its drop table: rolls of the table, each entry's chance multiplier, the chance of one
// more of an entry that drops, and a chance at a piece of gear of a tier fitting its level
struct MobLoot { int rolls = 1; float chanceMul = 1, extra = 0, gearChance = 0; int gearTier = 0; };
MobLoot mobLoot(int level, bool elite, const Player* killer);
ItemStack mobGearRoll(const MobLoot& l, double r1, double r2, double r3);   // r* in [0,1): which piece, its tier, its wear
} // namespace dq
