// assets/voxel_paint.cpp — sprite reading and shape voxelisation for item (and body) models.
#include "assets/voxel_paint.h"
#include "assets/tileset.h"
#include <deque>

namespace dq {

u32 tileVoxel(const TileSet& ts, int tile, int x, int y) {
  if (tile < 0 || tile >= ts.count) return 0;
  int S = ts.size;
  x = clampv(x, 0, S - 1); y = clampv(y, 0, S - 1);
  const u8* p = ts.tile(tile) + ((size_t)y * S + x) * 4;
  if (p[3] < 128) return 0;
  int r = p[0], g = p[1], b = p[2];
  if (p[3] == 253) { r = r * 124 * 13 / 2550; g = g * 191 * 13 / 2550; b = b * 72 * 13 / 2550; }   // TINT_GRASS * 1.3 (models' default tint)
  bool glow = !ts.emissive.empty() && ts.emissive[(size_t)tile * S * S + (size_t)y * S + x] > 127;
  return voxRGBA(r, g, b, glow ? VOXEL_GLOW : VOXEL_SOLID);
}

SpriteInfo SpriteInfo::of(const TileSet& ts, int tile) {
  SpriteInfo I;
  if (tile < 0 || tile >= ts.count) return I;
  I.tile = tile;
  I.R = (tile < (int)ts.res.size() && ts.res[tile]) ? ts.res[tile] : ts.size;
  I.R = clampv(I.R, 4, ts.size);
  const int R = I.R, st = ts.size / R;
  I.grid.assign(R * R, 0); I.raw.assign(R * R, 0); I.fill.assign(R * R, 0);
  for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
    const u8* p = ts.tile(tile) + ((size_t)(j * st) * ts.size + i * st) * 4;
    if (p[3] < 128) continue;
    SpritePx q; q.i = i; q.j = j; q.r = p[0]; q.g = p[1]; q.b = p[2];
    int mx = std::max({q.r, q.g, q.b}), mn = std::min({q.r, q.g, q.b});
    q.lum = 0.3f * q.r + 0.59f * q.g + 0.11f * q.b; q.sat = mx ? (float)(mx - mn) / mx : 0;
    I.px.push_back(q);
    I.grid[j * R + i] = 1;
    I.raw[j * R + i] = tileVoxel(ts, tile, i * st, j * st);
    I.lo = std::min(I.lo, q.lum); I.hi = std::max(I.hi, q.lum);
  }
  if (I.px.empty()) return I;
  I.edge = I.lo + (I.hi - I.lo) * 0.14f;   // the dark outline most item pictures have
  // spread the body's colours outward, nearest first (modelFill)
  std::deque<int> q;
  bool anyBody = false;
  for (auto& p : I.px) if (p.lum >= I.edge) { anyBody = true; break; }
  for (auto& p : I.px) if (!anyBody || p.lum >= I.edge) { I.fill[p.j * R + p.i] = I.raw[p.j * R + p.i] | 0xFF000000u; q.push_back(p.j * R + p.i); }
  while (!q.empty()) {
    int k = q.front(); q.pop_front();
    int i = k % R, j = k / R;
    const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& d : D) {
      int ii = i + d[0], jj = j + d[1];
      if (ii < 0 || jj < 0 || ii >= R || jj >= R) continue;
      int n = jj * R + ii;
      if (!I.fill[n]) { I.fill[n] = I.fill[k]; q.push_back(n); }
    }
  }
  return I;
}

u32 SpriteInfo::fillAt(float x, float y) const {
  if (fill.empty()) return 0;
  return fill[clampv(pj(y), 0, R - 1) * R + clampv(pi(x), 0, R - 1)];
}
u32 SpriteInfo::rawAt(float x, float y) const {
  int i = pi(x), j = pj(y);
  if (i < 0 || j < 0 || i >= R || j >= R) return 0;
  return raw[j * R + i];
}

