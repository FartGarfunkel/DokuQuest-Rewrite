// sim/catchup.h — time keeps passing where nobody is. Crops, trees, cane, vines, mushrooms, drips into cauldrons,
// furnaces, brewing stands, hoppers and growing animals go on whether a player is near or the chunk is loaded at all.
//
// The world keeps a simulation clock (seconds of play, saved in LevelInfo::data "simClock"; a night slept through adds
// the night). Every chunk remembers when it was last simulated, in its saved extra data ("simT": random ticks and
// hoppers, which run only within the simulation distance of a player; "simM": furnaces and brewing stands, which run
// wherever the chunk is loaded). A chunk missing the stamp (an old save, new land) starts from now, with nothing owed.
// When a chunk comes back (loaded again, a player walks back into range, the night is slept away) the time it missed is
// made up at once, not tick by tick: for every growing block the number of random ticks until its next success is drawn
// from the real per-tick odds (geometric, with the light, water, farmland and season it has now) and applied in order,
// up to a cap per catch-up; machines are stepped from one event to the next (an item done, the fuel out), hoppers moved
// in bulk at their own rate, chains in order, interleaved with the furnaces they feed. The work is queued and spread over
// the ticks (about a millisecond a tick), so a chunk arriving never holds a frame. Only the host does this; guests
// see the results through the normal chunk and block sync.
#pragma once
#include "sim/level.h"

namespace dq {

double simClock(const Level& lv);
// time passed without being simulated tick by tick (a night slept away): everything near and far makes it up
void advanceSimClock(Level& lv, double secs);
// seconds of play between two times of day (the day's and the night's own lengths: rules dayMinutes / nightMinutes)
double playSecondsBetween(Level& lv, double fromTime, double toTime);
int simDistance(Level& lv);   // chunks around a player in which blocks tick (rule "simulationDistance")
void catchUpTick(Level& lv, double dt);   // the gameplay system's tick: the clock, the chunks' stamps, the queued work
void catchUpFlush(Level& lv);             // every queued catch-up now (tests)
int catchUpPending(Level& lv);

struct CatchUpStats {
  long chunks = 0, events = 0, hopperMoves = 0, machineSteps = 0;
  double maxTickMs = 0, totalMs = 0;   // the longest any one tick spent catching up, and the total
  double maxScanMs = 0;                // the longest once-a-second look at the chunks' stamps
};
CatchUpStats& catchUpStats(Level& lv);

} // namespace dq
