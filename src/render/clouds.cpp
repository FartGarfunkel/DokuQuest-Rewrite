// render/clouds.cpp — see clouds.h. cloudTemplates / wispTemplates / cloudsAround / drawClouds of
// render/clouds.js, with cloudScene from clouds.js + skies.js.
#include "render/clouds.h"
#include "render/sky.h"
#include "sim/level.h"
#include "sim/weather.h"

namespace dq {

namespace {
constexpr float CLOUD_CELL = 112;
struct CV { float x, y, z; u8 kind, shade, pad0, pad1; };   // kind: 0 top, 1 side, 2 bottom
// the corners of face f (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z) of a box, counter-clockwise from outside
void boxFace(std::vector<CV>& out, int f, vec3 lo, vec3 hi, u8 kind, u8 shade) {
  vec3 c[4];
  switch (f) {
    case 0: c[0] = {hi.x, lo.y, hi.z}; c[1] = {hi.x, lo.y, lo.z}; c[2] = {hi.x, hi.y, lo.z}; c[3] = {hi.x, hi.y, hi.z}; break;
    case 1: c[0] = {lo.x, lo.y, lo.z}; c[1] = {lo.x, lo.y, hi.z}; c[2] = {lo.x, hi.y, hi.z}; c[3] = {lo.x, hi.y, lo.z}; break;
    case 2: c[0] = {lo.x, hi.y, hi.z}; c[1] = {hi.x, hi.y, hi.z}; c[2] = {hi.x, hi.y, lo.z}; c[3] = {lo.x, hi.y, lo.z}; break;
    case 3: c[0] = {lo.x, lo.y, lo.z}; c[1] = {hi.x, lo.y, lo.z}; c[2] = {hi.x, lo.y, hi.z}; c[3] = {lo.x, lo.y, hi.z}; break;
    case 4: c[0] = {lo.x, lo.y, hi.z}; c[1] = {hi.x, lo.y, hi.z}; c[2] = {hi.x, hi.y, hi.z}; c[3] = {lo.x, hi.y, hi.z}; break;
    default: c[0] = {hi.x, lo.y, lo.z}; c[1] = {lo.x, lo.y, lo.z}; c[2] = {lo.x, hi.y, lo.z}; c[3] = {hi.x, hi.y, lo.z}; break;
  }
  for (auto& p : c) out.push_back({p.x, p.y, p.z, kind, shade, 0, 0});
}
u8 shadeOf(int f) { return f == 2 ? 255 : f == 3 ? 170 : f < 2 ? 222 : 212; }
u8 kindOf(int f) { return f == 2 ? 0 : f == 3 ? 2 : 1; }

// a cloud shape: puffs in a cluster, flat beneath, heaped on top; only the outside of the heap,
// merged into as few big faces as will do
std::vector<CV> cloudShape(int t) {
  Mulberry32 rnd(9137 + t * 31);
  struct Box { int x0, l0, z0, x1, l1, z1; };
  std::vector<Box> boxes;
  auto box = [&](double x0, int y0, double z0, double x1, int y1, double z1) { boxes.push_back({(int)std::round(x0), y0 >> 1, (int)std::round(z0), (int)std::round(x1), y1 >> 1, (int)std::round(z1)}); };
  double Lx = 30 + rnd() * 46, Lz = 16 + rnd() * 22; int N = 5 + (int)std::floor(rnd() * 6);
  for (int k = 0; k < N; k++) {
    double px = (rnd() - 0.5) * Lx, pz = (rnd() - 0.5) * Lz * 0.75;
    double r = 6 + rnd() * 8 * (1 - std::abs(px) / Lx), hr = r * (0.55 + rnd() * 0.35); int by = (int)std::floor(rnd() * 2) * 2;
    for (int y = 0; y < hr; y += 2) {
      double f = std::sqrt(std::max(0.04, 1 - std::pow(y / hr, 2)));
      double rr = std::max(2.0, std::round(r * f)), rz = std::max(1.0, std::round(r * f * 0.62));
      box(px - rr, by + y, pz - rz, px + rr, by + y + 2, pz + rz);
      box(px - rz, by + y, pz - rr, px + rz, by + y + 2, pz + rr);
    }
  }
  int gx0 = 1 << 30, gz0 = 1 << 30, gx1 = -(1 << 30), gz1 = -(1 << 30), L = 0;
  for (auto& b : boxes) { gx0 = std::min(gx0, b.x0); gz0 = std::min(gz0, b.z0); gx1 = std::max(gx1, b.x1); gz1 = std::max(gz1, b.z1); L = std::max(L, b.l1); }
  int X = gx1 - gx0, Z = gz1 - gz0;
  std::vector<u8> G((size_t)X * L * Z, 0);
  auto at = [&](int x, int l, int z) -> int { return x < 0 || z < 0 || l < 0 || x >= X || z >= Z || l >= L ? 0 : G[((size_t)l * Z + z) * X + x]; };
  for (auto& b : boxes) for (int l = b.l0; l < b.l1; l++) for (int z = b.z0 - gz0; z < b.z1 - gz0; z++) for (int x = b.x0 - gx0; x < b.x1 - gx0; x++) G[((size_t)l * Z + z) * X + x] = 1;
  std::vector<CV> out;
  const float E = 0.03f;   // each face reaches a hair past its edges, so no pixel of sky shows between merged faces
  auto face = [&](int f, int a, int l0, int b, int c, int l1, int d) {
    int ax = f >> 1;
    vec3 lo(a + gx0 - (ax != 0 ? E : 0), l0 * 2 - (ax != 1 ? E : 0), b + gz0 - (ax != 2 ? E : 0));
    vec3 hi(c + gx0 + (ax != 0 ? E : 0), l1 * 2 + (ax != 1 ? E : 0), d + gz0 + (ax != 2 ? E : 0));
    boxFace(out, f, lo, hi, kindOf(f), shadeOf(f));
  };
  std::vector<u8> mask((size_t)std::max({X * Z, Z * L, X * L}));
  auto greedy = [&](int W, int H, auto filled, auto emit) {
    for (int j = 0; j < H; j++) for (int i = 0; i < W; i++) mask[j * W + i] = filled(i, j) ? 1 : 0;
    for (int j = 0; j < H; j++) for (int i = 0; i < W;) {
      if (!mask[j * W + i]) { i++; continue; }
      int w = 1; while (i + w < W && mask[j * W + i + w]) w++;
      int h = 1;
      for (; j + h < H; h++) { bool ok = true; for (int k = 0; k < w; k++) if (!mask[(j + h) * W + i + k]) { ok = false; break; } if (!ok) break; }
      for (int jj = j; jj < j + h; jj++) std::fill(mask.begin() + jj * W + i, mask.begin() + jj * W + i + w, 0);
      emit(i, j, i + w, j + h); i += w;
    }
  };
  for (int l = 0; l < L; l++) {
    greedy(X, Z, [&](int x, int z) { return at(x, l, z) && !at(x, l + 1, z); }, [&](int a, int b, int c, int d) { face(2, a, l, b, c, l + 1, d); });
    greedy(X, Z, [&](int x, int z) { return at(x, l, z) && !at(x, l - 1, z); }, [&](int a, int b, int c, int d) { face(3, a, l, b, c, l + 1, d); });
  }
  for (int x = 0; x < X; x++) {
    greedy(Z, L, [&](int z, int l) { return at(x, l, z) && !at(x + 1, l, z); }, [&](int a, int b, int c, int d) { face(0, x, b, a, x + 1, d, c); });
    greedy(Z, L, [&](int z, int l) { return at(x, l, z) && !at(x - 1, l, z); }, [&](int a, int b, int c, int d) { face(1, x, b, a, x + 1, d, c); });
  }
  for (int z = 0; z < Z; z++) {
    greedy(X, L, [&](int x, int l) { return at(x, l, z) && !at(x, l, z + 1); }, [&](int a, int b, int c, int d) { face(4, a, b, z, c, d, z + 1); });
    greedy(X, L, [&](int x, int l) { return at(x, l, z) && !at(x, l, z - 1); }, [&](int a, int b, int c, int d) { face(5, a, b, z, c, d, z + 1); });
  }
  return out;
}
// thin wisps high up, the kind that run ahead of a front: long flat streaks, one block thick
std::vector<CV> wispShape(int t) {
  Mulberry32 rnd(5501 + t * 17);
  std::vector<CV> out;
  for (int k = 0; k < 6 + t; k++) {
    float x0 = (float)(rnd() - 0.5) * 90, z0 = (float)(rnd() - 0.5) * 30, L = 24 + (float)rnd() * 60, W = 2 + (float)rnd() * 5;
    for (int f = 0; f < 6; f++) boxFace(out, f, vec3(x0, 0, z0), vec3(x0 + L, 1, z0 + W), 0, f == 3 ? 200 : 255);
  }
  return out;
}
} // namespace

CloudRenderer::~CloudRenderer() {
  for (auto& t : tpl) { if (t.vao) glDeleteVertexArrays(1, &t.vao); if (t.vbo) glDeleteBuffers(1, &t.vbo); }
  for (auto& t : wisp) { if (t.vao) glDeleteVertexArrays(1, &t.vao); if (t.vbo) glDeleteBuffers(1, &t.vbo); }
  if (inst) glDeleteBuffers(1, &inst);
}

void CloudRenderer::build() {
  built = true;
  shader.load("clouds.glsl", "clouds.glsl");
  glGenBuffers(1, &inst);
  auto make = [&](Tpl& t, const std::vector<CV>& v) {
    size_t quads = v.size() / 4;
    GLuint ibo = quadIndexBuffer(quads);
    glGenVertexArrays(1, &t.vao); glGenBuffers(1, &t.vbo);
    glBindVertexArray(t.vao);
    glBindBuffer(GL_ARRAY_BUFFER, t.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(CV), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CV), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(CV), (void*)12);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBindBuffer(GL_ARRAY_BUFFER, inst);
    for (int i = 0; i < 3; i++) { glEnableVertexAttribArray(2 + i); glVertexAttribDivisor(2 + i, 1); }
    glBindVertexArray(0);
    t.count = (GLsizei)(quads * 6);
  };
  for (int i = 0; i < 10; i++) make(tpl[i], cloudShape(i));
  for (int i = 0; i < 4; i++) make(wisp[i], wispShape(i));
}