const SpritePx* SpriteInfo::sample(float x, float y, const std::function<bool(const SpritePx&)>& ok) const {
  float ci = (x + 0.5f) * R - 0.5f, cj = (0.5f - y) * R - 0.5f;
  const SpritePx *best = nullptr, *any = nullptr; float bd = 1e9f, ad = 1e9f;
  for (auto& p : px) {
    if (ok && !ok(p)) continue;
    float d = (p.i - ci) * (p.i - ci) + (p.j - cj) * (p.j - cj);
    if (d < ad) { ad = d; any = &p; }
    if (p.lum >= edge && d < bd) { bd = d; best = &p; }
  }
  return best ? best : any ? any : px.empty() ? nullptr : &px[0];
}
const SpritePx* SpriteInfo::common(const std::function<bool(const SpritePx&)>& ok) const {
  std::unordered_map<int, int> n; const SpritePx* best = nullptr; int bn = 0;
  for (auto& p : px) {
    if (ok && !ok(p)) continue;
    int k = (p.r >> 4) << 8 | (p.g >> 4) << 4 | (p.b >> 4), c = ++n[k];
    if (c > bn) { bn = c; best = &p; }
  }
  return best;
}
const SpritePx* SpriteInfo::best(const std::function<float(const SpritePx&)>& score, const std::function<bool(const SpritePx&)>& ok) const {
  const SpritePx* b = nullptr; float bs = -1e9f;
  for (auto& p : px) { if (ok && !ok(p)) continue; float s = score(p); if (s > bs) { bs = s; b = &p; } }
  return b;
}

// ------------------------------------------------------------------ painter
template <class Inside>
void VoxelPainter::fillAABB(vec3 lo, vec3 hi, Inside inside, const PaintFn& fn) {
  ivec3 a = glm::max(cell(lo) - 1, ivec3(0)), b = glm::min(cell(hi) + 1, g.size - 1);
  PaintPt pt;
  for (int z = a.z; z <= b.z; z++) for (int y = a.y; y <= b.y; y++) for (int x = a.x; x <= b.x; x++) {
    pt.p = centre(ivec3(x, y, z));
    if (!inside(pt)) continue;
    u32 c = fn(pt);
    if (c) g.set(x, y, z, c);
  }
}

// half-open [lo, lo + max(len, minLen)) about the middle: always at least one voxel centre inside
static inline bool spanIn(float v, float c, float len, float minLen) { float h = std::max(len, minLen) * 0.5f; return v >= c - h && v < c + h; }

void VoxelPainter::box(vec3 lo, vec3 hi, const PaintFn& fn) {
  vec3 c = (lo + hi) * 0.5f, e = hi - lo; float v = frame.voxel;
  vec3 h = glm::max(e, vec3(v)) * 0.5f;
  fillAABB(c - h, c + h, [&](PaintPt& p) {
    if (!spanIn(p.p.x, c.x, e.x, v) || !spanIn(p.p.y, c.y, e.y, v) || !spanIn(p.p.z, c.z, e.z, v)) return false;
    p.t = e.y > 0 ? (p.p.y - lo.y) / e.y : 0.5f; p.s = e.x > 0 ? (p.p.x - c.x) / (e.x * 0.5f) : 0;
    return true;
  }, fn);
}

void VoxelPainter::seg(vec3 a, vec3 b, float w, float d, const PaintFn& fn, float bevel) {
  float v = frame.voxel;
  vec2 dir(b.x - a.x, b.y - a.y); float L = glm::length(dir);
  if (L < 1e-6f) { dir = vec2(1, 0); L = 0; } else dir /= L;
  vec2 nrm(-dir.y, dir.x);
  float Le = std::max(L, v), we = std::max(w, v * 1.42f), de = std::max(d, v);
  vec2 mid = (vec2(a) + vec2(b)) * 0.5f;
  float zc = a.z;
  float rx = Le * 0.5f * std::abs(dir.x) + we * 0.5f * std::abs(nrm.x), ry = Le * 0.5f * std::abs(dir.y) + we * 0.5f * std::abs(nrm.y);
  fillAABB(vec3(mid.x - rx, mid.y - ry, zc - de * 0.5f), vec3(mid.x + rx, mid.y + ry, zc + de * 0.5f), [&](PaintPt& p) {
    vec2 q = vec2(p.p) - mid;
    float al = glm::dot(q, dir), ac = glm::dot(q, nrm);
    if (al < -Le * 0.5f || al >= Le * 0.5f || ac < -we * 0.5f || ac >= we * 0.5f) return false;
    float s = ac / (we * 0.5f);
    float dd = bevel > 0 ? d * (1 - bevel * s * s) : d;
    if (!spanIn(p.p.z, zc, dd, v)) return false;
    p.t = Le > 0 ? al / Le + 0.5f : 0.5f; p.s = s;
    return true;
  }, fn);
}

