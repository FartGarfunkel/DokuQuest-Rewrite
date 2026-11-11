// render/char_blend.h — blending between a character's states (after AnimatedPlayer's per-state blend timers and Not Enough
// Animations' smoothing): when the state changes (sneak, swim, sit, sleep, eat, ladder, a jump...), the rig glides from
// the pose it showed to the new one over a moment instead of snapping. Settled in one state the live pose shows exactly
// as before. The blend is of the part matrices about the feet (rotations slerped, sizes and positions lerped), so it
// covers everything charRig does: the body laid flat to swim, the crawl, the bow draw, death.
#pragma once
#include "render/char_anim.h"

namespace dq {

// the pose's state as bits (the continuous things — walk phase, time, the look — aren't part of it)
u32 charStateKey(const CharPose& p);
// how long a change from one state to another blends (seconds; 0 snaps: first person in or out)
float charBlendDuration(u32 from, u32 to);
// a then b by w (0..1): rotation slerped, the rest (size, squash, position) lerped
mat4 blendMat(const mat4& a, const mat4& b, float w);

struct CharBlend {
  bool has = false;
  u32 key = 0;
  float t = 0, dur = 0;
  double last = 0;
  CharRig from, shown;   // about the feet (base's inverse applied)
};
// one frame: R is the live rig (render space), base its feet (translation; render/char_layer.cpp drawPlayer). R becomes what
// to draw. now: seconds.
void charBlendStep(CharBlend& b, CharRig& R, const mat4& base, u32 key, double now);

} // namespace dq
