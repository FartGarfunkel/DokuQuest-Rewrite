// render/clouds_volume.h — volumetric clouds: one layer of cloud marched through per pixel (at a quarter or half of the
// screen's size, then laid over it), between the Overworld's sky and the Skylands. Its shape is a tiling 3D noise
// (built once) thinned or thickened by a coverage map the weather writes round you every quarter second, so a front
// rolling in is the same clouds growing thicker, taller and darker, not a different thing; lightning lights them from
// inside, the sunset warms them, the far ones fade into the horizon's fog. Settings: extra "volumetricClouds" (on);
// off draws the voxel heaps (render/clouds.cpp) as before.
#pragma once
#include "render/frame.h"
#include "render/gl.h"
#include <future>

namespace dq {

class Level;
struct SkyEnv;

class VolumetricClouds {
public:
  ~VolumetricClouds();
  void update(Level& lv, const FrameCtx& f, const SkyEnv& env, double dt);
  void draw(const FrameCtx& f, const SkyEnv& env);   // in the translucent pass: over the terrain, by its depth
  bool active = false;
private:
  bool init();
  bool ready = false, failed = false;
  std::future<std::vector<u8>> noiseJob;   // the noise is built on a thread (~0.2 s); the voxel clouds show till it's in
  Shader march, comp;
  GLuint vao = 0, noise = 0, weather[2] = {0, 0}, fbo = 0, colTex = 0, distTex = 0;
  int wi = 0, fw = 0, fh = 0;
  double mapT = 1e9;
  glm::dvec2 mapOrigin{0};          // world x,z of the coverage map's corner
  float flash = 0, thunder = 0, mist = 0, baseCov = -1;
  glm::dvec2 wind{0};
};

} // namespace dq
