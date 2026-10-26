#include "common/holiday.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace dq {

static std::atomic<bool> g_enabled{true};
static std::atomic<long long> g_checkedAt{0};
static std::atomic<int> g_month{0};

void setHolidayEventsEnabled(bool on) { g_enabled = on; }

Holiday currentHoliday() {
  static const int forced = [] {   // -1 = none given, 0 = off, 1 = halloween
    const char* e = std::getenv("DQ_EVENT");
    if (!e || !*e) return -1;
    return std::strcmp(e, "halloween") == 0 ? 1 : 0;
  }();
  if (forced >= 0) return forced ? Holiday::Halloween : Holiday::None;
  if (!g_enabled) return Holiday::None;
  long long now = (long long)std::time(nullptr);
  if (g_month == 0 || now - g_checkedAt > 30) {
    std::time_t t = (std::time_t)now; std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    g_month = lt.tm_mon + 1; g_checkedAt = now;
  }
  return g_month == 10 ? Holiday::Halloween : Holiday::None;
}

} // namespace dq
