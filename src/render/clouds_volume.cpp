// render/clouds_volume.cpp — see clouds_volume.h.
#include "render/clouds_volume.h"
#include "render/clouds.h"
#include "render/sky.h"
#include "sim/level.h"
#include "sim/weather.h"
#include "common/noise.h"
#include "world/dims.h"

namespace dq {

namespace {
constexpr int NS = 32;                      // the noise: 32 voxels a side, repeating
constexpr int MAP_N = 64;                   // the coverage map: 64 x 64 cells
constexpr float MAP_CELL = 48.f;            // of 48 blocks (3072 across, round you)
constexpr double TILE = 1024.0;             // blocks the noise repeats over across
constexpr float SLAB_BOT = CLOUD_BASE_Y - 4.f, SLAB_TOP = CLOUD_BASE_Y + 52.f;   // the layer (into the cloud bank)
constexpr float REACH = 1300.f;             // cloud seen this far, fading into the haze

float hf(u32 h) { return (h & 0xFFFFFF) / 16777215.f; }
// cell noise that repeats every `cells` cells across the unit cube: distance to the nearest of one point per cell
float worley(float x, float y, float z, int cells, u32 seed) {
  const float px = x * cells, py = y * cells, pz = z * cells;
  const int ix = (int)std::floor(px), iy = (int)std::floor(py), iz = (int)std::floor(pz);
  float best = 9.f;
  for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
    const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
    const int wx = ((cx % cells) + cells) % cells, wy = ((cy % cells) + cells) % cells, wz = ((cz % cells) + cells) % cells;
    const u32 h = hash32(wx, wy, wz, (i32)seed);
    const float qx = cx + hf(h), qy = cy + hf(h * 747796405u + 1u), qz = cz + hf(h * 2891336453u + 7u);
    best = std::min(best, (qx - px) * (qx - px) + (qy - py) * (qy - py) + (qz - pz) * (qz - pz));
  }
  return std::min(1.f, std::sqrt(best));
}
// value noise that repeats every `cells` lattice steps
float vnoise(float x, float y, float z, int cells, u32 seed) {
  const float px = x * cells, py = y * cells, pz = z * cells;
  const int ix = (int)std::floor(px), iy = (int)std::floor(py), iz = (int)std::floor(pz);
  auto sm = [](float t) { return t * t * (3 - 2 * t); };
  const float fx = sm(px - ix), fy = sm(py - iy), fz = sm(pz - iz);
  auto v = [&](int a, int b, int c) { return hf(hash32(((a % cells) + cells) % cells, ((b % cells) + cells) % cells, ((c % cells) + cells) % cells, (i32)seed)); };
  auto l = [](float a, float b, float t) { return a + (b - a) * t; };
  return l(l(l(v(ix, iy, iz), v(ix + 1, iy, iz), fx), l(v(ix, iy + 1, iz), v(ix + 1, iy + 1, iz), fx), fy),
           l(l(v(ix, iy, iz + 1), v(ix + 1, iy, iz + 1), fx), l(v(ix, iy + 1, iz + 1), v(ix + 1, iy + 1, iz + 1), fx), fy), fz);
}
}

VolumetricClouds::~VolumetricClouds() {
  if (vao) glDeleteVertexArrays(1, &vao);
  GLuint t[5] = {noise, weather[0], weather[1], colTex, distTex};
  for (GLuint x : t) if (x) glDeleteTextures(1, &x);
  if (fbo) glDeleteFramebuffers(1, &fbo);
}

namespace {
// the noise: R big billows (value noise under inverted cell noise, Perlin-Worley style), G small wisps
std::vector<u8> buildNoise() {
  std::vector<u8> px((size_t)NS * NS * NS * 2);
  for (int z = 0; z < NS; z++) for (int y = 0; y < NS; y++) for (int x = 0; x < NS; x++) {
    const float u = (x + 0.5f) / NS, v = (y + 0.5f) / NS, w = (z + 0.5f) / NS;
    const float val = 0.55f * vnoise(u, v, w, 4, 11) + 0.3f * vnoise(u, v, w, 8, 12) + 0.15f * vnoise(u, v, w, 16, 13);
    const float cell = 1.f - (0.6f * worley(u, v, w, 3, 21) + 0.28f * worley(u, v, w, 6, 22) + 0.12f * worley(u, v, w, 12, 23));
    const float base = clampv((val - 0.5f) * 1.3f + cell * 0.95f - 0.12f, 0.f, 1.f);
    const float wisp = 1.f - (0.6f * worley(u, v, w, 8, 31) + 0.3f * worley(u, v, w, 16, 32) + 0.1f * worley(u, v, w, 32, 33));
    const size_t i = (((size_t)z * NS + y) * NS + x) * 2;
    px[i] = (u8)std::lround(base * 255); px[i + 1] = (u8)std::lround(clampv(wisp, 0.f, 1.f) * 255);
  }
  return px;
}
}

