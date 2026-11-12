// render/journal_model.cpp — see journal_model.h
#include "render/journal_model.h"
#include "render/content_draw.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

namespace dq {
namespace {
constexpr int STRIPS = 10, RIB_SEGS = 7;
constexpr float PI_F = 3.14159265f;

struct JMeshes { VoxelMeshGL leather, gold, pages; };
JMeshes& meshes(bool dark) {
  static auto* m = new std::unique_ptr<JMeshes>[2];   // (kept past exit: GL objects)
  auto& M = m[dark ? 1 : 0];
  if (M) return *M;
  M = std::make_unique<JMeshes>();
  auto unit = [](VoxelMeshGL& gl, int nx, int ny, int nz, const std::function<u32(int, int, int)>& col) {
    VoxelGrid g(ivec3(nx, ny, nz));
    for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) g.set(x, y, z, col(x, y, z));
    VoxelMeshData d; buildVoxelMesh(g, d); gl.upload(d);
  };
  // the leather of the drawn book (gui/book.png's cover), grained; a gold leaf a shade lighter at its edges
  unit(M->leather, 16, 16, 1, [](int x, int y, int) { const int n = (x * 7 + y * 13) % 5; return n == 0 ? voxRGBA(96, 30, 20) : n == 3 ? voxRGBA(118, 40, 26) : voxRGBA(108, 35, 23); });
  unit(M->gold, 4, 4, 1, [](int x, int y, int) { return x == 0 || y == 0 ? voxRGBA(240, 204, 110) : voxRGBA(206, 160, 62); });
  // the page block: its cut edge in fine lines of paper
  if (dark) unit(M->pages, 1, 16, 8, [](int, int, int z) { return (z & 1) ? voxRGBA(52, 46, 50) : voxRGBA(34, 30, 36); });
  else unit(M->pages, 1, 16, 8, [](int, int, int z) { return (z & 1) ? voxRGBA(214, 202, 172) : voxRGBA(240, 232, 208); });
  return *M;
}

struct Geo {
  float w, h, bend, T, Tc, tilt;
  // the paper's height over the cover at s (0 at the spine, 1 at the outer edge): down into the gutter at the spine
  float z(float s) const { return T * (1 - bend * 0.5f * (1 - std::sin(std::min(1.f, s / 0.24f) * PI_F * 0.5f))); }
  mat4 half(const mat4& frame, int sd) const { return glm::rotate(frame, -sd * tilt, vec3(0, 1, 0)); }
};
Geo geo(float w, float h, float bend) {
  Geo g; g.w = w; g.h = h; g.bend = bend;
  g.T = h * 0.045f; g.Tc = h * 0.014f; g.tilt = bend * 0.16f;
  return g;
}
void box(VoxelRenderer& VR, const VoxelMeshGL& m, const mat4& F, vec3 a, vec3 b, vec3 n, VoxelLight L) {
  const vec3 lo = glm::min(a, b), hi = glm::max(a, b);
  VR.draw(m, glm::scale(glm::translate(F, lo), (hi - lo) / n), L);
}
// a flat strip of the picture from A to B (x / z in F), between heights yb and yt; the picture's u ua at A to ub at B
void strip(ContentDraw& cd, const JournalPic& p, const mat4& F, vec3 A, vec3 B, float yb, float yt, float ua, float ub, float va, float vb, vec2 light, bool lit) {
  const vec3 X = B - A, Y(0, yt - yb, 0);
  vec3 Z = glm::cross(X, Y);
  if (glm::length(Z) < 1e-9f) return;
  Z = glm::normalize(Z);
  mat4 M(1); M[0] = vec4(X, 0); M[1] = vec4(Y, 0); M[2] = vec4(Z, 0); M[3] = vec4((A + B) * 0.5f + vec3(0, (yt + yb) * 0.5f, 0), 1);
  auto tu = [&](float u) { return p.texRect.x + u * (p.texRect.z - p.texRect.x); };
  auto tv = [&](float v) { return p.texRect.y + v * (p.texRect.w - p.texRect.y); };
  cd.picture(p.tex, F * M, vec4(tu(ua), tv(va), tu(ub), tv(vb)), vec4(1), light, lit);
}
}  // namespace

