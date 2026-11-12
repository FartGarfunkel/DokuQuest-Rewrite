// world_ao.glsl — shading in the corners (Settings > Shaders: Ultra; render/world_fx.cpp), from the depth of the solid scene
// before the water and the hand go in: where the ground meets a wall, under eaves, round the feet of things.
//   0: at half size, how much of the half-sphere over each point the scene closes off
//   1: blurred (4x4) and laid over the picture (multiplied: blending DST_COLOR, ZERO), fading out with distance
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
uniform sampler2D uDepth;    // the solid scene's depth
uniform sampler2D uAO;       // step 1: the AO at half size
uniform int uStep;
uniform mat4 uProj, uInvProj;
uniform vec2 uTexel;         // one texel of what's read
uniform float uRadius, uStrength, uFadeFar;

vec3 viewPos(vec2 uv) {
  float d = texture(uDepth, uv).r;
  vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }   // interleaved gradient noise

void main() {
  if (uStep == 0) {
    float d = texture(uDepth, vUV).r;
    if (d >= 0.99999) { fragColor = vec4(1.0); return; }   // the sky
    vec3 p = viewPos(vUV);
    if (-p.z > uFadeFar) { fragColor = vec4(1.0); return; }
    // the normal from the nearer of each pair of neighbours (an edge doesn't bend it)
    vec3 pl = viewPos(vUV - vec2(uTexel.x, 0.0)), pr = viewPos(vUV + vec2(uTexel.x, 0.0));
    vec3 pd = viewPos(vUV - vec2(0.0, uTexel.y)), pu = viewPos(vUV + vec2(0.0, uTexel.y));
    vec3 dx = abs(pr.z - p.z) < abs(p.z - pl.z) ? pr - p : p - pl;
    vec3 dy = abs(pu.z - p.z) < abs(p.z - pd.z) ? pu - p : p - pd;
    vec3 n = normalize(cross(dx, dy));
    if (dot(n, p) > 0.0) n = -n;
    // a tangent frame turned by noise, twelve samples in the half-sphere, nearer ones more
    float a = ign(gl_FragCoord.xy) * 6.2831853;
    vec3 rv = vec3(cos(a), sin(a), 0.0);
    vec3 t = normalize(rv - n * dot(rv, n)), b = cross(n, t);
    float occ = 0.0;
    const int N = 12;
    for (int i = 0; i < N; i++) {
      float fi = float(i);
      float r = (fi + 0.5) / float(N);
      float th = fi * 2.39996323;   // the golden angle
      float h = sqrt(1.0 - r);      // spread over the half-sphere
      vec3 s = (t * cos(th) * sqrt(r) + b * sin(th) * sqrt(r) + n * h) * uRadius * mix(0.25, 1.0, r * r);
      vec3 q = p + s;
      vec4 c = uProj * vec4(q, 1.0);
      vec2 quv = c.xy / c.w * 0.5 + 0.5;
      if (quv.x < 0.0 || quv.y < 0.0 || quv.x > 1.0 || quv.y > 1.0) continue;
      float sz = viewPos(quv).z;
      float range = smoothstep(0.0, 1.0, uRadius / max(abs(p.z - sz), 1e-3));
      occ += (sz >= q.z + 0.03 ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occ / float(N);
    fragColor = vec4(vec3(ao), 1.0);
    return;
  }
  // 1: blur and lay it over the picture
  float s = 0.0;
  for (int y = -2; y < 2; y++) for (int x = -2; x < 2; x++) s += texture(uAO, vUV + (vec2(x, y) + 0.5) * uTexel).r;
  s /= 16.0;
  float d = texture(uDepth, vUV).r;
  float fade = d >= 0.99999 ? 0.0 : 1.0 - smoothstep(uFadeFar * 0.6, uFadeFar, -viewPos(vUV).z);
  float ao = mix(1.0, s, uStrength * fade);
  fragColor = vec4(vec3(ao), 1.0);
}
#endif