void VoxelPainter::rod(vec3 p0, vec3 p1, vec2 w0, vec2 w1, const PaintFn& fn, vec3 ref) {
  vec3 d = p1 - p0; float L = glm::length(d);
  if (L < 1e-6f) return;
  d /= L;
  vec3 A = ref; if (std::abs(glm::dot(d, A)) > 0.95f) A = vec3(1, 0, 0);
  vec3 u = glm::normalize(glm::cross(d, A)), vv = glm::cross(u, d);
  float vx = frame.voxel;
  float r = std::max({w0.x, w0.y, w1.x, w1.y}) * 0.75f + vx;
  vec3 lo = glm::min(p0, p1) - vec3(r), hi = glm::max(p0, p1) + vec3(r);
  fillAABB(lo, hi, [&](PaintPt& p) {
    vec3 q = p.p - p0;
    float t = glm::dot(q, d) / L;
    if (t < 0 || t >= 1) return false;
    vec2 W = glm::mix(w0, w1, t);
    float a = glm::dot(q, u), b = glm::dot(q, vv);
    float ha = std::max(W.x, vx * 1.42f) * 0.5f, hb = std::max(W.y, vx * 1.42f) * 0.5f;
    if (a < -ha || a >= ha || b < -hb || b >= hb) return false;
    p.t = t; p.s = a / ha;
    return true;
  }, fn);
}

void VoxelPainter::hexa(const std::array<vec3, 8>& P, const PaintFn& fn) {
  // six faces (as planes through three of their corners), each normal pointing out; a point is inside
  // when it is behind all of them (the blocks are convex enough for this)
  auto C = [&](int cx, int cy, int cz) { return P[cx * 4 + cy * 2 + cz]; };
  vec3 ctr(0); for (auto& p : P) ctr += p; ctr /= 8.f;
  struct Pl { vec3 n; float d; };
  std::vector<Pl> pl;
  auto face = [&](vec3 a, vec3 b, vec3 c, vec3 e) {
    vec3 n = glm::cross(b - a, c - a) + glm::cross(c - a, e - a);
    if (glm::length(n) < 1e-9f) return;
    n = glm::normalize(n);
    vec3 m = (a + b + c + e) * 0.25f;
    if (glm::dot(n, ctr - m) > 0) n = -n;
    pl.push_back({n, glm::dot(n, m)});
  };
  face(C(0, 0, 0), C(0, 0, 1), C(0, 1, 1), C(0, 1, 0));
  face(C(1, 0, 0), C(1, 1, 0), C(1, 1, 1), C(1, 0, 1));
  face(C(0, 0, 0), C(1, 0, 0), C(1, 0, 1), C(0, 0, 1));
  face(C(0, 1, 0), C(0, 1, 1), C(1, 1, 1), C(1, 1, 0));
  face(C(0, 0, 0), C(0, 1, 0), C(1, 1, 0), C(1, 0, 0));
  face(C(0, 0, 1), C(1, 0, 1), C(1, 1, 1), C(0, 1, 1));
  vec3 lo(1e9f), hi(-1e9f); for (auto& p : P) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
  float tol = frame.voxel * 0.5f;
  vec3 ext = hi - lo; bool thin = std::min({ext.x, ext.y, ext.z}) < frame.voxel;
  fillAABB(lo, hi, [&](PaintPt& p) {
    for (auto& q : pl) if (glm::dot(q.n, p.p) - q.d > (thin ? tol : 0.f)) return false;
    p.t = hi.y > lo.y ? (p.p.y - lo.y) / (hi.y - lo.y) : 0.5f;
    return true;
  }, fn);
}

} // namespace dq
