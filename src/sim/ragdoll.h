// sim/ragdoll.h — limp bodies. A body that dies goes slack under simple physics instead of a scripted topple:
// every simulated piece (the torso, each limb, the head) is a small rigid frame of four verlet points (its centre and
// one point along each of its axes), joints pin a child's pivot to the spot on its parent where it hung, a cone
// keeps each joint from bending further than it could, gravity pulls, and the corners of every piece's box are
// pushed out of the blocks they sink into (with friction). It settles, then freezes (no more work), and is drawn
// from the simulated frames: part matrix = the piece's rigid motion since death x the part's matrix at death.
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
  vec3 origin{0};
  float age = 0;
  bool frozen = false;
  int stillSteps = 0;
private:
  struct Body { int part = -1, parent = -1; mat3 F0{1}; vec3 c0{0}, L{0.1f}; vec3 qSelf{0}, qParent{0}, rest{0}; bool hasRest = false; float limit = 1.3f; };
  std::vector<Body> bodies;
  std::vector<vec3> x, px;          // four particles per body: centre, +x, +y, +z
  std::vector<float> rlen;          // 6 rigid lengths per body
  std::vector<int> owner;           // part -> body (-1: not drawn)
  std::vector<mat4> m0;
  float acc = 0;
  void sub(const World& w, float h);
  mat3 frame(int b) const;
  vec3 at(int b, vec3 q) const;                                 // a point fixed in body b (local coords from its centre)
  void push(int b, vec3 q, vec3 d, float k = 1);                // move that point by d (PBD, unit masses)
  void rigid(int b);
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
  int cap = 12; float life = 14, fade = 2.5f, range = 64;
  std::vector<CreatureRagdoll> list;
  void update(Level& lv, int realm, vec3 focus, float dt);
  const CreatureRagdoll* find(u32 id) const { for (auto& r : list) if (r.id == id) return &r; return nullptr; }
  void clear() { list.clear(); seen.clear(); }
  std::function<void(CreatureRagdoll&, Creature&)> onNew;
private:
  std::unordered_map<u32, float> seen;   // ids already given a body (an entity lingers after death; never twice)
};

} // namespace dq
