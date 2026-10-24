#include "common/common.h"
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <map>

namespace dq {

static std::mutex g_logMx;
static FILE* g_logFile = nullptr;
static std::map<int, LogSink> g_sinks;
static int g_sinkId = 0;

void logSetFile(const std::string& path) {
  std::lock_guard<std::mutex> lk(g_logMx);
  if (g_logFile) fclose(g_logFile);
  g_logFile = fopen(path.c_str(), "w");
}
int logAddSink(LogSink sink) { std::lock_guard<std::mutex> lk(g_logMx); g_sinks[++g_sinkId] = std::move(sink); return g_sinkId; }
void logRemoveSink(int id) { std::lock_guard<std::mutex> lk(g_logMx); g_sinks.erase(id); }

void logMsg(LogLevel lv, const char* fmt, ...) {
#ifdef NDEBUG
  if (lv == LogLevel::Debug) return;
#endif
  char buf[4096];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  static const char* tags[] = {"debug", "info", "warn", "error"};
  std::lock_guard<std::mutex> lk(g_logMx);
  fprintf(stderr, "[%s] %s\n", tags[(int)lv], buf);
  if (g_logFile) { fprintf(g_logFile, "[%s] %s\n", tags[(int)lv], buf); fflush(g_logFile); }
  std::string s(buf);
  for (auto& [id, sink] : g_sinks) sink(lv, s);
}

double nowSeconds() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return duration<double>(steady_clock::now() - t0).count();
}
i64 unixMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
std::string strFormat(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  va_list ap2; va_copy(ap2, ap);
  int n = vsnprintf(nullptr, 0, fmt, ap); va_end(ap);
  std::string s(n > 0 ? n : 0, '\0');
  if (n > 0) vsnprintf(s.data(), n + 1, fmt, ap2);
  va_end(ap2);
  return s;
}
std::string toLower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::vector<std::string> splitStr(std::string_view s, char sep) {
  std::vector<std::string> out; size_t a = 0;
  for (size_t i = 0; i <= s.size(); i++) if (i == s.size() || s[i] == sep) { out.emplace_back(s.substr(a, i - a)); a = i + 1; }
  return out;
}
bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

} // namespace dq
