// terrain_fx.glsl — the block selection outline (lines) and the breaking-crack overlay.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec3 aPos;     // relative to the camera's integer block
layout(location = 1) in vec2 aUV;
uniform mat4 uViewProj;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = uViewProj * vec4(aPos, 1.0); }
#endif
#ifdef FRAGMENT
uniform int uMode;          // 0 outline colour, 1 crack (multiply-blended: grey 0.5 leaves the block as it is)
uniform vec4 uColor;
uniform float uLayer;
in vec2 vUV;
out vec4 fragColor;
void main() {
  if (uMode == 0) { fragColor = uColor; return; }
  vec4 t = tileColorGrad(fract(vUV), uLayer, dFdx(vUV), dFdy(vUV));
  fragColor = vec4(mix(vec3(0.5), t.rgb, t.a), 1.0);
}
#endif
