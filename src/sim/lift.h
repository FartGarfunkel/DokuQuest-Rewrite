// sim/lift.h — lifts (moving structures stage 3, docs/superpowers/specs/2026-11-16-moving-structures-design.md §1, §3).
// A Slider with a line of Lift Rails beside it, running the way it slides, is a lift: its piece travels along the rail.
// Rail index k counts from the rail block level with the slider (k = 0); a Lift Stop touching rail k is a floor where the
// piece has slid k - 1 blocks (so the stop by rail 1 is the ground floor: the piece resting on the slider). The slider's
// record keeps "at" (how far its piece has slid now). A stop's meta bit 1: the lift is standing at it (a redstone signal).
#pragma once
#include "sim/level.h"

namespace dq {

struct Lift {
  ivec3 slider{0}, rail0{0}, dir{0};
  int len = 0;                                    // rail blocks
  std::vector<std::pair<ivec3, int>> stops;       // each stop and where the piece stands at it (sorted by that)
  int maxAt() const { return std::max(0, len - 1); }
};
bool liftOfSlider(const World& w, ivec3 slider, Lift& out);
bool liftOfStop(const World& w, ivec3 stop, Lift& out, int* at);   // the lift this stop serves, and its floor
int liftNext(const Lift& l, int at);              // the next floor on from `at` (after the top: the bottom)
bool isLiftStop(BlockId id);
void liftMarkStops(World& w, const Lift& l, int at);   // the stop at `at` lit, the rest dark (-1: none)

} // namespace dq
