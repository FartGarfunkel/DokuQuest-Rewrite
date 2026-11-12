// sim/enchanting.h — the enchanting table and what enchantments do (game/enchanting.js + arcana.js's six and
// deepdark.js's Swift Sneak). Enchantments live on the stack: ItemStack::tag["en"] = { sharp: 3, unb: 2 }.
// Three offers per item from lapis + levels, stronger with up to fifteen bookshelves two blocks out; books
// carry one (anvil puts them on, grindstone takes them off). Every number is the original's.
#pragma once
#include "sim/magic_combat.h"
#include "core/registry.h"
#include "common/noise.h"
#include <array>

namespace dq {

class World;

std::string enchName(const std::string& k, int lv);       // "Sharpness III"
std::string enchKind(const std::string& itemId);           // book, ebook, sword, pickaxe, helmet, chest, ... ("" = none)
bool enchApplies(const std::string& k, const std::string& kind);
bool enchCompatible(const json& en, const std::string& k);
json enchOf(ItemStack& s);                                  // a treasure book decides what it holds when first looked at
int enchLv(const ItemStack& s, const std::string& k);
int enchArmorLv(const Player& p, const std::string& k);    // summed over the worn pieces
int enchArmorMax(const Player& p, const std::string& k);
int enchantability(const std::string& itemId);
int enchLevelFor(const std::string& k, int power);
json enchRoll(const std::string& kind, int L, int E, Mulberry32& r);
json enchRandomBook(Mulberry32& r, int power, bool treasure);

struct EnchOffer { int level = 0, cost = 0; json en; std::vector<std::string> order; };   // order = the picks in turn (the hint shows the first)
// null optional = can't be enchanted (or already is)
bool enchOffers(const ItemStack& s, int shelves, u32 seed, std::array<EnchOffer, 3>& out);
int enchShelves(const World& w, int x, int y, int z);
std::vector<int> enchRunes(u32 seed, int n);                // rune indices (-1 = a space)
u32 enchSeed(Player& p);                                   // stats.enchSeed (new after each enchanting)
// The table's panel: pick offer i for the item in `item` with `lapis`. Returns "" when done, else the
// warning the original toasted ("You need level 9", "That takes 2 lapis").
std::string enchApplyOffer(Level& lv, Player& p, ItemStack& item, ItemStack& lapis, int i, int shelves, vec3 tablePos, ItemStack* bookLeftover);

// ---- anvil / grindstone (stationResult additions) ----
struct StationResult { ItemStack out; int useA = 0, useB = 0, lv = 0, xp = 0; std::string note; bool ok = false; };
StationResult anvilEnchant(ItemStack& a, ItemStack& b);    // a book (or a same-item) onto gear
StationResult grindstoneDisenchant(ItemStack* a, ItemStack* b);

// ---- what they do (hooks for gameplay; every function is cheap) ----
float enchMiningBonus(const ItemStack& held);              // Efficiency: + eff² + 1 to the tool's speed
float enchDamageBonus(const ItemStack& held, const Entity* target);   // Sharpness, Smite (undead), Bane (arthropods)
float enchKnockFactor(const ItemStack& held);              // Knockback (melee)
float enchFireAspect(const ItemStack& held);               // seconds of burning (0 = none)
float protectionFactor(const Player& p, const std::string& cause, const std::string& element = "");   // multiply the harm
bool enchUnbreakingSaves(const ItemStack& s);              // this point of wear doesn't happen (held tools)
bool enchArmorWearSaves(const ItemStack& s);               // this hit doesn't wear this armour piece
int enchLootingExtra(const ItemStack& held, double chance); // extra drops of one loot line
// Silk Touch / Fortune: what a block drops instead (true = use `out`; the original's dropBlockItems hook)
bool enchBlockDrops(const ItemStack& held, const BlockDef& b, std::vector<ItemStack>& out);
std::string enchSmeltDrop(const ItemStack& held, const std::string& drop);   // Smelting Touch ("" = unchanged)
float enchAquaDivisor(const Player& p, bool eyeInWater);   // Aqua Affinity: break time / this
int enchBreathBonus(const Player& p);                      // Respiration
float enchMoveFactor(const Player& p, bool inWater, bool sneakingOrCrawling, bool flying);   // Depth Strider, Swiftness, Swift Sneak
float enchArrowDamageFactor(const ItemStack& bow);         // Power
int enchArrowPunch(const ItemStack& bow);
bool enchArrowFlame(const ItemStack& bow);
bool enchInfinity(const ItemStack& bow);
bool enchVolley(const ItemStack& bow);
int enchLure(const ItemStack& rod);                        // bite comes lure*4 s sooner (min 2)
int enchLuck(const ItemStack& rod);                        // treasure: luck * 3%
// after a melee blow that dealt `dealt` (Vampirism heals, Frost Aspect slows)
void enchAfterMelee(Level& lv, Player& p, Entity& target, float dealt);
// after `p` was hurt by `attacker` (Thorns)
void enchThorns(Level& lv, Player& p, Entity* attacker, float healthBefore);
int enchXpGain(Player& p, int n, bool* mended = nullptr);  // Insight, then Mending eats experience to repair

} // namespace dq
