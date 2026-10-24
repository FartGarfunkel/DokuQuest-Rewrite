// common.glsl — shared lighting, fog, wind and micro-voxel helpers (include from any shader).
#ifndef DQ_COMMON
#define DQ_COMMON
#ifndef MICRO_VOXELS
#define MICRO_VOXELS 1
#endif

uniform float uTime;
uniform float uDaylight;      // 0 night .. 1 noon (sky light multiplier)
uniform vec3 uSunDir;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uBrightness;    // settings.brightness
uniform float uMicroDist;     // micro-voxel relief distance
uniform float uWind;          // wind strength (weather)
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
  return vec3(0, 0, -1);
}
float faceShade(int n) {
  if (n == 2) return 1.0;
  if (n == 3) return 0.5;
  if (n < 2) return 0.8;
  return 0.65;
}
float lightCurve(float l) {
  // like the original: each level ~ 80% of the next brighter one, lifted by the brightness option
  float b = pow(0.8, (1.0 - l) * 15.0);
  return mix(b, sqrt(b), uBrightness);
}
vec3 lighting(float sky, float block, bool emissive) {
  if (emissive) return vec3(1.0);
  float s = lightCurve(sky) * uDaylight;
  vec3 b = lightCurve(block) * uBlockLightColor * step(0.01, block);
  return max(vec3(0.03 + 0.04 * uBrightness), max(vec3(s), b));
}
vec3 applyFog(vec3 c, float dist, vec3 dir) {
  float f = smoothstep(uFogStart, uFogEnd, dist);
  return mix(c, uFogColor, f);
}
// Wind sway (plants anchored at their base, leaves rustle) and liquid surface waves
vec3 waveOffset(uint wave, vec3 world, vec3 local) {
  if (wave == 0u) return vec3(0.0);
  float t = uTime;
  if (wave == 1u) {   // plants: top vertices only (the mesher flags them)
    float ph = dot(world.xz, vec2(0.37, 0.61));
    float s = sin(t * 1.9 + ph) * 0.06 + sin(t * 3.7 + ph * 1.7) * 0.025;
    return vec3(s, 0.0, s * 0.7) * (0.6 + uWind);
  }
  if (wave == 2u) {   // leaves
    float ph = dot(world, vec3(0.41, 0.23, 0.57));
    return vec3(sin(t * 1.3 + ph), sin(t * 1.7 + ph * 1.3) * 0.5, cos(t * 1.1 + ph)) * 0.025 * (0.5 + uWind);
  }
  if (wave == 3u) {   // water surface
    float w = sin(world.x * 0.9 + t * 1.6) * 0.035 + sin(world.z * 1.3 - t * 1.25) * 0.03 + sin((world.x + world.z) * 2.1 + t * 2.3) * 0.012;
    return vec3(0.0, w - 0.05, 0.0);
  }
  if (wave == 4u) {   // hanging (vines, lanterns): sway from the top
    float ph = dot(world.xz, vec2(0.53, 0.29));
    return vec3(sin(t * 1.4 + ph), 0.0, cos(t * 1.2 + ph)) * 0.04 * (0.5 + uWind);
  }
  return vec3(0.0);
}
// micro-voxel edges: a recessed voxel next to a raised one gets a darker rim on the shadowed side
vec2 microVoxelEdge(vec3 uv, int n) {
  vec2 px = vec2(1.0 / 32.0);
  float h = tileHeightAt(uv.xy, uv.z).r;
  float hl = tileHeightAt(fract(uv.xy - vec2(px.x, 0.0)), uv.z).r;
  float hu = tileHeightAt(fract(uv.xy - vec2(0.0, px.y)), uv.z).r;
  float e = max(0.0, h - hl) * 0.6 + max(0.0, h - hu) * 0.8;
  return vec2(e, 0.0);
}
vec3 waterColor(vec3 c, vec3 viewVec, vec3 n) {
  vec3 v = normalize(-viewVec);
  float fres = pow(1.0 - max(0.0, dot(v, vec3(0.0, 1.0, 0.0))), 3.0);
  vec3 refl = mix(uSkyColor, uFogColor, 0.3);
  vec3 h = normalize(v + uSunDir);
  float spec = pow(max(0.0, h.y), 120.0) * uDaylight;
  return mix(c, refl, fres * 0.6) + spec * 0.6;
}
#endif
