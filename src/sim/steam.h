// sim/steam.h — the Sulphur Springs' steam vents (BTA): each vent breathes out scalding steam for a moment every few
// seconds, on its own beat. The simulation (sim/bta_more.cpp: scald and lift what stands over it) and the client
// (render/env_layers.cpp: the plume and its hiss) both ask this, so they agree without sending anything.
#pragma once
#include "common/common.h"
#include <cmath>

namespace dq {
inline float steamPhase(double time, int x, int y, int z) {   // 0..1 through one breath of this vent
  const u32 h = (u32)(x * 73856093) ^ (u32)(y * 19349663) ^ (u32)(z * 83492791);
  const double period = 6.0 + (h % 5);
  return (float)(std::fmod(time + (h % 997) * 0.01, period) / period);
}
inline bool steamGust(double time, int x, int y, int z) { return steamPhase(time, x, y, z) < 0.22f; }
} // namespace dq
