// render/sky.h — the sky of each land (game/game.js computeEnv + game/weather.js + game/skies.js +
// game/seamless.js + game/theend.js + game/riftarc.js): gradient, sun, moon phases, stars, sunrise and
// sunset, the northern lights, rainbows, storm banks, mist and the cloud bank between the stacked
// bands; it also fills the frame's light/fog/wind values (FrameCtx) the other renderers use.
#pragma once
#include "render/frame.h"

namespace dq {

class Level;

// One land's sky and light this frame (the original's `env`).
struct SkyEnv {
  float day = 1, minLight = 0.05f, fogStart = 0.55f, stars = 0;
  bool showSun = false, voidBelow = false, underwater = false;
  vec3 zenith{0.3f, 0.55f, 0.9f}, horizon{0.7f, 0.8f, 0.95f}, fog{0.7f, 0.8f, 0.95f};
  vec3 sunDir{0, 1, 0}, sunGlow{0};
  vec4 storm{0, 1, 1000, 0}; vec3 stormCol{0.3f, 0.3f, 0.35f}; bool hasStorm = false;
  float aurora = 0, rainbow = 0, mist = 0, cloud = 0, sea = 0, raining = 0, wind = 0;
  int moonPhase = 0;
  vec2 fogRange{64, 128};   // blocks (worked out by computeFrame)
};

// The sky of `dim` (a band of the stacked world) or a side realm, as seen from `camPos` (portal views
// call this for the other side).
SkyEnv computeSkyEnv(Level& lv, int dim, int realm, glm::dvec3 camPos, bool blendBands = true);

// distant terrain reach this frame (render/distant.cpp sets it; the fog reaches that far)
void setSkyFarRange(float blocks);
float skyFarRange();

class SkyRenderer {
public:
  bool init();
  // fills f.skyColor/fogColor/daylight/sunDir/fog/wind/weather values from the level and camera band
  void computeFrame(const Level& lv, FrameCtx& f, int renderDistChunks);
  void draw(const FrameCtx& f);
  SkyEnv env;               // this frame's sky (clouds, distant terrain and weather read it)
private:
  Shader shader;
  GLuint vao = 0;
  double skyT = 0;
};
const SkyEnv& currentSkyEnv();   // the last computed frame's sky

} // namespace dq
