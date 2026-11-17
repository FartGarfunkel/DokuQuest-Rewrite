// sim/world_spawn.h — where a new world starts: the edge of a village (or out in the wilds with "Start in the wild"),
// else the first dry land out from the middle, else the middle of a floating island. The same for a single-player world,
// one opened to LAN and a dedicated server (a server's newcomers had been put at the middle: often the sea floor).
#pragma once
#include "sim/level.h"

namespace dq {

vec3 chooseWorldSpawn(Level& lv, float* yaw = nullptr);   // also sets lv.info.spawn

} // namespace dq
