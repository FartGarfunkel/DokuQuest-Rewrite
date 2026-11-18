// sim/skills.h — skills that grow with use, and gear that grows with you (game/skills.js + rpg.js's two new
// skills). Fourteen skills, 1..99 on a long curve (level 2 at 83 xp, 99 at 13,034,431),
// kept in Player::stats["skills"] = { skill: xp }. Tools, weapons and armour level to 50 with use
// (ItemStack tag "gx"). Small, felt rewards — the hooks below are for the gameplay code to call.
#pragma once
#include "sim/magic_combat.h"
#include "core/registry.h"
#include <map>

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
// from places without a Level at hand (the inventory screen): taken from a furnace's output, awarded on the next skillsTick
void skillQueueSmelt(Player& p, const std::string& id, int n);
void skillsTick(Level& lv, Player& p);
// blocks a player put down: breaking them again teaches nothing (kept in the chunks and saved with them, world/chunk.h)
void skillNotePlaced(Level& lv, int realm, int x, int y, int z);
bool skillTakePlaced(Level& lv, int realm, int x, int y, int z);   // was it placed? (and forget it)
bool skillWasPlaced(Level& lv, int realm, int x, int y, int z);   // was it placed? (a look only: it stays remembered)
bool smithSavesWear(const Player& p);          // a skilled smith's tools: this bit of wear doesn't happen

// ---- the perks the gameplay code asks about (data/magic.json "perks"; their numbers in sim/balance.h) ----
// Each runs where the player's blow, catch or bite is worked out: a guest's own game for what it simulates itself
// (melee, arrows, eating, walking, its armour), the host with the guest's mirror (fishing, taming, its creatures).
// a felled tree's logs (block key -> count) and its extras: Lumberjack's fifth more logs, Forester's sapling
// (`sapling`: the kind it would grow from, "" = none known)
void woodcuttingFelled(const Player& p, std::map<std::string, int>& logs, std::map<std::string, int>& extras, const std::string& sapling);
bool woodcuttingExtraLog(const Player& p);     // Lumberjack on a log chopped on its own: one more
int woodcuttingFellWear(const Player& p, int logs);   // the axe's wear for felling a tree (Heartwood halves it)
bool woodcuttingSavesWear(const Player& p);    // Heartwood on a log chopped on its own: this bit of wear doesn't happen
float fishingBiteFactor(const Player& p);      // Patient Angler: how much longer a bite stays on the hook
int fishingCatchCount(const Player& p, const std::string& kind);   // Double Catch: 1, now and then 2 fish
float fishingTreasureFactor(const Player& p);  // Sea's Favour: treasure x this
float meleeCritBonus(const Player& p);         // Keen Edge: added to a melee blow's crit chance
Entity* cleaveTarget(Level& lv, const Player& p, const Entity& target);   // Cleave: the foe beside the one struck (or null)
float bloodthirstHeal(const Player& p, float dealt);   // Bloodthirst: health back for a melee blow
float archerySpeedFactor(const Player& p);     // Steady Aim: arrow speed (and so its damage) x this
bool archeryKeepsArrow(const Player& p);       // Retrieval: this shot doesn't use its arrow
float archeryRangeFactor(const Player& p, float dist);   // Eagle Eye: an arrow's damage x this from `dist` blocks
float tameChanceOf(const Player& p, float chance);       // Beast Master: the chance a creature takes to you
float petHarmFactor(Level& lv, const std::string& owner);   // Bonded: harm a creature of `owner`'s takes x this
float petHitFactor(Level& lv, const std::string& owner);    // Pack Leader: a creature of `owner`'s hits x this
float foodWaitFactor(const Player& p);         // Quick Bite: the wait between bites x this
float feastRegenSecs(const Player& p);         // Feast: seconds of Regeneration after eating (0 = none)
bool armourSavesWear(const Player& p);         // Unbreakable: this blow doesn't wear the armour
float exploringSpeedFactor(const Player& p);   // Pathfinder, Wayfarer: walking and running x this

} // namespace dq
