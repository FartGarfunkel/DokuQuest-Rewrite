// sim/inventory.h — item stacks and containers (port of game/inventory.js). A container is a
// vector<ItemStack>; stacks serialise as the original's plain {id, n, d?, c?, g?...} objects.
#pragma once
#include "core/itemstack.h"

namespace dq {

class Player;

using Slots = std::vector<ItemStack>;
// the original canMerge: same item, no contents, no wear, stackable
bool canMergeStacks(const ItemStack& a, const ItemStack& b);
int maxStackOf(const std::string& id);
inline ItemStack mkStack(const std::string& id, int n = 1) { return ItemStack(id, n); }
std::string itemName(const ItemStack& s);
// Adds as much as fits (merging first, then empty slots, in `order` if given); returns leftover.
int addToSlots(Slots& slots, ItemStack& stack, const std::vector<int>* order = nullptr);
int countIn(const Slots& slots, const std::string& id);
int removeFrom(Slots& slots, const std::string& id, int n);   // returns what couldn't be removed
ItemStack normalizeStack(const json& j);                       // ITEM_ALIASES, n >= 1, keeps d, g, c
json slotsToJson(const Slots& s);
void slotsFromJson(const json& a, Slots& s);

// PlayerInventory helpers
Slots* backpackSlots(Player& p);       // the equipped backpack's contents (resized to its size), or null
int playerCount(Player& p, const std::string& id);              // main + backpack
int playerCountTag(Player& p, const std::string& key);          // key or #tag
int playerRemove(Player& p, const std::string& id, int n);      // backpack first, then main (like the original)
int playerRemoveTag(Player& p, const std::string& key, int n);
int armorPoints(const Player& p);
float fireResist(const Player& p);
float charmBonus(const Player& p, const std::string& kind);   // hearts | fire | breath | fall
int itemLight(const std::string& id);
int carriedLight(const Player& p);

} // namespace dq
