// render/be_sky.h — Better End's End sky (nebulae, stars, the fog band): see be_sky.cpp.
#pragma once
#include "render/gl.h"

namespace dq {

class BeSky {
public:
  struct V { vec3 p; vec2 uv; };
  bool init();
  // viewProjRot: projection x the view's rotation only (the sky is centred on the camera). ticks: the world's time
  // in ticks (20 a second). fade: 1, less while blinded or passing into the Nether.
  void draw(const mat4& viewProjRot, double ticks, vec3 fogColor, float fogDensity, float fade = 1);
private:
  struct Range { int first = 0, count = 0; };
  Range stars1, stars2, stars3, stars4, nebula1, nebula2, horizon, fog;
  Shader shader;
  GLuint vao = 0, vbo = 0, tHorizon = 0, tNebula1 = 0, tNebula2 = 0, tStars = 0, tFog = 0;
  bool ready = false;
};

} // namespace dq
