// distant.glsl — distant terrain tiles (render/distant.cpp, game/distant.js DIST_VS/DIST_FS): blocky
// columns coloured from the tiles' average colours, a little variety cell to cell, the storm's shadow and
// rain over the far land, fog at the far edge; cut away where the real chunks are drawn.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec2 aXZ;      // tile/32768 units
layout(location = 1) in float aY;      // 1/8 block
layout(location = 2) in vec4 aCol;     // rgb, shade
layout(location = 3) in uint aFlags;   // 1 water, 2 glowing (lava)
uniform mat4 uViewProj;
uniform vec3 uTileOrigin;              // tile corner relative to the camera's block (y = -camera block y)
uniform float uTileScale;
uniform float uDrop;
uniform vec3 uCamFrac;
out vec4 vCol;
out vec3 vPos;                         // relative to the camera's block
out vec3 vRel;                         // relative to the eye
out vec2 vLocal;
flat out uint vFlags;
void main() {
  vec3 local = vec3(aXZ.x * uTileScale, aY / 8.0 - uDrop, aXZ.y * uTileScale);
  vec3 p = local + uTileOrigin;
  gl_Position = uViewProj * vec4(p, 1.0);
  vCol = aCol; vPos = p; vRel = p - uCamFrac; vLocal = local.xz; vFlags = aFlags;
}
#endif
#ifdef FRAGMENT
uniform float uDay, uCell;   // uMinLight: common.glsl
uniform vec2 uTileWorld;               // tile corner in world x,z (mod 8192) for the variety pattern
uniform sampler2D uMask;
uniform vec3 uMaskO;                   // mask corner relative to the camera's block, mask size in blocks
uniform vec4 uFront, uTrail, uStorm;   // the weather (game/skies.js stormAt)
uniform vec2 uAnchor;                  // relative to the camera's block
uniform vec3 uRainCol;
in vec4 vCol;
in vec3 vPos;
in vec3 vRel;
in vec2 vLocal;
flat in uint vFlags;
out vec4 fragColor;
float stormAt(vec2 p) {
  vec2 r = p - uAnchor; float s = uStorm.x;
  if (uTrail.w > 0.5) s = clamp((uTrail.z - dot(r, uTrail.xy)) / uStorm.z + 0.5, 0.0, 1.0);
  if (uFront.w > 0.5) s = max(s, clamp((dot(r, uFront.xy) - uFront.z) / uStorm.z + 0.5, 0.0, 1.0));
  return s * s * (3.0 - 2.0 * s);
}
void main() {
  // where a real chunk is drawn, it's drawn instead (chunk by chunk, so while chunks are still loading this fills in)
  vec2 mp = (vPos.xz - uMaskO.xy) / uMaskO.z;
  if (mp.x >= 0.0 && mp.y >= 0.0 && mp.x < 1.0 && mp.y < 1.0 && texture(uMask, mp).r > 0.5) discard;
  vec3 c = vCol.rgb * vCol.a;
  if ((vFlags & 2u) == 0u) c *= max(uDay, uMinLight);
  // a little variety cell to cell, as the real blocks have
  vec2 cell = floor((vLocal + uTileWorld + 0.013) / uCell);
  c *= 0.94 + 0.12 * fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
  float dist = length(vRel);
  if (uStorm.y > 0.001) {
    float st = stormAt(vPos.xz);
    c *= 1.0 - st * uStorm.y * 0.4;                                        // in the storm's shadow
    c = mix(c, uRainCol, st * uStorm.w * smoothstep(40.0, 420.0, dist));   // rain falling between you and it
  }
  fragColor = vec4(applyFog(c, dist, vec3(0.0)), 1.0);
}
#endif
