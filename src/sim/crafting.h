// sim/crafting.h — recipe matching exactly like the original (core/items.js matchGrid + the wrappers in
// game/dyeing.js and game/maps.js), recipe-book helpers (game/inventory.js) and the furnace/smoker/blast
// furnace rules (core/items.js SMELTING, game/game.js tickFurnace, game/itemfix.js). Pure logic: the server
// can validate crafting with it.
#pragma once
#include "core/itemstack.h"
#include <map>

namespace dq {

constexpr double SMELT_TIME = 5;   // seconds per item

// What the crafting grid spells. idx >= 0: reg().recipes[idx]; -1: a special recipe (repair, dye, maps).
struct CraftMatch {
  bool ok = false;
  std::string out; int n = 1;
  int idx = -1;
  int w = 1, h = 1;
  int repair = -1;          // >= 0: the repaired tool's durability used (0 = as new)
  json extra;               // fields for the result stack (col, map, gx, d...)
  const Recipe* recipe = nullptr;
  explicit operator bool() const { return ok; }
  ItemStack result() const; // the stack it makes
};

bool tagMatch(const std::string& want, const std::string& id);          // want = key or #tag
CraftMatch matchGrid(const std::vector<ItemStack>& slots, int W);       // W×W grid (2 or 3)
void recipeDims(const Recipe& r, int& w, int& h);                        // shapeless recipes carry their own w/h
std::vector<std::pair<std::string, int>> recipeIngredients(const Recipe& r);   // [[keyOrTag, count]] in order
std::vector<std::string> recipeLayout(const Recipe& r, int W);          // W×W wanted ingredient per cell ("" = empty)
std::string ingredientLabel(const std::string& key);                    // "Any Log", "Coal or Charcoal", item name
std::string ingredientIcon(const std::string& key);                     // first item of a tag
std::string recipeCategory(const Recipe& r);                            // blocks | redstone | tools | armor | food | misc
bool isRedstoneItem(const ItemDef* d);

// The things a player carries, for "can I make this?" (main inventory + the worn backpack's contents).
struct Bags {
  std::vector<ItemStack>* main = nullptr;
  std::vector<ItemStack>* bp = nullptr;      // may be null
  int count(const std::string& id) const;
  int countTag(const std::string& key) const;
  int remove(const std::string& id, int n);  // returns what couldn't be removed
  int removeTag(const std::string& key, int n);
};
std::map<std::string, int> recipeMissing(const Bags& b, const Recipe& r, const std::vector<ItemStack>* grid);
bool canCraft(const Bags& b, const Recipe& r, const std::vector<ItemStack>* grid = nullptr);

// dyeing (game/dyeing.js, game/blocks2.js recolorKey)
const std::string& dyeHex(const std::string& colour);                   // "red" -> "#a12722"
std::string dyeMix(const std::vector<std::string>& hexes);
std::string recolorKey(const std::string& key, const std::string& colour);   // "" if none

// furnaces: kind = "furnace" | "smoker" | "blast" (BlockDef props "furnace")
int fuelValue(const std::string& key);                                  // items smelted per fuel item
std::string smeltResult(const std::string& kind, const std::string& input);   // "" = nothing
bool tickFurnace(json& e, double dt, const std::string& kind);          // one step; true when something visible changed

// optional hooks for systems that live elsewhere (maps): zooming a map out returns the new map id or ""
struct CraftHooks { std::function<std::string(const std::string& mapId)> mapZoom; };
CraftHooks& craftHooks();

} // namespace dq
