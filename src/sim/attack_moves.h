// sim/attack_moves.h — melee attacks as moves (studied and measured, our own numbers: see
// docs/ANIMATION_STUDY.md): a press starts a move; the blade is live only while it is really passing through the air
// in front of you (hitFrom..hitTo), and it lands on whatever it passes through there; the next press is taken from
// `recovery`; chains of cuts, a lunging thrust when sprinting, a chop from the air.
// Owner: claude for the timings and the blade's path (both come from the drawn poses, render/char_anim.cpp
// attackPose, sampled into sim/attack_blades_data.inc; tools/test/anim_test.cpp checks the two agree). The rules that use
// them (starting moves, hits, damage, lunges, stuns; sim/combat*.cpp, sim/controller.cpp): grok.
#pragma once
#include "common/common.h"
#include <string>

namespace dq {

// what a held thing fights with
enum class Moveset : u8 { Fist, Sword, Dagger, Axe, Tool, Greatsword, Spear, Mace };
struct ItemDef;
// toolType: the item's "tool" type (items.json; "" for an empty hand); key: the item's key
Moveset movesetOf(const std::string& toolType, const std::string& itemKey);
// an item's optional "moveset" field (greatsword, spear, mace). Missing or unknown keeps the old rule.
Moveset movesetOf(const ItemDef* item);

enum AttackMoveId : u8 {
  MV_NONE = 0,
  MV_SWORD1, MV_SWORD2, MV_SWORD3, MV_SWORD_DASH, MV_SWORD_AIR,   // down from over the right shoulder, flat backhand, rising finisher; thrust; overhead chop
  MV_DAGGER1, MV_DAGGER2, MV_DAGGER3,                             // stab from the hip, backhand slash, lunging stab (also its dash)
  MV_AXE1, MV_AXE2,                                               // chop down right to left, backhand chop
  MV_FIST1, MV_FIST2, MV_FIST3,                                   // left jab, right cross, left hook
  MV_TOOL1, MV_TOOL2,                                             // a tool's two blows (pickaxe, shovel, hoe, staff...)
  MV_GS1, MV_GS2,                                                 // greatsword: the sword's two cuts, longer reach, until its own drawing
  MV_SPEAR1, MV_SPEAR2,                                           // spear: a thrust, then the return
  MV_MACE1, MV_MACE2,                                             // mace: the axe's two chops, shorter and heavier
  MV_COUNT
};

struct AttackMove {
  const char* name;
  float len;        // the whole move, s (after it, back to walking/standing)
  float turnLock;   // from here to `recovery` the blow keeps the facing it had (aim during the wind-up only)
  float hitFrom, hitTo;   // the blade is live (it lands on what it passes through) — the clip's preDelay..contact
  float recovery;   // the next press is taken from here (a press earlier is kept for then); walking speed back
  float lunge;      // blocks travelled forward over 0..hitTo (lungeAt); stop short at a target in reach
  float dmg;        // x the weapon's damage
  float impact;     // x the knockback / stagger
  u8 strikes;       // most targets one move lands on
  bool leftHand;    // the blow is the left fist's (jab, hook)
  float reach = 1;  // x the drawn blade (a spear past 1, a mace under it)
  float pierce = 0; // share of armour this cut ignores, on top of the attacker's own
};
const AttackMove& attackMove(u8 id);   // MV_NONE / out of range: a zeroed move

// the chain: the n-th press (n counts from 0; it wraps), or the dash (sprinting at the press) or the air move (off the
// ground at the press). A chain restarts when 0.8 s pass after a move ends with no press.
int comboLength(Moveset m);
u8 attackMoveFor(Moveset m, int n, bool dash, bool air);
constexpr float COMBO_WINDOW = 0.8f;

// ---- where the blade is (the drawn poses: render/char_anim.cpp) ----
// The attacker's frame: right, up, ahead (blocks) from the feet, facing yaw (the player's yaw: ahead is
// (-sin yaw, 0, -cos yaw)), looking level. hilt: the fist; tip: the blade's end (for a fist, a hand's length out).
void bladeAt(u8 id, float t, vec3& hilt, vec3& tip);
// a frame point to the world: the blow follows the look up and down (turned about the shoulder by the pitch, + up)
vec3 attackFrameToWorld(const vec3& local, const vec3& feet, float yaw, float pitch);
// does the blade, swept over t0..t1 (its live part only, clipped to hitFrom..hitTo), pass through the box (lo..hi,
// world)? reach scales the blade's length (1 = the drawn sword; a longer weapon > 1). when: the move time it met it.
bool bladeSweepHits(u8 id, float t0, float t1, const vec3& feet, float yaw, float pitch, const vec3& lo, const vec3& hi,
                    float reach = 1, float* when = nullptr, bool leftHand = false);
class Creature; struct RigModel; struct ShieldBox;
// the blade against a posed creature's parts. -1 none, -2 no posed rig (caller may use the AABB).
// hitAt: world point on the blade. part: the mesh part.
int bladeSweepHitsCreature(u8 id, float t0, float t1, const vec3& feet, float yaw, float pitch, Creature& c,
                           const RigModel& M, float reach = 1, vec3* hitAt = nullptr, int* part = nullptr,
                           bool leftHand = false, vec3* hitDir = nullptr);
class Entity;
int bladeSweepHitsEntity(u8 id, float t0, float t1, const vec3& feet, float yaw, float pitch, Entity& e,
                         float reach = 1, vec3* hitAt = nullptr, bool leftHand = false, vec3* hitDir = nullptr);
bool bladeSweepHitsShield(u8 id, float t0, float t1, const vec3& feet, float yaw, float pitch, const ShieldBox& box,
                          float reach = 1, bool leftHand = false);
// forward travel so far (0 .. lunge), easing in with the strike: move the body by lungeAt(t1) - lungeAt(t0) a tick
float lungeAt(u8 id, float t);

}  // namespace dq
