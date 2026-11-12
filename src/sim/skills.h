// sim/skills.h — skills that grow with use, and gear that grows with you (game/skills.js + rpg.js's two new
// skills). Fourteen RuneScape-style skills, 1..99 on the classic curve (level 2 at 83 xp, 99 at 13,034,431),
// kept in Player::stats["skills"] = { skill: xp }. Tools, weapons and armour level to 50 with use
// (ItemStack tag "gx"). Small, felt rewards — the hooks below are for the gameplay code to call.
#pragma once
#include "sim/magic_combat.h"
#include "core/registry.h"

namespace dq {

int skillLevel(double xp);
double skillXpFor(int level);                                   // SKILL_XP[level]
double skillXp(const Player& p, const std::string& k);
int skillLv(const Player& p, const std::string& k);
float skillBonus(const Player& p, const std::string& k, float max);   // 0 at level 1, max at 99
int totalLevel(const Player& p);
// Earn experience (not in creative): toasts a level, a perk (rpg.js), emits "skillXp" [player, skill, xp, level]
// for the HUD's little "+12 Mining" pops. The Path's own skill gets 10% more (paths.js).
void skillGain(Level& lv, Player& p, const std::string& k, double xp);

// ---- gear levels ----
constexpr int GEAR_MAX = 50;
int gearLevelOf(double gx);
inline int gearLevel(const ItemStack& s) { return gearLevelOf(s.tag.is_object() ? s.tag.value("gx", 0.0) : 0.0); }
double gearXpFor(int level);
float gearBonus(const ItemStack& s, float max);                // 0 at level 1, max at 50
void gearGain(Level& lv, Player& p, ItemStack& s, double n);   // tools, armour, bows

// ---- what a block is worth, and to which skill (ORE_XP) ----
struct BlockSkill { std::string skill; int xp = 0; bool ore = false; };
bool blockSkill(const BlockDef& b, BlockSkill& out);

// ---- hooks for the gameplay code ----
// break time divisor from skills, a well-used tool and the Stonebreaker/Earthmover perks (skills.js + rpg.js)
float skillBreakDivisor(const Player& p, const BlockDef& b, const ItemStack& held);
// a block broken by `p` (survival, it was harvestable, `placed` = put there by a player): experience, gear,
// the lucky extra drop, perks (Prospector, Motherlode, Keen Eye, Treasure Hunter, Bountiful, Replanter).
void onBlockBroken(Level& lv, Player& p, int x, int y, int z, const BlockDef& b, u8 meta, bool harvest, bool placed);
// a blow landed on a creature by a player (melee/archery skill bonus is in skillHitFactor)
float skillHitFactor(const Player& p, const std::string& kind, const ItemStack& held);   // kind melee|archery
void onPlayerHitMob(Level& lv, Player& p, Entity& mob, const std::string& kind, float dealt, float hpBefore);
void onCrafted(Level& lv, Player& p, const std::string& out, int made, bool metal, bool alloy);   // Smithing (+ Fine Work)
void onSmeltedTaken(Level& lv, Player& p, const std::string& id, int n);                          // Cooking / Smithing
void onFishCaught(Level& lv, Player& p, const std::string& kind);                                 // fish | treasure | junk
void onBred(Level& lv, Player& p);
float cookingHealFactor(const Player& p);      // food heals more (Cooking + Hearty)
bool smithSavesWear(const Player& p);          // a skilled smith's tools: this bit of wear doesn't happen

} // namespace dq
