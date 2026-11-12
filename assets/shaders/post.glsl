// post.glsl — the built-in shader levels (render/post_layer.cpp GradeLayer; Settings > Shaders: low, medium, high, ultra), run
// over the finished picture:
//   0 the bright parts, at half size          4 a step down the bloom chain (13 taps)     5 a step back up (a tent, added)
//   6 the picture's brightness (log), small    7 the eyes adjusting toward it (1x1, kept from frame to frame)
//   3 all of it together: the wobble under water, sharpening, the bloom, sun rays, a lens flare, the eyes' exposure, a film
//     grade, a vignette and a little noise against banding.
// A shader pack may replace this file like any other.
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
uniform sampler2D uSrc;      // the frame (steps 0, 3, 6), the level being stepped (4, 5), last frame's exposure (7)
uniform sampler2D uBloom;    // step 3: the bloom; step 7: the brightness (its smallest mip is the average)
uniform sampler2D uExp;      // step 3: the exposure (1x1)
uniform int uStep;
uniform vec2 uTexel;         // one texel of uSrc
uniform float uBloomK, uRaysK, uSharpK, uGradeK, uVignetteK, uThreshold, uFlareK, uWarp, uTime, uAspect, uExpOn, uAdapt, uExpMax, uLumLod;
uniform vec2 uSunUV;         // the sun on the screen (0..1), if it's anywhere near
uniform vec3 uSunTint;
uniform int uRaySteps;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
  if (uStep == 0) {   // the bright parts: four texels of the frame into one at half size
    vec3 c = vec3(0.0);
    for (int i = 0; i < 4; i++) {
      vec3 s = texture(uSrc, vUV + (vec2(i & 1, i >> 1) - 0.5) * uTexel).rgb;
      c += s * smoothstep(uThreshold, 1.0, luma(s));
    }
    fragColor = vec4(c * 0.25, 1.0);
    return;
  }
  if (uStep == 4) {   // down a level: the 13-tap filter (no fireflies, no blockiness)
    vec2 t = uTexel;
    vec3 a = texture(uSrc, vUV + t * vec2(-2.0, 2.0)).rgb, b = texture(uSrc, vUV + t * vec2(0.0, 2.0)).rgb, c = texture(uSrc, vUV + t * vec2(2.0, 2.0)).rgb;
    vec3 d = texture(uSrc, vUV + t * vec2(-2.0, 0.0)).rgb, e = texture(uSrc, vUV).rgb, f = texture(uSrc, vUV + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(uSrc, vUV + t * vec2(-2.0, -2.0)).rgb, h = texture(uSrc, vUV + t * vec2(0.0, -2.0)).rgb, i = texture(uSrc, vUV + t * vec2(2.0, -2.0)).rgb;
    vec3 j = texture(uSrc, vUV + t * vec2(-1.0, 1.0)).rgb, k = texture(uSrc, vUV + t * vec2(1.0, 1.0)).rgb;
    vec3 l = texture(uSrc, vUV + t * vec2(-1.0, -1.0)).rgb, m = texture(uSrc, vUV + t * vec2(1.0, -1.0)).rgb;
    fragColor = vec4(e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125, 1.0);
    return;
  }
  if (uStep == 5) {   // back up a level: a 3x3 tent of the smaller one, added onto this one (blending)
    vec2 t = uTexel;
    vec3 c = texture(uSrc, vUV).rgb * 4.0;
    c += (texture(uSrc, vUV + vec2(t.x, 0.0)).rgb + texture(uSrc, vUV - vec2(t.x, 0.0)).rgb + texture(uSrc, vUV + vec2(0.0, t.y)).rgb + texture(uSrc, vUV - vec2(0.0, t.y)).rgb) * 2.0;
    c += texture(uSrc, vUV + t).rgb + texture(uSrc, vUV - t).rgb + texture(uSrc, vUV + vec2(t.x, -t.y)).rgb + texture(uSrc, vUV + vec2(-t.x, t.y)).rgb;
    fragColor = vec4(c / 16.0, 1.0);
    return;
  }
  if (uStep == 6) {   // how bright the picture is, as a log (its mips average it), the middle counting most
    vec3 c = texture(uSrc, vUV).rgb;
    float w = 1.0 - 0.6 * length(vUV - 0.5);
    fragColor = vec4(log(max(luma(c), 0.0) + 0.02) * w, w, 0.0, 1.0);
    return;
  }
  if (uStep == 7) {   // the eyes: toward the exposure this brightness wants, a little each frame
    vec2 lw = textureLod(uBloom, vec2(0.5), uLumLod).rg;
    float avg = exp(lw.x / max(lw.y, 1e-3)) - 0.02;
    float want = clamp(pow(0.36 / max(avg + 0.03, 1e-3), 0.55), 0.82, uExpMax);   // daylight ~1, a dark cave up to the cap
    float prev = texture(uSrc, vec2(0.5)).r;
    if (prev <= 0.0) prev = want;
    fragColor = vec4(vec3(mix(prev, want, uAdapt)), 1.0);
    return;
  }
  // ---- 3: all of it together ----
  vec2 uv = vUV;
  if (uWarp > 0.0) {   // under water (or in lava): the view wavers
    uv += uWarp * vec2(sin(uv.y * 23.0 + uTime * 2.3) + 0.5 * sin(uv.y * 41.0 - uTime * 3.1), cos(uv.x * 19.0 + uTime * 1.9) + 0.5 * cos(uv.x * 37.0 + uTime * 2.7)) * 0.0025;
    uv = clamp(uv, uTexel, 1.0 - uTexel);
  }
  vec3 c = texture(uSrc, uv).rgb;
  if (uSharpK > 0.0) {   // sharpen: a touch of the difference from the neighbours
    vec3 n = texture(uSrc, uv + vec2(uTexel.x, 0.0)).rgb + texture(uSrc, uv - vec2(uTexel.x, 0.0)).rgb
           + texture(uSrc, uv + vec2(0.0, uTexel.y)).rgb + texture(uSrc, uv - vec2(0.0, uTexel.y)).rgb;
    c = max(c + (c - n * 0.25) * uSharpK, 0.0);
  }
  vec3 b = uBloomK > 0.0 ? texture(uBloom, uv).rgb : vec3(0.0);
  c += b * uBloomK;
  // sun rays: the bright parts smeared out from the sun across the screen (only when it's in or near the view)
  if (uRaysK > 0.0 && uRaySteps > 0) {
    vec2 d = (uv - uSunUV) / float(uRaySteps) * 0.85;
    vec2 p = uv; float decay = 1.0; vec3 r = vec3(0.0);
    for (int i = 0; i < 48; i++) {
      if (i >= uRaySteps) break;
      p -= d;
      r += texture(uBloom, p).rgb * decay;
      decay *= 0.94;
    }
    c += r / float(uRaySteps) * uRaysK * uSunTint;
  }
  // a lens flare: ghosts strung along the line from the sun through the middle, and a glint of a star on the sun, as
  // strong as the sun is in view (how bright the bloom is where the sun is)
  if (uFlareK > 0.0) {
    float vis = smoothstep(0.18, 0.7, luma(texture(uBloom, uSunUV).rgb)) * uFlareK;
    if (vis > 0.001) {
      vec2 axis = vec2(0.5) - uSunUV;
      vec3 fl = vec3(0.0);
      const float POS[5] = float[5](0.42, 0.7, 1.15, 1.45, 1.85);
      const float RAD[5] = float[5](0.035, 0.07, 0.025, 0.11, 0.05);
      for (int i = 0; i < 5; i++) {
        vec2 q = uv - (uSunUV + axis * POS[i]);
        q.x *= uAspect;
        float r = length(q) / RAD[i];
        float disc = smoothstep(1.0, 0.75, r) * (0.55 + 0.45 * smoothstep(0.4, 1.0, r));   // brighter at the rim
        vec3 col = i == 0 ? vec3(1.0, 0.75, 0.45) : i == 1 ? vec3(0.45, 0.8, 1.0) : i == 2 ? vec3(1.0, 0.55, 0.85) : i == 3 ? vec3(0.5, 1.0, 0.6) : vec3(0.8, 0.7, 1.0);
        fl += col * disc * 0.06;
      }
      vec2 s = uv - uSunUV; s.x *= uAspect;
      float sd = length(s), ang = atan(s.y, s.x);
      fl += uSunTint * pow(max(0.0, 1.0 - sd / 0.22), 3.0) * (0.35 + 0.65 * pow(abs(cos(ang * 3.0)), 24.0)) * 0.35;   // the star
      c += fl * vis;
    }
  }
  if (uExpOn > 0.0) c *= texture(uExp, vec2(0.5)).r;   // the eyes, adjusted to the dark of a cave or the glare of the snow
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
  c += (hash(gl_FragCoord.xy) + hash(gl_FragCoord.yx + 17.0) - 1.0) / 255.0;   // a little noise: no banding in the gradients
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#endif