bool VolumetricClouds::init() {
  if (ready || failed) return ready;
  if (!noiseJob.valid()) { noiseJob = std::async(std::launch::async, buildNoise); return false; }
  if (noiseJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
  std::vector<u8> px = noiseJob.get();
  failed = true;
  if (!march.load("clouds_vol.glsl", "clouds_vol.glsl") || !comp.load("clouds_comp.glsl", "clouds_comp.glsl")) { DQ_WARN("clouds: volumetric shaders failed, the voxel clouds stay"); return false; }
  glGenVertexArrays(1, &vao);
  glGenTextures(1, &noise);
  glBindTexture(GL_TEXTURE_3D, noise);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RG8, NS, NS, NS, 0, GL_RG, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_3D, w, GL_REPEAT);
  glBindTexture(GL_TEXTURE_3D, 0);
  glGenTextures(2, weather);
  std::vector<u8> zero((size_t)MAP_N * MAP_N * 2, 0);
  for (GLuint t : weather) {
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, MAP_N, MAP_N, 0, GL_RG, GL_UNSIGNED_BYTE, zero.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &fbo);
  ready = true; failed = false;
  return true;
}

void VolumetricClouds::update(Level& lv, const FrameCtx& f, const SkyEnv& env, double dt) {
  active = false;
  if (!f.settings || !f.settings->clouds || !f.settings->extra.value("volumetricClouds", true) || f.cam.realm != REALM_MAIN) return;
  if (f.dim != DIM_OVERWORLD && f.dim != DIM_AETHER) return;
  if (!init()) return;
  active = true;
  WeatherSystem* W = weatherOf(lv);
  const std::string kind = W ? W->stormKind() : "clear";
  thunder = kind == "thunder" ? 1.f : 0.f;
  flash = W ? std::max(W->flashT > 0 ? std::min(1.f, W->flashT / 0.2f) : 0.f, f.dim == DIM_AETHER && W->belowFlash > 0 ? W->belowFlash / 0.3f : 0.f) : 0.f;
  mist = std::min(1.f, env.mist);
  wind = W ? glm::dvec2(W->cloudX, W->cloudZ) : glm::dvec2(0);
  // the coverage map, round you: the land's usual cover, thickening to full where a storm or a shower is
  mapT += dt;
  // the land's usual cover eases over (stepping into a desert used to thin every cloud in the sky at once)
  const std::string zone = W ? W->zone : "temperate";
  const float wantBase = zone == "desert" ? 0.22f : zone == "jungle" || zone == "swamp" ? 0.55f : zone == "savanna" ? 0.32f : 0.42f;
  if (baseCov < 0) baseCov = wantBase; else baseCov += (wantBase - baseCov) * std::min(1.f, (float)dt / 8.f);
  const double cx = std::floor(f.cam.pos.x / MAP_CELL), cz = std::floor(f.cam.pos.z / MAP_CELL);
  const glm::dvec2 origin((cx - MAP_N / 2) * MAP_CELL, (cz - MAP_N / 2) * MAP_CELL);
  if (mapT < 0.25 && origin == mapOrigin) return;
  mapT = 0; mapOrigin = origin;
  const float base = baseCov;
  const float darkK = f.dim == DIM_OVERWORLD ? (kind == "thunder" ? 1.f : 0.8f) : 0.45f;
  std::vector<u8> px((size_t)MAP_N * MAP_N * 2);
  for (int j = 0; j < MAP_N; j++) for (int i = 0; i < MAP_N; i++) {
    const float x = (float)(origin.x + (i + 0.5) * MAP_CELL), z = (float)(origin.y + (j + 0.5) * MAP_CELL);
    const float k = W ? std::max(W->stormAt(x, z), std::min(1.f, W->showerK * 0.8f)) : 0.f;
    const float cover = clampv(base + k * (1 - base) * 0.98f, 0.f, 1.f);
    px[((size_t)j * MAP_N + i) * 2] = (u8)std::lround(cover * 255);
    px[((size_t)j * MAP_N + i) * 2 + 1] = (u8)std::lround(clampv(k * darkK, 0.f, 1.f) * 255);
  }
  wi ^= 1;   // (the other texture: the one the last frames read may still be in use)
  glBindTexture(GL_TEXTURE_2D, weather[wi]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAP_N, MAP_N, GL_RG, GL_UNSIGNED_BYTE, px.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void VolumetricClouds::draw(const FrameCtx& f, const SkyEnv& env) {
  if (!active || !ready || f.portalDepth > 0 || f.width <= 0 || f.height <= 0) return;
  // a quarter of the screen across on the legacy renderer (a sixteenth of its pixels), half on the modern one
  const int div = gfx.modern ? 2 : 4;
  const int w = std::max(1, f.width / div), h = std::max(1, f.height / div);
  GLint prevFbo = 0, vp[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, vp);
  if (w != fw || h != fh) {
    fw = w; fh = h;
    if (!colTex) { glGenTextures(1, &colTex); glGenTextures(1, &distTex); }
    auto mk = [&](GLuint t, GLenum ifmt, GLenum fmt) {
      glBindTexture(GL_TEXTURE_2D, t);
      glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    mk(colTex, GL_RGBA16F, GL_RGBA); mk(distTex, GL_R16F, GL_RED);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, distTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { DQ_WARN("clouds: no volumetric target, the voxel clouds stay"); ready = false; failed = true; }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    if (!ready) return;
  }
  const glm::mat4 invRot = glm::inverse(f.cam.proj * mat4(mat3(f.cam.view)));
  static float frameNo = 0; frameNo = std::fmod(frameNo + 1.f, 64.f);
  // the march
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, bufs);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
  march.use();
  f.apply(march);
  march.set("uInvViewProj", invRot);
  march.set("uCamY", (float)f.cam.pos.y);
  const glm::dvec2 camW(f.cam.pos.x - wind.x, f.cam.pos.z - wind.y);
  march.set("uCamXZ", vec2((float)(camW.x - std::floor(camW.x / TILE) * TILE), (float)(camW.y - std::floor(camW.y / TILE) * TILE)));
  march.set("uWeatherRect", vec3((float)(mapOrigin.x - f.cam.pos.x), (float)(mapOrigin.y - f.cam.pos.z), 1.f / (MAP_N * MAP_CELL)));
  march.set("uSlab", vec2(SLAB_BOT, SLAB_TOP));
  march.set("uSteps", gfx.modern ? 36.f : 18.f);
  march.set("uMaxLen", 900.f);
  // the light: the sun by day (warm when it's low), the moon's pale blue by night
  const float sunUp = env.sunDir.y;
  const bool moon = sunUp < -0.05f;
  const vec3 ldir = glm::normalize(moon ? -env.sunDir : env.sunDir);
  const float low = 1.f - clampv(sunUp / 0.35f, 0.f, 1.f);
  vec3 sunCol = moon ? vec3(0.32f, 0.38f, 0.52f) * 0.5f * (1 - env.day) : glm::mix(vec3(1.f, 0.97f, 0.92f), vec3(1.f, 0.62f, 0.42f), low) * (0.25f + 0.85f * env.day);
  const vec3 ambTop = glm::mix(env.zenith, env.horizon, 0.45f) * (0.55f + 0.35f * env.day) + vec3(std::max(env.minLight, 0.06f) * 0.6f);
  march.set("uLightDir", vec3(ldir.x, std::max(ldir.y, 0.08f), ldir.z));
  march.set("uLightCol", sunCol);
  march.set("uAmbTop", ambTop);
  march.set("uAmbBot", ambTop * vec3(0.62f, 0.66f, 0.74f));
  march.set("uStormCol", env.stormCol * (0.35f + 0.65f * std::max(env.day, 0.15f)));
  march.set("uFogCol", env.fog);
  // (a mist or rain haze greys the far clouds; it used to cut them off past 70 blocks, so they vanished in any mist)
  march.set("uCloudFog", vec2(std::max(260.f, REACH * 0.4f) * (1 - 0.6f * mist), lerpf(REACH, 520.f, mist)));
  march.set("uFlash", flash);
  march.set("uThunder", thunder);
  march.set("uFrame", frameNo);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_3D, noise); march.set("uNoise", 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, weather[wi]); march.set("uWeather", 1);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  // laid over the picture, at its own depth
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_SRC_ALPHA);
  comp.use();
  comp.set("uInvViewProj", invRot);
  comp.set("uViewProj", f.cam.viewProj);
  comp.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colTex); comp.set("uCol", 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, distTex); comp.set("uDist", 1);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_3D, 0);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND);
  glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}

} // namespace dq
