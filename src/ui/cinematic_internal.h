// ui/cinematic_internal.h — what the station scenes share (ui/cinematic_table.cpp holds the scene and the camera;
// ui/cinematic_work.cpp the grindstone, smithing table, loom, fletching and cartography tables and the stonecutter)
#pragma once
#include "ui/cinematic.h"
#include "core/itemstack.h"
#include <string>

namespace dq {
class App; class Input; class Player;
namespace cine {

constexpr double INTRO = 0.6, OUTRO = 0.45;

struct Scene {
  int phase = 0;   // 0 off, 1 the approach, 2 at work, 3 leaving
  double t = 0, clock = 0;
  ivec3 at{0};
  glm::dvec3 viewPos{0}; float viewYaw = 0, viewPitch = 0;   // the lean over the table
  glm::dvec3 lastPos{0}; float lastYaw = 0, lastPitch = 0;    // the pose shown last frame (the leave sets off from it)
  int kind = 0;    // 0 the crafting table, 1 an anvil, 2 the enchanting table, 3 a brewing stand, 4 a furnace
  float fov = 66, roll = 0;
  vec3 rest[2]{};  // where the hands lie at rest (right, left)
  vec3 front{0};   // a station with a front of its own (a furnace): lean in there, wherever you stand
  vec3 viewOff{0};     // the eye's offset from bending (cinematicViewOffset)
  float bend = 0;      // seamless first person: how far bent toward the work (0..1, eased)
  bool lean = false;   // the camera leans over the station (Options "Lean the camera over stations"); off: seamless first person
};
Scene& scene();
double inOutCubic(double u);
bool sceneWalkOff(const Input& in);   // the walk keys, a jump (not a pad's A), or the left stick pushed
float wrapPi(float a);
void leanPose(Scene& S, vec3 eye);    // the lean over the station (S.kind) from the eye, the hands' rest

// the bag on a pad (a console's way): LB / RB go between the station and the bag, the d-pad walks the bag, A takes a
// stack into your hand, B (or LB / RB) back to the station
struct BagPad { bool on = false; int cur = 27; };
bool bagPadUpdate(Input& in, Player& p, BagPad& b);
void drawBag(App& a, Player& p, int padCur);   // your bag beside the station (click a stack to work with it)
std::string nameOf(const ItemStack& s);

}  // namespace cine
}  // namespace dq