void drawJournalBody(VoxelRenderer& VR, const mat4& frame, float w, float h, float bend, VoxelLight L, bool darkPages) {
  JMeshes& M = meshes(darkPages);
  if (!M.leather.ready()) return;
  const Geo g = geo(w, h, bend);
  const float hw = w * 0.5f, m = w * 0.008f, hh = h * 0.5f;
  for (int sd : {1, -1}) {
    const mat4 F = g.half(frame, sd);
    // the cover board (a little larger than the pages: its edge shows all round)
    box(VR, M.leather, F, vec3(0, -hh - m, -g.Tc), vec3(sd * (hw + m), hh + m, 0), vec3(16, 16, 1), L);
    // the gold corners on the back board
    const float cw = w * 0.07f, ch = h * 0.11f, gt = g.Tc * 0.35f;
    for (float yy : {-1.f, 1.f}) {
      const float ye = yy * (hh + m);
      box(VR, M.gold, F, vec3(sd * (hw + m), ye, -g.Tc - gt), vec3(sd * (hw + m - cw), ye - yy * ch * 0.35f, -g.Tc), vec3(4, 4, 1), L);
      box(VR, M.gold, F, vec3(sd * (hw + m), ye, -g.Tc - gt), vec3(sd * (hw + m - cw * 0.35f), ye - yy * ch, -g.Tc), vec3(4, 4, 1), L);
    }
    // the page block under the paper, following its curve down into the gutter (its cut edges show at the sides)
    for (int i = 0; i < STRIPS; i++) {
      const float s0 = (float)i / STRIPS, s1 = (float)(i + 1) / STRIPS;
      const float top = g.z((s0 + s1) * 0.5f) - h * 0.003f;
      box(VR, M.pages, F, vec3(sd * s0 * hw * 0.995f, -hh * 0.985f, 0), vec3(sd * s1 * hw * 0.995f, hh * 0.985f, top), vec3(1, 16, 8), L);
    }
  }
  // the spine: the leather rounded under the fold, joining the boards
  const float sw = g.T * 0.9f;
  box(VR, M.leather, frame, vec3(-sw, -hh - m, -g.Tc - g.T * 0.35f), vec3(sw, hh + m, -g.Tc * 0.2f), vec3(16, 16, 1), L);
}

void drawJournalPages(ContentDraw& cd, const mat4& frame, float w, float h, float bend, const JournalPic& p, double time, vec2 light, bool lit,
                      const JournalPic* oldPic, int turnDir) {
  if (!p.tex) return;
  const Geo g = geo(w, h, bend);
  const float hw = w * 0.5f, hh = h * 0.5f;
  const vec4 sp = p.spread;
  const float du = sp.z - sp.x, dv = sp.w - sp.y;
  if (du <= 0 || dv <= 0) return;
  // the spread on the two halves, strip by strip down the curve
  for (int sd : {1, -1}) {
    const mat4 F = g.half(frame, sd);
    // (mid-turn: the side the page lands on still shows the old pages under it)
    const JournalPic& q = oldPic && oldPic->tex && sd != (turnDir >= 0 ? 1 : -1) ? *oldPic : p;
    const vec4 qs = q.spread; const float qdu = qs.z - qs.x;
    for (int i = 0; i < STRIPS; i++) {
      const float s0 = (float)i / STRIPS, s1 = (float)(i + 1) / STRIPS;
      const vec3 A(sd * s0 * hw, 0, g.z(s0)), B(sd * s1 * hw, 0, g.z(s1));
      const float ua = qs.x + qdu * (0.5f + sd * s0 * 0.5f), ub = qs.x + qdu * (0.5f + sd * s1 * 0.5f);
      strip(cd, q, F, A, B, -hh, hh, ua, ub, qs.y, qs.w, light, lit);
    }
  }
  // the ribbons: cloth out from between the pages, the end stirring in the air
  const float amp = 0.08f + 0.3f * bend;
  for (size_t r = 0; r < p.ribbons.size(); r++) {
    const vec4 rb = p.ribbons[r];
    int sd = 0;
    if (rb.z > sp.z + 1e-4f) sd = 1; else if (rb.x < sp.x - 1e-4f) sd = -1;
    if (!sd) continue;
    const float u0 = sd > 0 ? std::max(rb.x, sp.z) : rb.x, u1 = sd > 0 ? rb.z : std::min(rb.z, sp.x);
    if (u1 <= u0) continue;
    const float len = (u1 - u0) / du * w;
    const float yt = (0.5f - (rb.y - sp.y) / dv) * h, yb = (0.5f - (rb.w - sp.y) / dv) * h;
    const mat4 F = g.half(frame, sd);
    vec3 A(sd * hw, 0, g.z(1) * 0.55f);
    float ang = 0;
    for (int k = 0; k < RIB_SEGS; k++) {
      const float f = (k + 1.f) / RIB_SEGS;
      ang += (amp * std::sin((float)time * 3.3f + k * 0.85f + r * 1.9f) * f - bend * 0.05f * f) / RIB_SEGS * 2.f;
      const vec3 B = A + vec3(sd * std::cos(ang), 0, std::sin(ang)) * (len / RIB_SEGS);
      const float ua = sd > 0 ? u0 + (u1 - u0) * k / RIB_SEGS : u1 - (u1 - u0) * k / RIB_SEGS;
      const float ub = sd > 0 ? u0 + (u1 - u0) * f : u1 - (u1 - u0) * f;
      strip(cd, p, F, A, B, yb, yt, ua, ub, rb.y, rb.w, light, lit);
      A = B;
    }
  }
}

