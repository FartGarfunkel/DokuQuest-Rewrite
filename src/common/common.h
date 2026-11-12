// common/common.h — basic types, logging, math helpers shared by every DokuQuest module.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace dq {

using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
using i8 = int8_t; using i16 = int16_t; using i32 = int32_t; using i64 = int64_t;
using glm::vec2; using glm::vec3; using glm::vec4; using glm::ivec2; using glm::ivec3; using glm::ivec4; using glm::mat3; using glm::mat4; using glm::uvec2; using glm::uvec3;

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = PI * 2.0f;

template <class T> inline T clampv(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) { float t = clampv((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
inline float fractf(float x) { return x - std::floor(x); }
inline int floordiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
inline int floormod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
inline int ifloor(float v) { int i = (int)v; return (float)i > v ? i - 1 : i; }
inline int ifloor(double v) { int i = (int)v; return (double)i > v ? i - 1 : i; }

// ---- logging (stderr + log file in the data folder) ----
enum class LogLevel { Debug, Info, Warn, Error };
void logMsg(LogLevel lv, const char* fmt, ...)
#if defined(__GNUC__)
  __attribute__((format(printf, 2, 3)))
#endif
  ;
void logSetFile(const std::string& path);
void installCrashHandler();   // log the signal / uncaught exception and a backtrace when the process dies
using LogSink = std::function<void(LogLevel, const std::string&)>;
int logAddSink(LogSink sink);   // returns id (e.g. the launcher's server console)
void logRemoveSink(int id);
#define DQ_LOG(...) ::dq::logMsg(::dq::LogLevel::Info, __VA_ARGS__)
#define DQ_WARN(...) ::dq::logMsg(::dq::LogLevel::Warn, __VA_ARGS__)
#define DQ_ERR(...) ::dq::logMsg(::dq::LogLevel::Error, __VA_ARGS__)
#define DQ_DEBUG(...) ::dq::logMsg(::dq::LogLevel::Debug, __VA_ARGS__)

double nowSeconds();          // monotonic clock
i64 unixMillis();             // wall clock (save timestamps)
std::string strFormat(const char* fmt, ...)
#if defined(__GNUC__)
  __attribute__((format(printf, 1, 2)))
#endif
  ;
std::string toLower(std::string s);
std::vector<std::string> splitStr(std::string_view s, char sep);
bool startsWith(std::string_view s, std::string_view p);
bool endsWith(std::string_view s, std::string_view p);

} // namespace dq
