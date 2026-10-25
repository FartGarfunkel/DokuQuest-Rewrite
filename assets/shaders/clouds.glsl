// clouds.glsl — instanced cloud heaps and wisps (render/clouds.cpp): white tops, pale sides,
// grey-blue undersides, lit by the day, tinted by storms / sunset / lightning, fading into the fog.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aKindShade;      // kind (0 top, 1 side, 2 bottom), shade 0..255
layout(location = 2) in vec4 iPosScale;       // camera-relative position, scale
layout(location = 3) in vec4 iParams;         // y stretch (thunderheads), quarter turns, storm-fog mix, -
layout(location = 4) in vec4 iTint;
uniform mat4 uViewProj;
uniform vec3 uCamFrac;
out vec3 vCol;
out float vDist;
flat out vec4 vTint;
flat out float vFk;
void main() {
  float r = iParams.y * 1.5707963;
  float c = cos(r), s = sin(r);
  vec3 p = aPos * vec3(iPosScale.w, iPosScale.w * iParams.x, iPosScale.w);
  p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
  vec3 w = p + iPosScale.xyz;
  gl_Position = uViewProj * vec4(w, 1.0);
  int k = int(aKindShade.x + 0.5);
  vec3 base = k == 0 ? vec3(1.0) : k == 1 ? vec3(0.933, 0.949, 0.973) : vec3(0.769, 0.808, 0.863);
  vCol = base * (aKindShade.y / 255.0);
  vDist = length(w - uCamFrac);
  vTint = iTint;
  vFk = iParams.z;
}
#endif
#ifdef FRAGMENT
uniform float uDay, uMinLight, uAlpha;
uniform vec2 uCloudFog;
uniform vec3 uFogCol, uStormCol;
in vec3 vCol;
in float vDist;
flat in vec4 vTint;
flat in float vFk;
out vec4 fragColor;
void main() {
  vec3 c = vCol * max(uDay, uMinLight);
  c = mix(c, vTint.rgb, vTint.a);
  vec3 fog = mix(uFogCol, uStormCol, vFk);
  c = mix(c, fog, smoothstep(uCloudFog.x, uCloudFog.y, vDist));
  c = applyFog(c, vDist, vec3(0.0));   // inside the cloud bank or a mist the world closes in
  fragColor = vec4(c, uAlpha);
}
#endif
