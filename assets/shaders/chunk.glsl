// chunk.glsl — terrain sections. Every block face is a 32x32 grid of micro-voxels: the colour of
// each comes from the tile array (one layer per tile), its depth from the height array.
//  MICRO_VOXELS 0: flat   1: shaded micro-voxels   2: relief (parallax) micro-voxels near the camera
#include "common.glsl"

#ifdef VERTEX
layout(location = 0) in vec3 aPos;      // 1/32 block units, section-relative
layout(location = 1) in uint aLayer;
layout(location = 2) in vec2 aUV;       // 1/32 tile units
layout(location = 3) in uvec2 aNL;      // nf, light
layout(location = 4) in vec3 aTint;
layout(location = 5) in uint aMat;

uniform mat4 uViewProj;
uniform vec3 uOrigin;        // section origin relative to the camera's integer block
uniform vec3 uWorldOrigin;   // camera's integer block, wrapped (for wind/wave phase)

out vec3 vUV;
out vec3 vTint;
out vec2 vLight;
out float vAO;
out vec3 vRel;               // position relative to the camera block
flat out int vNormal;
flat out uint vMat;

void main() {
  vec3 p = aPos / 32.0;
  uint nf = aNL.x;
  int n = int(nf & 7u);
  uint ao = (nf >> 3) & 3u;
  uint wave = nf >> 5;
  vec3 rel = uOrigin + p;
  vec3 world = rel + uWorldOrigin;
  rel += waveOffset(wave, world, p);
  vRel = rel;
  vUV = vec3(aUV / 32.0, float(aLayer));
  vTint = aTint;
  vLight = vec2(float(aNL.y >> 4), float(aNL.y & 15u)) / 15.0;
  vAO = float(ao) / 3.0;
  vNormal = n;
  vMat = aMat;
  gl_Position = uViewProj * vec4(rel, 1.0);
}
#endif

#ifdef FRAGMENT
uniform vec3 uCamFrac;       // camera position inside its block
uniform int uPass;           // 0 opaque, 1 cutout, 2 translucent
in vec3 vUV;
in vec3 vTint;
in vec2 vLight;
in float vAO;
in vec3 vRel;
flat in int vNormal;
flat in uint vMat;
out vec4 fragColor;

void main() {
  vec3 uv = vec3(fract(vUV.xy), vUV.z);
  vec2 gdx = dFdx(vUV.xy), gdy = dFdy(vUV.xy);
  vec4 col = tileColorGrad(uv.xy, uv.z, gdx, gdy);
  if (uPass == 1 && col.a < 0.5) discard;
  vec3 nrm = faceNormal(vNormal);
  float relief = 0.0;
#if MICRO_VOXELS >= 1
  if ((vMat & 16u) == 0u) {
    float h = tileHeightAt(uv.xy, uv.z).r;
    relief = h;
#if MICRO_VOXELS >= 2
    float dist = length(vRel - uCamFrac);
    if (dist < uMicroDist) {
      vec2 g = microVoxelEdge(uv, vNormal);
      relief = mix(relief, h + g.x, 1.0 - smoothstep(uMicroDist * 0.6, uMicroDist, dist));
    }
#endif
  }
#endif
  vec3 tint = (vTint.r > 0.99 && vTint.g > 0.99 && vTint.b > 0.99) ? vec3(1.0) : vTint;
  col.rgb *= tint;
  float shade = faceShade(vNormal) * mix(0.55, 1.0, vAO) * (1.0 - relief * 0.38);
  vec3 lit = lighting(vLight.x, vLight.y, (vMat & 1u) != 0u);
  vec3 c = col.rgb * shade * lit;
  if ((vMat & 2u) != 0u && (vMat & 32u) == 0u) {   // water
    c = waterColor(c, vRel - uCamFrac, nrm);
    col.a = 0.72;
  }
  float d = length(vRel - uCamFrac);
  c = applyFog(c, d, vRel - uCamFrac);
  fragColor = vec4(c, uPass == 2 ? col.a : 1.0);
}
#endif
