// render/cape_sim.h — a cape that hangs and flies as cloth: eight strips from the shoulders, each swinging on a damped
// spring to the angle the air past the back would hold it at (running, falling, turning, the wind), quicker at the
// shoulders than the hem, so it trails behind when you run, swings when you turn, drops when you stop and stirs in the
// wind; held to the body (never in through the back or the legs, never bent more than cloth bends strip to strip).
// After Wavey Capes (a cape that moves with you, kept behind the back, its bends limited), our own code.
//
//   CapeSim sim;                                      // one per character (CharacterLayer::Anim)
//   capeStep(sim, body, anchor, wind, time, maxLift); // each frame: body = the body part's matrix (model px -> world)
//   sim.bend[i], sim.side[i]                          // each strip's turn from the one above (radians), to draw with
#pragma once
#include "common/common.h"

namespace dq {

struct CapeSim {
  static constexpr int N = 8;          // strips (render/char_layer draws capeMesh rows 10..17, each 2 px tall)
  static constexpr float SEG = 2.f;    // a strip's length, model px
  vec3 pt[N + 1]{}, prev[N + 1]{};     // (state: each strip's swing speed; the back's last facing, the eased speed)
  vec3 lastAnchor{0};
  double t = -1;                       // the last step's time (-1: not started)
  float acc = 0;                       // time not yet stepped (fixed steps)
  float bend[N]{}, side[N]{};          // out: each strip's turn from the one above: out behind (x axis), sideways (z axis)
  float lift[N]{}, sway[N]{};          // out: each strip's angle from hanging straight down (body-local)
};

// Steps the cloth to `time` (seconds) and fills bend/side. body: body-local model px -> world; anchor: where it hangs
// from (body-local px); wind: the air's speed in the world (blocks / s); maxLift: the most it may fly out behind (radians;
// small under a backpack). A jump of more than two blocks (a teleport) hangs it afresh.
void capeStep(CapeSim& s, const mat4& body, vec3 anchor, vec3 wind, double time, float maxLift);

} // namespace dq
