// render/weather_draw.cpp — see weather_draw.h
#include "render/weather_draw.h"
#include "render/frame.h"
#include <algorithm>

namespace dq {

WeatherDraw::~WeatherDraw() {}   // (GL objects outlive the context at quit: left to the driver, as the other layers do)

void WeatherDraw::ensureGl() {
  if (tried) return;
  tried = true;
  if (!shader.load("weather.glsl", "weather.glsl")) return;
  glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
  glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  for (int i = 0; i < 5; i++) {
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (const void*)(uintptr_t)(i * sizeof(vec4)));
    glVertexAttribDivisor(i, 1);
  }
  glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void WeatherDraw::draw(const FrameCtx& f, const WeatherSim& sim, vec3 camVel) {
  if (sim.count() == 0) return;
  ensureGl();
  const WeatherSprites& S = weatherSprites();
  if (!shader.ok() || !S.tex) return;
  const Camera& cam = f.cam;
  const vec3 eye = vec3(cam.pos - cam.origin), cv = camVel / 20.f;
  // the see-through ones back to front (fog sheets overlap a lot), then the opaque ones in any order
  order.clear();
  for (const WParticle& p : sim.all()) if (p.alpha > 0.004f || !kindDef(p.kind).blended) order.push_back({(float)glm::length(vec3(p.pos - cam.origin) - eye), &p});
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
    const bool ba = kindDef(a.second->kind).blended, bb = kindDef(b.second->kind).blended;
    return ba != bb ? ba : a.first > b.first;   // (blended first: drawn in their own pass below)
  });
  inst.clear();
  size_t blendedN = 0;
  for (auto& [d, pp] : order) {
    const WParticle& p = *pp;
    const WKindDef& K = kindDef(p.kind);
    if (K.blended) blendedN++;
    Inst q;
    const vec3 rel = vec3(p.pos - cam.origin);
    float size = p.size;
    if (K.distScale > 0) size += p.size * d * K.distScale;   // Particle Rain's distance scaling (fog banks)
    const vec3 rv = p.vel - cv;
    float stretch = 1;
    if (K.rot == WRot::Velocity) { stretch = std::clamp(glm::dot(rv, rv), 0.25f, 1.f); if (p.hit) stretch = std::min(stretch, std::max(0.f, p.impact)); }
    q.posSize = vec4(rel, size);
    q.velRoll = vec4(rv, p.roll);
    q.uv = S.uv(p.sprite);
    // falling things right at your eyes fade away (a flake a hand's width off filled the view as a soft square)
    float a = p.alpha;
    if (p.kind <= WKind::HeavySnow || p.kind == WKind::Dust) a *= std::clamp((d - 0.6f) / 1.6f, 0.f, 1.f);
    if (p.kind == WKind::Haze) a *= std::clamp((d - 2.f) / 4.f, 0.f, 1.f);   // (a haze blob at your face would fill the view)
    if (p.kind == WKind::FogFlat || p.kind == WKind::FogUpright) {
      // CustomParticle.tickFading: a sheet lying flat fades as you see it edge on (below 0.4 rad), one standing up as
      // you look down on it (over 45 degrees), so neither is ever a hard edge
      const vec3 o = rel - eye;
      const float ang = std::abs(std::atan2(o.y, std::sqrt(o.x * o.x + o.z * o.z)));
      a *= p.kind == WKind::FogFlat ? std::min(ang, 0.4f) / 0.4f : 1 - std::min(ang, 0.7853982f) / 0.7853982f;
    }
    q.tint = vec4(p.tint, std::clamp(a, 0.f, 1.f));
    q.misc = vec4((p.light >> 4) / 15.f, (p.light & 15) / 15.f, (float)((int)K.rot | (K.blended ? 0 : 8) | (p.face << 4)), stretch);
    inst.push_back(q);
  }
  if (inst.empty()) return;
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  const size_t bytes = inst.size() * sizeof(Inst);
  if (bytes > cap) { cap = bytes * 2; glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cap, nullptr, GL_STREAM_DRAW); }
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, inst.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", cam.viewProj);
  shader.set("uCamFrac", eye);
  const vec3 right = cam.right(), up = glm::cross(right, cam.forward());
  shader.set("uCamRight", right); shader.set("uCamUp", up);
  // unit 15: unit 0 is where common.glsl's array samplers default to, and two sampler types on one unit fail the draw
  // (the Legacy renderer drew nothing)
  glActiveTexture(GL_TEXTURE0 + 15); glBindTexture(GL_TEXTURE_2D, S.tex); shader.set("uAtlas", 15);
  glBindVertexArray(vao);
  glDisable(GL_CULL_FACE);
  if (blendedN) {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)blendedN);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  }
  if (inst.size() > blendedN) {
    // the opaque ones start after the blended ones in the buffer: draw them with the VAO re-pointed past those
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int i = 0; i < 5; i++) glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (const void*)(uintptr_t)(blendedN * sizeof(Inst) + i * sizeof(vec4)));
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)(inst.size() - blendedN));
    for (int i = 0; i < 5; i++) glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (const void*)(uintptr_t)(i * sizeof(vec4)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0); glActiveTexture(GL_TEXTURE0);
  glEnable(GL_CULL_FACE);
}

} // namespace dq
