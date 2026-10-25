// common.glsl — shared lighting, fog, wind and micro-voxel helpers (include from any shader).
// Lighting follows the original render/gl.js terrain shader: light levels 0..1 go through
// curve(l) = l / (3.2 - 2.2 l), sky light is scaled by the day, block light is warm (1, 0.85, 0.62),
// and nothing falls below env.minLight.
#ifndef DQ_COMMON
#define DQ_COMMON
#ifndef MICRO_VOXELS
#define MICRO_VOXELS 1
#endif
// Shader packs: the colour grade every world shader puts on its final colour. A pack's common.glsl can
// define it and then include builtin:common.glsl (the game's own file) to change the whole look in two lines.
#ifndef DQ_GRADE
#define DQ_GRADE(c) (c)
#endif

uniform float uTime;
uniform float uDaylight;      // sky light multiplier (original env.day)
uniform vec3 uSunDir;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uBrightness;    // settings.brightness
uniform float uMicroDist;     // micro-voxel relief distance
uniform float uWind;          // wind strength (original env.wind: 0.45 + strength * 0.9, 0 when waving is off)
uniform float uSea;           // storm swell on open water
uniform float uMinLight;      // light floor
uniform int uEyeFluid;        // camera in 0 air, 1 water, 2 lava
uniform vec3 uBlockLightColor;

// tile pages (see TileTextures): layer < uPageSize in the first array, the rest in the second
uniform sampler2DArray uTiles;
uniform sampler2DArray uTiles2;
uniform sampler2DArray uHeights;
uniform sampler2DArray uHeights2;
uniform float uPageSize;
vec4 tileColorGrad(vec2 uv, float layer, vec2 dx, vec2 dy) {
  return layer < uPageSize ? textureGrad(uTiles, vec3(uv, layer), dx, dy) : textureGrad(uTiles2, vec3(uv, layer - uPageSize), dx, dy);
}
vec4 tileHeightAt(vec2 uv, float layer) {
  return layer < uPageSize ? textureLod(uHeights, vec3(uv, layer), 0.0) : textureLod(uHeights2, vec3(uv, layer - uPageSize), 0.0);
}

vec3 faceNormal(int n) {
  if (n == 0) return vec3(1, 0, 0);
  if (n == 1) return vec3(-1, 0, 0);
  if (n == 2) return vec3(0, 1, 0);
  if (n == 3) return vec3(0, -1, 0);
  if (n == 4) return vec3(0, 0, 1);
  if (n == 5) return vec3(0, 0, -1);
  return vec3(0, 1, 0);
}
// the original face shading by direction (x 0.8, up 1, down 0.52, z 0.64), for any axis normal
float axisShade(vec3 n) {
  vec3 a = abs(n);
  if (a.y >= a.x && a.y >= a.z) return n.y > 0.0 ? 1.0 : 0.52;
  return a.x >= a.z ? 0.8 : 0.64;
}
float faceShade(int n) { return n == 6 ? 1.0 : axisShade(faceNormal(n)); }
float lightCurve(float l) { return l / (3.2 - 2.2 * l); }
vec3 lighting(float sky, float block, bool emissive) {
  if (emissive) return vec3(1.0);
  vec3 lc = max(vec3(lightCurve(sky) * uDaylight), vec3(lightCurve(block)) * uBlockLightColor);
  return max(lc, vec3(uMinLight));
}
vec3 applyFog(vec3 c, float dist, vec3 dir) {
  float f = smoothstep(uFogStart, uFogEnd, dist);
  return mix(c, uFogColor, f);
}

