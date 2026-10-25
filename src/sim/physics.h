// sim/physics.h — AABB-vs-voxel collision and ray casting (port of game/physics.js).
// Entities: feet-centre position, half width w, height h. Shaped blocks (slabs, stairs, fences,
// doors, models...) collide with their real boxes from world/shapes. Internally everything is
// computed in double precision; positions are written back to the float Entity::pos rounded
// toward the free side of any contact, so a body never ends up inside a wall at large coordinates.
#pragma once
#include "world/world.h"
#include "sim/entity.h"
#include <cmath>

namespace dq {

struct AABB { vec3 lo, hi; bool overlaps(const AABB& o) const { return lo.x < o.hi.x && hi.x > o.lo.x && lo.y < o.hi.y && hi.y > o.lo.y && lo.z < o.hi.z && hi.z > o.lo.z; } };
struct DBox { double x0, y0, z0, x1, y1, z1; };

// Collision boxes (world units) of every solid block overlapping the region. Scaffolding is a
// one-way platform: with `feet` set (not NaN) it only holds up a body above it that isn't sneaking.
void gatherBoxes(const World& w, double minX, double minY, double minZ, double maxX, double maxY, double maxZ,
                 std::vector<DBox>& out, double feet = NAN, bool sneak = false);
bool collides(const World& w, double minX, double minY, double minZ, double maxX, double maxY, double maxZ);
// Collision boxes of one block (compat helper).
void blockBoxes(const World& w, int x, int y, int z, std::vector<AABB>& out);

// Move an entity by its velocity for dt, sliding along walls and stepping up onto slabs and
// stairs (stepHeight; airStep = the little lip a body just off the ground may still step over).
struct MoveParams { float stepHeight = 0.6f, airStep = 0; bool sneaking = false; };
struct MoveOut { bool hitWall = false, hitX = false, hitZ = false, hitCeiling = false, landed = false; };
MoveOut moveEntity(const World& w, Entity& e, float dt, const MoveParams& mp = {});

// compat: move a box (centre-bottom pos) by delta
struct MoveResult { bool hitX = false, hitY = false, hitZ = false, onGround = false; };
MoveResult moveBox(const World& w, vec3& pos, float hw, float h, vec3 delta, float stepUp = 0.6f);

// Which liquids/portals/cactus/ladders touch the entity's box (eye = eye height, < 0 = none).
struct Contacts {
  bool water = false, lava = false, eyeWater = false, eyeLava = false, cactus = false, ladder = false, scaffold = false,
       web = false, fire = false, hot = false;
  int portal = 0;
};
Contacts sampleContacts(const World& w, const Entity& e, float eye = -1);

// Voxel DDA ray cast against the blocks' real shapes. pickLiquid: still liquid sources count.
struct RayHit { bool hit = false; ivec3 block{0}; ivec3 normal{0}; float dist = 0; BlockId id = 0; vec3 point{0}; };
RayHit raycastBlocks(const World& w, glm::dvec3 origin, vec3 dir, float maxDist, bool pickLiquid = false);
// Ray vs AABB (slab method): distance or -1.
double rayAABB(glm::dvec3 o, glm::dvec3 d, glm::dvec3 lo, glm::dvec3 hi);
// The original's look vector: yaw 0 looks toward -z.
inline vec3 lookVector(float yaw, float pitch) { float cp = std::cos(pitch); return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp}; }

} // namespace dq
