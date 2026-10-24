// render/sky.h — sky gradient, sun, moon and stars per dimension, plus the frame's light/fog
// values. Crossing between stacked dimensions blends their skies (the cloud bank between the
// Overworld and the Skylands, the crust between the Overworld and the Ember Depths).
#pragma once
#include "render/frame.h"

namespace dq {

class Level;

class SkyRenderer {
public:
  bool init();
  // fills f.skyColor/fogColor/daylight/sunDir/fog distances from the level clock and camera band
  void computeFrame(const Level& lv, FrameCtx& f, int renderDistChunks);
  void draw(const FrameCtx& f);
private:
  Shader shader;
  GLuint vao = 0;
};

} // namespace dq
