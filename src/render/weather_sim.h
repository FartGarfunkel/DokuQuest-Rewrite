// render/weather_sim.h — the weather particles' simulation (no GL; tools/test/weather_test.cpp drives it headless):
// rain, heavy rain, snow and heavy snow from a sphere round the camera; splashes, ripples, wall streaks and steam where
// rain lands; desert dust, skittering dust, tumbling shrubs and haze; ground fog sheets. After Particle Rain 4.0.3 by
// pigcart (MIT): ParticleSpawner (spawning), CustomParticle / WeatherParticle (motion, fading, collision), the
// particles.json numbers. Velocities are in blocks per tick (1/20 s) like Particle Rain's; the wind is
// sim/wind_field.h. render/weather_draw.cpp draws what this holds.
#pragma once
#include "common/common.h"
#include "sim/wind_field.h"
#include "render/weather_sprites.h"

namespace dq {
class World;
class WeatherSystem;
struct TileSet;

enum class WKind : u8 { Rain, HeavyRain, Snow, HeavySnow, Dust, SkitterDust, Shrub, Haze, FogFlat, FogUpright, Ripple, Splash, Streak, Steam, Count };
enum class WRot : u8 { Velocity, Camera, Upright, Flat, CameraCopy, Wall };   // Wall: flat against a block side (face)

struct WKindDef {
  float gravity, wind, stormWind, size, rotSpeed, bounce, opacity, distScale;
  int lifetime;       // ticks (0: lives until it leaves the range)
  WRot rot;
  bool blended;       // see-through and soft (false: opaque, alpha-tested)
};
const WKindDef& kindDef(WKind k);

struct WRng {   // xorshift: cheap and the same every run for a seed
  u32 s;
  explicit WRng(u32 seed = 1) : s(seed * 2654435761u + 0x9e3779b9u) { if (!s) s = 1; }
  u32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (next() & 0xffffff) / 16777216.f; }   // 0..1
};

struct WeatherInputs {
  World* world = nullptr;
  WeatherSystem* W = nullptr;
  WindParams wind;
  vec3 cam{0}, camVel{0};            // camera position (world) and velocity (blocks per second)
  int band = 2;                      // the camera's dimension band (DIM_*); the weather is cleared when it changes
  float dist = 16, surface = 100;    // sky particles' range; ground particles' (fog) range
  bool surfaceFx = true, groundFog = true;
  double t = 0;                      // seconds (for animation)
  vec3 fogCol{0.75f, 0.8f, 0.85f}, waterCol{0.25f, 0.42f, 0.85f};
  const TileSet* tiles = nullptr;    // the ground's colour for dust (render/map_colors.h); nullptr: sand-coloured
};

struct WeatherHit { vec3 pos{0}; WKind kind = WKind::Rain; int face = 0; bool liquid = false; };   // face 0 top, 1-4 sides, 5 bottom

struct WParticle {
  glm::dvec3 pos{0};
  vec3 vel{0};              // blocks per tick
  vec3 tint{1};
  WKind kind = WKind::Rain;
  u8 sprite = 0, light = 0xF0, face = 0;   // light: sky << 4 | block, sampled every few ticks
  float size = 0.5f, roll = 0, rollSpeed = 0, alpha = 0, age = 0, life = 0, impact = 1;   // impact: 1, shrinking to 0 after a hit
  int bounces = 0;
  bool hit = false;
};

class WeatherSim {
public:
  void setBudget(int max) { budget = std::max(0, max); }
  int budgetMax() const { return budget; }
  int count() const { return (int)parts.size(); }
  const std::vector<WParticle>& all() const { return parts; }
  const std::vector<WeatherHit>& hits() const { return lastHits; }
  void update(const WeatherInputs& in, double dt);
  void clear() { parts.clear(); lastHits.clear(); }
private:
  std::vector<WParticle> parts;
  std::vector<WeatherHit> lastHits;
  int budget = 1500, band = -1;
  double skyAcc = 0;
  WRng rng{12345};
  double surfAcc = 0;
  void spawnSky(const WeatherInputs& in, float ticks);
  void spawnSurface(const WeatherInputs& in, float ticks);
  double dustAcc = 0, fogAcc = 0;
  void spawnFog(const WeatherInputs& in, float ticks);
  void spawnDust(const WeatherInputs& in, float ticks);
  void onHit(const WeatherInputs& in, const WeatherHit& h);
  void fade(WParticle& p);
  void spawnParticle(const WeatherInputs& in, WKind k, glm::dvec3 at);
  void step(const WeatherInputs& in, WParticle& p, float ticks);
};

// ground fog's opacity over its life: fades in to full at half its life and out again (FadeType.IN_AND_OUT)
float fogAlpha(const WParticle& p);

// Particle Rain's sky spawn point on a sphere of `radius` round the origin: slow, weighted toward the top of the upper
// half; fast (falling, flying), anywhere on the sphere, so you never run out of the weather
vec3 skySpawnPoint(WRng& r, float radius, bool fast);
// the first block the segment from..to enters that stops weather (solid, leaves or liquid), and where it meets it
bool sweepHit(World& w, vec3 from, vec3 to, WeatherHit& out);

} // namespace dq
