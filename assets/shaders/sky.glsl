// sky.glsl — the full-screen sky (render/gl.js skyFS): zenith-horizon gradient, sunset glow, stars,
// the northern lights, a storm's bank of cloud on the horizon, a rainbow, the void below; plus the
// sun and the moon (with its phases) drawn as the original's pixel-art billboards.
#include "common.glsl"
#ifdef VERTEX
out vec2 vNdc;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
  vNdc = p;
  gl_Position = vec4(p, 0.9999, 1.0);
}
#endif
#ifdef FRAGMENT
uniform mat4 uInvViewProj;
uniform vec3 uZenith, uHorizon, uSunGlow, uStormCol;
uniform float uStars, uVoid, uAurora, uRainbow, uSkyT, uShowSun, uSunFade;
uniform vec4 uStorm;
uniform int uMoonPhase;
uniform float uHarvest;   // Halloween: a full moon is a harvest moon, 1.6x bigger and orange
in vec2 vNdc;
out vec4 fragColor;
float h(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
float h2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// a billboard in the sky toward `d` (renderer.js drawBillboard: 80 away, `size` across); returns the
// tile pixel (0..15) under this view direction, or -1
vec2 billboard(vec3 dir, vec3 d, float size) {
  float k = dot(dir, d);
  if (k <= 0.0) return vec2(-1.0);
  vec3 rr = vec3(d.z, 0.0, -d.x); float rl = length(rr); rr = rl > 1e-4 ? rr / rl : vec3(1.0, 0.0, 0.0);
  vec3 uu = cross(d, rr);
  vec2 q = vec2(dot(dir, rr), dot(dir, uu)) / k * 80.0 / size;   // -0.5..0.5 across the quad
  if (abs(q.x) >= 0.5 || abs(q.y) >= 0.5) return vec2(-1.0);
  return floor((q + 0.5) * 16.0);
}

void main() {
  vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 d = normalize(w.xyz / w.w);
  float t = pow(clamp(d.y, 0.0, 1.0), 0.45);
  vec3 c = mix(uHorizon, uZenith, t);
  if (d.y < 0.0) c = mix(uHorizon, uHorizon * (1.0 - uVoid * 0.85), clamp(-d.y * 3.0, 0.0, 1.0));
  float sg = max(dot(d, uSunDir), 0.0);
  c += uSunGlow * pow(sg, 6.0) * clamp(1.0 - abs(d.y) * 2.0, 0.0, 1.0);
  if (uStars > 0.0 && d.y > 0.0) {
    vec3 q = floor(d * 220.0);
    float s = step(0.9975, h(q));
    c += vec3(s) * uStars * clamp(d.y * 3.0, 0.0, 1.0);
  }
  // the northern lights: curtains of green, purple at the top, folding and drifting across the north
  if (uAurora > 0.001 && d.y > 0.0) {
    float az = atan(d.x, -d.z), north = smoothstep(-0.35, 0.45, -d.z / max(0.001, length(d.xz)));
    vec3 au = vec3(0.0);
    for (int i = 0; i < 3; i++) {
      float fi = float(i), x = az * (2.2 + fi * 0.9) + fi * 1.7;
      float fold = sin(x * 2.0 + uSkyT * (0.05 + fi * 0.02)) * 0.5 + sin(x * 4.7 - uSkyT * 0.07 + fi) * 0.3 + sin(x * 9.3 + uSkyT * 0.11) * 0.2;
      float base = 0.16 + fi * 0.07 + 0.05 * fold, hh = d.y - base;
      float curtain = smoothstep(-0.015, 0.02, hh) * exp(-max(hh, 0.0) * (5.0 + fi * 2.0));
      float rays = 0.55 + 0.45 * sin(az * (70.0 + fi * 23.0) + sin(az * 6.0 + uSkyT * 0.2 + fi) * 5.0);
      float glow = 0.6 + 0.4 * sin(az * 3.0 - uSkyT * 0.09 + fi * 2.1);
      vec3 col = mix(vec3(0.12, 1.0, 0.45), vec3(0.65, 0.25, 0.95), clamp(hh * 4.5, 0.0, 1.0));
      au += col * curtain * rays * glow * (1.0 - fi * 0.25);
    }
    c += au * north * uAurora * 0.55;
  }
  // a storm still far off: a dark bank of cloud along that side of the sky, its top rising as it comes closer
  if (uStorm.w > 0.001 && d.y > -0.06) {
    vec2 hd = normalize(d.xz + vec2(1e-5));
    float facing = dot(hd, uStorm.xy);
    if (facing > 0.0) {
      float az = atan(d.x, d.z);
      float top = 95.0 * facing / max(uStorm.z, 60.0);
      top += (0.012 * sin(az * 19.0) + 0.008 * sin(az * 47.0 + 1.3) + 0.005 * sin(az * 113.0 + 2.1)) * sqrt(facing);
      top = min(top, 0.9);
      float bank = smoothstep(top + 0.006, top - 0.006, d.y) * smoothstep(0.0, 0.3, facing);
      float rim = smoothstep(top - 0.035, top, d.y);
      c = mix(c, mix(uStormCol, uStormCol * 1.7 + 0.04, rim * 0.55), bank * uStorm.w);
    }
  }
  // a rainbow after the rain: the bow 42 degrees round the point opposite the sun, red outside, violet in
  if (uRainbow > 0.001 && d.y > -0.02) {
    float r = acos(clamp(dot(d, -uSunDir), -1.0, 1.0));
    float b = (r - 0.705) / 0.04;
    if (b > -0.25 && b < 1.25) {
      float hh = (1.0 - clamp(b, 0.0, 1.0)) * 0.78;
      vec3 bow = clamp(vec3(abs(hh * 6.0 - 3.0) - 1.0, 2.0 - abs(hh * 6.0 - 2.0), 2.0 - abs(hh * 6.0 - 4.0)), 0.0, 1.0);
      float edge = smoothstep(-0.25, 0.1, b) * smoothstep(1.25, 0.9, b);
      c += bow * edge * uRainbow * 0.22 * smoothstep(-0.02, 0.06, d.y);
    }
  }
  // the sun (a bright square with a soft rim) and the moon (with its phase), added like light
  if (uShowSun > 0.5) {
    vec2 sp = billboard(d, uSunDir, 14.0);
    if (sp.x >= 0.0) {
      float dd = max(abs(sp.x - 7.5), abs(sp.y - 7.5));
      if (dd < 4.5) c += vec3(1.0, 0.973, 0.847) * uSunFade;
      else if (dd < 7.5) c += vec3(1.0, 0.847, 0.416) * (200.0 - (dd - 4.5) * 60.0) / 255.0 * uSunFade;
    }
    vec2 mp = billboard(d, -uSunDir, 10.0 * (1.0 + 0.6 * uHarvest));
    if (mp.x >= 2.0 && mp.x < 14.0 && mp.y >= 2.0 && mp.y < 14.0) {
      vec3 mc = vec3(0.875, 0.894, 0.925) * (0.9 + h2(mp) * 0.1);
      ivec2 ip = ivec2(mp);
      if ((ip == ivec2(5, 5)) || (ip == ivec2(6, 5)) || (ip == ivec2(9, 9)) || (ip == ivec2(10, 9)) || (ip == ivec2(9, 10)) || (ip == ivec2(5, 10)) || (ip == ivec2(11, 4))) mc = vec3(0.682, 0.714, 0.769);
      mc = mix(mc, mc * vec3(1.0, 0.55, 0.22) * 1.12, uHarvest);
      // the phase: 0 full, 1-3 waning (lit on the left), 4 new, 5-7 waxing (lit on the right)
      float u = (mp.x - 7.5) / 6.0, s = cos(float(uMoonPhase) * 0.785398);
      bool lit = uMoonPhase == 0 || (uMoonPhase < 4 ? u < s : (uMoonPhase > 4 && u > -s));
      c += mc * (lit ? 1.0 : 0.07) * uSunFade;
    }
  }
  fragColor = vec4(DQ_GRADE(c), 1.0);
}
#endif
