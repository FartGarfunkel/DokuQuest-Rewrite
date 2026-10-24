#include "common/noise.h"

namespace dq {

u32 hash32(i32 a, i32 b, i32 c, i32 d) {
  // JS: (a|0)*374761393 + ... is a double sum of four products (exact up to 2^53), then |0 via Math.imul.
  double h = (double)a * 374761393.0 + (double)b * 668265263.0 + (double)c * 2147483647.0 + (double)d * 1274126177.0;
  // ToInt32 of the double (modulo 2^32)
  double m = std::fmod(std::trunc(h), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  u32 hv = (u32)(u64)m;
  hv = (hv ^ (hv >> 13)) * 1274126177u;
  hv ^= hv >> 16;
  return hv;
}

i32 seedFromString(const std::string& in) {
  std::string s = in;
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  size_t st = 0; while (st < s.size() && isspace((unsigned char)s[st])) st++;
  s = s.substr(st);
  bool num = !s.empty();
  for (size_t i = 0; i < s.size(); i++) { char c = s[i]; if (!(isdigit((unsigned char)c) || (i == 0 && c == '-' && s.size() > 1))) { num = false; break; } }
  if (num) { double v = std::strtod(s.c_str(), nullptr); if (std::fabs(v) < 2147483648.0) return (i32)(i64)v; }
  // FNV-1a over UTF-16 code units (ASCII-equivalent for typical seeds)
  u32 h = 2166136261u;
  for (unsigned char ch : s) { h ^= ch; h *= 16777619u; }
  return (i32)h;
}

static const float GRAD3[36] = {1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0, 1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, -1, 0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1};

SimplexNoise::SimplexNoise(u32 seed) {
  Mulberry32 rand(seed);
  u8 p[256];
  for (int i = 0; i < 256; i++) p[i] = (u8)i;
  for (int i = 255; i > 0; i--) { int j = (int)std::floor(rand() * (i + 1)); u8 t = p[i]; p[i] = p[j]; p[j] = t; }
  for (int i = 0; i < 512; i++) { perm[i] = p[i & 255]; permMod12[i] = perm[i] % 12; }
}

double SimplexNoise::noise2(double xin, double yin) const {
  const double F2 = 0.5 * (std::sqrt(3.0) - 1), G2 = (3 - std::sqrt(3.0)) / 6;
  double s = (xin + yin) * F2;
  int i = (int)std::floor(xin + s), j = (int)std::floor(yin + s);
  double t = (i + j) * G2;
  double x0 = xin - (i - t), y0 = yin - (j - t);
  int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
  double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2, x2 = x0 - 1 + 2 * G2, y2 = y0 - 1 + 2 * G2;
  int ii = i & 255, jj = j & 255;
  double n0 = 0, n1 = 0, n2 = 0;
  double t0 = 0.5 - x0 * x0 - y0 * y0;
  if (t0 > 0) { int gi = permMod12[ii + perm[jj]] * 3; t0 *= t0; n0 = t0 * t0 * (GRAD3[gi] * x0 + GRAD3[gi + 1] * y0); }
  double t1 = 0.5 - x1 * x1 - y1 * y1;
  if (t1 > 0) { int gi = permMod12[ii + i1 + perm[jj + j1]] * 3; t1 *= t1; n1 = t1 * t1 * (GRAD3[gi] * x1 + GRAD3[gi + 1] * y1); }
  double t2 = 0.5 - x2 * x2 - y2 * y2;
  if (t2 > 0) { int gi = permMod12[ii + 1 + perm[jj + 1]] * 3; t2 *= t2; n2 = t2 * t2 * (GRAD3[gi] * x2 + GRAD3[gi + 1] * y2); }
  return 70 * (n0 + n1 + n2);
}

double SimplexNoise::noise3(double xin, double yin, double zin) const {
  const double F3 = 1.0 / 3, G3 = 1.0 / 6;
  double s = (xin + yin + zin) * F3;
  int i = (int)std::floor(xin + s), j = (int)std::floor(yin + s), k = (int)std::floor(zin + s);
  double t = (i + j + k) * G3;
  double x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t);
  int i1, j1, k1, i2, j2, k2;
  if (x0 >= y0) {
    if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
    else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
  } else {
    if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
    else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
    else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
  }
  double x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
  double x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
  double x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
  int ii = i & 255, jj = j & 255, kk = k & 255;
  double n = 0;
  double t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 > 0) { int gi = permMod12[ii + perm[jj + perm[kk]]] * 3; t0 *= t0; n += t0 * t0 * (GRAD3[gi] * x0 + GRAD3[gi + 1] * y0 + GRAD3[gi + 2] * z0); }
  double t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 > 0) { int gi = permMod12[ii + i1 + perm[jj + j1 + perm[kk + k1]]] * 3; t1 *= t1; n += t1 * t1 * (GRAD3[gi] * x1 + GRAD3[gi + 1] * y1 + GRAD3[gi + 2] * z1); }
  double t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 > 0) { int gi = permMod12[ii + i2 + perm[jj + j2 + perm[kk + k2]]] * 3; t2 *= t2; n += t2 * t2 * (GRAD3[gi] * x2 + GRAD3[gi + 1] * y2 + GRAD3[gi + 2] * z2); }
  double t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 > 0) { int gi = permMod12[ii + 1 + perm[jj + 1 + perm[kk + 1]]] * 3; t3 *= t3; n += t3 * t3 * (GRAD3[gi] * x3 + GRAD3[gi + 1] * y3 + GRAD3[gi + 2] * z3); }
  return 32 * n;
}

double SimplexNoise::fbm2(double x, double y, int octaves, double lacunarity, double gain) const {
  double amp = 1, freq = 1, sum = 0, norm = 0;
  for (int o = 0; o < octaves; o++) { sum += amp * noise2(x * freq, y * freq); norm += amp; amp *= gain; freq *= lacunarity; }
  return sum / norm;
}

} // namespace dq
