// sim/spinning.h — TerraFirmaCraft's spindle: a fired clay whorl on a stick. Hold use with wool in your other hand: a moment's
// spinning and a block of wool becomes four yarn; yarn weaves into wool cloth (and jute fibre into burlap).
#pragma once
#include "sim/level.h"

namespace dq {

class Player;
constexpr float SPIN_HOLD = 1.5f;
constexpr int YARN_PER_WOOL = 4;
bool spindleUse(Level& lv, Player& p, bool repeat, float step = 0.22f);   // true if it took the click

} // namespace dq
