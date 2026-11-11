// weather.glsl — weather particles (render/weather_draw.cpp), one instance each: rain stretched along its motion
// relative to the camera, snow and dust facing the camera, fog sheets lying flat or standing up, ripples flat on the
// water. After Particle Rain's CustomParticle render modes (pigcart, MIT). Lit like the blocks round them, fogged.
#include "common.glsl"
#ifdef VERTEX
layout(location = 0) in vec4 iPosSize;   // camera-origin-relative position, half size
layout(location = 1) in vec4 iVelRoll;   // velocity relative to the camera (blocks per tick), roll
layout(location = 2) in vec4 iUV;        // u0, v0, u1, v1
layout(location = 3) in vec4 iTint;      // rgb, alpha
layout(location = 4) in vec4 iMisc;      // sky light 0..1, block light 0..1, rotation mode, stretch (rain) / 1
uniform mat4 uViewProj;
uniform vec3 uCamFrac, uCamRight, uCamUp;
out vec2 vUV;
out vec4 vTint;
out vec2 vLight;
out float vDist;
flat out float vOpaque;
vec2 rot2(vec2 c, float a) { float s = sin(a), k = cos(a); return vec2(c.x * k - c.y * s, c.x * s + c.y * k); }
void main() {
  vec2 corner = vec2(float(gl_VertexID & 1) * 2.0 - 1.0, float((gl_VertexID >> 1) & 1) * 2.0 - 1.0);
  vec3 c = iPosSize.xyz;
  float size = iPosSize.w;
  int mode = int(iMisc.z + 0.5) & 7;
  vec3 toCam = uCamFrac - c;
  vec3 A = uCamRight, B = uCamUp;
  vec2 k = corner;
  if (mode == 0) {   // along its motion (relative to you), turned about that line to face you
    vec3 v = iVelRoll.xyz;
    vec3 d = dot(v, v) > 1e-10 ? normalize(v) : vec3(0.0, -1.0, 0.0);
    vec3 side = cross(d, toCam);
    A = dot(side, side) > 1e-10 ? normalize(side) : uCamRight;
    B = -d;
    k.y *= iMisc.w;   // squished as it lands, stretched with speed
  } else if (mode == 2) {   // upright, turned to face you
    vec3 h = vec3(toCam.x, 0.0, toCam.z);
    A = dot(h, h) > 1e-8 ? normalize(cross(vec3(0.0, 1.0, 0.0), h)) : uCamRight;
    B = vec3(0.0, 1.0, 0.0);
    k = rot2(k, iVelRoll.w);
  } else if (mode == 5) {   // flat against a block's side (face 1,2: an x side; 3,4: a z side), a hair off it
    int face = (int(iMisc.z + 0.5) >> 4) & 7;
    vec3 n = face == 1 ? vec3(-1.0, 0.0, 0.0) : face == 2 ? vec3(1.0, 0.0, 0.0) : face == 3 ? vec3(0.0, 0.0, -1.0) : vec3(0.0, 0.0, 1.0);
    A = face <= 2 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    B = vec3(0.0, 1.0, 0.0);
    c += n * 0.012;
  } else if (mode == 3) {   // lying flat
    A = vec3(1.0, 0.0, 0.0); B = vec3(0.0, 0.0, 1.0);
    k = rot2(k, iVelRoll.w);
  } else {   // facing the camera (with its roll)
    k = rot2(k, iVelRoll.w);
  }
  vec3 p = c + (A * k.x + B * k.y) * size;
  gl_Position = uViewProj * vec4(p, 1.0);
  vUV = vec2(mix(iUV.x, iUV.z, corner.x * 0.5 + 0.5), mix(iUV.w, iUV.y, corner.y * 0.5 + 0.5));
  vTint = iTint;
  vLight = iMisc.xy;
  vDist = length(p - uCamFrac);
  vOpaque = float((int(iMisc.z + 0.5) & 8) != 0);
}
#endif
#ifdef FRAGMENT
uniform sampler2D uAtlas;
in vec2 vUV;
in vec4 vTint;
in vec2 vLight;
in float vDist;
flat in float vOpaque;
out vec4 fragColor;
void main() {
  vec4 t = texture(uAtlas, vUV);
  float a = t.a * vTint.a;
  if (vOpaque > 0.5) { if (t.a < 0.5) discard; a = 1.0; }
  else if (a < 0.01) discard;
  // water and snow catch what light there is: under a storm's dark sky they still read paler than the clouds behind
  vec3 c = t.rgb * vTint.rgb * max(lighting(vLight.x, vLight.y, false), vec3(0.5 * vLight.x + 0.08));
  fragColor = vec4(DQ_GRADE(applyFog(c, vDist, vec3(0.0))), a);
}
#endif
