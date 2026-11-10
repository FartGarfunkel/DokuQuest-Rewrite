// ui/creative_catalogue.h — the creative catalogue's tabs and what goes in each (ui/inventory.cpp shows them: as icon
// tabs over the classic panel's catalogue, or as the book inventory's bookmarks). Every item the game has, sorted into
// fifteen tabs like Minecraft's creative tabs plus one for each world (Nether, Skylands, the End), and within a tab
// kept in families: a wood's planks, stairs, slabs, fences and doors together; a colour's wool, carpet, glass...; a
// metal's tools in order (sword, pickaxe, axe, shovel, hoe), armour head to foot.
#pragma once
#include <string>
#include <vector>

namespace dq {

struct CreativeTab { const char* key; const char* name; const char* icon; };
const std::vector<CreativeTab>& creativeTabs();                       // [0] is "all"
const std::vector<std::string>& creativeItems(const std::string& tab);   // item keys, in order (built once)
std::string creativeTabOf(const std::string& itemKey);

} // namespace dq
