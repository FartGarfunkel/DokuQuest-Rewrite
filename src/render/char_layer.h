// render/char_layer.h — players as voxel characters (render/skins.js avatarDraws and everything that wraps it:
// game/anim.js, game/capes.js, game/carried.js, game/charparts.js, render/gear3d.js gearDraws, render/packarmor.js).
//
// Every entity of type "player" is drawn in third person (the local player in the F5 views; other players as
// the network adds them), the local player's own torso and legs in first person when settings.fpBody is on
// (looking down shows your chest, belt and legs). Worn armour, capes, backpacks, a lantern on the belt, held
// items and the tools you carry (a sword across the back, a pickaxe at the hip) all show.
//
// What a player wears beyond Player::armor/offhand comes from Player::data (set by gameplay / networking):
//   data["back"] = backpack item key, data["cape"] = cape item key, data["hook"] = belt item (lantern, torch),
//   data["hideGear"] = [helmet, chest, legs, boots, backpack, cape] 0/1 (charparts.js), data["sleeping"],
//   data["hang"], data["climb"] (0..1) for the ledge grab. Arm timers: Player::swingT, eatT, bowPull.
#pragma once
#include "client/session.h"
#include "render/char_anim.h"
#include "render/char_blend.h"
#include "render/cape_sim.h"
#include <unordered_map>

namespace dq { struct PaperMap; }

namespace dq {

class CharacterLayer : public RenderLayer {
public:
  const char* name() const override { return "characters"; }
  int order() const override { return 200; }
  void update(GameSession& s, double dt) override;
  void drawOpaque(GameSession& s, const FrameCtx& f) override;

