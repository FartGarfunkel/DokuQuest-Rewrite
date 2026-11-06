// particles.glsl — particle quads (render/particles.cpp): a piece of a tile, lit like the blocks
// around it (or glowing), tinted, alpha-tested, fogged. A cube (flag bit 2) already holds its
// texel colour in the tint, foliage mark included, so that face does not sample the atlas.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in uint aLayer;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uint aLight;
layout(location = 4) in vec3 aTint;
layout(location = 5) in uint aFlags;
layout(location = 6) in float aAlpha;
uniform mat4 uViewProj;
uniform vec3 uCamFrac;
out vec2 vUV;
flat out float vLayer;
out vec2 vLight;
out vec3 vTint;
flat out uint vFlags;
out float vDist;
out float vAlpha;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vUV = aUV / 128.0;
  vLayer = float(aLayer);
  vLight = vec2(float(aLight >> 4u), float(aLight & 15u)) / 15.0;
  vTint = aTint;
  vFlags = aFlags;
  vDist = length(aPos - uCamFrac);
  vAlpha = aAlpha;
}
#endif
#ifdef FRAGMENT
in vec2 vUV;
flat in float vLayer;
in vec2 vLight;
in vec3 vTint;
flat in uint vFlags;
in float vDist;
in float vAlpha;
out vec4 fragColor;
void main() {
  vec2 uv = clamp(vUV, 0.0, 0.999);
  bool baked = (vFlags & 4u) != 0u;
  bool soft = (vFlags & 2u) != 0u;
  vec4 t = baked ? vec4(1.0) : (vLayer < uPageSize ? texture(uTiles, vec3(uv, vLayer)) : texture(uTiles2, vec3(uv, vLayer - uPageSize)));
  float a = 1.0;
  if (soft) {
    // a puff: soft at its rim (whatever the picture's edge), its own alpha, the fade.
    // A voxel puff is one colour across the face, so there is no rim to fade.
    vec2 q = vUV * 2.0 - 1.0;
    float rim = 1.0 - smoothstep(0.55, 1.0, length(q));
    if (baked || abs(dFdx(vUV).x) + abs(dFdy(vUV).y) + abs(dFdx(vUV).y) + abs(dFdy(vUV).x) < 1e-4) rim = 1.0;
    a = t.a * vAlpha * rim;
    if (a < 0.01) discard;
  } else if (t.a < 0.5) discard;
  vec3 lc = (vFlags & 1u) != 0u ? vec3(1.0) : lighting(vLight.x, vLight.y, false);
  vec3 c = t.rgb * vTint * lc * 0.9;
  fragColor = vec4(DQ_GRADE(applyFog(c, vDist, vec3(0.0))), a);
}
#endif
