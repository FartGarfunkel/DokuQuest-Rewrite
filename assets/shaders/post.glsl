// post.glsl — the built-in shader levels (render/post_layer.cpp; Settings > Shaders: low, medium, high, ultra),
// run over the finished picture: a bright pass and blur for the bloom, then one pass that puts it together with sun
// rays, a film grade, a vignette and a little sharpening. A shader pack may replace this file like any other.
#ifdef VERTEX
out vec2 vUV;
void main() {   // one triangle over the screen
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif
#ifdef FRAGMENT
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSrc;      // the frame (step 0, 3) or the bloom being blurred (1, 2)
uniform sampler2D uBloom;    // step 3: the blurred bright parts
uniform int uStep;           // 0 bright pass, 1 blur across, 2 blur down, 3 put together
uniform vec2 uTexel;         // one texel of uSrc
uniform float uBloomK, uRaysK, uSharpK, uGradeK, uVignetteK, uThreshold;
uniform vec2 uSunUV;         // the sun on the screen (0..1), if it's anywhere near
uniform vec3 uSunTint;
uniform int uRaySteps;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
  if (uStep == 0) {
    // the bright parts, four texels at once (this runs at a quarter of the size)
    vec3 c = vec3(0.0);
    for (int i = 0; i < 4; i++) {
      vec2 o = vec2(i & 1, i >> 1) - 0.5;
      vec3 s = texture(uSrc, vUV + o * uTexel).rgb;
      c += s * smoothstep(uThreshold, 1.0, luma(s));
    }
    fragColor = vec4(c * 0.25, 1.0);
    return;
  }
  if (uStep == 1 || uStep == 2) {
    vec2 d = uStep == 1 ? vec2(uTexel.x, 0.0) : vec2(0.0, uTexel.y);
    const float w[5] = float[5](0.227, 0.195, 0.122, 0.054, 0.016);
    vec3 c = texture(uSrc, vUV).rgb * w[0];
    for (int i = 1; i < 5; i++) c += (texture(uSrc, vUV + d * float(i) * 1.5).rgb + texture(uSrc, vUV - d * float(i) * 1.5).rgb) * w[i];
    fragColor = vec4(c, 1.0);
    return;
  }
  vec3 c = texture(uSrc, vUV).rgb;
  // sharpen: a touch of the difference from the neighbours
  if (uSharpK > 0.0) {
    vec3 n = texture(uSrc, vUV + vec2(uTexel.x, 0.0)).rgb + texture(uSrc, vUV - vec2(uTexel.x, 0.0)).rgb
           + texture(uSrc, vUV + vec2(0.0, uTexel.y)).rgb + texture(uSrc, vUV - vec2(0.0, uTexel.y)).rgb;
    c = max(c + (c - n * 0.25) * uSharpK, 0.0);
  }
  vec3 b = uBloomK > 0.0 ? texture(uBloom, vUV).rgb : vec3(0.0);
  c += b * uBloomK;
  // sun rays: the bright parts smeared out from the sun across the screen (only when it's in or near the view)
  if (uRaysK > 0.0 && uRaySteps > 0) {
    vec2 d = (vUV - uSunUV) / float(uRaySteps) * 0.85;
    vec2 p = vUV; float decay = 1.0; vec3 r = vec3(0.0);
    for (int i = 0; i < 48; i++) {
      if (i >= uRaySteps) break;
      p -= d;
      r += texture(uBloom, p).rgb * decay;
      decay *= 0.94;
    }
    c += r / float(uRaySteps) * uRaysK * uSunTint;
  }
  // the grade: a gentle filmic curve, a little more colour, warm lights and cool shadows
  if (uGradeK > 0.0) {
    vec3 t = mix(c, 0.8 + 0.2 * (1.0 - exp(-(c - 0.8) * 5.0)), step(0.8, c));   // a soft shoulder (bloom can push over 1)
    t = mix(t, smoothstep(0.0, 1.0, t), 0.35);          // a light S-curve
    float l = luma(t);
    t = mix(vec3(l), t, 1.12);                           // a little more colour
    t *= mix(vec3(0.97, 0.99, 1.04), vec3(1.03, 1.0, 0.96), smoothstep(0.2, 0.8, l));
    c = mix(c, t, uGradeK);
  }
  if (uVignetteK > 0.0) {
    vec2 q = vUV - 0.5;
    c *= 1.0 - uVignetteK * smoothstep(0.25, 0.85, dot(q, q) * 2.2);
  }
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#endif
