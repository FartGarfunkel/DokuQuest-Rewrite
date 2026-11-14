// sim/shield_shape.h — where a player's shield is in the world: a box the size of the shield model (assets/shield3d.h),
// where the third-person rig puts it, carried on the forearm or raised on guard, so it is a real thing that arrows and
// blows strike. The numbers are measured from render/char_layer.cpp shieldPlacement on the char_poses.cpp carry and
// guard (DQ_DEV_SHBOX logs them); re-measure if those poses change. Owner: claude (geometry); the rules using it: grok.
#pragma once
#include "common/common.h"

namespace dq {
class Player;

struct ShieldBox {
  vec3 c{0};                    // its middle (world, blocks)
  vec3 ax{1, 0, 0}, ay{0, 1, 0}, az{0, 0, 1};   // across it, up it, out of its face (unit)
  vec3 half{0.3125f, 0.40625f, 0.094f};          // half its width, height, thickness
};

// the box for a body at pos (its feet) facing yaw, looking at pitch (+ up); guard: raised (else carried at the side);
// rightHand: on the right forearm (a shield held in the main hand)
ShieldBox shieldBoxAt(const vec3& pos, float yaw, float pitch, bool guard, bool sneak, bool rightHand);
// the player's shield (false: none held, or no shield pose now: rolling, swimming, crawling, riding, asleep, dead).
// The main hand's shield first, as the renderer draws it.
bool playerShieldBox(const Player& p, ShieldBox& out);
// which forearm the shield is on: 1 the main (right) hand's, -1 the off (left) hand's, 0 none to raise (sim/combat_rpg.cpp:
// one in a hand, or the shield slot's on the forearm of the hand without a weapon; both hands armed, slung)
int shieldSide(const Player& p);
// where along o + d * t (0 <= t <= maxT; d need not be unit) the segment first enters the box
bool shieldRayHit(const ShieldBox& b, const vec3& o, const vec3& d, float maxT, float* tHit = nullptr);
// is the point inside it (with a margin)
bool shieldContains(const ShieldBox& b, const vec3& p, float margin = 0);
}  // namespace dq
