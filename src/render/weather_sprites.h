// render/weather_sprites.h — the pictures the weather particles use (render/weather_sim, weather_draw), in one
// small atlas: rain and snow sliced from the resource pack's environment/rain.png and snow.png the way Particle Rain
// does (pigcart, MIT: 2 columns, 16 slices; the heavy ones merge 4 slices by highest alpha), drawn here when the pack
// has none (the game's own art); ripple rings, wall streaks, fog sheets, dust and a dead bush.
#pragma once
#include "common/common.h"

namespace dq {
struct TileSet;

enum class WSprite : u8 {
  Rain0 = 0,          // .. 15
  HeavyRain0 = 16,    // .. 19
  Snow0 = 20,         // .. 35
  HeavySnow0 = 36,    // .. 39
  Ripple0 = 40,       // .. 47 (growing rings, animated)
  Streak = 48,
  FogFlat = 49,
  FogUpright = 50,
  Dust0 = 51,         // .. 53
  Shrub = 54,
  Count = 55
};

struct WeatherSprites {
  unsigned tex = 0;        // GL texture (RGBA8, nearest)
  int cell = 32, cols = 8, rows = 7;
  bool fromPack = false;   // rain/snow came from the resource pack
  vec4 uv(int sprite) const;   // u0, v0, u1, v1 (half a texel inside the cell)
  std::vector<u8> rgba;        // the atlas (kept for tests and re-uploads)
};

WeatherSprites& weatherSprites();
// (re)build from the active pack and the tile set (App::reloadTiles); uploads when a GL context is current (upload=true)
void buildWeatherSprites(const TileSet& ts, bool upload = true);

} // namespace dq
