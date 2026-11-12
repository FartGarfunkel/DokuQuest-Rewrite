// render/be_sky.cpp — the End's sky as Better End 3.2.5 draws it (BetterEndSkyRenderer; github.com/quiqueck/BetterEnd,
// MIT; docs/CREDITS.md). Over the fog-coloured background, in this order: the horizon band (nebula_1, a cylinder
// turning with time), two sets of nebula clouds (nebula_2, nebula_3, at twice the stars' distance), two sets of
// textured stars (stars.png, four sizes), the fog band (fog.png, as thick as the land's fog density is over 1), and
// two sets of point stars. Each set turns about its own axis, once in 360000 ticks (x1, x2 or x3). Star places
// come from java.util.Random with Better End's seeds, so the sky is the same one. Geometry is camera-centred at
// 1/20 of Better End's distances (the angles are the same).
#include "render/be_sky.h"
#include "assets/tileset.h"
#include "common/fs.h"
#include <cmath>

namespace dq {

namespace {
// java.util.Random (48-bit LCG)
struct JRand {
  u64 s;
  explicit JRand(i64 seed) : s(((u64)seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1)) {}
  int next(int bits) { s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1); return (int)(i64)(s >> (48 - bits)); }
  double nextDouble() { return (double)(((i64)next(26) << 27) + next(27)) * (1.0 / (double)(1LL << 53)); }
  int nextInt(int bound) {
    if ((bound & -bound) == bound) return (int)(((i64)bound * (i64)next(31)) >> 31);
    int bits, val;
    do { bits = next(31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
    return val;
  }
};
constexpr float SCALE = 0.05f;   // Better End draws at 100 / 200 blocks; we draw at 5 / 10

// makeStars / makeUVStars / makeFarFog (spec §1.3)
void makeQuads(std::vector<BeSky::V>& out, int count, i64 seed, double minS, double maxS, int kind) {
  JRand rnd(seed);
  for (int i = 0; i < count; i++) {
    double x = rnd.nextDouble() * 2 - 1;
    double y = kind == 2 ? rnd.nextDouble() - 0.5 : rnd.nextDouble() * 2 - 1;
    double z = rnd.nextDouble() * 2 - 1;
    double size = minS + rnd.nextDouble() * (maxS - minS);
    double L = x * x + y * y + z * z;
    if (!(L < 1 && L > 0.001)) continue;
    double dist = kind == 2 ? 2 : 1;
    if (kind == 2) size *= 2;
    double inv = 1 / std::sqrt(L) * dist;
    double nx = x * inv, ny = y * inv, nz = z * inv;
    double px = nx * 100, py = ny * 100, pz = nz * 100;
    double a1 = std::atan2(nx, nz), s1 = std::sin(a1), c1 = std::cos(a1);
    double a2 = std::atan2(std::sqrt(nx * nx + nz * nz), ny), s2 = std::sin(a2), c2 = std::cos(a2);
    double a3 = rnd.nextDouble() * 6.283185307179586, s3 = std::sin(a3), c3 = std::cos(a3);
    float minV = kind == 1 ? rnd.nextInt(4) / 4.f : 0.f;
    BeSky::V q[4];
    for (int k = 0; k < 4; k++) {
      double qx = ((k & 2) - 1) * size, qy = (((k + 1) & 2) - 1) * size;
      double aa = qx * c3 - qy * s3, ab = qy * c3 + qx * s3, dy = aa * s2, ae = -aa * c2;
      double dx = ae * s1 - ab * c1, dz = ab * s1 + ae * c1;
      q[k].p = vec3((float)((px + dx) * SCALE), (float)((py + dy) * SCALE), (float)((pz + dz) * SCALE));
      float u = (float)((k >> 1) & 1), v = (float)(((k + 1) >> 1) & 1);
      q[k].uv = kind == 1 ? vec2(u, v / 4 + minV) : vec2(u, v);
    }
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    for (int t : tri) out.push_back(q[t]);
  }
}
// makeCylinder: 16 quads round the camera, v 0 at y = -50 and 1 at +50
void makeCylinder(std::vector<BeSky::V>& out, float r) {
  for (int i = 0; i < 16; i++) {
    float a0 = i / 16.f * 6.2831853f, a1 = (i + 1) / 16.f * 6.2831853f;
    vec3 p0(std::sin(a0) * r, -50, std::cos(a0) * r), p1(std::sin(a1) * r, -50, std::cos(a1) * r);
    BeSky::V q[4] = {{p0 * SCALE, vec2(i / 16.f, 0)}, {p1 * SCALE, vec2((i + 1) / 16.f, 0)},
                     {vec3(p1.x, 50, p1.z) * SCALE, vec2((i + 1) / 16.f, 1)}, {vec3(p0.x, 50, p0.z) * SCALE, vec2(i / 16.f, 1)}};
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    for (int t : tri) out.push_back(q[t]);
  }
}
GLuint loadSkyTex(const char* name, bool smooth) {
  auto b = readBinary(assetsDir() / "sky" / "betterend" / name);
  if (!b) { DQ_WARN("Better End sky: no %s", name); return 0; }
  int w, h; std::vector<u8> px;
  if (!loadPngMem(b->data(), b->size(), w, h, px)) return 0;
  return makeTexture2D(w, h, px.data(), !smooth, false, false);
}
}

bool BeSky::init() {
  if (ready) return true;
  if (!shader.load("be_sky.glsl", "be_sky.glsl")) return false;
  std::vector<V> v;
  auto range = [&](Range& r, auto&& build) { r.first = (int)v.size(); build(); r.count = (int)v.size() - r.first; };
  range(stars1, [&] { makeQuads(v, 3500, 41315, 0.1, 0.30, 0); });
  range(stars2, [&] { makeQuads(v, 2000, 35151, 0.1, 0.35, 0); });
  range(stars3, [&] { makeQuads(v, 1000, 61354, 0.4, 1.2, 1); });
  range(stars4, [&] { makeQuads(v, 1000, 61355, 0.4, 1.2, 1); });
  range(nebula1, [&] { makeQuads(v, 30, 11515, 40, 60, 2); });
  range(nebula2, [&] { makeQuads(v, 10, 14151, 40, 60, 2); });
  range(horizon, [&] { makeCylinder(v, 100); });
  range(fog, [&] { makeCylinder(v, 70); });
  glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
  glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V), v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)12);
  glBindVertexArray(0);
  tHorizon = loadSkyTex("nebula_1.png", true); tNebula1 = loadSkyTex("nebula_2.png", true); tNebula2 = loadSkyTex("nebula_3.png", true);
  tStars = loadSkyTex("stars.png", false); tFog = loadSkyTex("fog.png", true);
  ready = true;
  return true;
}

