// distant.glsl — distant terrain tiles (render/distant.cpp, game/distant.js DIST_VS/DIST_FS): voxels drawn with
// the blocks' own textures, one texture per voxel face as Cubyz draws its far chunks (tinted as the chunk shader
// tints grass, leaves and water), the storm's shadow and rain over the far land, fog at the far edge; cut away
// where the real chunks are drawn.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec2 aXZ;      // tile/32768 units
layout(location = 1) in float aY;      // 1/8 block
layout(location = 2) in uint aTile;    // the face's texture layer
layout(location = 3) in uint aRgb;     // the tint, 5-6-5
layout(location = 4) in uint aPk;      // bits 0..3: 1 water, 2 glowing (lava), 4 tint every pixel, 8 average colour only; 4..6 the face; 7..14 the shade
uniform mat4 uViewProj;
uniform vec3 uTileOrigin;              // tile corner relative to the camera's block (y = -camera block y)
uniform float uTileScale;
uniform float uDrop;
uniform vec3 uCamFrac;
out vec4 vCol;
out vec3 vPos;                         // relative to the camera's block
out vec3 vRel;                         // relative to the eye
out vec2 vLocal;
out float vY;                          // the world height (the texture's rows)
flat out uint vFlags;
flat out float vTile;
void main() {
  vec3 local = vec3(aXZ.x * uTileScale, aY / 8.0 - uDrop, aXZ.y * uTileScale);
  vec3 p = local + uTileOrigin;
  gl_Position = uViewProj * vec4(p, 1.0);
  vCol = vec4(float((aRgb >> 11u) & 31u) / 31.0, float((aRgb >> 5u) & 63u) / 63.0, float(aRgb & 31u) / 31.0, float((aPk >> 7u) & 255u) / 255.0);
  vPos = p; vRel = p - uCamFrac; vLocal = local.xz; vFlags = (aPk & 15u) | (((aPk >> 4u) & 7u) << 8u); vY = aY / 8.0; vTile = float(aTile);
}
#endif
#ifdef FRAGMENT
uniform float uDay, uCell;   // uMinLight: common.glsl
uniform sampler2D uMask;
uniform vec3 uMaskO;                   // mask corner relative to the camera's block, mask size in blocks
uniform vec4 uFront, uTrail, uStorm;   // the weather (game/skies.js stormAt)
uniform vec2 uAnchor;                  // relative to the camera's block
uniform vec3 uRainCol;
uniform float uFadeIn;                 // a new tile fading in (dithered: Horizon's fade, render/distant.cpp)
uniform vec4 uFadeOut;                 // a parent under its new children: each quarter as far gone as its child has come
uniform float uTileSize;
uniform vec2 uTileWorld;              // the tile's corner in the world (mod 8192), so the pictures line up with the blocks'
in vec4 vCol;
in vec3 vPos;
in vec3 vRel;
in vec2 vLocal;
in float vY;
flat in uint vFlags;
flat in float vTile;
out vec4 fragColor;
float stormAt(vec2 p) {
  vec2 r = p - uAnchor; float s = uStorm.x;
  if (uTrail.w > 0.5) s = clamp((uTrail.z - dot(r, uTrail.xy)) / uStorm.z + 0.5, 0.0, 1.0);
  if (uFront.w > 0.5) s = max(s, clamp((dot(r, uFront.xy) - uFront.z) / uStorm.z + 0.5, 0.0, 1.0));
  return s * s * (3.0 - 2.0 * s);
}
float bayer4(vec2 p) {   // a 4x4 ordered dither, 1/32 .. 31/32
  ivec2 q = ivec2(mod(p, 4.0));
  int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
  return (float(m[q.y * 4 + q.x]) + 0.5) / 16.0;
}
void main() {
  float th = bayer4(gl_FragCoord.xy);
  if (th > uFadeIn) discard;
  if (uFadeOut != vec4(0.0)) {
    int q = (vLocal.x >= uTileSize * 0.5 ? 1 : 0) + (vLocal.y >= uTileSize * 0.5 ? 2 : 0);
    if (th <= uFadeOut[q]) discard;
  }
  // where a real chunk is drawn, it's drawn instead (chunk by chunk, so while chunks are still loading this fills in)
  vec2 mp = (vPos.xz - uMaskO.xy) / uMaskO.z;
  if (mp.x >= 0.0 && mp.y >= 0.0 && mp.x < 1.0 && mp.y < 1.0 && texture(uMask, mp).r > 0.5) discard;
  // the voxel's texture
  vec3 c;
  if ((vFlags & 8u) != 0u) c = vCol.rgb;   // leaves and the like: their average colour (see-through pixels mean nothing far off)
  else {
    int face = int((vFlags >> 8u) & 7u);
    // one picture per BLOCK, as the real chunks draw them (a coarse voxel 16 blocks across shows 16x16 of them, not one
    // picture stretched over it); where a block is under a pixel, its average colour, so the far land doesn't shimmer
    vec2 wl = vLocal + uTileWorld;
    vec2 uv = face < 2 ? vec2(wl.y, -vY) : face < 4 ? wl : vec2(wl.x, -vY);
    vec2 gx = dFdx(uv), gy = dFdy(uv);
    vec4 t = tileColorGrad(fract(uv), vTile, gx, gy);
    float fp = max(length(gx), length(gy));   // blocks per pixel
    if (fp > 0.3) {
      vec4 avg = (tileColorLod(vec2(0.25, 0.25), vTile, 4.0) + tileColorLod(vec2(0.75, 0.25), vTile, 4.0) + tileColorLod(vec2(0.25, 0.75), vTile, 4.0) + tileColorLod(vec2(0.75, 0.75), vTile, 4.0)) * 0.25;
      t = mix(t, avg, smoothstep(0.3, 0.9, fp));
    }
    if ((vFlags & 4u) != 0u) t.rgb *= vCol.rgb * 1.25;
    else if (vCol.r + vCol.g + vCol.b < 2.99) {   // grass and leaf pixels marked for the tint (alpha 253), as chunk.glsl
      float w = clamp((1.0 - t.a) * 127.5, 0.0, 1.0);
      vec3 tint = vCol.rgb * 1.3;
      if (uSeason.a > 0.0) tint = mix(tint, uSeason.rgb * (dot(tint, vec3(0.299, 0.587, 0.114)) * 1.6), uSeason.a);
      t.rgb *= mix(vec3(1.0), tint, w);
    }
    c = t.rgb;
  }
  c *= vCol.a;
  if ((vFlags & 2u) == 0u) c *= max(uDay, uMinLight);
  float dist = length(vRel);
  if ((vFlags & 1u) != 0u && ((vFlags >> 8u) & 7u) == 2u) {
    // open water, as chunk.glsl draws it: the sea floor darkening it through the water, the sky in it at a low angle
    vec3 v = -vRel / max(dist, 1e-4);
    float fres = pow(1.0 - clamp(v.y, 0.0, 1.0), 3.0);
    c *= 0.8;
    c = mix(c, mix(uSkyColor, uFogColor, 0.35) * max(uDay, uMinLight), fres * 0.45);
  }
  if (uStorm.y > 0.001) {
    float st = stormAt(vPos.xz);
    c *= 1.0 - st * uStorm.y * 0.4;                                        // in the storm's shadow
    c = mix(c, uRainCol, st * uStorm.w * smoothstep(40.0, 420.0, dist));   // rain falling between you and it
  }
  fragColor = vec4(DQ_GRADE(applyFog(c, dist, vec3(0.0))), 1.0);
}
#endif
