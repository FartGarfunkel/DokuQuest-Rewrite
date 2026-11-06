// common.glsl — shared lighting, fog, wind and micro-voxel helpers (include from any shader).
// Lighting follows Cubyz: sun and block are added in quadrature, sun scaled by uAmbient (1 by day,
// a blue floor at night). Nothing falls below env.minLight.
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
uniform vec3 uNearFog;
uniform vec4 uSeason;    // seasons: grass and leaves turn toward rgb by a (0 = summer green)   // the old fog: start, end, how thick (0 = none); the far lands stay faint beyond it
uniform float uBrightness;    // settings.brightness
uniform float uMicroDist;     // micro-voxel relief distance
uniform float uWind;          // wind strength (original env.wind: 0.45 + strength * 0.9, 0 when waving is off)
uniform float uSea;           // storm swell on open water
uniform float uMinLight;      // light floor
uniform vec3 uAmbient;        // max(night blue, day scalar). Item previews set this to 1.
uniform int uEyeFluid;        // camera in 0 air, 1 water, 2 lava
uniform vec3 uBlockLightColor;

// tile pages (see TileTextures): layer < uPageSize in the first array, the rest in the second
uniform sampler2DArray uTiles;
uniform sampler2DArray uTiles2;
uniform sampler2DArray uHeights;
uniform sampler2DArray uHeights2;
uniform sampler2DArray uNormals;      // baked smooth normal maps: xy = tangent-space normal, z = crevice darkening, w = blurred depth
uniform sampler2DArray uNormals2;
uniform float uPageSize;
vec4 tileColorGrad(vec2 uv, float layer, vec2 dx, vec2 dy) {
  return layer < uPageSize ? textureGrad(uTiles, vec3(uv, layer), dx, dy) : textureGrad(uTiles2, vec3(uv, layer - uPageSize), dx, dy);
}
vec4 tileColorLod(vec2 uv, float layer, float lod) {
  return layer < uPageSize ? textureLod(uTiles, vec3(uv, layer), lod) : textureLod(uTiles2, vec3(uv, layer - uPageSize), lod);
}
vec4 tileHeightAt(vec2 uv, float layer) {
  return layer < uPageSize ? textureLod(uHeights, vec3(uv, layer), 0.0) : textureLod(uHeights2, vec3(uv, layer - uPageSize), 0.0);
}

vec4 tileNormalAt(vec2 uv, float layer) {
  return layer < uPageSize ? textureLod(uNormals, vec3(uv, layer), 0.0) : textureLod(uNormals2, vec3(uv, layer - uPageSize), 0.0);
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
// Face shading by direction, the same ratios as the near mesh and the distant walls
// (x 170/255, up 1, down 145/255, z 205/255). Micro-voxel side walls use this so they agree with the block face.
float axisShade(vec3 n) {
  vec3 a = abs(n);
  if (a.y >= a.x && a.y >= a.z) return n.y > 0.0 ? 1.0 : 145.0 / 255.0;
  return a.x >= a.z ? 170.0 / 255.0 : 205.0 / 255.0;
}
float faceShade(int n) { return n == 6 ? 1.0 : axisShade(faceNormal(n)); }
float lightCurve(float l) { return l / (3.2 - 2.2 * l); }
// Dynamic point lights (render/dyn_light.cpp, port of the original dynLight): carried torches, glowing drops, lit TNT,
// fireballs. Position relative to the camera's block, w = level 0..15. Light falls off one level per block and
// only lights faces turned toward it. Each light has its own colour (uDynColor): a held torch is warm even when the
// block-light volume here is some other colour, such as glow lichen's green. Returns a block light 0..1.
uniform vec4 uDyn[8];
uniform vec3 uDynColor[8];
uniform int uDynN;
float dynLight(vec3 pos, vec3 n) {
  float best = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= uDynN) break;
    vec3 d = uDyn[i].xyz - pos;
    float l = length(d);
    float lv = (uDyn[i].w - l) / 15.0;
    if (lv <= best) continue;
    float face = clamp(dot(n, d / max(l, 0.001)) * 2.0 + 0.6, 0.0, 1.0);
    best = max(best, lv * face);
  }
  return clamp(best, 0.0, 1.0);
}
// the same lights, already through lightCurve and tinted by each light's own colour
vec3 dynLightRgb(vec3 pos, vec3 n) {
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 8; i++) {
    if (i >= uDynN) break;
    vec3 d = uDyn[i].xyz - pos;
    float l = length(d);
    float lv = (uDyn[i].w - l) / 15.0;
    if (lv <= 0.0) continue;
    float face = clamp(dot(n, d / max(l, 0.001)) * 2.0 + 0.6, 0.0, 1.0);
    acc = max(acc, vec3(lightCurve(clamp(lv * face, 0.0, 1.0))) * uDynColor[i]);
  }
  return acc;
}
// sun31 and block31 are Cubyz's 0..31 channels. sqrt(sun^2 + block^2), then the brightness floor.
vec3 cubyzCombine(vec3 sun31, vec3 block31) {
  vec3 amb = sun31 * uAmbient;
  vec3 q = sqrt(amb * amb + block31 * block31);
  return max(min(q, vec3(31.0)) / 31.0, vec3(uMinLight));
}
// A carried light, in the same 0..31 units: level*17 is the byte, one step of 8 per block.
vec3 dynBlock31(vec3 pos, vec3 n) {
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 8; i++) {
    if (i >= uDynN) break;
    vec3 d = uDyn[i].xyz - pos;
    float l = length(d);
    float raw = uDyn[i].w * 17.0 - l * 8.0;
    if (raw <= 0.0) continue;
    float face = clamp(dot(n, d / max(l, 0.001)) * 2.0 + 0.6, 0.0, 1.0);
    acc = max(acc, vec3((raw / 8.0) * face) * uDynColor[i]);
  }
  return acc;
}
vec3 lighting(float sky, float block, bool emissive) {
  if (emissive) return vec3(1.0);
  return cubyzCombine(vec3(sky * 31.0), vec3(block * 31.0) * uBlockLightColor);
}
// coloured block light (render/light_colour_layer.cpp): the colour of the light where this point is, from the volume
// round the camera; rel = the point relative to the camera's block (as vRel)
uniform sampler3D uLightVol;
uniform int uLightVolOn;
uniform vec3 uLightVolOrigin, uLightVolSize;
vec3 lightingAt(float sky, float block, bool emissive, vec3 rel, vec3 n) {
  if (emissive) return vec3(1.0);
  vec3 col = uBlockLightColor;
  if (uLightVolOn != 0) {
    vec3 t = (rel - uLightVolOrigin) / uLightVolSize;
    if (all(greaterThan(t, vec3(0.0))) && all(lessThan(t, vec3(1.0)))) col = texture(uLightVol, t).rgb;
  }
  // Entities and particles only have a 0..1 nibble. The volume tints that gray. Terrain carries its own RGB
  // and does not sample the volume (that would paint the colour a second time).
  vec3 block31 = vec3(block * 31.0) * col;
  if (uDynN > 0) block31 = max(block31, dynBlock31(rel, n));
  return cubyzCombine(vec3(sky * 31.0), block31);
}
vec3 applyFog(vec3 c, float dist, vec3 dir) {
  float f = smoothstep(uFogStart, uFogEnd, dist);
  if (uNearFog.z > 0.0) f = max(f, smoothstep(uNearFog.x, uNearFog.y, dist) * uNearFog.z);
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
