// world/dirs.h — the four horizontal directions N E S W (world/shapes.js DIRX/DIRZ), shared by every area.
#pragma once
namespace dq {
inline constexpr int DIRX[4] = {0, 1, 0, -1}, DIRZ[4] = {-1, 0, 1, 0};   // N E S W
} // namespace dq