// Wind sway and waves — the original sway codes (render/gl.js terrainVS):
//   1 plant tip, 2 leaves, 3 water, 4..15 hanging (swing grows), 16..31 partway up a solid plant,
//   32..47 water surface + how open it is, 48..63 / 64..79 cloth flapping across z / x,
//   80..95 the tip of a 3D grass blade (fades into the ground with distance: chunk.glsl)
vec3 waveOffset(float sway, vec3 world) {
  if (sway < 0.5) return vec3(0.0);
  vec3 o = vec3(0.0);
  float t = uTime;
  vec2 w = world.xz;
  float gust = 0.6 + 0.4 * sin(t * 0.35 + w.x * 0.02 + w.y * 0.015);
  if (sway > 79.5) {
    // the tip of a 3D grass blade (render/mesh_plants.cpp emitGrassTufts): a quick flutter on the gusts
    float k = (sway - 80.0) / 15.0;
    o.x += (sin(t * 2.3 + w.x * 0.9 + w.y * 0.4) + 0.5 * sin(t * 4.1 + w.x * 2.3 - w.y * 1.7)) * 0.05 * uWind * gust * k;
    o.z += cos(t * 1.9 + w.y * 0.8 + w.x * 0.3) * 0.04 * uWind * gust * k;
    return o;
  }
  if (sway > 47.5) {
    bool acrossX = sway > 63.5;
    float k = (sway - (acrossX ? 64.0 : 48.0)) / 15.0;
    float ph = w.x * 0.37 + w.y * 0.29;
    float wave = sin(t * 5.1 - k * 4.2 + ph) * 0.65 + sin(t * 3.3 - k * 2.6 + ph * 1.7) * 0.35;
    float a = k * (0.07 + 0.2 * uWind) * gust * wave;
    if (acrossX) o.x += a; else o.z += a;
    o.y -= abs(a) * 0.2 * k;
  } else if (sway > 31.5) {
    float k = (sway - 32.0) / 15.0, A = k * (0.04 + 0.14 * uSea);
    float ripple = sin(t * 1.6 + w.x * 0.8 + w.y * 0.55) * 0.035 + sin(t * 1.05 - w.x * 0.37 + w.y * 0.77) * 0.025;
    float swell = sin(t * 0.9 + w.x * 0.21 + w.y * 0.13) * 0.6 + sin(t * 0.63 - w.x * 0.09 + w.y * 0.17) * 0.4;
    float chop = sin(t * 2.7 + w.x * 1.3 - w.y * 0.9) * uSea * k;
    o.y += ripple * (1.0 + 0.6 * uSea * k) + swell * A + chop * 0.025 - A * 0.7 - 0.06;
  } else if (sway < 1.5 || sway > 15.5) {
    float pk = sway > 15.5 ? (sway - 16.0) / 15.0 : 1.0;
    o.x += (sin(t * 1.9 + w.x * 0.37 + w.y * 0.23) + 0.4 * sin(t * 3.3 + w.x * 1.1)) * 0.075 * uWind * gust * pk;
    o.z += cos(t * 1.5 + w.y * 0.31 + w.x * 0.11) * 0.055 * uWind * gust * pk;
  } else if (sway < 2.5) {
    float ph = w.x * 0.45 + w.y * 0.38 + world.y * 0.5;
    o.x += sin(t * 1.4 + ph) * 0.028 * uWind * gust;
    o.y += sin(t * 1.9 + ph * 1.3) * 0.018 * uWind * gust;
    o.z += cos(t * 1.2 + ph) * 0.028 * uWind * gust;
  } else if (sway < 3.5) {
    o.y += sin(t * 1.6 + w.x * 0.8 + w.y * 0.55) * 0.035 + sin(t * 1.05 - w.x * 0.37 + w.y * 0.77) * 0.025 - 0.06;
  } else {
    float k = (sway - 4.0) / 11.0;
    float ph = floor(w.x) * 1.7 + floor(w.y) * 2.3;
    o.x += sin(t * 1.7 + ph) * 0.07 * k * (0.35 + uWind) * gust;
    o.z += cos(t * 1.3 + ph * 0.7) * 0.05 * k * (0.35 + uWind) * gust;
  }
  return o;
}

// older call form (kinds 0 none, 1 plant, 2 leaves, 3 liquid, 4 hanging) kept for other shaders
vec3 waveOffset(uint wave, vec3 world, vec3 local) { return waveOffset(wave == 4u ? 10.0 : float(wave), world); }
vec3 waterColor(vec3 c, vec3 viewVec, vec3 n) {
  vec3 v = normalize(-viewVec);
  float fres = pow(1.0 - max(0.0, dot(v, n)), 3.0);
  vec3 h = normalize(v + uSunDir);
  float spec = pow(max(0.0, dot(h, n)), 120.0) * uDaylight;
  return mix(c, mix(uSkyColor, uFogColor, 0.3), fres * 0.6) + spec * 0.6;
}

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
#endif