void BeSky::draw(const mat4& viewProjRot, double ticks, vec3 fogColor, float fogDensity, float fade) {
  if (!ready || fade <= 0.001f) return;
  const float time = (float)(std::fmod(ticks, 360000.0) * 1.7453292e-5), time2 = time * 2, time3 = time * 3;
  // the four axes (LegacyRandomSource(131), spec §1.2)
  static const vec3 AX1 = glm::normalize(vec3(0.8852f, 0.1505f, 0.4403f)), AX2 = glm::normalize(vec3(0.4635f, 0.1207f, 0.8779f)),
                    AX3 = glm::normalize(vec3(0.5913f, 0.2016f, 0.7809f)), AX4 = glm::normalize(vec3(0.8559f, 0.4956f, 0.1479f));
  const vec3 Y(0, 1, 0);
  shader.use();
  shader.set("uTex", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(vao);
  auto layer = [&](const Range& r, GLuint tex, const vec3& axis, float angle, vec4 col) {
    if (r.count <= 0 || col.a <= 0.001f) return;
    mat4 m = angle != 0 ? glm::rotate(mat4(1), angle, axis) : mat4(1);
    shader.set("uMVP", viewProjRot * m);
    shader.set("uColor", col);
    shader.set("uUseTex", tex ? 1.f : 0.f);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLES, r.first, r.count);
  };
  const vec3 pink(0.77f, 0.31f, 0.73f);
  layer(horizon, tHorizon, Y, time, vec4(pink, 0.7f * fade));
  layer(nebula1, tNebula1, Y, -time, vec4(pink, 0.2f * fade));
  layer(nebula2, tNebula2, Y, time2, vec4(pink, 0.2f * fade));
  layer(stars3, tStars, AX3, time, vec4(pink, 0.6f * fade));
  layer(stars4, tStars, AX4, time2, vec4(1, 1, 1, 0.6f * fade));
  layer(fog, tFog, Y, 0, vec4(fogColor, std::clamp(fogDensity - 1.f, 0.f, 1.f)));
  layer(stars1, 0, AX1, time3, vec4(1, 1, 1, 0.6f * fade));
  layer(stars2, 0, AX2, time2, vec4(0.95f, 0.64f, 0.93f, 0.6f * fade));
  glBindVertexArray(0);
}

} // namespace dq
