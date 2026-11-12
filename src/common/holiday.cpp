#include "common/holiday.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace dq {

static std::atomic<bool> g_enabled{true};
static std::atomic<long long> g_checkedAt{0};
static std::atomic<int> g_month{0}, g_day{0};

void setHolidayEventsEnabled(bool on) { g_enabled = on; }

static int forcedEvent() {   // -1 = none given, 0 = off, 1 halloween, 2 christmas, 3 new year (Christmas too)
  static const int f = [] {
    const char* e = std::getenv("DQ_EVENT");
    if (!e || !*e) return -1;
    return std::strcmp(e, "halloween") == 0 ? 1 : std::strcmp(e, "christmas") == 0 ? 2 : std::strcmp(e, "newyear") == 0 ? 3 : 0;
  }();
  return f;
}

Holiday currentHoliday() {
  const int forced = forcedEvent();
  if (forced >= 0) return forced == 1 ? Holiday::Halloween : forced >= 2 ? Holiday::Christmas : Holiday::None;
  if (!g_enabled) return Holiday::None;
  long long now = (long long)std::time(nullptr);
  if (g_month == 0 || now - g_checkedAt > 30) {
    std::time_t t = (std::time_t)now; std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    g_month = lt.tm_mon + 1; g_day = lt.tm_mday; g_checkedAt = now;
  }
  return g_month == 10 ? Holiday::Halloween : g_month == 12 ? Holiday::Christmas : Holiday::None;
}

bool newYear() {
  const int forced = forcedEvent();
  if (forced >= 0) return forced == 3;
  currentHoliday();   // refreshes the date
  if (!g_enabled) return false;
  return (g_month == 12 && g_day == 31) || (g_month == 1 && g_day == 1);
}

} // namespace dq
