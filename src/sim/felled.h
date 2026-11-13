// sim/felled.h — spawning a felled tree (the "falling_tree" entity lives in sim/creature_entities.cpp).
#pragma once
#include "sim/entity.h"
#include "core/itemstack.h"
#include <array>
#include <vector>

namespace dq {
class Level;
// a felled tree tips over around its stump, then breaks up; its drops come down at the stump (where it was chopped)
struct FelledBlock { ivec3 p; BlockId id; };
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<FelledBlock>& blocks, ivec3 stump, float dirYaw, const std::vector<ItemStack>& drops);
// the same from x,y,z,id records (sim/gameplay.h)
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<std::array<int, 4>>& blocks, ivec3 pivot, float dirYaw, const std::vector<ItemStack>& drops);
class World;
// the whole tree a log belongs to: its logs, then the leaves round them (x,y,z,id); false = not one that falls
// (sim/controller.cpp: the player's felling and the village lumberjack's)
bool scanTreeAt(const World& w, int realm, int x, int y, int z, std::vector<std::array<int, 4>>& logs, std::vector<std::array<int, 4>>& leaves);
} // namespace dq
