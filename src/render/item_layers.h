// render/item_layers.h — items in the world as voxel models:
//  * HeldItemLayer: the first-person arm and what it holds (game/local.js hand(): Minecraft's swing turned
//    about the fist, the step bob, the lag when you turn (settings.heldSway), the equip dip, eating, the
//    off hand; the 3D torch held upright, blocks held as blocks, tools by the handle);
//  * DroppedItemLayer: entities of type "item" (data["stack"] = ItemStack json) hover, bob and spin
//    (game/entities.js ItemEntity.draws), bigger stacks showing more copies.
// Both register through registerItemLayers() (called once from GameSession's constructor).
#pragma once
#include "client/session.h"
#include "assets/item_models.h"

namespace dq {

class HeldItemLayer : public RenderLayer {
public:
  const char* name() const override { return "held-item"; }
  int order() const override { return 900; }
  void update(GameSession& s, double dt) override;
  void drawOverlay(GameSession& s, const FrameCtx& f) override;
  // gameplay hooks (local.js): swing the arm (attack, place, use), start the eating/drinking motion
  void swingArm() { if (swing == 0 || swing > 0.6f) swing = 0.0001f; }
  void eat() { eatAnim = 0.45f; }
  void setSwinging(bool on) { swinging = on; }   // held swing while mining (set every frame)
  float swing = 0, equip = 1, eatAnim = 0, bob = 0, bobAmt = 0;
  // other areas may take over a hand (a map held up, render/map_render.cpp): the hook draws it and returns the
  // hands it took (1 = main, 2 = off); or draw over the item in a hand (the live compass and clock dials,
  // render/instruments.cpp): m maps the item model's space (before VoxelModel::matrix()) to view space
  inline static std::function<int(HeldItemLayer&, GameSession&, const FrameCtx&, const mat4& proj)> handsHook;
  inline static std::function<void(GameSession&, const FrameCtx&, const mat4& proj, const ItemStack&, const mat4& m, const VoxelModel&)> itemHook;
private:
  bool swinging = false;
  std::string lastHeld = "\x01";
  float hy = 0, hp = 0, hsx = 0, hsy = 0; bool haveLook = false;
  double time = 0;
  vec2 light{1, 0};
};

class DroppedItemLayer : public RenderLayer {
public:
  const char* name() const override { return "dropped-items"; }
  int order() const override { return 100; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override;
};

// the first-person arm (models.js MODELS.player_arm: a sleeve and a hand), as a voxel model
const VoxelModel& firstPersonArm();

void registerItemLayers();   // idempotent
void registerPileLayer();    // render/pile_layer.cpp (called from registerItemLayers)
void registerLecternLayer(); // render/lectern_layer.cpp (called from registerItemLayers)

} // namespace dq
