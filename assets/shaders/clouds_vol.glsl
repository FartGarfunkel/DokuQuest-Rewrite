// clouds_vol.glsl — the volumetric cloud layer, marched per pixel at a fraction of the screen (render/clouds_volume.cpp).
// Out: in-scattered light (already multiplied by how much cloud it went through) and what's left of the sky behind
// (transmittance), plus the light-weighted distance into the cloud so the full-size pass can put it behind or in front
// of the land. The shape: a tiling 3D noise (R: big billows, G: the wisps that eat their edges), cut back by the
// weather's coverage map (R: how much sky is cloud, G: how stormy), rounded at the base, taller in storms.
#include "common.glsl"
#ifdef VERTEX
out vec2 vNdc;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
  vNdc = p;
  gl_Position = vec4(p, 0.0, 1.0);
}
#endif
#ifdef FRAGMENT
uniform mat4 uInvViewProj;      // NDC -> view direction (rotation only)
uniform float uCamY;            // camera height (global y)
uniform vec2 uCamXZ;            // camera x,z on the noise's tile (wrapped), wind included
uniform sampler3D uNoise;
uniform sampler2D uWeather;
uniform vec3 uWeatherRect;      // the map's corner relative to the camera (x, z), 1 / its size
uniform vec2 uSlab;             // bottom and top of the layer (global y)
uniform float uSteps;           // marching steps
uniform float uMaxLen;          // longest stretch marched (grazing rays)
uniform vec3 uLightDir, uLightCol, uAmbTop, uAmbBot, uStormCol, uFogCol;
uniform vec2 uCloudFog;         // fade into the fog from .x to .y blocks
uniform float uFlash, uThunder, uFrame;
in vec2 vNdc;
layout(location = 0) out vec4 outCol;
layout(location = 1) out float outDist;

const float TILE = 1024.0, VTILE = 256.0;

vec2 weatherAt(vec3 rel) { return texture(uWeather, (rel.xz - uWeatherRect.xy) * uWeatherRect.z).rg; }
float remap(float v, float a, float b) { return clamp((v - a) / max(b - a, 1e-4), 0.0, 1.0); }

// density at a camera-relative point (per block of travel)
float density(vec3 rel, bool detail, out float h, out float storm) {
  float y = uCamY + rel.y;
  h = (y - uSlab.x) / (uSlab.y - uSlab.x);
  vec2 w = weatherAt(rel);
  storm = w.g;
  float cov = w.r;
  if (cov < 0.02 || h <= 0.0 || h >= 1.0) return 0.0;
  // a flat-ish base, a rounded top that climbs with the coverage (fair-weather heaps low, storm towers the whole layer)
  float top = mix(0.42, 1.0, clamp(cov * 1.2 - 0.15 + uThunder * storm * 0.3, 0.0, 1.0));
  float prof = smoothstep(0.0, 0.08, h) * (1.0 - smoothstep(top * 0.55, top, h));
  vec3 np = vec3((uCamXZ.x + rel.x) / TILE, y / VTILE, (uCamXZ.y + rel.z) / TILE);
  float base = texture(uNoise, np * 2.0).r;
  float shape = remap(base * prof, 1.0 - min(1.0, cov * 1.45 + 0.1), 1.0);   // (fair weather: scattered heaps; a storm: all of it)
  if (shape <= 0.0) return 0.0;
  if (detail) {
    float d = texture(uNoise, np * 8.0).g;   // (whole repeats over the 1024-block tile: no seam where it wraps)
    shape = remap(shape, d * mix(0.42, 0.18, h), 1.0);   // the wisps eat the edges, more at the base
  }
  return sqrt(shape) * mix(0.16, 0.24, storm);   // (solid cores: a heap overhead is only 30-60 blocks deep)
}

float hg(float c, float g) { float g2 = g * g; return (1.0 - g2) / (12.566 * pow(1.0 + g2 - 2.0 * g * c, 1.5)); }

void main() {
  vec4 wv = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(wv.xyz / wv.w);
  outCol = vec4(0.0, 0.0, 0.0, 1.0); outDist = 0.0;
  // where the ray is inside the layer
  float t0, t1;
  if (abs(dir.y) < 1e-4) { if (uCamY < uSlab.x || uCamY > uSlab.y) return; t0 = 0.0; t1 = uMaxLen; }
  else {
    float a = (uSlab.x - uCamY) / dir.y, b = (uSlab.y - uCamY) / dir.y;
    t0 = max(min(a, b), 0.0); t1 = max(a, b);
  }
  if (t1 <= t0 || t0 > uCloudFog.y) return;
  t1 = min(t1, min(t0 + uMaxLen, uCloudFog.y));
  // long grazing stretches toward the horizon get more samples (fewer left banded strips there)
  float steps = min(48.0, uSteps * mix(1.0, 2.2, smoothstep(150.0, 900.0, t1 - t0)));
  float dt = (t1 - t0) / steps;
  // interleaved gradient noise: each pixel starts a little further in (the bilinear blow-up hides the dither)
  // (the same pattern every frame: a new one each frame flickered, with nothing to smooth it over time)
  float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  float t = t0 + dt * jit;
  float c = dot(dir, uLightDir);
  float phase = mix(hg(c, 0.6), hg(c, -0.25), 0.35) * 6.0 + 0.35;   // a silver lining toward the sun, soft elsewhere
  vec3 col = vec3(0.0); float T = 1.0, dSum = 0.0, wSum = 0.0;
  for (int i = 0; i < 64; i++) {
    if (float(i) >= steps || T < 0.03) break;
    vec3 p = dir * t;
    float h, storm;
    float d = density(p, true, h, storm);
    if (d > 0.0) {
      // light toward the sun (or moon): three steps through the cloud above
      float od = 0.0, hh, ss;
      od += density(p + uLightDir * 6.0, false, hh, ss) * 8.0;
      od += density(p + uLightDir * 20.0, false, hh, ss) * 16.0;
      float Tl = max(exp(-od * 1.1), 0.3 * exp(-od * 0.25));   // (the second: light scattered many times)
      float powder = 1.0 - exp(-d * dt * 2.0);
      vec3 amb = mix(uAmbBot, uAmbTop, h);
      vec3 L = uLightCol * Tl * phase * mix(0.5, 1.0, powder) + amb;
      // a storm's cloud: dark through and through, its belly the colour of the storm; lightning from inside
      L = mix(L, uStormCol * (0.6 + 0.5 * h), storm * 0.75);
      L += uFlash * vec3(0.92, 0.94, 1.0) * (0.5 + storm) * (1.0 - h * 0.5);
      float S = d * dt;
      float a = 1.0 - exp(-S);
      col += T * a * L;
      dSum += t * T * a; wSum += T * a;
      T *= 1.0 - a;
      t += dt;
    } else t += dt * 1.6;   // (open sky between the heaps: longer strides)
  }
  if (wSum <= 0.0) return;
  float tw = dSum / wSum;
  // far clouds go into the horizon's haze, all the way out by the fog's end
  float fogK = smoothstep(uCloudFog.x, uCloudFog.y, tw);
  col = mix(col, uFogCol * (1.0 - T), fogK);
  T = mix(T, 1.0, fogK * fogK);
  outCol = vec4(DQ_GRADE(col), T);
  outDist = tw;
}
#endif
