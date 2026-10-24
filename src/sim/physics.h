// sim/physics.h — collision of boxes with the block world (game/physics.js).
#pragma once
#include "world/world.h"
#include "sim/entity.h"

namespace dq {

struct AABB { vec3 lo, hi; bool overlaps(const AABB& o) const { return lo.x < o.hi.x && hi.x > o.lo.x && lo.y < o.hi.y && hi.y > o.lo.y && lo.z < o.hi.z && hi.z > o.lo.z; } };

// Collision boxes of the block at (x,y,z) in world space (empty = passable). Shapes (slabs,
// stairs, fences...) give their real boxes.
void blockBoxes(const World& w, int x, int y, int z, std::vector<AABB>& out);

struct MoveResult { bool hitX = false, hitY = false, hitZ = false, onGround = false; };
// Move an entity box (centre-bottom pos, half width hw, height h) by delta with collision, stepping
// up `stepUp` blocks when walking into a low obstacle.
MoveResult moveBox(const World& w, vec3& pos, float hw, float h, vec3 delta, float stepUp = 0.6f);

// Ray cast through blocks (for the crosshair). Returns hit block, face normal and distance.
struct RayHit { bool hit = false; ivec3 block{0}; ivec3 normal{0}; float dist = 0; BlockId id = 0; vec3 point{0}; };
RayHit raycastBlocks(const World& w, glm::dvec3 origin, vec3 dir, float maxDist, bool hitLiquids = false);

} // namespace dq
