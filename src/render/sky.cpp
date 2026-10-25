#include "render/sky.h"
#include "sim/level.h"

namespace dq {

bool SkyRenderer::init() {
  glGenVertexArrays(1, &vao);
  return shader.load("sky.glsl", "sky.glsl");
}

struct DimSky { vec3 zenith, horizon, fog; float minLight; bool sun; };
static DimSky dimSky(int dim, float day) {
  switch (dim) {
    case DIM_NETHER: return {vec3(0.25f, 0.06f, 0.03f), vec3(0.45f, 0.14f, 0.06f), vec3(0.42f, 0.12f, 0.05f), 0.22f, false};   // original env.day of the Depths
    case DIM_END: return {vec3(0.06f, 0.03f, 0.10f), vec3(0.16f, 0.12f, 0.22f), vec3(0.12f, 0.09f, 0.17f), 0.78f, false};   // the End is lit by its sky at 0.78 (game/theend.js)
    case DIM_AETHER: {
      vec3 z = glm::mix(vec3(0.02f, 0.03f, 0.08f), vec3(0.36f, 0.62f, 1.0f), day);
      vec3 h = glm::mix(vec3(0.05f, 0.06f, 0.12f), vec3(0.80f, 0.90f, 1.0f), day);
      return {z, h, h, 0.15f, true};
    }
    default: {
      vec3 z = glm::mix(vec3(0.01f, 0.015f, 0.04f), vec3(0.33f, 0.55f, 0.95f), day);
      vec3 h = glm::mix(vec3(0.03f, 0.04f, 0.08f), vec3(0.72f, 0.83f, 0.97f), day);
      return {z, h, h, 0.08f, true};
    }
  }
}

void SkyRenderer::computeFrame(const Level& lv, FrameCtx& f, int rd) {
  // sun angle: 0 at sunrise, half a turn at sunset
  double frac = lv.dayFraction();
  float ang = (float)(frac * TAU);
  f.sunDir = glm::normalize(vec3(std::cos(ang), std::sin(ang), 0.25f));
  float day = clampv(f.sunDir.y * 2.2f + 0.35f, 0.f, 1.f);
  double y = f.cam.pos.y;
  int d = dimOfY(clampv((int)std::floor(y), 0, WORLD_H - 1));
  DimSky s = dimSky(d, day);
  // blend toward the neighbouring band near the edges (seamless crossings)
  float ly = (float)(y - dimBase(d));
  if (d == DIM_OVERWORLD && ly > DIM_H - 24) {
    DimSky a = dimSky(DIM_AETHER, day); float t = smoothstepf(DIM_H - 24, DIM_H, ly);
    s.zenith = glm::mix(s.zenith, a.zenith, t); s.horizon = glm::mix(s.horizon, a.horizon, t); s.fog = glm::mix(s.fog, a.fog, t);
  } else if (d == DIM_AETHER && ly < 24) {
    DimSky o = dimSky(DIM_OVERWORLD, day); float t = 1 - smoothstepf(0, 24, ly);
    s.zenith = glm::mix(s.zenith, o.zenith, t); s.horizon = glm::mix(s.horizon, o.horizon, t); s.fog = glm::mix(s.fog, o.fog, t);
  }
  f.dim = d;
  f.skyColor = s.zenith;
  f.fogColor = s.fog;
  f.daylight = s.sun ? std::max(s.minLight, day) : s.minLight;
  float far = rd * 16.f;
  if (d == DIM_NETHER) { f.fogStart = far * 0.25f; f.fogEnd = far * 0.8f; }
  else { f.fogStart = far * 0.72f; f.fogEnd = far * 0.98f; }
  if (f.settings && f.settings->farTerrain > 0 && d != DIM_NETHER) { f.fogStart = (float)f.settings->farTerrain * 0.55f; f.fogEnd = (float)f.settings->farTerrain; }
}

void SkyRenderer::draw(const FrameCtx& f) {
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
  shader.use();
  f.apply(shader);
  shader.set("uInvViewProj", glm::inverse(f.cam.proj * mat4(mat3(f.cam.view))));
  shader.set("uHorizon", f.fogColor);
  shader.set("uDim", f.dim);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
}

} // namespace dq
