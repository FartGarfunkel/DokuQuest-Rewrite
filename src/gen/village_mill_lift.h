// gen/village_mill_lift.h — a lift to the millstone in a style's own windmill piece (gen/village_mill_lift.cpp)
#pragma once
#include "gen/village_pieces.h"
#include <array>
#include <optional>

namespace dq {

// in the piece's own (unturned) coordinates: the slider under the shaft (its x, y, z; the platform goes on it), the rail's
// column beside it and the stops' column beyond the rail (x, z), and the y the platform rises to (under the stones' floor)
struct MillLift { std::array<int, 3> shaft{}; std::array<int, 2> rail{}, stop{}; int top = 0; };
std::optional<MillLift> millLiftOf(const VPPiece& p);   // none: no millstone, or it's on the ground floor, or no room

} // namespace dq
