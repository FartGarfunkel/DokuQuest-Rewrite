// render/frame.h — per-frame values every renderer shares (camera, time, sky, fog, settings).
#pragma once
#include "render/camera.h"
#include "render/gl.h"

namespace dq {

// Tile arrays are split in pages (GL guarantees 2048 layers on the hardware we target).
struct TileTextures { GLuint color[2] = {0, 0}, height[2] = {0, 0}; int page = 2048; void bind(Shader& s, int firstUnit = 0) const; };

struct FrameCtx {
  Camera cam;
  double time = 0;           // seconds since start (animation)
  float daylight = 1;        // sky light multiplier
  vec3 sunDir{0, 1, 0};
  vec3 skyColor{0.5f, 0.7f, 1.f}, fogColor{0.7f, 0.8f, 1.f};
  float fogStart = 64, fogEnd = 128;
  float wind = 0.3f;
  vec3 blockLightColor{1.0f, 0.85f, 0.65f};
  int width = 1280, height = 720;
  const Settings* settings = nullptr;
  int dim = 2;               // band the camera is in
  // sky & weather extras (render/sky.cpp fills them every frame)
  vec3 horizonColor{0.7f, 0.8f, 1.f};
  float minLight = 0.05f;    // light floor of this land
  float rain = 0, thunder = 0, flash = 0;   // rain/snow 0..1 where the camera is (wet look), thunderstorm, lightning flash
  float mist = 0;            // mist, fog bank, sandstorm, blizzard 0..1
  float sea = 0;             // sea swell 0..1 (bigger waves in a storm)
  float cloudBank = 0;       // inside the cloud bank between the Overworld and the Skylands
  vec2 windDir{1, 0};        // the way the wind blows (x, z); strength is `wind`
  bool underwater = false;
  // set common.glsl uniforms on a bound shader
  void apply(Shader& s) const;
};

} // namespace dq
