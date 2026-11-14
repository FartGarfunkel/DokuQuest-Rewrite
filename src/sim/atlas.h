// sim/atlas.h — the book's map page as an atlas (after Xaero's World Map and Minimap in Soulrend): a map put in the book's
// map slot makes the book map the world as you go, one 256-block tile at a time (paper maps of scale 1, "atlas_<dim>_<x>_<z>",
// shared by everyone in the world, saved with its maps); the map page shows them all laid together. A compass in the
// compass slot lets the minimap show in the top-right corner (the player's choice, on the map page). Kept in the player's
// stats "atlas": {map: stack, compass: stack, mini: bool}.
#pragma once
#include "common/common.h"
#include <string>

namespace dq {
struct ItemStack; class Level; struct Player; struct PaperMap;

constexpr int ATLAS_SCALE = 1;
bool atlasHasMap(const Player& p);
bool atlasHasCompass(const Player& p);
bool atlasMinimap(const Player& p);                       // on, and a compass in the book
void setAtlasMinimap(Player& p, bool on);
ItemStack atlasSlot(const Player& p, const char* which);   // "map" / "compass"
// put a stack in a slot (one item of it); "" = done, else why not. The old one comes back in `back`.
std::string atlasPut(Player& p, const char* which, ItemStack& from, ItemStack& back);
ItemStack atlasTake(Player& p, const char* which);
std::string atlasTileId(int jsDim, int x, int z);
PaperMap* atlasTileAt(Level& lv, int jsDim, int x, int z, bool create);
void atlasExplore(Level& lv, Player& p);                  // the tile under you and those the mapping reaches into
} // namespace dq
