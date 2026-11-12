// sim/prospect.h — TerraFirmaCraft's prospector's pick: tapped on rock it tells you the most plentiful ore within 12 blocks
// and roughly how much ("traces of", "a small amount of", "a large amount of", "a very large amount of"), or nothing.
#pragma once
#include "world/world.h"

namespace dq {

constexpr int PROSPECT_RANGE = 12;
std::string prospect(World& w, int x, int y, int z);

} // namespace dq
