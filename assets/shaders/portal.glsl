// portal.glsl — portals (render/portal_view.cpp):
//   uMode 0  the portal's own animated picture (render/doku.js addAnim portal; theend.js end_portal tile)
//   uMode 1  the window's mask (stencil/depth only; a rift's ragged edge is cut here too)
//   uMode 2  the glow at the rim of a live window and its fade-in (game/portalview.js PV_FS)
//   uMode 3  full-screen depth clear inside the window (no vertex input)
#include "common.glsl"

#ifdef VERTEX
layout(location = 0) in vec3 aPos;   // relative to the camera's integer block
layout(location = 1) in vec2 aUV;    // 0..1 across the window
layout(location = 2) in vec2 aPat;   // block units across the face (pattern)
uniform mat4 uViewProj;
uniform int uMode;
out vec2 vUV;
out vec2 vPat;
out vec3 vRel;
void main() {
  if (uMode == 3) {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vUV = p; vPat = p; vRel = vec3(0.0);
    gl_Position = vec4(p, 1.0, 1.0);
    return;
  }
  vUV = aUV; vPat = aPat; vRel = aPos;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
#endif

#ifdef FRAGMENT
uniform int uMode;
uniform int uKind;       // 0 ember, 1 sky, 2 end, 3 rift
uniform vec3 uRim;
uniform float uFade;
uniform int uTorn;
uniform vec3 uCamFrac;
in vec2 vUV;
in vec2 vPat;
in vec3 vRel;
out vec4 fragColor;

float phash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(phash(i), phash(i + vec2(1.0, 0.0)), f.x), mix(phash(i + vec2(0.0, 1.0)), phash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 hexc(float r, float g, float b) { return vec3(r, g, b) / 255.0; }

void main() {
  if (uMode == 3) { fragColor = vec4(0.0); return; }
  float edge = min(min(vUV.x, 1.0 - vUV.x), min(vUV.y, 1.0 - vUV.y)) * 9.0;
  if (uTorn == 1) {
    // a torn hole in the air with a ragged, shifting edge
    vec2 c = vUV * 2.0 - 1.0;
    float a = atan(c.y, c.x), r = length(c);
    float lim = 0.8 + (vnoise(vec2(a * 2.5 + 3.0, uTime * 0.5)) - 0.5) * 0.3 + (vnoise(vec2(a * 9.0, uTime * 1.4)) - 0.5) * 0.12;
    if (r > lim) discard;
    edge = (lim - r) * 10.0;
  }
  if (uMode == 1) { fragColor = vec4(0.0); return; }
  if (uMode == 2) {
    float gl = 1.0 - clamp(edge, 0.0, 1.0);
    gl = gl * gl * (0.75 + 0.25 * sin(uTime * 3.0 + vUV.y * 14.0 + vUV.x * 5.0));
    float g = gl * 0.8;
    float a = 1.0 - (1.0 - g) * uFade;   // = mix(rim*0.55, mix(view, rim, g), fade) as an overlay
    if (a < 0.004) discard;
    fragColor = vec4(uRim * (g * uFade + 0.55 * (1.0 - uFade)) / a, a);
    return;
  }
  // the portal's own picture: 16 x 16 per block, 8 frames a second like the original's painted tile
  vec2 px = floor(fract(vPat) * 16.0);
  float x = px.x, y = 15.0 - px.y;
  vec3 col; float alpha = 200.0 / 255.0;
  if (uKind == 2) {
    // the End's starry deep, drifting slowly
    vec2 cell = floor(vPat);
    col = hexc(5.0, 4.0, 10.0);
    for (int L = 0; L < 2; L++) {
      vec2 q = px + cell * 16.0 + vec2(float(L) * 7.0, 0.0) + floor(vec2(uTime * (0.6 + float(L) * 0.5), uTime * 0.25));
      float h = phash(q + float(L) * 31.7);
      if (h > 0.93) {
        float k = phash(q * 1.7 + 3.1) * 6.0;
        vec3 s = k < 1.0 ? hexc(26.0, 176.0, 160.0) : k < 2.0 ? hexc(90.0, 58.0, 160.0) : k < 3.0 ? hexc(42.0, 106.0, 208.0)
               : k < 4.0 ? hexc(192.0, 240.0, 255.0) : k < 5.0 ? hexc(58.0, 208.0, 138.0) : hexc(138.0, 74.0, 208.0);
        col = s * (L == 0 ? 1.0 : 0.6);
      }
    }
    alpha = 1.0;
  } else {
    float t = floor(fract(uTime) * 8.0) / 8.0 * 6.2831853;
    float v = sin(x * 0.7 + y * 0.35 + t) + sin(y * 0.9 - x * 0.2 - t * 2.0) + sin(length(vec2(x, y) - 7.5) * 0.8 - t) * 0.6;
    vec3 a, b, c;
    if (uKind == 1) { a = hexc(42.0, 90.0, 200.0); b = hexc(90.0, 200.0, 240.0); c = hexc(230.0, 247.0, 255.0); }
    else if (uKind == 3) { a = hexc(42.0, 10.0, 72.0); b = hexc(138.0, 58.0, 208.0); c = hexc(224.0, 176.0, 255.0); }
    else { a = hexc(106.0, 20.0, 48.0); b = hexc(232.0, 100.0, 28.0); c = hexc(255.0, 211.0, 74.0); }
    col = v > 1.1 ? c : v > 0.0 ? b : a;
  }
  float dist = length(vRel - uCamFrac);
  col = mix(col, uFogColor, smoothstep(uFogStart, uFogEnd, dist));
  fragColor = vec4(col, alpha);
}
#endif
