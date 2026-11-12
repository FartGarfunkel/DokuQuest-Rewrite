// colour_filter.glsl — the colour-blind filters (Options → Graphics → Colour filter; BTA's accessibility shaders): the
// finished picture is read back and the colours a kind of colour blindness loses are moved to ones it keeps
// (daltonising: Machado et al. 2009's simulation, Fidaner's shift of the lost difference).
uniform sampler2D uTex;
uniform int uKind;   // 1 protanopia (red), 2 deuteranopia (green), 3 tritanopia (blue); 0 none
uniform int uPhoto;  // photo mode's shader (ui/photo_mode.cpp): 1 black & white .. 9 vignette; 0 none
uniform float uAmt;  // its intensity
uniform vec2 uGrade; // Options → Colour Style: saturation, contrast (1, 1 = the picture as drawn)

#ifdef VERTEX
out vec2 vUV;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);   // one triangle over the whole view
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
out vec4 fragColor;
vec3 photo(vec3 c) {
  float l = dot(c, vec3(0.299, 0.587, 0.114));
  vec3 r = c;
  if (uPhoto == 1) r = vec3(l);
  else if (uPhoto == 2) r = vec3(dot(c, vec3(0.393, 0.769, 0.189)), dot(c, vec3(0.349, 0.686, 0.168)), dot(c, vec3(0.272, 0.534, 0.131)));
  else if (uPhoto == 3) r = (mix(vec3(l), c, 1.6) - 0.5) * 1.12 + 0.5;
  else if (uPhoto == 4) r = mix(vec3(l), c, 0.55) * 0.82 + vec3(0.10, 0.09, 0.11);
  else if (uPhoto == 5) r = c * vec3(1.10, 1.0, 0.80) + vec3(0.02, 0.01, 0.0);
  else if (uPhoto == 6) r = c * vec3(0.84, 0.97, 1.14);
  else if (uPhoto == 7) r = floor(c * 5.0 + 0.5) / 5.0;
  else if (uPhoto == 8) {   // a soft glow round the bright parts
    vec2 px = 3.0 / vec2(textureSize(uTex, 0));
    vec3 b = vec3(0.0);
    for (int i = -2; i <= 2; i++) for (int j = -2; j <= 2; j++) b += texture(uTex, vUV + vec2(i, j) * px).rgb;
    b /= 25.0;
    r = mix(c, b, 0.3) + max(b - 0.5, 0.0) * 1.2;
  } else if (uPhoto == 9) {
    float d = distance(vUV, vec2(0.5));
    r = c * (1.0 - smoothstep(0.3, 0.8, d) * 0.9);
  }
  return mix(c, clamp(r, 0.0, 1.0), clamp(uAmt, 0.0, 1.0));
}

void main() {
  vec3 c = texture(uTex, vUV).rgb;
  if (uGrade != vec2(1.0)) {   // the colour style first: saturation against Rec. 709 luma, then contrast about mid-grey
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, uGrade.x);
    c = clamp((c - 0.5) * uGrade.y + 0.5, 0.0, 1.0);
  }
  if (uPhoto > 0) c = photo(c);
  if (uKind == 0) { fragColor = vec4(c, 1.0); return; }
  mat3 sim;   // column-major: these are the rows of Machado's matrices, transposed
  if (uKind == 1) sim = mat3(0.152286, 0.114503, -0.003882, 1.052583, 0.786281, -0.048116, -0.204868, 0.099216, 1.051998);
  else if (uKind == 2) sim = mat3(0.367322, 0.280085, -0.011820, 0.860646, 0.672501, 0.042940, -0.227968, 0.047413, 0.968881);
  else sim = mat3(1.255528, -0.078411, 0.004733, -0.076749, 0.930809, 0.691367, -0.178779, 0.147602, 0.303900);
  vec3 err = c - sim * c;
  vec3 shift = vec3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b);
  if (uKind == 3) shift = vec3(err.r + 0.7 * err.b, err.g + 0.7 * err.b, 0.0);
  fragColor = vec4(clamp(c + shift, 0.0, 1.0), 1.0);
}
#endif
