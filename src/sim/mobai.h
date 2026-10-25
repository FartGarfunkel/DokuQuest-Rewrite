// sim/mobai.h — A* path finding on foot for creatures (game/mobai.js). Creature::navDir uses it.
#pragma once
#include "world/world.h"

namespace dq {

struct PathOpts { int h = 2, maxDrop = 3, maxNodes = 700; bool swim = true; float w = 1; };
// Path from s to t in block cells (feet). Empty if nothing better than standing still was found.
// When t can't be reached the path leads to the nearest reachable spot.
std::vector<ivec3> findPath(const World& w, ivec3 s, ivec3 t, const PathOpts& o = {});

} // namespace dq
