// render/creature_layer.h — draws creatures (voxel bodies posed by sim/creature_rig), what they hold,
// blob shadows, the Cinder Golem's telegraph decals, projectiles (arrows, fireballs, spit, snowballs, hex
// flasks), severed parts, felled trees and the ambient wildlife (game/fauna.js: butterflies, fireflies,
// songbirds, fish — client only, never saved or sent).
// Registered by registerCreatureLayers(), called once from GameSession's constructor.
//
// Dev aid: DQ_ZOO=<page>|all|type,type:variant,... lines creatures up in front of the player on a grass
// floor (like the original's tools/probes/zoo08.js): calm (no AI), noon, spawning off. Pages hold 16 types.
#pragma once
#include "client/session.h"

namespace dq {

class Fauna;

class CreatureLayer : public RenderLayer {
public:
  CreatureLayer();
  ~CreatureLayer() override;
  const char* name() const override { return "creatures"; }
  int order() const override { return 10; }
  void update(GameSession& s, double dt) override;
  void drawOpaque(GameSession& s, const FrameCtx& f) override;
  void drawTranslucent(GameSession& s, const FrameCtx& f) override;
  int drawn = 0;            // creatures drawn last frame (debug)
private:
  std::unique_ptr<Fauna> fauna;
  bool zooDone = false;
  void zoo(GameSession& s);
};

void registerCreatureLayers();   // idempotent

} // namespace dq
