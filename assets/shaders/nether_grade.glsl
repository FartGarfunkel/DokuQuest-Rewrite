// nether_grade.glsl — Better than Adventure 8.0's colour grade for the Nether (render/nether_grade_layer.cpp), in BTA's
// order: brightness, contrast about mid-grey, exposure, saturation against Rec. 709 luma, then the colour mods.
uniform sampler2D uTex;
uniform float uBrightness, uContrast, uExposure, uSaturation;
uniform vec3 uRgbMod;
uniform sampler2D uDepth;          // the world's depth (heat haze)
uniform float uHaze, uTime, uFar;  // haze amount (BTA: scale * slider * 3), seconds, the render distance in blocks
uniform vec2 uNearFar;

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
void main() {
  vec2 uv = vUV;
  if (uHaze > 0.0) {   // BTA post.fsh: a faint shimmer that grows with distance
    float z = texture(uDepth, vUV).r * 2.0 - 1.0;
    float lin = 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
    float s = clamp((lin / uFar - 0.068) / 0.2, 0.0, 1.0) * uHaze;
    float t = uTime;
    uv += vec2(sin(uv.y * 65.0 - 10.0 * t) + sin(uv.y * 250.0 - 14.0 * t), 2.0 * sin(uv.x * 50.0 + 5.0 * t)) * s * 1e-4;
  }
  vec3 c = texture(uTex, uv).rgb;
  c += uBrightness;
  c = 0.5 + (1.0 + uContrast) * (c - 0.5);
  c *= 1.0 + uExposure;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(l), c, 1.0 + uSaturation);
  c *= uRgbMod;
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#endif
