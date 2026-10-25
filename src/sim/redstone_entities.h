// sim/redstone_entities.h — short-lived entities of the redstone port: blocks sliding out of (or back into)
// a piston (PistonAnim, game/redstone.js) and lit TNT (PrimedTNT, game/blockfx.js). Drawn by render/vehicle_layer.
#pragma once
#include "sim/level.h"

namespace dq {

class PistonAnim : public Entity {
public:
  struct Piece { ivec3 from, to; BlockId id; u8 meta; bool retracting = false; };   // retracting: nothing is left behind
  PistonAnim() { type = "piston_anim"; w = 0; h = 0; noGravity = true; }
  std::vector<Piece> pieces, extra;
  int mv = 0;
  float t = 0, dur = 0.12f;
  std::vector<u32> riders;
  bool done = false;
  void tick(Level& lv, double dt) override;
  void finish(Level& lv);      // the real blocks take their places
  json save() const override;  // never kept: the level finishes moves before saving
  float progress() const { return clampv(t / dur, 0.f, 1.f); }
};

class PrimedTnt : public Entity {
public:
  PrimedTnt() { type = "primed_tnt"; w = 0.49f; h = 0.98f; }
  float fuse = 4;
  void tick(Level& lv, double dt) override;
  json save() const override;
  void load(const json& j) override;
};

// Blow a round hole, knock everyone back, set off nearby TNT (game/blockfx.js Game.explode). Emits "explode"
// [x, y, z, power, realm] first: a handler returning false (e.g. a combat system with its own) takes over.
void explode(Level& lv, int realm, vec3 at, float power, bool noBlocks = false);

} // namespace dq
