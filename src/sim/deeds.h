// sim/deeds.h — the progression systems feeding each other.
//  · Deeds: each chapter of the story finished (the side branches too) is a talent point and a keepsake of it; each kind
//    of boss beaten for the first time (each player their own, everyone close by when it falls) a stat point; the
//    harder achievements a talent point each. A toast says what and why. Kept in the player's data "deeds" {tp, sp,
//    got: {"ch:village": 1, "boss:wither": 1, "ach:diamond": 1}}: granted by the world's own game (the host), saved
//    with the player, sent to a guest's game with their other progression (net/server_host.cpp diffMirrors "dd").
//    treePointsEarned (sim/skilltree.h) and statPointsEarned (sim/balance.h) add them.
//  · Work experience: the hands-on crafts teach their skill (Smithing from the metal chain, Taming from creatures,
//    Cooking from every fire). The amounts are here, beside the existing ones in sim/skills.cpp.
//  · The Paths and the skill trees: a Path chosen at a village trainer makes its tree's starting node free, and the
//    tree tab names the trainer; a tree's start taken points to the trainer of its Path.
#pragma once
#include "sim/level.h"
#include "sim/player.h"
#include "core/itemstack.h"

namespace dq {
struct SkillTree;
struct TreeNode;

// ---- deeds: bonus points ----------------------------------------------------------------------------------------------
int deedTalentPoints(const Player& p);
int deedStatPoints(const Player& p);
bool deedDone(const Player& p, const std::string& key);
// once per key: the points, the keepsake (if any) into the bag, a toast "<why>: +1 talent point". false: had it already
bool deedGrant(Level& lv, Player& p, const std::string& key, int talent, int stat, const std::string& why, const ItemStack& keepsake = {});
void deedsCheckStory(Level& lv, Player& p);                               // each chapter done and not yet rewarded
void deedsOnAchievement(Level& lv, Player& p, const std::string& id);     // (sim/achievements.cpp grant)
bool deedHardAchievement(const std::string& id);                          // one worth a talent point
ItemStack deedKeepsake(const std::string& chapter, const std::string& title);
void registerDeeds();                                                      // the checker: the story, bosses, crafting

// ---- work experience --------------------------------------------------------------------------------------------------
namespace workxp {
// Smithing (crafting a metal piece at a station is 40, an ingot out of a furnace 10)
constexpr double KNAP_STONE = 5, KNAP_CLAY = 6;       // a piece knapped or shaped
constexpr double KILN_LIT = 20;                       // a pit kiln fired
constexpr double CHARCOAL_LIT = 25;                   // a charcoal pit lit
constexpr double FORGE_LIT = 10, BELLOWS = 1;         // a forge lit; a pump of the bellows (at most one every BELLOWS_GAP s)
constexpr double BELLOWS_GAP = 3;
constexpr double BLOOMERY_LIT = 30;                   // a bloomery fired
constexpr double CAST_POUR = 15, CAST_OPEN = 15;      // metal poured into a mold; a cast piece broken out
constexpr double ANVIL_WORK = 30, ANVIL_EXACT = 15;   // a piece finished on the anvil; more for exact work
constexpr double TABLE_METAL = 20;                    // a metal tool or armour made at the crafting table
// Taming (breeding is 25)
constexpr double TAME = 50;                           // a wild creature tamed
constexpr double RIDE_PER = 50, RIDE_XP = 3;          // every 50 blocks ridden
constexpr double CARE = 5;                            // a companion fed back to health, a farm animal's daily feed
constexpr double HATCH_CHICK = 8, HATCH_DRAKE = 150;  // an egg that hatches: a chick, a dragon's drake
// Cooking (a cooked food out of a furnace is 12)
constexpr double CAMPFIRE = 10;                       // food cooked on a campfire or firepit
constexpr double TABLE_FOOD = 6;                      // food put together at the crafting table (bread, stews, pies)
}
// skillGain for whoever's own state is here (a guest's copy on the host is not theirs to grow: it'd be lost)
void workXp(Level& lv, Player& p, const std::string& skill, double xp);
void workXpNear(Level& lv, int realm, vec3 at, float range, const std::string& skill, double xp);   // the nearest player
void onKnapped(Level& lv, Player& p, const std::string& material);
void onAnvilWorked(Level& lv, Player& p, int quality);                   // quality: anvilQuality (0 exact)
void onRidden(Level& lv, Player& p, float metres);                       // a mount's rider moved so far

// ---- the Paths and the trees --------------------------------------------------------------------------------------------
std::string treeOfPath(const std::string& path);      // knight -> warrior, ranger, rogue, mage
std::string pathOfTree(const std::string& tree);      // warrior -> knight ...
std::string trainerJobOf(const std::string& path);    // villagers.json "trains": knight -> guard
std::string playerPathKey(const Player& p);           // "" none
std::string pathTrainerName(const Player& p);         // who taught it ("" unknown)
bool treeStartFree(const Player& p, const SkillTree& t, const TreeNode& n);   // the start of their Path's tree
int treeFreeNodes(const Player& p);                   // 1 while that start is learnt
void pathTreeOnPick(Level& lv, Player& p, const std::string& path);           // (pathPick) its tree's start, free
void pathTreeOnStart(Level& lv, Player& p, const std::string& tree);          // (treeLearn of a start) who teaches its Path
std::string treeTrainerLine(const Player& p, const std::string& tree);        // the tree tab's line about its trainer

} // namespace dq
