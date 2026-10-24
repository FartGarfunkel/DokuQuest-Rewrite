// sky.glsl — full-screen sky: gradient (zenith -> horizon), sun disc + glow, moon, stars.
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
uniform vec3 uHorizon;
uniform int uDim;
in vec2 vNdc;
out vec4 fragColor;
float hash(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
void main() {
  vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(w.xyz / w.w);
  float up = dir.y;
  vec3 c = mix(uHorizon, uSkyColor, smoothstep(-0.05, 0.55, up));
  if (up < 0.0) c = mix(uHorizon, uHorizon * 0.55, smoothstep(0.0, -0.4, up));
  if (uDim == 2 || uDim == 3) {
    float sd = dot(dir, uSunDir);
    c += vec3(1.0, 0.85, 0.55) * pow(max(sd, 0.0), 200.0) * 2.0;                  // glow
    c = mix(c, vec3(1.0, 0.95, 0.75), smoothstep(0.9993, 0.9996, sd));          // disc
    float md = dot(dir, -uSunDir);
    c = mix(c, vec3(0.85, 0.88, 0.95), smoothstep(0.9994, 0.9997, md) * (1.0 - uDaylight * 0.7));
    float night = 1.0 - smoothstep(0.1, 0.5, uDaylight);
    vec3 cell = floor(dir * 220.0);
    float st = step(0.9975, hash(cell)) * night * smoothstep(-0.05, 0.2, up);
    c += vec3(st) * (0.6 + 0.4 * sin(uTime * 3.0 + hash(cell) * 40.0));
  } else if (uDim == 0) {
    vec3 cell = floor(dir * 160.0);
    c += vec3(0.7, 0.6, 0.9) * step(0.996, hash(cell)) * 0.6;
  }
  fragColor = vec4(c, 1.0);
}
#endif
