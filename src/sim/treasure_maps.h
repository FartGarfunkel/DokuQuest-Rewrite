// sim/treasure_maps.h — a treasure map (found in loot) shows an X some way off on dry land the first time it's used, says
// which way and how far, and marks the X on your maps. When the land at the X is first loaded a chest of treasure is buried
// a few blocks under it; dig it up. Each map has its own X (kept on the map and in the world: lv.info.data["treasures"]).
// Using one opens it as a map (contentScreen "treasure_map" {slot, x, z}: ui/treasure_map_screen.cpp): the land round the
// X drawn like a filled map, a red X, the way from where you read it and a compass rose. It is an ordinary item otherwise:
// use it on a chest and it goes in, crouch + use sets it down, it drops and stacks keep their X in the tag.
#pragma once
#include "sim/level.h"
#include "sim/maps.h"

namespace dq {

void registerTreasureMaps();

constexpr int TREASURE_MAP_SCALE = 2;   // the picture: 128 pixels of 4 blocks, 512 blocks across, the X in the middle
inline bool treasureTarget(const ItemStack& s, int& tx, int& tz) {
  if (s.empty() || s.id != "treasure_map" || !s.tag.is_object() || !s.tag.contains("tx") || !s.tag.contains("tz")) return false;
  if (!s.tag["tx"].is_number() || !s.tag["tz"].is_number()) return false;
  tx = s.tag["tx"].get<int>(); tz = s.tag["tz"].get<int>(); return true;
}
inline bool isTreasureMapStack(const ItemStack& s) { int x, z; return treasureTarget(s, x, z); }
// a map that's read in the hands: a filled map, or a treasure map with its X
inline bool isReadableMapStack(const ItemStack& s) { return isMapStack(s) || isTreasureMapStack(s); }

// the picture of the land round an X (made once per X and kept while the world is open, never saved; its id starts
// "treasure~"): the land that's loaded as it is, the rest as the generator makes it. Its markers carry the X. The
// generator's part is drawn on a worker (about a second) and laid in on a later call; call it each frame it's shown.
PaperMap* treasureMapPicture(Level& lv, int tx, int tz);
// the picture a readable map in the hands shows: a treasure map's picture, else the filled map's own (MapStore::forStack)
PaperMap* readableMapFor(Level& lv, Player& p, ItemStack& s);
// the eight ways (north is -z): "north", "north-east", ...
std::string treasureDirection(float dx, float dz);
// the written hint for the map read at (px, pz): "Buried north-east of here, about 300 blocks away — past the river, in the woods"
std::string treasureHint(Level& lv, int tx, int tz, float px, float pz);

} // namespace dq
