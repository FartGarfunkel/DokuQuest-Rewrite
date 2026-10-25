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
    vec3 lastPos{0}, vel{0};
    float bob = 0, bobAmt = 0, swimPh = 0, landT = 0, fallStart = 0; bool falling = false;
    float bodyYaw = 0, legTurn = 0, stillT = 0, lastFwd = 0, accLean = 0, lastBody = 0, turnLean = 0;
    float capeA = 0.1f, capeSway = 0, deathT = 0, ladderPh = 0, wing = 0;
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
  std::string held, off;            // in the hands
  std::vector<std::string> carried; // carried.js: hotbar things not in the hand (third person)
  bool hatItem = false;             // the helmet slot holds a hat (no 3D-look hiding under it)
};
// p's equipment slots (Player::cape/back/hook or the inventory's Player::data copies, whichever holds the stack)
std::string wornItem(const Player& p, const char* slot);
bool gearHidden(const Player& p, int k);      // charparts.js hideGear[k]: 0 helmet .. 3 boots, 4 backpack, 5 cape
CharWear charWearOf(const Player& p, const CharacterSpec& spec, bool carried = true);
// Draw a whole voxel character with the voxel renderer already begun (the world, a menu preview, the doll).
// base: render space at the feet (translation only). fp = the first-person body; detail = the look's shapes.
struct CharDrawOpts { VoxelLight light{1, 0}; vec4 tint{0}; bool fp = false, detail = true, headOnly = false; };
void drawCharacter(const CharacterSpec& spec, const CharPose& ps, const CharWear& w, const mat4& base, const CharDrawOpts& o);
// skins.js skinFace: the lid down over the eyes now and then (always in sleep) and, while ps.talk, the mouth opening and closing.
// head = the head's matrix (part * scale); VoxelRenderer already begun.
void drawCharFace(const CharModel& M, const CharPose& ps, const mat4& head, VoxelLight light, vec4 tint);
// Carry On (render/carry_draw.cpp): what Player::carrying {kind:'block'|'mob'} holds in the arms. at = the chest frame in blocks
// (x right, y up, z back, the thing sits at its origin); fp = the hand view's size. VoxelRenderer already begun.
void drawCarriedThing(const json& carrying, const mat4& at, bool fp, VoxelLight light, vec4 tint);
// the local player's chestplate sleeve over the first-person arm (arm = the arm model's placement, before matrix())
void drawFirstPersonSleeve(const mat4& arm, VoxelLight light);

} // namespace dq
