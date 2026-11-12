// sim/felled.h — spawning a felled tree (the "falling_tree" entity lives in sim/creature_entities.cpp).
#pragma once
#include "sim/entity.h"
#include "core/itemstack.h"

namespace dq {
class Level;
// a felled tree tips over around its stump, then breaks into drops where the trunk fell
struct FelledBlock { ivec3 p; BlockId id; };
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<FelledBlock>& blocks, ivec3 stump, float dirYaw, const std::vector<ItemStack>& drops);
} // namespace dq
