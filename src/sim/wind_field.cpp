// sim/wind_field.cpp — see wind_field.h
#include "sim/wind_field.h"
#include "sim/weather.h"
#include "world/dims.h"
#include <cmath>

namespace dq {
namespace {
// Particle Rain: -(sin(p*f + t) * sin(p*f*0.5 + t*0.5) * max + max), gust frequency f 0.15, max (strength) 0.03,
// t = game ticks * modulation speed 0.1. In -0.06..0.
constexpr double FREQ = 0.15, MAXW = 0.03, TWO_PI = 6.283185307179586;
double one(double p, double t) { return -(std::sin(p * FREQ + t) * std::sin(p * FREQ * 0.5 + t * 0.5) * MAXW + MAXW); }
double phase(double seconds) { return std::fmod(seconds * 20 * 0.1, 2 * TWO_PI); }   // (both terms repeat every 4 pi)
// along the wind (0..0.06) and across it (-0.03..0.03), at most 0.06 long
vec2 raw(double x, double z, double t) {
  const double ox = one(x, t), oz = one(z, t);
  vec2 v((float)(-(ox + oz) * 0.5), (float)((ox - oz) * 0.5));
  const float l = glm::length(v);
  return l > 0.06f ? v * (0.06f / l) : v;
}
} // namespace

WindParams windParams(Level& lv) {
  WindParams p;
  p.t = (double)lv.info.day * DAY_LEN + lv.info.time;
  if (WeatherSystem* W = weatherOf(lv)) { p.dir = W->windDir; p.strength = W->windStrength; p.gust = W->gust; }
  return p;
}

vec3 windAt(const WindParams& w, double x, double y, double z) {
  const int gy = (int)std::floor(y);
  const float bandY = (float)(gy - dimBase(dimOfY(gy))) + (float)(y - gy);
  const float ramp = std::clamp((bandY - 50.f) / 40.f, 0.f, 1.f);   // PR yLevelWindMultiplier: start 50, over 40
  if (ramp <= 0) return vec3(0);
  const vec2 r = raw(x, z, phase(w.t)) * (ramp * (0.5f + w.strength) * (1 + w.gust));
  const vec2 along(-std::sin(w.dir), -std::cos(w.dir)), across(-along.y, along.x);
  const vec2 v = along * r.x + across * r.y;
  return vec3(v.x, 0, v.y);
}

float gustAt(const WindParams& w, double x, double z) {
  const vec2 r = raw(x, z, phase(w.t));
  return std::clamp(r.x / 0.06f * 0.7f + w.gust * 0.6f, 0.f, 1.f);
}

} // namespace dq
