// voxel.glsl — voxel models: items (held, dropped, icons), the first-person arm, and later creatures and
// characters. Each vertex carries its voxel's colour and baked ambient occlusion; positions are in grid
// units and uModel maps them to render space (camera-relative, like the terrain).
#include "common.glsl"

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uCamFrac;       // camera position inside its block (fog distance)
uniform vec2 uLight;         // sky, block light 0..1 where the model is
uniform vec4 uTint;          // rgb, amount (hurt flash)
uniform vec2 uGlint;         // enchantment glint amount, time
uniform float uNeutral;      // 1 = icon/preview lighting (fixed light, no fog)
uniform float uPass;         // 0 all, 1 all but glass, 2 only glass (assets/voxel.cpp VoxelRenderer::draw)
uniform vec4 uLiquidXZ;      // a vessel's liquid: its box in grid units (x0, x1, z0, z1)
uniform vec2 uLiquidY;       // (y0, y1); y1 == y0: no liquid
uniform vec3 uSlosh;         // its surface's slope (x, z) as it's carried, time

#ifdef VERTEX
layout(location = 0) in vec3 aPos;
layout(location = 1) in uint aNA;
layout(location = 2) in vec4 aCol;

out vec4 vCol;
out vec3 vRel;
out vec3 vN;
out float vAO;
out vec3 vLocal;

const vec3 NRM[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));

void main() {
  vec3 pos = aPos;
  if (abs(aCol.a * 255.0 - 252.0) < 0.5 && uLiquidY.y > uLiquidY.x) {
    // the liquid's surface leans with the slosh (the top most, its floor not at all), and idly wobbles a little
    float k = clamp((pos.y - uLiquidY.x) / (uLiquidY.y - uLiquidY.x), 0.0, 1.0);
    vec2 c = vec2(uLiquidXZ.x + uLiquidXZ.y, uLiquidXZ.z + uLiquidXZ.w) * 0.5;
    vec2 s = uSlosh.xy + vec2(sin(uSlosh.z * 2.1), cos(uSlosh.z * 1.7)) * 0.03;
    pos.y += dot(pos.xz - c, s) * k * k;
  }
  vec4 w = uModel * vec4(pos, 1.0);
  vRel = w.xyz;
  vN = normalize(mat3(uModel) * NRM[int(aNA & 7u)]);
  vAO = float((aNA >> 3) & 3u) / 3.0;
  vCol = aCol;
  vLocal = pos;
  gl_Position = uViewProj * w;
}
#endif

#ifdef FRAGMENT
in vec4 vCol;
in vec3 vRel;
in vec3 vN;
in float vAO;
in vec3 vLocal;
out vec4 fragColor;

void main() {
  vec3 n = normalize(vN);
  // the world's face shading (top 1, sides 0.8 / 0.65, bottom 0.5), smooth as a model turns
  float shade = n.x * n.x * 0.8 + n.z * n.z * 0.65 + n.y * n.y * (n.y > 0.0 ? 1.0 : 0.5);
  float a8 = vCol.a * 255.0;
  bool glass = abs(a8 - 253.0) < 0.5, liquid = abs(a8 - 252.0) < 0.5;
  if ((uPass > 0.5 && uPass < 1.5 && glass) || (uPass > 1.5 && !glass)) discard;
  bool glow = a8 > 253.5 && a8 < 254.7;
  vec3 c = vCol.rgb;
  float ao = mix(0.55, 1.0, vAO);
  vec3 lit;
  if (uNeutral > 0.5) {
    float d = max(0.0, dot(n, uSunDir));
    lit = vec3(0.62 + 0.38 * d) * shade / max(shade, 0.65);
  } else {
    lit = lightingAt(uLight.x, uLight.y, glow, vRel, n) * shade;
  }
  c *= glow ? vec3(1.0) : lit * ao;
  if (uGlint.x > 0.0) {
    float g = fract((vLocal.x + vLocal.y * 0.7 - vLocal.z * 0.4) * 0.045 - uGlint.y * 0.35);
    g = smoothstep(0.0, 0.25, g) * (1.0 - smoothstep(0.25, 0.5, g));
    c += vec3(0.45, 0.22, 0.75) * g * uGlint.x;
  }
  c = mix(c, uTint.rgb, uTint.a);
  if (uNeutral < 0.5) {
    vec3 v = vRel - uCamFrac;
    c = applyFog(c, length(v), v);
  }
  float alpha = 1.0;
  if (glass) {   // clear glass: faint face-on, stronger at the rim, a sheen on top
    vec3 v = normalize(-(vRel - uCamFrac));
    float rim = 1.0 - abs(dot(n, v));
    alpha = (uNeutral > 0.5 ? 0.45 : 0.22) + 0.45 * rim * rim;
    c += vec3(0.12) * rim;
  }
  if (liquid) c *= 1.12;   // (a little bright through the glass)
  if (liquid && vCol.r > 0.9 && vCol.g < 0.65 && vCol.b < 0.3) c = mix(c, vCol.rgb * 1.15, 0.75);   // lava glows
  fragColor = vec4(DQ_GRADE(c), alpha);
}
#endif
