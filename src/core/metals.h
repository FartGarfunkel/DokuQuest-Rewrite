// core/metals.h — metals, alloys and ore (data/metals.json; after TerraFirmaCraft): every metal thing carries its makeup
// (a Comp: metal -> fraction). A makeup inside a named alloy's ranges is that alloy (bronze, brass, bismuth bronze, black
// bronze, rose gold, sterling silver) with its own stats; any other mix works too, its stats, melting point and colour
// blended by proportion (it works only as well as its weakest metal; a little tougher for each metal in it, a little worse
// when no metal makes up most of it). core/tool_parts makes tools of any makeup (material key "@<comp>").
#pragma once
#include "core/itemstack.h"
#include <map>

namespace dq {

struct Metal {
  std::string key, name, ingot;
  float melt = 1000;                 // °C
  vec3 rgb{0.7f};                    // 0..1
  int tier = 1, anvil = 0;           // a pickaxe's tier; the anvil tier needed to work it
  float speed = 4, damage = 3, durability = 100;
  bool cast = false;                 // can be poured into a tool mold
  std::map<std::string, std::pair<float, float>> ranges;   // a named alloy's makeup
};
using Comp = std::map<std::string, float>;

const Metal* metalDef(const std::string& key);                // a metal or a named alloy
const std::vector<const Metal*>& baseMetals();
const std::vector<const Metal*>& namedAlloys();
Comp normComp(Comp c);                                         // fractions summing to 1, tiny shares (< 0.5%) dropped
std::string compKey(const Comp& c);                            // "@copper=0.90,tin=0.10"
Comp parseCompKey(const std::string& key);
std::string namedAlloy(const Comp& c);                         // the alloy (or the one metal, at 98%+) it is; "" for a free mix
Metal blendMetal(const Comp& c);                               // what it works like (named: its own; a mix: blended)
std::string compName(const Comp& c);                           // "Bronze", "Copper", "Copper-Zinc-Tin Alloy"
Comp compOfStack(const ItemStack& s);                          // tag "comp", else the item's own metal ({} if none)
std::string metalOfItem(const std::string& itemId);            // an ingot's, an ore's (sim units), "" if none
std::string metalOfOreBlock(const std::string& blockKey);     // copper_ore -> copper ("" if not a metal ore)
int unitsOf(const std::string& itemId);                        // metal units in it: ore piece 25, sample 10, ingot 100...
std::string ingotFor(const Comp& c);                           // the ingot item that holds this makeup (its own, or "alloy_ingot")

} // namespace dq
