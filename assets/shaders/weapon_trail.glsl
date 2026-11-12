// weapon_trail.glsl — the pale streak a weapon leaves while its blow is live (render/weapon_trail_layer.cpp): plain
// quads, camera-relative positions, each vertex its own colour and alpha (the newest part brightest), fogged.
#include "common.glsl"
uniform mat4 uViewProj;
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
out vec4 vCol;
out float vDist;
void main() {
  vCol = aCol;
  vDist = length(aPos);
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
#endif
#ifdef FRAGMENT
in vec4 vCol;
in float vDist;
out vec4 fragColor;
void main() {
  if (vCol.a < 0.004) discard;
  fragColor = vec4(DQ_GRADE(applyFog(vCol.rgb, vDist, vec3(0.0))), vCol.a);
}
#endif
