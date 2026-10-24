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
    float capeA = 0.1f, capeSway = 0, deathT = 0;
    u64 seen = 0;
  };
private:
  std::unordered_map<u32, Anim> anims;
  u64 frameNo = 0;
  double time = 0;
  int devFrames = 0;
  std::string devPose, tilesStamp;
  void devSwitches(GameSession& s);
  void drawPlayer(GameSession& s, const FrameCtx& f, Player& p, Anim& st, bool fp);
};

void registerCharacterLayers();               // idempotent; called from GameSession's constructor
const VoxelModel* localPlayerArm();           // the local player's skin arm for the first-person hand (or null)

} // namespace dq
