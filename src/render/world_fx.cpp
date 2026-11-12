// render/world_fx.cpp — see world_fx.h
#include "render/world_fx.h"
#include "render/world_renderer.h"
#include "render/sky.h"
#include "client/session.h"
#include "client/app.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace dq {
namespace {

struct Fx {
  int level = 0; bool on = false;
  // the sun's shadow map
  bool shadow = false; GLuint shTex = 0, shCol = 0, shFbo = 0; int shRes = 0; mat4 shVP{1}; float shK = 0, shTexel = 0, shBias = 0;
  // the map is kept between redraws: drawn round a centre on a grid, again when you leave it, the sun has moved on, or a
  // second and a half has passed (shVP is in the camera-block space of shOrigin: shifted to this frame's when bound)
  glm::dvec3 shOrigin{0}; ivec3 shCell{INT32_MIN}; double shT = -1e9; float shSunY = -9; int shLevel = 0; bool shValid = false;
  // the solid scene, copied before the water
  bool copy = false, ssr = false, ao = false, copyBroken = false, copied = false;
  GLuint col = 0, dep = 0; int w = 0, h = 0; ivec2 org{0};
  // the corners' shading
  Shader* aoSh = nullptr; bool aoTried = false; GLuint aoTex = 0, aoFbo = 0; int aw = 0, ah = 0; GLuint vao = 0;   // (the shader never freed: this outlives the GL context, and deleting it then crashed the quit)
  // stand-ins so each sampler always has a texture of its own kind
  GLuint dShadow = 0, dCol = 0, dDep = 0;
};
Fx& fx() { static Fx f; return f; }

GLuint makeTex(GLenum internal, GLenum format, GLenum type, int w, int h, GLenum filter, const void* data = nullptr) {
  GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, w, h, 0, format, type, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  return t;
}
void compareMode(GLuint t) {
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
}
void standIns(Fx& F) {
  if (F.dShadow) return;
  const float one = 1.f; const u8 grey[4] = {128, 128, 128, 255};
  F.dShadow = makeTex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, 1, 1, GL_LINEAR, &one); compareMode(F.dShadow);
  F.dDep = makeTex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, 1, 1, GL_NEAREST, &one);
  F.dCol = makeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 1, 1, GL_LINEAR, grey);
  glBindTexture(GL_TEXTURE_2D, 0);
}
bool ensureShadow(Fx& F, int res) {
  if (F.shFbo && F.shRes == res) return true;
  if (F.shFbo) { glDeleteFramebuffers(1, &F.shFbo); glDeleteTextures(1, &F.shTex); glDeleteTextures(1, &F.shCol); F.shFbo = F.shTex = F.shCol = 0; }
  F.shTex = makeTex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, res, res, GL_LINEAR); compareMode(F.shTex);
  F.shCol = makeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, res, res, GL_NEAREST);   // (unwritten: a framebuffer wants a colour)
  glGenFramebuffers(1, &F.shFbo); glBindFramebuffer(GL_FRAMEBUFFER, F.shFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, F.shCol, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, F.shTex, 0);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!ok) { glDeleteFramebuffers(1, &F.shFbo); glDeleteTextures(1, &F.shTex); glDeleteTextures(1, &F.shCol); F.shFbo = F.shTex = F.shCol = 0; F.shRes = 0; return false; }
  F.shRes = res;
  return true;
}