void CloudRenderer::update(Level& lv, const FrameCtx& f, const SkyEnv& env) {
  list.clear(); active = false;
  if (!f.settings || !f.settings->clouds || f.cam.realm != REALM_MAIN) return;
  int d = f.dim;
  if (d != DIM_OVERWORLD && d != DIM_AETHER) return;
  WeatherSystem* W = weatherOf(lv);
  active = true;
  // cloudScene (clouds.js + skies.js)
  double ox = W ? W->cloudX : 0, oz = W ? W->cloudZ : 0;
  i32 seed = lv.info.seed & 0xffff;
  std::string zone = W ? W->zone : "temperate", kind = W ? W->stormKind() : "clear";
  float base = zone == "desert" ? 0.22f : zone == "jungle" || zone == "swamp" ? 0.55f : zone == "savanna" ? 0.32f : 0.42f;
  thunder = kind == "thunder" ? 1.f : 0.f;
  float darkK = d == DIM_OVERWORLD ? (kind == "thunder" ? 1.f : 0.8f) : 0.45f;
  flash = W ? std::max(W->flashT > 0 ? std::min(1.f, W->flashT / 0.2f) : 0.f, d == DIM_AETHER && W->belowFlash > 0 ? W->belowFlash / 0.3f : 0.f) : 0.f;
  float far = (float)(f.settings->farTerrain);
  reach = far > 1024 && gfx.modern ? std::min(1400.f, 680 + (far - 1024) * 0.35f) : 680.f;
  double cx = f.cam.pos.x, cz = f.cam.pos.z;
  int kx0 = (int)std::floor((cx - ox - reach) / CLOUD_CELL), kx1 = (int)std::floor((cx - ox + reach) / CLOUD_CELL);
  int kz0 = (int)std::floor((cz - oz - reach) / CLOUD_CELL), kz1 = (int)std::floor((cz - oz + reach) / CLOUD_CELL);
  for (int kz = kz0; kz <= kz1; kz++) for (int kx = kx0; kx <= kx1; kx++) {
    for (int n = 0; n < 2; n++) {
      u32 h = hash32(kx, kz, 4471 + n, seed); float q = (h & 1023) / 1023.f;
      float jx = ((h >> 10) & 255) / 255.f, jz = ((h >> 18) & 255) / 255.f;
      double x = ox + (kx + 0.15 + jx * 0.7) * CLOUD_CELL, z = oz + (kz + 0.15 + jz * 0.7) * CLOUD_CELL;
      float k = W ? std::max(W->stormAt((float)x, (float)z), std::min(1.f, W->showerK * 0.8f)) : 0.f;
      float cover = clampv(base + k * (1 - base) * 0.98f, 0.f, 1.f);
      if (q > cover * (n ? 0.55f + 0.45f * k : 1.f)) continue;
      float dd = (float)std::hypot(x - cx, z - cz);
      if (dd > reach + 60) continue;
      Cloud c; c.pos = vec3((float)x, CLOUD_BASE_Y + (float)((h >> 26) & 31) - k * 12, (float)z); c.t = (h >> 3) % 10;
      c.s = 0.8f + ((h >> 13) & 63) / 63.f * 0.9f + k * 0.25f; c.r = (h >> 7) & 3; c.d = dd; c.dark = k * darkK; c.tower = thunder * k * (q < 0.45f ? 1.f : 0.35f); c.wisp = false;
      list.push_back(c);
    }
    // wisps, high above
    u32 hw = hash32(kx, kz, 9001, seed); float qw = (hw & 1023) / 1023.f;
    double wx = ox + (kx + 0.5) * CLOUD_CELL, wz = oz + (kz + 0.5) * CLOUD_CELL;
    float wk = 0.05f;
    if (W && W->front.on) { float dd = (float)((wx - W->anchor.x) * W->front.ux + (wz - W->anchor.y) * W->front.uz); wk += 0.5f * clampv(1 - std::abs(dd - (W->front.edge - 450)) / 500, 0.f, 1.f); }
    if (qw < wk) {
      float dd = (float)std::hypot(wx - cx, wz - cz);
      if (dd <= reach + 60) { Cloud c; c.pos = vec3((float)wx, CLOUD_BASE_Y + 52 + (float)((hw >> 20) & 15), (float)wz); c.t = (hw >> 3) & 3; c.s = 1 + ((hw >> 9) & 31) / 31.f; c.r = (hw >> 7) & 3; c.d = dd; c.wisp = true; c.dark = 0; c.tower = 0; list.push_back(c); }
    }
  }
  (void)env;
}

