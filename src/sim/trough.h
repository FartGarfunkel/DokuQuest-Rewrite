// sim/trough.h — the feeding trough (Animal Feeding Trough's, tools/port/polish_data.py): a wooden trough holding one
// stack of food (block entity {type:"trough", items:[stack]}). Use it with something in hand to tip the stack in (the
// same food tops it up), with an empty hand to take it back; break it and the food spills. A grown farm animal that
// eats that food, ready to breed (not in love, not just bred), comes to a trough within 8 blocks, eats one and is in the
// mood (an immersive world's husbandry still holds: once a day, and only the familiar breed; sim/creature_beasts.cpp
// farmAI). The food shows in it as it fills (render/trough_layer.cpp).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"
#include <optional>

namespace dq {
class Player;
class Creature;

bool troughUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);   // true if it used the click
ItemStack troughStack(World& w, int x, int y, int z);                                   // what's in it (empty if none)
int troughLevel(int count);                                                            // 0 empty, 1..4 by the 16s (the mod's LEVEL)
// the nearest trough within r of c holding food c eats (troughs are looked for near the players every couple of seconds)
std::optional<ivec3> troughFor(Level& lv, const Creature& c, float r = 8);
bool troughBite(Level& lv, World& w, ivec3 at, const Creature& c);   // takes one of c's food from it; false if there's none

}
