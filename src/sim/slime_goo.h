// sim/slime_goo.h — slimes splat where they land (particles2.js slimeSplat) and leave a sticky puddle that slows
// whoever walks through it until it dries; thrown slimeballs splat the same way.
#pragma once
#include "common/common.h"

namespace dq {
class Level;
void slimeGooAt(Level& lv, int realm, vec3 pos, float size, bool magma = false);
void registerSlimeGoo();
} // namespace dq