void drawAO(Fx& F, const FrameCtx& f, const GLint* vp, GLint fbo) {
  if (!F.aoTried) { F.aoTried = true; F.aoSh = new Shader(); if (!F.aoSh->load("world_ao.glsl", "world_ao.glsl")) { F.aoSh = nullptr; DQ_WARN("world fx: world_ao.glsl failed"); } }
  if (!F.aoSh) return;
  const int W = vp[2], H = vp[3], aw = std::max(1, W / 2), ah = std::max(1, H / 2);
  if (!F.aoFbo || F.aw != aw || F.ah != ah) {
    if (F.aoFbo) { glDeleteFramebuffers(1, &F.aoFbo); glDeleteTextures(1, &F.aoTex); }
    F.aoTex = makeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, aw, ah, GL_LINEAR);
    glGenFramebuffers(1, &F.aoFbo); glBindFramebuffer(GL_FRAMEBUFFER, F.aoFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, F.aoTex, 0);
    F.aw = aw; F.ah = ah;
  }
  if (!F.vao) glGenVertexArrays(1, &F.vao);
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  Shader& S = *F.aoSh; S.use();
  S.set("uDepth", 0); S.set("uAO", 1);
  S.set("uProj", f.cam.proj); S.set("uInvProj", glm::inverse(f.cam.proj));
  S.set("uRadius", 0.6f); S.set("uStrength", 0.85f); S.set("uFadeFar", 48.f);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, F.dep);
  glBindFramebuffer(GL_FRAMEBUFFER, F.aoFbo); glViewport(0, 0, aw, ah);
  S.set("uStep", 0); S.set("uTexel", vec2(1.f / W, 1.f / H));
  glBindVertexArray(F.vao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, F.aoTex); glActiveTexture(GL_TEXTURE0);
  glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO);   // multiplied into the picture
  S.set("uStep", 1); S.set("uTexel", vec2(1.f / aw, 1.f / ah));
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}

} // namespace

void worldFxBegin(GameSession& s, const FrameCtx& f) {
  Fx& F = fx();
  F.on = F.shadow = F.copy = F.ssr = F.ao = F.copied = false;   // (the shadow map itself is kept: shValid)
  const Settings& S = s.app.settings;
  F.level = std::clamp(S.extra.value("shaderLevel", 0), 0, 4);
  if (F.level < 3 || !S.shaderPack.empty() || s.split() || f.portalDepth > 0) return;
  F.on = true;
  standIns(F);
  F.copy = !F.copyBroken; F.ssr = F.copy && F.level >= 4; F.ao = F.copy && F.level >= 4;
  // ---- the sun's shadow map: by day, under an open sky ----
  const SkyEnv* sky = f.sky.get();
  static const bool dbg = std::getenv("DQ_FX_DEBUG") != nullptr;
  if (dbg) { static double lt = 0; if (f.time - lt > 2) { lt = f.time; DQ_LOG("world fx: level %d sky %d sun %d under %d sunDir %.2f %.2f %.2f day %.2f eye %d", F.level, sky ? 1 : 0, sky && sky->showSun, sky && sky->underwater, f.sunDir.x, f.sunDir.y, f.sunDir.z, f.daylight, f.eyeFluid); } }
  if (!sky || !sky->showSun || sky->underwater || f.sunDir.y < 0.06f || f.daylight < 0.15f || f.eyeFluid != 0) return;
  const int res = F.level >= 4 ? 2048 : 1536;
  const float R = F.level >= 4 ? 48.f : 28.f, NEAR = 1.f, FAR = 300.f;
  if (!ensureShadow(F, res)) return;
  F.shK = (F.level >= 4 ? 0.5f : 0.42f) * smoothstepf(0.06f, 0.25f, f.sunDir.y) * clampv(f.daylight, 0.f, 1.f) * (1 - f.rain * 0.7f);
  const float cellSz = R * 0.25f;
  const ivec3 cell(ifloor((float)f.cam.pos.x / cellSz), ifloor((float)f.cam.pos.y / cellSz), ifloor((float)f.cam.pos.z / cellSz));
  if (F.shValid && F.shLevel == F.level && cell == F.shCell && f.time - F.shT < 1.5 && std::abs(f.sunDir.y - F.shSunY) < 0.01f) { F.shadow = true; return; }   // (the map still holds)
  F.shCell = cell; F.shT = f.time; F.shSunY = f.sunDir.y; F.shLevel = F.level; F.shOrigin = f.cam.origin; F.shValid = true;
  // centred on the cell (so the redraws don't creep), the camera within a quarter of the map of it
  const vec3 c = vec3((glm::dvec3(cell) + 0.5) * (double)cellSz - f.cam.origin), L = glm::normalize(f.sunDir);
  const vec3 up = std::abs(L.y) > 0.98f ? vec3(0, 0, 1) : vec3(0, 1, 0);
  mat4 view = glm::lookAt(c + L * 200.f, c, up);
  // the map's texels fixed to the world (snapped from the camera's absolute place), or its edges crawl as you walk
  const float texel = 2 * R / (float)res;
  const glm::dmat3 rot = glm::dmat3(glm::mat3(view));
  const glm::dvec3 a = rot * ((glm::dvec3(cell) + 0.5) * (double)cellSz);
  const vec2 offs((float)(a.x - std::floor(a.x / texel) * texel), (float)(a.y - std::floor(a.y / texel) * texel));
  view = glm::translate(mat4(1), vec3(-offs, 0.f)) * view;
  F.shVP = glm::ortho(-R, R, -R, R, NEAR, FAR) * view;
  F.shTexel = 1.f / (float)res; F.shBias = texel / (FAR - NEAR) * 1.2f;
  GLint prevFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
  GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebuffer(GL_FRAMEBUFFER, F.shFbo); glViewport(0, 0, res, res);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glClear(GL_DEPTH_BUFFER_BIT);
  glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 3.f);
  s.worldRenderer.drawShadowMap(f, F.shVP, c, R + cellSz, true);   // (leaves and plants cast too: a tree is never just its trunk's shadow)
  glDisable(GL_POLYGON_OFFSET_FILL);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glEnable(GL_CULL_FACE);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
  F.shadow = true;
}

