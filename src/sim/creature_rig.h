// sim/creature_rig.h — creature rigs (render/rigs.js, rigdefs*.js data in data/models.json) and their
// animation: the per-kind walks (rigs.js RIG_ANIM, rigdefs2.js, aquatic.js), the extra poses (RIG_POST:
// sitting, ramming, rearing, slithering, the fuseling's jaw...), the life layer (rigs2.js: tail springs,
// ear twitches, turn lean, lying down, paddling, shaking dry, flinching, the death crumple) and anim.js's
// creature additions. Pure math on the creature's state — the server uses it for dismemberment
// (which part a blow hit), the client to draw the voxel parts.
#pragma once
#include "sim/creature.h"

namespace dq {

struct RigPart {
  std::string name, role, limb, only, notFlag;
  int parent = -1, side = 0, idx = 0;
  bool front = false, antler = false, wool = false, glow = false, unlit = false, noCull = false, eyes = false, hasHand = false;
  float alpha = 1;
  vec3 pivot{0}, min{0}, max{0}, rest{0}, hand{0};
  json tex;                       // tile name, per-face object/array (models.js tilesOf)
  // old (pre-rig) models: animation flags
  float swing = 0, look = 0, hop = 0, flap = 0, reach = 0, slam = 0;
};
struct RigModel {
  std::string key, rig, post;
  float scale = 1, hipY = 0, rollY = 0, bodyDepth = 4, height = 16, bodyY = 0, bodyLen = 12, legH = 6, pitch0 = 0, seatY = 0;
  bool hasRollY = false, gallop = false, armsReach = false, heavy = false, flukes = false, hasNeck = false;
  std::vector<RigPart> parts;     // parents before children
  std::unordered_map<std::string, int> index;
  int find(const std::string& n) const { auto it = index.find(n); return it == index.end() ? -1 : it->second; }
};
const RigModel* rigModel(const std::string& key);   // data/models.json (MODEL_DEFS); nullptr if unknown

// The creature's base transform (entities.js draws): baby/slime/fuse scaling, yaw, the death topple and
// the surface tilt of a climbing spider. `full` = with the drawing-only parts (squash, topple).
mat4 creatureBase(const Creature& m, bool full);
// Every part's matrix (model space, relative to the creature's feet) into m.mats; a lost part (and what
// hangs from it) gets a zero matrix (mats[i][3][3] == 0). Advances the life layer from m.age.
void rigPose(Creature& m, const RigModel& M, const mat4& base);
inline bool rigPartPosed(const Creature& m, int i) { return i >= 0 && i < (int)m.mats.size() && m.mats[i][3][3] != 0; }
// Is this part drawn right now? (antlers on stags, wool until shorn, `only`/`not` state flags)
bool rigPartShown(const Creature& m, const RigPart& p);
// Ray (world space) against the posed parts: index of the first part hit, -1 for none.
int rigHitPart(const Creature& m, const RigModel& M, vec3 o, vec3 d);

} // namespace dq
