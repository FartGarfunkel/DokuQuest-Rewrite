// gen/aether_noise.cpp — see aether_noise.h.
#include "gen/aether_noise.h"
#include "common/noise.h"
#include <cmath>
#include <random>
#include <unordered_map>

namespace dq {

namespace {
const int GRAD[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                         {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline double lerpd(double t, double a, double b) { return a + t * (b - a); }
inline double gd(int h, double x, double y, double z) { const int* g = GRAD[h & 15]; return g[0] * x + g[1] * y + g[2] * z; }
inline double wrapc(double d) { const double W = 3.3554432E7; return d - std::floor(d / W + 0.5) * W; }
inline double yclamp(double y, double y0, double y1, double v0, double v1) { double t = std::clamp((y - y0) / (y1 - y0), 0.0, 1.0); return v0 + t * (v1 - v0); }
}

// Minecraft's ImprovedNoise.noise(x, y, z, yScale, yMax): with yScale the y gradient's offset snaps (the smear)
double AetherNoise::Octave::noise(double x, double y, double z, double yScale, double yMax) const {
  double d = x + xo, e = y + yo, f = z + zo;
  int i = (int)std::floor(d), j = (int)std::floor(e), k = (int)std::floor(f);
  double g = d - i, h = e - j, l = f - k, o = 0;
  if (yScale != 0) { double m = (yMax >= 0 && yMax < h) ? yMax : h; o = std::floor(m / yScale + 1.0e-7) * yScale; }
  double dy = h - o;
  auto P = [&](int n) { return (int)p[n & 255]; };
  int a = P(i), b = P(i + 1), aa = P(a + j), ab = P(a + j + 1), ba = P(b + j), bb = P(b + j + 1);
  double x1 = gd(P(aa + k), g, dy, l), x2 = gd(P(ba + k), g - 1, dy, l);
  double x3 = gd(P(ab + k), g, dy - 1, l), x4 = gd(P(bb + k), g - 1, dy - 1, l);
  double x5 = gd(P(aa + k + 1), g, dy, l - 1), x6 = gd(P(ba + k + 1), g - 1, dy, l - 1);
  double x7 = gd(P(ab + k + 1), g, dy - 1, l - 1), x8 = gd(P(bb + k + 1), g - 1, dy - 1, l - 1);
  double s = fade(g), t = fade(h), u = fade(l);
  return lerpd(u, lerpd(t, lerpd(s, x1, x2), lerpd(s, x3, x4)), lerpd(t, lerpd(s, x5, x6), lerpd(s, x7, x8)));
}

AetherNoise::AetherNoise(u64 seed) : seed_(seed) {
  std::mt19937_64 r(seed ^ 0xAE7E2ULL);
  std::uniform_real_distribution<double> U(0, 1);
  auto make = [&](std::vector<Octave>& v, int n) {
    v.resize(n);
    for (auto& o : v) {
      o.xo = U(r) * 256; o.yo = U(r) * 256; o.zo = U(r) * 256;
      for (int i = 0; i < 256; i++) o.p[i] = (u8)i;
      for (int i = 0; i < 256; i++) { int j = (int)(r() % (u64)(256 - i)); std::swap(o.p[i], o.p[i + j]); }
    }
  };
  make(minL, 16); make(maxL, 16); make(mainN, 8);
}

// old_blended_noise {xz_scale .25, y_scale .25, xz_factor 80, y_factor 160, smear_scale_multiplier 8}
double AetherNoise::blended(int bx, int by, int bz) const {
  const double xzMul = 684.412 * 0.25, yMul = 684.412 * 0.25, xzF = 80, yF = 160, smear = 8;
  double d = bx * xzMul, e = by * yMul, f = bz * xzMul;
  double g = d / xzF, h = e / yF, i = f / xzF;
  double j = yMul * smear, k = j / yF;
  double l = 0, m = 0, n = 0, o = 1;
  for (int pi = 0; pi < 8; pi++) { n += mainN[pi].noise(wrapc(g * o), wrapc(h * o), wrapc(i * o), k * o, h * o) / o; o /= 2; }
  double q = (n / 10.0 + 1.0) / 2.0;
  bool hi = q >= 1.0, lo = q <= 0.0;
  o = 1;
  for (int ri = 0; ri < 16; ri++) {
    double s = wrapc(d * o), t = wrapc(e * o), u = wrapc(f * o), v = j * o;
    if (!hi) l += minL[ri].noise(s, t, u, v, e * o) / o;
    if (!lo) m += maxL[ri].noise(s, t, u, v, e * o) / o;
    o /= 2;
  }
  double a = l / 512.0, b = m / 512.0;
  double c = q < 0 ? a : q > 1 ? b : a + q * (b - a);
  return c / 128.0;
}

double AetherNoise::corner(int x, int y, int z) const {
  if (y <= 8 || y >= 128) return -0.15;   // G1 = 0 or G2 = 0: always air
  const double N = blended(x, y, z);
  const double g1 = yclamp(y, 8, 40, 0, 1), g2 = yclamp(y, 56, 128, 1, 0);
  return -0.05 + (-0.1 + g1 * (0.1 + (-0.2 + g2 * (0.2 + (-0.13 + N)))));
}

const double* AetherNoise::cornerColumn(int x, int z) const {
  struct Key { int x, z; const AetherNoise* n; bool operator==(const Key& o) const { return x == o.x && z == o.z && n == o.n; } };
  struct KH { size_t operator()(const Key& k) const { return (size_t)hash32(k.x, k.z, (i32)(intptr_t)k.n, 77); } };
  struct Col { double v[CY]; };
  thread_local std::unordered_map<Key, Col, KH> cache;
  if (cache.size() > 8192) cache.clear();
  Key key{x, z, this};
  auto it = cache.find(key);
  if (it != cache.end()) return it->second.v;
  Col c;
  for (int i = 0; i < CY; i++) c.v[i] = corner(x, i * 4, z);
  return cache.emplace(key, c).first->second.v;
}

} // namespace dq
