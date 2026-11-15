// render/spell_hands.h — a spell held in an open palm (render/magic_fx.cpp keeps it, render/char_layer.cpp draws it):
// after a hand casts, its arm comes up and holds the spell before them a few seconds (palm out), each cast thrusting it out
#pragma once
#include "common/common.h"

namespace dq {
void noteSpellCast(u32 playerId, int hand);   // hand: 0 right, 1 left, -1 both (now, on the wall clock)
// how far each arm is up holding its spell (0..1) and the cast's thrust (0..1), [0] right [1] left
void spellHandPose(u32 playerId, float ready[2], float thrust[2]);
} // namespace dq
