// clouds_comp.glsl — lays the marched cloud layer (clouds_vol.glsl, smaller than the screen) over the picture: its light
// added, the scene behind dimmed by what the cloud leaves (blend ONE, SRC_ALPHA), at the cloud's own depth so land in
// front of it keeps it hidden and the cloud covers land behind it.
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
uniform sampler2D uCol, uDist;
uniform mat4 uInvViewProj, uViewProj;
uniform vec3 uCamFrac;
in vec2 vNdc;
out vec4 fragColor;
void main() {
  vec2 uv = vNdc * 0.5 + 0.5;
  vec4 c = texture(uCol, uv);
  if (c.a > 0.996) discard;
  float t = texture(uDist, uv).r;
  vec4 wv = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(wv.xyz / wv.w);
  vec4 clip = uViewProj * vec4(uCamFrac + dir * t, 1.0);
  gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 0.999999);
  fragColor = c;
}
#endif