void CloudRenderer::drawSet(const FrameCtx& f, const SkyEnv& env, bool wisps) {
  if (!active || list.empty()) return;
  if (!built) build();
  if (!shader.ok()) return;
  float glow = env.sunGlow.x / 0.9f, th = thunder;
  // per cloud: where, how big, how dark (rain greys them, a storm blackens them, the sunset warms them,
  // lightning lights them from inside), and whether it fades into the storm's dark bank behind it
  insts.clear();
  std::vector<std::pair<int, int>> groups;   // (template, count) in order
  for (int t = 0; t < (wisps ? 4 : 10); t++) {
    int n0 = (int)insts.size();
    for (auto& c : list) {
      if (c.wisp != wisps || c.t != t) continue;
      float sy = wisps ? 1.f : 1 + 1.7f * c.tower, R = 48 * c.s;
      vec3 o = vec3(glm::dvec3(c.pos) - f.cam.origin);
      if (!f.cam.frustum.box(o - vec3(R * 1.6f, 0, R * 1.6f), o + vec3(R * 1.6f, 32 * c.s * sy, R * 1.6f))) continue;
      Inst in{}; in.x = o.x; in.y = o.y; in.z = o.z; in.s = c.s; in.sy = sy; in.rot = (float)c.r;
      in.fk = env.hasStorm && c.dark > 0.2f ? std::min(1.f, c.dark) : 0.f;
      vec4 tint(0);
      if (!wisps) {
        if (flash > 0.02f) tint = vec4(0.92f, 0.94f, 1, flash * (0.35f + c.dark * 0.4f));
        else if (c.dark > 0.02f) tint = vec4(lerpf(0.42f, 0.26f, th), lerpf(0.45f, 0.28f, th), lerpf(0.5f, 0.34f, th), c.dark * lerpf(0.55f, 0.82f, th));
        else if (glow > 0.02f) tint = vec4(1.0f, 0.66f, 0.5f, glow * 0.32f * env.day);
      }
      in.tr = tint.x; in.tg = tint.y; in.tb = tint.z; in.ta = tint.w;
      insts.push_back(in);
    }
    if ((int)insts.size() > n0) groups.push_back({t, (int)insts.size() - n0});
  }
  if (!wisps) drawn = (int)insts.size();
  if (insts.empty()) return;
  glBindBuffer(GL_ARRAY_BUFFER, inst);
  glBufferData(GL_ARRAY_BUFFER, insts.size() * sizeof(Inst), insts.data(), GL_STREAM_DRAW);
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", f.cam.viewProj);
  shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  shader.set("uDay", env.day);
  shader.set("uMinLight", std::max(env.minLight, 0.12f));
  float mist = std::min(1.f, env.mist);
  shader.set("uCloudFog", vec2(std::max(260.f, reach * 0.45f) * (1 - mist), lerpf(reach + 40, 70, mist)));
  shader.set("uFogCol", env.fog);
  shader.set("uStormCol", env.stormCol);
  shader.set("uAlpha", wisps ? 0.26f * (1 - mist) : 1.f);
  size_t first = 0;
  for (auto& [t, n] : groups) {
    Tpl& T = wisps ? wisp[t] : tpl[t];
    glBindVertexArray(T.vao);
    glBindBuffer(GL_ARRAY_BUFFER, inst);
    for (int i = 0; i < 3; i++) glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (void*)(first * sizeof(Inst) + i * 16));
    glDrawElementsInstanced(GL_TRIANGLES, T.count, GL_UNSIGNED_INT, nullptr, n);
    first += n;
  }
  glBindVertexArray(0);
}

void CloudRenderer::drawOpaque(const FrameCtx& f, const SkyEnv& env) { drawSet(f, env, false); }
void CloudRenderer::drawWisps(const FrameCtx& f, const SkyEnv& env) {
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
  drawSet(f, env, true);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

} // namespace dq