void drawJournalLeaf(ContentDraw& cd, const mat4& frame, float w, float h, float bend, const JournalPic& o, const JournalPic& n, float t, int dir, vec2 light, bool lit) {
  if (!o.tex || !n.tex || t < 0 || t > 1) return;
  const Geo g = geo(w, h, bend);
  const float hw = w * 0.5f, hh = h * 0.5f * 0.985f;
  const int sg = dir >= 0 ? 1 : -1;   // the side it lifts from
  const float u = t * t * (3 - 2 * t);
  const float phi = u * PI_F;                                  // over the spine
  const float curl = std::sin(u * PI_F) * 0.9f;                // its free edge lagging behind, the paper bending
  const float lift = g.z(0) + h * 0.004f;                      // (just over the pages)
  const float eps = h * 0.0025f;
  vec3 P(0, 0, lift);
  for (int i = 0; i < STRIPS; i++) {
    const float s0 = (float)i / STRIPS, s1 = (float)(i + 1) / STRIPS;
    const float a = std::max(0.f, phi - curl * (s0 + s1) * 0.5f);
    // (held up close to read, it rises less: it would sweep into your face)
    const float zs = 0.25f + 0.75f * std::min(1.f, bend);
    const vec3 d(sg * std::cos(a), 0, std::sin(a) * zs), nrm(-sg * std::sin(a), 0, std::cos(a));
    const vec3 Q = P + d * (hw / STRIPS);
    // the face (the old page, up as it starts) or the back (the new one, up as it lands): whichever faces the reader
    if (std::cos(a) >= 0) {
      const vec4 sp = o.spread; const float du = sp.z - sp.x;
      strip(cd, o, frame, P + nrm * eps, Q + nrm * eps, -hh, hh, sp.x + du * (0.5f + sg * s0 * 0.5f), sp.x + du * (0.5f + sg * s1 * 0.5f), sp.y, sp.w, light, lit);
    } else {
      const vec4 sp = n.spread; const float du = sp.z - sp.x;
      strip(cd, n, frame, P - nrm * eps, Q - nrm * eps, -hh, hh, sp.x + du * (0.5f - sg * s0 * 0.5f), sp.x + du * (0.5f - sg * s1 * 0.5f), sp.y, sp.w, light, lit);
    }
    P = Q;
  }
}

std::string journalLayoutString(const JournalPic& p) {
  std::string s;
  char b[64];
  auto add = [&](vec4 r) { std::snprintf(b, sizeof b, "%.4f,%.4f,%.4f,%.4f;", r.x, r.y, r.z, r.w); s += b; };
  add(p.spread);
  for (const vec4& r : p.ribbons) add(r);
  return s;
}
void journalLayoutParse(const std::string& s, JournalPic& p) {
  p.ribbons.clear();
  size_t i = 0; bool first = true;
  while (i < s.size()) {
    vec4 r;
    if (std::sscanf(s.c_str() + i, "%f,%f,%f,%f", &r.x, &r.y, &r.z, &r.w) != 4) break;
    if (first) p.spread = r; else if (p.ribbons.size() < 24) p.ribbons.push_back(r);
    first = false;
    const size_t e = s.find(';', i);
    if (e == std::string::npos) break;
    i = e + 1;
  }
}

} // namespace dq
