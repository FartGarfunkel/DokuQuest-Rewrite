// common/holiday.h — seasonal events from the PC's local clock. October = Halloween (the whole month), December =
// Christmas (the whole month), New Year = the 31st of December and the 1st of January (fireworks over the villages).
// DQ_EVENT=halloween|christmas|newyear forces one on (tests), DQ_EVENT=none forces them off; Settings::holidayEvents
// switches events off.
#pragma once

namespace dq {

enum class Holiday { None, Halloween, Christmas };

void setHolidayEventsEnabled(bool on);   // the game mirrors Settings::holidayEvents here (default on)
Holiday currentHoliday();                // cached for a few seconds; cheap enough for per-frame calls
inline bool halloween() { return currentHoliday() == Holiday::Halloween; }
inline bool christmas() { return currentHoliday() == Holiday::Christmas; }
bool newYear();                          // the 31st of December or the 1st of January (fireworks)

} // namespace dq
