// be_sky.glsl — Better End's End sky layers (render/be_sky.cpp): a texture (or a flat colour) times a colour, blended
// over the fog-coloured background. No distance fog, like Better End's sky shaders.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
  vUV = aUV;
  gl_Position = uMVP * vec4(aPos, 1.0);
  gl_Position.z = gl_Position.w * 0.9999;   // at the far end of the depth range, behind the world
}
#endif
#ifdef FRAGMENT
uniform sampler2D uTex;
uniform vec4 uColor;
uniform float uUseTex;
in vec2 vUV;
out vec4 fragColor;
void main() {
  vec4 t = uUseTex > 0.5 ? texture(uTex, vUV) : vec4(1.0);
  fragColor = t * uColor;
}
#endif
