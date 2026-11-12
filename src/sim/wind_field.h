// sim/wind_field.h — the wind felt at a point: Particle Rain's gust field (pigcart, MIT; ParticleRain.getWind:
// sine products of position and time, stronger with height, nothing low down) laid along the weather's own wind
// (WeatherSystem::windDir / windStrength / gust). A pure function of world time and position, so every player in a
// multiplayer world sees the same gusts. Weather particles (render/weather_sim) and the wind sound use it.
#pragma once
#include "common/common.h"

namespace dq {
class Level;

struct WindParams {
  float dir = 0, strength = 0.6f, gust = 0;   // WeatherSystem's: it blows toward (-sin dir, -cos dir)
  double t = 0;                                // world time in seconds (day * DAY_LEN + time)
};
WindParams windParams(Level& lv);
// blocks per tick (1/20 s) a light particle is pushed by, horizontal; y is global (band-local 50..90 ramps it in)
vec3 windAt(const WindParams& w, double x, double y, double z);
float gustAt(const WindParams& w, double x, double z);   // 0..1, how hard it is gusting here (the wind sound)

} // namespace dq
