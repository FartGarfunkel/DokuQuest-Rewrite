// colour_filter.glsl — the colour-blind filters (Options → Graphics → Colour filter; BTA's accessibility shaders): the
// finished picture is read back and the colours a kind of colour blindness loses are moved to ones it keeps
// (daltonising: Machado et al. 2009's simulation, Fidaner's shift of the lost difference).
uniform sampler2D uTex;
uniform int uKind;   // 1 protanopia (red), 2 deuteranopia (green), 3 tritanopia (blue)

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
void main() {
  vec3 c = texture(uTex, vUV).rgb;
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
