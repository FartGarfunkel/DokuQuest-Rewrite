// spellfx.glsl — spell sparks, motes, glyphs and bubbles (render/magic_fx.cpp): camera-facing quads (or flat on
// the ground) whose look is drawn here like the original's painted tiles (game/magic.js mana_* glows: a white-hot
// middle fading out through the spell's colour; brewing.js fxmote_* discs and brew_bubble rings; enchanting.js
// glyphs). Positions are camera-relative, like the terrain.
#include "common.glsl"

uniform mat4 uViewProj;
uniform vec3 uRight;
uniform vec3 uUp;

#ifdef VERTEX
layout(location = 0) in vec3 aPos;      // centre (render space)
layout(location = 1) in vec3 aCorner;   // x, y in -1..1; z = size
layout(location = 2) in vec4 aCol;
layout(location = 3) in vec2 aShape;    // shape, flat (1 = lying on the ground)

out vec2 vUV;
out vec4 vCol;
flat out int vShape;

void main() {
  vec3 r = aShape.y > 0.5 ? vec3(1, 0, 0) : uRight;
  vec3 u = aShape.y > 0.5 ? vec3(0, 0, 1) : uUp;
  vec3 p = aPos + (r * aCorner.x + u * aCorner.y) * aCorner.z;
  vUV = aCorner.xy;
  vCol = aCol;
  vShape = int(aShape.x + 0.5);
  gl_Position = uViewProj * vec4(p, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
in vec4 vCol;
flat in int vShape;
out vec4 fragColor;

void main() {
  float d = length(vUV);
  vec4 c;
  if (vShape == 0 || vShape == 4) {            // a soft glow (4: a flame, warmer in the middle)
    if (d >= 1.0) discard;
    vec3 hot = vShape == 4 ? vec3(1.0, 0.95, 0.6) : vec3(1.0);
    c = vec4(mix(hot, vCol.rgb, clamp(d * 1.6, 0.0, 1.0)), pow(1.0 - d, 1.3));
  } else if (vShape == 1) {                    // an effect mote: a disc, brighter in the middle
    float dd = d * 7.5;
    if (dd >= 6.0) discard;
    c = vec4(vCol.rgb * (1.25 - dd * 0.06), dd < 4.0 ? 1.0 : 0.63);
  } else if (vShape == 2) {                    // a bubble: a thin bright ring
    float dd = d * 7.5;
    if (dd >= 5.5 && dd < 7.5) discard;
    if (dd >= 7.5) discard;
    c = dd > 3.8 ? vec4(0.91, 0.96, 1.0, 0.86) : vec4(0.78, 0.88, 1.0, 0.27);
  } else if (vShape >= 10) {                   // a glyph (10 + which of six): strokes on a little grid
    vec2 g = floor((vUV * 0.5 + 0.5) * 8.0);
    float h = fract(sin(dot(g, vec2(12.9898, 78.233)) + float(vShape - 10) * 91.0) * 43758.5453);
    bool stroke = (g.x == 3.0 || g.y == 4.0 || abs(g.x - g.y) < 0.5) && h > 0.35 && g.x > 0.0 && g.x < 7.0 && g.y > 0.0 && g.y < 7.0;
    if (!stroke) discard;
    c = vec4(vCol.rgb, 1.0);
  } else {                                     // 5: a crumb of a block
    if (max(abs(vUV.x), abs(vUV.y)) > 0.9) discard;
    c = vec4(vCol.rgb, 1.0);
  }
  c.a *= vCol.a;
  fragColor = c;
}
#endif