  struct Anim {   // per entity: what a pose needs that the entity doesn't keep (LocalPlayer.pose state)
    bool init = false;
    u8 workTapSeen = 0; double workTapAt = -10;   // a thing set down at a station: the reaching hand taps
    float book = 0; double bookT = -1; u8 bookFlip = 0; bool bookSeen = false; double flipAt = -10; int flipDir = 1;   // the journal in their hands at a menu
    vec3 lastPos{0}, vel{0};
    float bob = 0, bobAmt = 0, swimPh = 0, crawlPh = 0, landT = 0, landAmt = 1, fallStart = 0; bool falling = false;
    float bodyYaw = 0, legTurn = 0, stillT = 0, lastFwd = 0, accLean = 0, lastBody = 0, turnLean = 0;
    float capeA = 0.1f, capeSway = 0, deathT = 0, ladderPh = 0, wing = 0;
    float idleT = 0, footL = 0, footR = 0, footT = 0, footTL = 0, footTR = 0;   // Story Mode-style life: standing about, the ground under each foot (eased, sampled)
    float pitch = 0, hurtT = 0, lastHurt = 0; bool airLeft = false, wasAir = false, backward = false;   // the look eased, seconds since a hit, the jump's lead leg
    float block = 0, rollU = 0, rollYaw = 0, lastDodge = 0; bool rollBack = false;
    float guardHit = 0, lastParry = 0, lastGuard = 0;   // a blow taken on the shield (1 -> 0), the shield's raise (eased), a dodge roll's progress (sim/combat_rpg.h)
    float runAmt = 0, sneakAmt = 0;                    // sprinting / sneaking eased in and out (Epic Fight keeps both cycles running while it blends)
    vec2 hurtDir{0};                                   // where the last blow came from: x their right, y their front (-1..1)
    float lastSwing = 0, swingU = 0; double swingAt = -10; u8 swingN = 0;
    // getting into bed (render/char_layer.cpp bedFrame): 0 standing beside it .. 1 lying in it; the bed found under them,
    // where they stood, which side they sat on
    float bedU = 0; bool bedFound = false; vec3 bedHead{0}, bedDir{0}, bedSide{0}, standPos{0}; float standYaw = 0, bedTop = 0;   // weapon combos: the cut in the chain and how far through it (its own clock: render/char_anim.cpp)
    CharBlend blend;   // render/char_blend.h: gliding between states
    CapeSim cape;      // render/cape_sim.h: their cape as cloth
    u64 seen = 0;
  };
private:
  std::unordered_map<u32, Anim> anims;
  struct Sheet { const PaperMap* map; const Player* holder; mat4 M; float S; vec2 light; };   // maps held up (drawn after the bodies)
  std::vector<Sheet> sheets;
  u64 frameNo = 0;
  double time = 0;
  int devFrames = 0;
  std::string devPose, tilesStamp;
  void devSwitches(GameSession& s);
  void drawPlayer(GameSession& s, const FrameCtx& f, Player& p, Anim& st, bool fp);
};

void registerCharacterLayers();               // idempotent; called from GameSession's constructor
const VoxelModel* localPlayerArm();           // the local player's skin arm for the first-person hand (or null)

// What a player wears and holds, as others see it (charparts.js: hidden gear still protects, nobody sees it).
struct CharWear {
  std::string armor[4], dye[4];     // item keys (helmet .. boots), leather dye "rrggbb"
  std::string back, hook, cape;     // backpack item, belt item, cape TILE key ("" = none)
  std::string quiver;               // the worn quiver ("" = none); quiverFill: how many arrows show in its mouth
  int quiverFill = 0;                // 0 empty .. 4 full (sim/quiver.h quiverFill)
  std::string held, off;            // in the hands
  std::string heldModel;            // a mixed tool's own model (core/tool_parts.h partToolModelKey), "" = the item's
  u32 rodOwner = 0;                 // the player whose rod tip is recorded for the fishing line (0 = nobody)
  bool heldGlint = false, offGlint = false;   // enchanted: the shimmer on it
  std::vector<std::string> carried; // carried.js: hotbar things not in the hand (third person)
  bool hatItem = false;             // the helmet slot holds a hat (no 3D-look hiding under it)
};
// p's equipment slots (Player::cape/back/hook or the inventory's Player::data copies, whichever holds the stack)
std::string wornItem(const Player& p, const char* slot);
bool gearHidden(const Player& p, int k);      // charparts.js hideGear[k]: 0 helmet .. 3 boots, 4 backpack, 5 cape, 6 quiver
CharWear charWearOf(const Player& p, const CharacterSpec& spec, bool carried = true);
// Draw a whole voxel character with the voxel renderer already begun (the world, a menu preview, the doll).
// base: render space at the feet (translation only). fp = the first-person body; detail = the look's shapes.
struct CapeSim;
// immersive first person in bed: where your eyes are as you sit on its edge, turn and lie down (render/char_layer.cpp;
// client/play.cpp puts the view there). false: not getting into or out of a bed
bool bedEyeWorld(vec3& eye, float& yaw, float& recline);   // yaw: the body's facing (+ your look about); recline: 0 up .. PI/2 lying
struct CharDrawOpts { float book = -1, bookLeaf = -1; int bookDir = 1; const std::string* bookPic = nullptr; const std::string* bookLay = nullptr; u64 bookKey = 0;   // bookPic: what's on its pages (Player::bookPic)   // the journal: < 0 on the belt, 0..1 held open in both hands (render/book_model.h); a page turning
  VoxelLight light{1, 0}; vec4 tint{0}; bool fp = false, detail = true, headOnly = false, fpNoArms = false; float eye = 1.62f; double time = 0; glm::dvec3 camPos{0}; CharBlend* blend = nullptr; CapeSim* cape = nullptr; };   // cape: the cloth of this one's cape (render/cape_sim.h; nullptr: drawn from the pose alone)   // blend: ease between states (nullptr: snap)   // time / camPos: for the rod tip record
void drawCharacter(const CharacterSpec& spec, const CharPose& ps, const CharWear& w, const mat4& base, const CharDrawOpts& o);
// skins.js skinFace: the lid down over the eyes now and then (always in sleep) and, while ps.talk, the mouth opening and closing.
// head = the head's matrix (part * scale); VoxelRenderer already begun.
void drawCharFace(const CharModel& M, const CharPose& ps, const mat4& head, VoxelLight light, vec4 tint);
// Carry On (render/carry_draw.cpp): what Player::carrying {kind:'block'|'mob'} holds in the arms. at = the chest frame in blocks
// (x right, y up, z back, the thing sits at its origin); fp = the hand view's size. VoxelRenderer already begun.
void drawCarriedThing(const json& carrying, const mat4& at, bool fp, VoxelLight light, vec4 tint);
// the local player's chestplate sleeve over the first-person arm (arm = the arm model's placement, before matrix())
void drawFirstPersonSleeve(const mat4& arm, VoxelLight light);
// first person on guard behind a shield (render/item_layers.cpp): where the third-person guard puts the shield (its model
// placement, before matrix()), its fist and elbow, in view space (x right, y up, -z ahead; blocks) from the eye — so
// first person matches third person
struct FpGuard { mat4 shield{1}, upper{1}, fore{1}; vec3 shieldFist{0}, shieldElbow{0}; int upperPart = 0, forePart = 0; };   // upper / fore: the skin's own limb parts (part * scale)
// draw one of the local player's own skin limb parts (CP_*) at m (part * scale, view space), the voxel renderer begun
void drawFpLimb(const Player& p, int part, const mat4& m, VoxelLight light);
bool fpGuardFrames(const Player& p, bool shieldLeft, const std::string& weapon, FpGuard& out, float k = 1);   // k: how far on guard (0: carried at the side)

} // namespace dq
