// render/char_anim.h — how a player's body moves (port of render/skins.js avatarAnim, the player half of
// game/anim.js and the matrices of avatarDraws / gear3d.js gearDraws): walk, run, sneak, swim (the crawl and
// treading water), crawl, ledge grab, swings, bow draw, eating, sit, sleep, falling and landing, flying, the
// hurt tilt; the body turns into the way you go while the head looks where you aim.
#pragma once
#include "assets/char_model.h"

namespace dq {

// skins.js avatarDraws' pose (+ anim.js's additions)
struct CharPose {
  u8 atkMove = 0; float atkT = 0;   // an attack move in progress (sim/attack_moves.h): drawn from its fitted keys (render/attack_poses.cpp)
  float yaw = 0, headPitch = 0, walk = 0, walkAmt = 0, swing = 0, time = 0, vy = 0, land = 0, landAmt = 1, bow = 0, eat = 0;   // landAmt: how hard (0.3 a hop .. 1.6 a long drop)
  float swimPh = 0, swimMv = 0, swimPitch = 0, climb = 0, deathT = 0, lean = 0, castWind = 0;
  bool crawl = false; float crawlPh = 0, crawlMv = 0;   // on your belly in a low gap: a commando crawl (charRig crawlLimbs), not the swim
  bool sneak = false, sit = false, sleep = false, swim = false, hang = false, air = false, sprint = false;
  bool bowArm = false;   // a bow carried in the right hand: that arm hangs at the side, still (no gestures turn the bow over)
  bool scope = false;    // a spyglass up to the eye: the arm raised, the elbow out, the glass level before the eye
  bool gui = false;      // in a menu (What Are They Up To): both forearms up before them, the right hand tapping at a floating screen
  bool doze = false;     // idle a long while: dozing on their feet, the head dropped, breathing slow
  bool held = false, off = false, carry = false, paddle = false, fp = false, fpLeftArm = false;
  // anim.js: the body turned its own way, leans
  bool hasBodyYaw = false; float bodyYaw = 0, headTurn = 0, legTurn = 0, accLean = 0, turnLean = 0;
  bool fly = false; float flySpeed = 0, flyVy = 0;
  bool turned = false;      // set by charRig when the body yaw is used
  // capes.js
  float capeA = 0.1f, capeSway = 0;
  bool still = false;       // the rest pose (the skin editor's model)
  // movement.js: shimmying along a ledge, hand over hand; on a ladder or vines (climbing up or down)
  float shimPh = 0, shimMv = 0;
  bool ladder = false; float ladderPh = 0, ladderMv = 0;
  // maps.js: a paper map held up in front of the chest (both hands, or one)
  bool mapMain = false, mapOff = false;
  // game/villagers.js: the villagers' moves on the shared body (waving, talking, arms folded, reading, the head turning)
  float headYaw = 0, wave = 0, hoe = 0; bool talk = false, fold = false, read = false;
  // a villager at their trade's work (render/villager_layer.cpp from the sim's "wk"): which (CharWork) and how long since
  // the last stroke landed (0..1 over its follow-through; 1 between strokes, the hammer held up ready)
  u8 work = 0; float workU = 1;
  // working a station in first person (render/cinematic_hands.cpp): the body's own arms reach for these (render space,
  // the same space as charRig's base), bent at the elbows, instead of the hand view's arm
  bool reach = false; vec3 reachR{0}, reachL{0};
  // game/elytra.js: gliding on elytra (arms swept back, legs together; the wings open by wingOpen 0..1)
  bool glide = false; float wingOpen = 0;
  // Story Mode-style life (render/char_anim.cpp): how long it has stood idle (varied idles: looking round, a stretch, a
  // scratch of the head, a look at its hands, a tap of the foot), who it is (picks its idles), and the ground under each
  // foot relative to where it stands (blocks, + higher): the feet plant on steps and slabs, the hips drop, a knee bends
  float idleT = 0; u32 seed = 0; float footL = 0, footR = 0;
  // render/char_poses.cpp (after AnimatedPlayer, Not Enough Animations and Hold My Items): drinking rather than eating (a
  // bottle tipped up, the head back), seconds since a hit (the flinch; 0: none), the jump's lead leg, walking backwards
  bool drink = false, airLeft = false, backward = false; float hurt = 0;
  // poses ready for the shield, crossbow, trident and spear (0..1 how far in): a shield raised across the body in the
  // off hand, a crossbow being wound / levelled to shoot, a trident drawn back over the shoulder, a spear couched for a charge
  float block = 0, xbowCharge = 0, throwCharge = 0, spearCharge = 0; bool xbowAim = false;
  bool blockMain = false;   // the shield in the main hand: the right arm raises it (block above is how far up)
  i8 shieldArm = 0;         // a shield strapped on a forearm (1: the left, -1: the right): carried with the forearm level
  // a dodge roll (sim/combat_rpg.h): 0..1 through it (0: none), tucked and over once about the middle; backwards when it
  // goes away from where they face (a step back), else forwards with the body turned the way it goes
  float roll = 0; bool rollBack = false;
  bool armed = false;
  float runAmt = -1, sneakAmt = -1;   // eased amounts (-1: from sprint / sneak as they are)
  vec2 hurtDir{0, 1};             // the blow came from: x their right, y their front
  u8 swingKind = 0;               // 0 the Minecraft swing; 1-3 a weapon combo's cuts (render/char_anim.cpp weaponSwing)       // a weapon or a shield in hand: standing, they keep a fighter's stance (render/char_anim.cpp)
  float guardHit = 0;       // a blow taken on the shield: 1 when it lands, down to 0 (render/char_poses.cpp knocks the guard back)
  bool sitGround = false;
  float bed = -1, bedRecline = 0;   // getting into bed (render/char_layer.cpp bedFrame: 0 beside it .. 1 lying in it; -1 no), how far laid back   // sat on the ground (by a campfire: ui/cinematic_work.cpp), knees up, the seat down on it
};

// part angles (x, y, z; applied as rotY, rotX, rotZ) and the lift of the whole body (blocks)
enum CharWork : u8 { CW_NONE = 0, CW_HAMMER, CW_DIP, CW_BREW, CW_GRIND, CW_CHOP, CW_KNEAD, CW_CUT, CW_WEAVE, CW_TEND, CW_FIT, CW_SORT, CW_CRAFT, CW_HOE };
CharWork charWorkOf(const std::string& doing);
struct CharAngles { vec3 body{0}, head{0}, armR{0}, foreR{0}, armL{0}, foreL{0}, legR{0}, shinR{0}, legL{0}, shinL{0}; float lift = 0;
                    vec2 toolR{0}, toolL{0}; };   // toolR/L: the grip's turn at the fist (x, z), the held thing turned in the hand
// an attack move's pose over the stance (render/attack_poses.cpp): the move's keys at its time, blended in from the stance
// and back out to it
void attackPose(CharAngles& A, const CharPose& pose);
// the held thing's frame from the forearm's, turned by the grip (the fist at 0,-6,-1 of the forearm)
mat4 gripTurn(const mat4& fore, vec2 turn);
CharAngles charAnimate(const CharPose& pose);
// the newer poses on top of charAnimate's (render/char_poses.cpp; charAnimate calls it last)
void charMorePoses(CharAngles& A, const CharPose& pose);
// a bow in the hand (render/char_layer.cpp, render/villager_layer.cpp): its size (item units to blocks) and where the
// fist's middle is down the forearm (model px from the elbow)
constexpr float BOW_SCALE = 0.85f, BOW_FIST = 6.3f;

// every part's matrix for one frame: part space (model pixels about the part's pivot) -> render space.
// draw a skin part with part[i] * scale[i] * mesh.local().
struct CharRig {
  mat4 part[CP_COUNT];
  mat4 scale[CP_COUNT];
  mat4 body;                               // the body (scaled), for things hung on it (packs, capes, carried items)
  mat4 bodyRaw;                            // unscaled
  std::map<std::string, mat4> gear;        // gear3d parts (head neck body skirtF skirtB armR ... cape capeLo tail)
  bool fpTorso = false;
  vec2 toolR{0}, toolL{0};                 // the grips' turns (CharAngles), for heldInHand
};
// base: render space at the entity's feet (translation only; block units). Stature and the body shape apply here.
void charRig(const CharModel& m, CharPose pose, const mat4& base, CharRig& out, bool cloak, bool underPack);

#ifndef DQ_WRAP_ANGLE
#define DQ_WRAP_ANGLE
inline float wrapAngle(float a) { return std::fmod(std::fmod(a + PI * 3, TAU) + TAU, TAU) - PI; }
#endif

} // namespace dq
