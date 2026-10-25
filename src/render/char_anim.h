// render/char_anim.h — how a player's body moves (port of render/skins.js avatarAnim, the player half of
// game/anim.js and the matrices of avatarDraws / gear3d.js gearDraws): walk, run, sneak, swim (the crawl and
// treading water), crawl, ledge grab, swings, bow draw, eating, sit, sleep, falling and landing, flying, the
// hurt tilt; the body turns into the way you go while the head looks where you aim.
#pragma once
#include "assets/char_model.h"

namespace dq {

// skins.js avatarDraws' pose (+ anim.js's additions)
struct CharPose {
  float yaw = 0, headPitch = 0, walk = 0, walkAmt = 0, swing = 0, time = 0, vy = 0, land = 0, bow = 0, eat = 0;
  float swimPh = 0, swimMv = 0, swimPitch = 0, climb = 0, deathT = 0, lean = 0, castWind = 0;
  bool sneak = false, sit = false, sleep = false, swim = false, hang = false, air = false, sprint = false;
  bool held = false, off = false, carry = false, paddle = false, fp = false, fpLeftArm = false;
  // anim.js: the body turned its own way, leans
  bool hasBodyYaw = false; float bodyYaw = 0, headTurn = 0, legTurn = 0, accLean = 0, turnLean = 0;
  bool fly = false; float flySpeed = 0, flyVy = 0;
  bool turned = false;      // set by charRig when the body yaw is used
  // capes.js
  float capeA = 0.1f, capeSway = 0;
  bool still = false;       // the rest pose (the skin editor's model)
  // game/villagers.js: the villagers' moves on the shared body (waving, talking, arms folded, reading, the head turning)
  float headYaw = 0, wave = 0, hoe = 0; bool talk = false, fold = false, read = false;
};

// part angles (x, y, z; applied as rotY, rotX, rotZ) and the lift of the whole body (blocks)
struct CharAngles { vec3 body{0}, head{0}, armR{0}, foreR{0}, armL{0}, foreL{0}, legR{0}, shinR{0}, legL{0}, shinL{0}; float lift = 0; };
CharAngles charAnimate(const CharPose& pose);

// every part's matrix for one frame: part space (model pixels about the part's pivot) -> render space.
// draw a skin part with part[i] * scale[i] * mesh.local().
struct CharRig {
  mat4 part[CP_COUNT];
  mat4 scale[CP_COUNT];
  mat4 body;                               // the body (scaled), for things hung on it (packs, capes, carried items)
  mat4 bodyRaw;                            // unscaled
  std::map<std::string, mat4> gear;        // gear3d parts (head neck body skirtF skirtB armR ... cape capeLo tail)
  bool fpTorso = false;
};
// base: render space at the entity's feet (translation only; block units). Stature and the body shape apply here.
void charRig(const CharModel& m, CharPose pose, const mat4& base, CharRig& out, bool cloak, bool underPack);

#ifndef DQ_WRAP_ANGLE
#define DQ_WRAP_ANGLE
inline float wrapAngle(float a) { return std::fmod(std::fmod(a + PI * 3, TAU) + TAU, TAU) - PI; }
#endif

} // namespace dq
