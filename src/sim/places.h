// sim/places.h — far places and finding them (game/places.js 0.9.0.28, the finding part of game/structures.js
// structTick), and a house of your own (game/hearth.js §3, the player's side).
// Places come from contentHooks().placesNear (the structures area: Sky Temple, Ember Bastions, dungeons...).
// Walk within 6 blocks of one (and 24 of its height) and you've found it: Player::stats["sseen"][id] = [x, z, name],
// "You have found the Sky Temple!", 25 experience. They go on your maps and in the journal of places
// (ui/content_screens.cpp makePlacesScreen).
#pragma once
#include "sim/content.h"

namespace dq {

std::string foundText(const std::string& name);   // "You have found the Sky Temple!" / "...a Cloud Shrine!"
// a house given to a village's Hero: its bed becomes your spawn (stats["homes"][vid] = {bed, door, name})
void homeSettle(Level& lv, Player& p, const std::string& vid, ivec3 bed, ivec3 door, const std::string& villageName);

// everything you've found, for the journal
struct FoundPlace { std::string id, name, kind; float x = 0, z = 0; int dim = 0; bool cleared = false; };   // kind: place, village, dungeon, waystone, home
std::vector<FoundPlace> foundPlaces(Level& lv, const Player& p);

// contentHooks().placesNear from the world generator's structure plans (gen/structures_api.h), unless another
// area set one already. Villages are left out: they are found by villages.js's own rule (stats.vseen).
void registerPlacesNearHook();

} // namespace dq
