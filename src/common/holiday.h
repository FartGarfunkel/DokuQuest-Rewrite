// common/holiday.h — seasonal events from the PC's local clock. October = Halloween (the whole month).
// DQ_EVENT=halloween forces it on (tests), DQ_EVENT=none forces it off; Settings::holidayEvents switches events off.
#pragma once

namespace dq {

enum class Holiday { None, Halloween };

void setHolidayEventsEnabled(bool on);   // the game mirrors Settings::holidayEvents here (default on)
Holiday currentHoliday();                // cached for a few seconds; cheap enough for per-frame calls
inline bool halloween() { return currentHoliday() == Holiday::Halloween; }

} // namespace dq