void worldFxAfterOpaque(GameSession& s, const FrameCtx& f) {
  Fx& F = fx();
  (void)s;
  if (!F.on || !F.copy || f.portalDepth > 0) return;
  GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
  GLint fbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
  const int W = vp[2], H = vp[3];
  if (W <= 16 || H <= 16) return;
  if (!F.col || F.w != W || F.h != H) {
    if (F.col) { glDeleteTextures(1, &F.col); glDeleteTextures(1, &F.dep); }
    F.col = makeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, W, H, GL_LINEAR);
    F.dep = makeTex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, W, H, GL_NEAREST);
    F.w = W; F.h = H;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbo);
  glActiveTexture(GL_TEXTURE0);
  glGetError();
  glBindTexture(GL_TEXTURE_2D, F.dep);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], W, H);
  if (glGetError() != GL_NO_ERROR) { F.copyBroken = true; F.copy = F.ssr = F.ao = false; DQ_WARN("world fx: the depth can't be copied here: no water you see into, reflections or corner shading"); return; }
  if (F.ao) drawAO(F, f, vp, fbo);   // (before the colour copy: the water shows the shaded scene)
  glBindTexture(GL_TEXTURE_2D, F.col);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  F.org = ivec2(vp[0], vp[1]); F.copied = true;
}

void worldFxEnd() { Fx& F = fx(); F.on = F.shadow = F.copied = false; }

void worldFxBind(Shader& sh, const FrameCtx& f, int pass) {
  Fx& F = fx();
  const bool on = F.on && f.portalDepth == 0;
  standIns(F);   // (always: the samplers left at unit 0 would share it with the tile array, a draw the driver may refuse)
  const bool shadow = on && F.shadow && pass < 2, refr = on && F.copied && pass == 2;
  glActiveTexture(GL_TEXTURE0 + 10); glBindTexture(GL_TEXTURE_2D, shadow ? F.shTex : F.dShadow);
  glActiveTexture(GL_TEXTURE0 + 8); glBindTexture(GL_TEXTURE_2D, refr ? F.col : F.dCol);
  glActiveTexture(GL_TEXTURE0 + 9); glBindTexture(GL_TEXTURE_2D, refr ? F.dep : F.dDep);
  glActiveTexture(GL_TEXTURE0);
  sh.set("uShadowMap", 10); sh.set("uSceneColor", 8); sh.set("uSceneDepth", 9); sh.set("uShadowPass", 0);
  // the map's space is the camera block it was drawn from: moved to this frame's
  sh.set("uShadowK", shadow ? F.shK : 0.f); sh.set("uShadowVP", F.shVP * glm::translate(mat4(1), vec3(f.cam.origin - F.shOrigin))); sh.set("uShadowTexel", F.shTexel); sh.set("uShadowBias", F.shBias);
  sh.set("uRefract", refr ? 1.f : 0.f); sh.set("uSSR", refr && F.ssr ? 1.f : 0.f);
  sh.set("uScreen", vec4((float)F.org.x, (float)F.org.y, 1.f / (float)std::max(1, F.w), 1.f / (float)std::max(1, F.h)));
  sh.set("uNearFar", vec2(f.cam.nearZ, f.cam.farZ));
}

} // namespace dq
