// render/weather_draw.h — draws render/weather_sim's particles: one instanced draw for the see-through ones and one for
// the opaque ones (assets/shaders/weather.glsl), from the weather sprite atlas (render/weather_sprites.h).
#pragma once
#include "render/weather_sim.h"
#include "render/gl.h"

namespace dq {
struct FrameCtx;

class WeatherDraw {
public:
  // camVel: the camera's velocity in blocks per second (rain is stretched along its motion relative to you)
  void draw(const FrameCtx& f, const WeatherSim& sim, vec3 camVel);
  ~WeatherDraw();
private:
  struct Inst { vec4 posSize, velRoll, uv, tint, misc; };
  Shader shader;
  bool tried = false;
  GLuint vao = 0, vbo = 0;
  size_t cap = 0;
  std::vector<Inst> inst;
  std::vector<std::pair<float, const WParticle*>> order;
  void ensureGl();
};

const WeatherSim* weatherSimNow();   // the live one (the F3 overlay), or nullptr

} // namespace dq
