// dreamlight.glsl — soft, dreamlike light over the finished picture (render/dreamlight_layer.cpp; Options → Graphics →
// Lighting). After Horizon's Golden Hour shader pack (MIT, github.com/horizon-rust/horizon; credited in docs/CREDITS.md):
// golden light when the sun is low, a soft shoulder for the brightest colours, a little more colour and a light vignette,
// and a halo round the brightest things (the sun, lava, torches at night). On top, ours: a soft-focus glow (the picture
// blurred and laid over itself, lightly, the "Orton" look of dreamy photographs) and shadows lifted a touch toward the
// sky's colour.
uniform sampler2D uTex;
uniform vec2 uRes;          // pixels
uniform float uStrength;    // 1 dreamlike, 1.6 strong
uniform float uDaylight;    // 1 while the sun is up in the Overworld, fading round the horizon
uniform float uSunLow;      // 1 when the sun is on the horizon, 0 from a quarter of the way up
uniform float uRain;
uniform vec3 uLift;         // the shadows' tint (the sky's or the land's haze)

#ifdef VERTEX
out vec2 vUV;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
out vec4 fragColor;
float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 bright(vec2 uv) {
  vec3 c = textureLod(uTex, uv, 0.0).rgb;
  return c * smoothstep(0.72, 1.0, luminance(c));
}
void main() {
  vec3 c = texture(uTex, vUV).rgb;
  vec2 px = vec2(uRes.y / 720.0) / uRes;   // the same look at any resolution
  // the halo round the brightest pixels, and the soft-focus blur: taps on three rings
  vec3 halo = vec3(0.0), soft = vec3(0.0);
  for (int i = 0; i < 12; i++) {
    float a = float(i) * 0.5235988;
    vec2 d = vec2(cos(a), sin(a));
    float r = (i & 1) == 1 ? 7.0 : 3.0;
    halo += bright(vUV + d * r * px);
    soft += textureLod(uTex, vUV + d * (r * 1.6) * px, 0.0).rgb;
    halo += bright(vUV + d.yx * vec2(1.0, -1.0) * 13.0 * px) * 0.5;
  }
  soft /= 12.0;
  float k = uStrength;
  c += halo * (0.32 * k / 10.0);
  // soft focus: the blur screened over the picture, more in its brighter parts
  vec3 screen = 1.0 - (1.0 - c) * (1.0 - soft);
  c = mix(c, screen, 0.22 * k * smoothstep(0.1, 0.7, luminance(soft)));
  // golden light with the sun low
  float golden = 0.4 * mix(0.3, 1.0, uSunLow) * uDaylight * (1.0 - uRain * 0.7) * k;
  c *= mix(vec3(1.0), vec3(1.08, 1.0, 0.86), golden);
  // the shadows lifted a little toward the haze
  c += uLift * (1.0 - smoothstep(0.0, 0.35, luminance(c))) * 0.05 * k;
  // a soft shoulder: the brightest colours roll off instead of clipping (white stays white)
  c = c / (1.0 + 0.25 * c) * 1.25;
  c = mix(vec3(luminance(c)), c, 1.0 + 0.1 * k);
  // a light vignette
  float aspect = uRes.x / uRes.y;
  float dv = length((vUV - 0.5) * vec2(aspect, 1.0)) / length(vec2(aspect, 1.0) * 0.5);
  c *= 1.0 - smoothstep(0.55, 1.1, dv) * 0.22 * min(k, 1.3);
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#endif
