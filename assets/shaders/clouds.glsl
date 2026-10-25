// clouds.glsl — instanced cloud heaps and wisps (render/clouds.cpp): voxel cumulus shaded like a soft
// volume — bright where the sun reaches the top, grey-blue in the shadowed belly, each voxel a touch
// different, with a thin halo (uShell) round the heap so its edges look soft. Lit by the day, tinted by
// storms / sunset / lightning, fading into the fog.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aKindShade;      // kind (0 top, 1 side, 2 bottom), shade 0..255, height up the heap 0..255, face 0..5
layout(location = 2) in vec4 iPosScale;       // camera-relative position, scale
layout(location = 3) in vec4 iParams;         // y stretch (thunderheads), quarter turns, storm-fog mix, -
layout(location = 4) in vec4 iTint;
uniform mat4 uViewProj;
uniform vec3 uCamFrac;
uniform float uShell;                         // 0: the heap itself; > 0: the soft halo round it (grown by this much)
out vec3 vCol;
out float vDist;
out float vH;
out vec3 vN;
out vec3 vW;
out vec3 vV;
flat out vec4 vTint;
flat out float vFk;
void main() {
  float r = iParams.y * 1.5707963;
  float c = cos(r), s = sin(r);
  vec3 a = aPos;
  if (uShell > 0.0) { a.xz *= 1.0 + uShell; a.y = a.y * (1.0 + uShell) - uShell * 4.0; }
  vec3 p = a * vec3(iPosScale.w, iPosScale.w * iParams.x, iPosScale.w);
  p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
  vec3 w = p + iPosScale.xyz;
  gl_Position = uViewProj * vec4(w, 1.0);
  int k = int(aKindShade.x + 0.5);
  vec3 base = k == 0 ? vec3(1.0) : k == 1 ? vec3(0.933, 0.949, 0.973) : vec3(0.769, 0.808, 0.863);
  vCol = base * (aKindShade.y / 255.0);
  vH = aKindShade.z / 255.0;
  int f = int(aKindShade.w + 0.5);
  vec3 n = faceNormal(f);
  vN = vec3(c * n.x + s * n.z, n.y, -s * n.x + c * n.z);
  vW = aPos / 2.0 + vec3(iParams.y * 17.0, 0.0, iPosScale.w * 31.0);   // voxel cells of this heap (2-block voxels)
  vDist = length(w - uCamFrac);
  vV = (w - uCamFrac) / max(vDist, 1e-3);
  vTint = iTint;
  vFk = iParams.z;
}
#endif
#ifdef FRAGMENT
uniform float uDay, uAlpha;   // uMinLight: common.glsl
uniform float uShell;
uniform vec2 uCloudFog;
uniform vec3 uFogCol, uStormCol;
in vec3 vCol;
in float vDist;
in float vH;
in vec3 vN;
in vec3 vW;
in vec3 vV;
flat in vec4 vTint;
flat in float vFk;
out vec4 fragColor;
void main() {
  vec3 N = normalize(vN);
  // light through the heap: the top in full sun, the belly in its own shadow
  float up = smoothstep(-0.05, 0.95, vH);
  vec3 belly = vec3(0.66, 0.71, 0.80), crown = vec3(1.0);
  vec3 c = mix(belly, crown, up);
  float sunUp = clamp(uSunDir.y * 3.0, 0.0, 1.0);
  float lit = 0.84 + 0.16 * max(dot(N, uSunDir), 0.0) * sunUp;
  c *= lit * mix(0.86, 1.0, step(-0.5, N.y));               // undersides a little darker still
  c = mix(c, vCol, 0.25);
  // each voxel of the heap a touch lighter or darker (Cube World's mottled clouds)
  vec3 cell = floor(vW - N * 0.5);
  c *= 0.965 + 0.06 * hash12(cell.xz + cell.y * 7.31);
  c *= max(uDay, uMinLight);
  c = mix(c, vTint.rgb, vTint.a);
  vec3 fog = mix(uFogCol, uStormCol, vFk);
  c = mix(c, fog, smoothstep(uCloudFog.x, uCloudFog.y, vDist));
  c = applyFog(c, vDist, vec3(0.0));   // inside the cloud bank or a mist the world closes in
  float a = uAlpha;
  if (uShell > 0.0) {
    // the halo: clear where you look straight at a face, hazy where faces turn away (the heap's rim)
    float rim = 1.0 - abs(dot(N, vV));
    a *= rim * rim * (0.55 + 0.45 * up);
    if (a < 0.01) discard;
  }
  fragColor = vec4(DQ_GRADE(c), a);
}
#endif
