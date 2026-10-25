// particles.glsl — particle quads (render/particles.cpp): a piece of a tile, lit like the blocks
// around it (or glowing), tinted, alpha-tested, fogged.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in uint aLayer;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uint aLight;
layout(location = 4) in vec3 aTint;
layout(location = 5) in uint aFlags;
uniform mat4 uViewProj;
uniform vec3 uCamFrac;
out vec2 vUV;
flat out float vLayer;
out vec2 vLight;
out vec3 vTint;
flat out uint vFlags;
out float vDist;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vUV = aUV / 128.0;
  vLayer = float(aLayer);
  vLight = vec2(float(aLight >> 4u), float(aLight & 15u)) / 15.0;
  vTint = aTint;
  vFlags = aFlags;
  vDist = length(aPos - uCamFrac);
}
#endif
#ifdef FRAGMENT
in vec2 vUV;
flat in float vLayer;
in vec2 vLight;
in vec3 vTint;
flat in uint vFlags;
in float vDist;
out vec4 fragColor;
void main() {
  vec2 uv = clamp(vUV, 0.0, 0.999);
  vec4 t = vLayer < uPageSize ? texture(uTiles, vec3(uv, vLayer)) : texture(uTiles2, vec3(uv, vLayer - uPageSize));
  if (t.a < 0.5) discard;
  vec3 lc = (vFlags & 1u) != 0u ? vec3(1.0) : lighting(vLight.x, vLight.y, false);
  vec3 c = t.rgb * vTint * lc * 0.9;
  fragColor = vec4(DQ_GRADE(applyFog(c, vDist, vec3(0.0))), 1.0);
}
#endif
