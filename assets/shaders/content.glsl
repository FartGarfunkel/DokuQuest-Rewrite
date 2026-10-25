// content.glsl — flat pictures and lines in the world: the map held in your hands or hung in a frame, the
// instruments' live dials, the fishing line and the grappling hook's rope. Positions in model units; uModel maps
// them to render space (camera-relative) like voxel.glsl. A picture's alpha cuts it out.
#include "common.glsl"

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uCamFrac;
uniform vec2 uLight;         // sky, block light 0..1
uniform vec4 uColor;         // multiplies the picture (or is the colour, without one)
uniform vec4 uUV;            // u0, v0, u1, v1 of the picture
uniform float uUseTex;
uniform float uLit;          // 1 = lit like the world, 0 = as painted
uniform sampler2D uTex;

#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
out vec3 vRel;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vRel = w.xyz;
  vUV = mix(uUV.xy, uUV.zw, aUV);
  gl_Position = uViewProj * w;
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
in vec3 vRel;
out vec4 fragColor;
void main() {
  vec4 c = uColor;
  if (uUseTex > 0.5) c *= texture(uTex, vUV);
  if (c.a < 0.35) discard;
  if (uLit > 0.5) c.rgb *= lighting(uLight.x, uLight.y, false);
  vec3 v = vRel - uCamFrac;
  fragColor = vec4(DQ_GRADE(applyFog(c.rgb, length(v), v)), 1.0);
}
#endif
