// render/fp_motion.h — the first-person hand's extra motion (render/item_layers.cpp), after Hold My Items' animation
// scripts and Punchy's per-kind animation sets (Punched Stuffs, used with permission): how fast each kind of thing
// swings, the swap (the old thing lowered out of view, the new one brought up with a little overshoot), and the hand's
// place by what you're doing (sprinting, landing, on a ladder, swimming, drinking). Pure math (tools/test/anim_test.cpp).
#pragma once
#include "common/common.h"

namespace dq {

// swing progress per second: Minecraft's 3.6 for most things; heavy heads (axes, maces, hammers) slower, daggers quicker;
// mining never slower than 3.6
float fpSwingRate(const std::string& itemId, bool mining);

constexpr float FP_SWAP_TIME = 0.36f;   // seconds for a whole swap
// how far down the hand is (0 in place .. 1 out of view) at t (0..1 through the swap); a little below 0 as the new one settles
float fpSwapLower(float t);
// is the old thing still the one in the hand at t?
bool fpSwapShowsOld(float t);

struct FpState { float sprint = 0, land = 0, climb = 0, swim = 0, drink = 0; };   // each 0..1, eased by the caller
struct FpOffset { float dy = 0, dz = 0, pitch = 0, roll = 0; };                   // blocks (view space), radians
FpOffset fpStateOffset(const FpState& s, float time);
// drunk rather than eaten: potions, bottles, mugs, flasks, milk (render/char_layer.cpp and the hand view)
bool isDrinkItem(const std::string& itemId);

} // namespace dq
