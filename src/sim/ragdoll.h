// sim/ragdoll.h — limp bodies. A body that dies goes slack under simple physics instead of a scripted topple:
// every simulated piece (the torso, each limb, the head) is a small rigid frame of four verlet points (its centre and
// one point along each of its axes), joints pin a child's pivot to the spot on its parent where it hung, a cone
// keeps each joint from bending further than it could (turning the limb about its joint), gravity pulls, and points
// all over the surface of every drawn box (a piece's own and the parts riding on it, an eighth of a block apart) are
// pushed out of the blocks' real shapes (the shortest way of 26, across edges and corners too), the piece moved as a
// rigid body, always last in each round so nothing a joint pulls stays inside. Weighted by size; friction; at most 12
// blocks/s a step (nothing tunnels through a slab). It settles, then freezes (no more work), and is drawn from the
// simulated frames: part matrix = the piece's rigid motion since death x the part's matrix at death.
//
// Pure maths on a World (no GL): the client draws ragdolls (render/ragdoll_layer.cpp) and the headless tests run
// them. They are cosmetic only — nothing in the game reads them, so a guest simulating its own copy from the same
// start can never put a game out of step.
#pragma once
#include "world/world.h"

namespace dq {

class Level;
class Creature;

// One drawable part at the moment of death. m: part space -> model space (relative to the ragdoll's origin, block
// units, any scale included); lo/hi: the part's box in part space. Parents come before children. simulate = false:
// the part rides rigidly on its nearest simulated ancestor (eyes, teeth, the lower half of a limb...).
struct RagdollPartIn { int parent = -1; mat4 m{1}; vec3 lo{0}, hi{0}; bool simulate = true; float limit = 1.3f; };

class Ragdoll {
public:
  // vel: the body's velocity at death (blocks/s); kick: the killing blow's push (topples it away from the blow)
  void build(vec3 origin, const std::vector<RagdollPartIn>& parts, vec3 vel, vec3 kick, u32 seed, vec3 side = vec3(0));   // side: tip over along this axis (its flank)
  void step(const World& w, float dt);              // fixed 60 Hz substeps inside; does nothing once frozen
  void settle(const World& w, float maxSecs = 6);   // run until it freezes (a body first seen long after it fell)
  mat4 partMatrix(int i) const;                     // model space (relative to origin) now
  int parts() const { return (int)m0.size(); }
  bool posed(int i) const { return i >= 0 && i < (int)owner.size() && owner[i] >= 0; }
  vec3 centre() const;                              // the middle of the body (relative to origin)
  float lowest() const;                             // the lowest particle's height (relative to origin)
  // how deep the drawn body is in solid blocks right now (blocks): every part's box, sampled through its volume, against the
  // blocks' real collision shapes (the tests hold this under 1/16; `part` gets the worst part's index)
  float penetration(const World& w, int* part = nullptr) const;
  float jointGap(int* part = nullptr) const;                           // the widest a joint has pulled apart now (blocks)
  vec3 origin{0};
  float age = 0;
  bool frozen = false;
  int stillSteps = 0;
  float motion = 0; int motionPart = -1;                                 // the most any point moved in the last step (blocks)
private:
  struct Body { int part = -1, parent = -1; mat3 F0{1}; vec3 c0{0}, L{0.1f}; vec3 qSelf{0}, qParent{0}, rest{0}; bool hasRest = false; float limit = 1.3f;
                std::vector<vec3> pts;            // points over the surface of every box this piece carries (its own, and the parts riding on it)
                float I = 0.01f, R = 0.1f, m = 0.002f; };   // how hard it is to turn (its points' spread), its reach, its weight (volume)
  std::vector<Body> bodies;
  std::vector<vec3> x, px;          // four particles per body: centre, +x, +y, +z
  std::vector<float> rlen;          // 6 rigid lengths per body
  std::vector<int> owner;           // part -> body (-1: not drawn)
  std::vector<mat4> m0;
  std::vector<vec3> lo0, hi0;       // each part's box (part space)
  float acc = 0, wakeT = 0, awakeT = 0;
  std::vector<u8> contact;          // each piece touched the blocks last step
  void sub(const World& w, float h);
  mat3 frame(int b) const;
  vec3 at(int b, vec3 q) const;                                 // a point fixed in body b (local coords from its centre)
  void push(int b, vec3 q, vec3 d, float k = 1);                // move that point by d (PBD over the piece's four points)
  void rigid(int b);
  void snap(int b);                                             // make the piece exactly rigid again (what's drawn = what collided)
  bool collide(const World& w, int b, int rounds);              // push the piece's surface out of the blocks; true = touching
  void solve(const World& w, std::vector<u8>& touched, int rounds);   // one round: rigid pieces, joints and cones, then the blocks
};

// Creatures: which die limp, and their ragdoll from their pose right now.
bool creatureRagdolls(const Creature& m);
bool ragdollFromCreature(Creature& m, Ragdoll& out);

// The client's set of creature ragdolls (cosmetic): a creature that has just died near `focus` gets one, at most `cap`
// at a time (the oldest go first); each lies there `life` seconds and fades over the last `fade`.
struct CreatureRagdoll {
  u32 id = 0; int realm = 0; std::string type, model; Ragdoll rd; float t = 0;
  std::vector<u8> shown;                       // which parts were showing (wool, antlers...)
  std::string drawKey; vec4 tint{0}, wool{0};  // filled in by the drawer (onNew) while the creature is still there
  bool glows = false;
};
class RagdollSet {
public:
  int cap = 8; float life = 14, fade = 2.5f, range = 64;
  std::vector<CreatureRagdoll> list;
  void update(Level& lv, int realm, vec3 focus, float dt);
  const CreatureRagdoll* find(u32 id) const { for (auto& r : list) if (r.id == id) return &r; return nullptr; }
  void clear() { list.clear(); seen.clear(); }
  std::function<void(CreatureRagdoll&, Creature&)> onNew;
private:
  std::unordered_map<u32, float> seen;   // ids already given a body (an entity lingers after death; never twice)
};

} // namespace dq
