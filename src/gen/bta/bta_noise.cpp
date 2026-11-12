// Bit-exact ports of BTA 8.0.1 noise classes. See bta_noise.h.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include "bta_noise.h"

#include <cmath>
#include <limits>
#include <vector>

namespace bta {

// ---- MathHelper ----------------------------------------------------------------------------------
namespace mh {
namespace {
struct SinTable {
  float t[65536];
  SinTable() {
    const double PI = 3.141592653589793;
    for (int i = 0; i < 65536; ++i) t[i] = (float)std::sin((double)i * PI * 2.0 / 65536.0);
  }
};
const SinTable& table() { static const SinTable s; return s; }
}  // namespace
const float* sinTable() { return table().t; }
float sin(float rad) { return table().t[jf2i(rad * 10430.38f) & 0xFFFF]; }
float cos(float rad) { return table().t[jf2i(rad * 10430.38f + 16384.0f) & 0xFFFF]; }
}  // namespace mh

// ---- ImprovedPerlinNoise -------------------------------------------------------------------------
namespace {
inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
inline double lerp(double t, double a, double b) { return a + t * (b - a); }
inline double grad(int hash, double x, double y, double z) {
  int h = hash & 0xF;
  double u = h < 8 ? x : y;
  double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}
inline double grad2(int hash, double x, double y) {
  int h = hash & 0xF;
  double u = (double)(1 - ((h & 8) >> 3)) * x;
  double v = h < 4 ? 0.0 : (h == 12 || h == 14 ? x : y);
  return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -v : v);
}
template <class P>
void initPerm(P* self, int* p, JavaRandom& r) {
  self->xo = r.nextDouble() * 256.0;
  self->yo = r.nextDouble() * 256.0;
  self->zo = r.nextDouble() * 256.0;
  for (int i = 0; i < 256; ++i) p[i] = i;
  for (int i = 0; i < 256; ++i) {
    int n = r.nextInt(256 - i) + i;
    int t = p[i]; p[i] = p[n]; p[n] = t;
    p[i + 256] = p[i];
  }
}
void skipOctaves(JavaRandom& r, int pre) {
  for (int i = 0; i < pre; ++i) {
    r.nextDouble(); r.nextDouble(); r.nextDouble();
    for (int j = 0; j < 256; ++j) r.nextInt(256 - j);
  }
}
}  // namespace

ImprovedPerlin::ImprovedPerlin(JavaRandom& r, Legacy t) {
  edgeY_ = t != Legacy::ALPHA;  // DEFAULT(true,true,true) ALPHA(false,false,true) BETA(true,false,true)
  edgeZ_ = t == Legacy::DEFAULT;
  farLands_ = true;
  initPerm(this, p_, r);
}

std::vector<ImprovedPerlin> ImprovedPerlin::genOctaves(int64_t seed, int n, int pre, Legacy t) {
  JavaRandom r(seed);
  skipOctaves(r, pre);
  std::vector<ImprovedPerlin> v;
  v.reserve(n);
  for (int i = 0; i < n; ++i) v.emplace_back(r, t);
  return v;
}

double ImprovedPerlin::rangeMin() { return -std::sqrt(0.75); }
double ImprovedPerlin::rangeMax() { return std::sqrt(0.75); }

double ImprovedPerlin::fastFloor(double v) const {
  if (farLands_) {
    int32_t i = jd2i(v);
    if (v < (double)i) i = jsub(i, 1);
    return (double)i;
  }
  return std::floor(v);
}

double ImprovedPerlin::getValue3(double x, double y, double z) const {
  const int* p = p_;
  int X = jd2i(fastFloor(x)) & 0xFF;
  int Y = jd2i(fastFloor(y)) & 0xFF;
  int Z = jd2i(fastFloor(z)) & 0xFF;
  x -= fastFloor(x);
  y -= fastFloor(y);
  z -= fastFloor(z);
  double u = fade(x), v = fade(y), w = fade(z);
  int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
  int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
  return lerp(w,
              lerp(v, lerp(u, grad(p[AA], x, y, z), grad(p[BA], x - 1.0, y, z)),
                   lerp(u, grad(p[AB], x, y - 1.0, z), grad(p[BB], x - 1.0, y - 1.0, z))),
              lerp(v, lerp(u, grad(p[AA + 1], x, y, z - 1.0), grad(p[BA + 1], x - 1.0, y, z - 1.0)),
                   lerp(u, grad(p[AB + 1], x, y - 1.0, z - 1.0), grad(p[BB + 1], x - 1.0, y - 1.0, z - 1.0))));
}

void ImprovedPerlin::addRegion3(double* out, double x, double y, double z, int xSize, int ySize, int zSize,
                                double xScale, double yScale, double zScale, double amplitude) const {
  const int* p = p_;
  if (ySize == 1 && edgeY_) {
    int i = 0;
    for (int xi = 0; xi < xSize; ++xi) {
      double xx = (x + (double)xi) * xScale + xo;
      int X = jd2i(fastFloor(xx)) & 0xFF;
      xx -= fastFloor(xx);
      double u = fade(xx);
      for (int zi = 0; zi < zSize; ++zi) {
        double zz = (z + (double)zi) * zScale + zo;
        int Z = jd2i(fastFloor(zz)) & 0xFF;
        zz -= fastFloor(zz);
        double w = fade(zz);
        int A = p[X], AA = p[A] + Z, B = p[X + 1], BA = p[B] + Z;
        double value = lerp(w, lerp(u, grad2(p[AA], xx, zz), grad(p[BA], xx - 1.0, 0.0, zz)),
                            lerp(u, grad(p[AA + 1], xx, 0.0, zz - 1.0), grad(p[BA + 1], xx - 1.0, 0.0, zz - 1.0)));
        out[i] = out[i] + value * amplitude;
        ++i;
      }
    }
  } else if (zSize == 1 && edgeZ_) {
    int i = 0;
    for (int xi = 0; xi < xSize; ++xi) {
      double xx = (x + (double)xi) * xScale + xo;
      int X = jd2i(fastFloor(xx)) & 0xFF;
      xx -= fastFloor(xx);
      double u = fade(xx);
      for (int yi = 0; yi < ySize; ++yi) {
        double yy = (y + (double)yi) * yScale + zo;  // sic: zo
        int Y = jd2i(fastFloor(yy)) & 0xFF;
        yy -= fastFloor(yy);
        double v = fade(yy);
        int A = p[X], AA = p[A] + Y, B = p[X + 1], BA = p[B] + Y;
        double value = lerp(v, lerp(u, grad2(p[AA], xx, yy), grad(p[BA], xx - 1.0, 0.0, yy)),
                            lerp(u, grad(p[AA + 1], xx, 0.0, yy - 1.0), grad(p[BA + 1], xx - 1.0, 0.0, yy - 1.0)));
        out[i] = out[i] + value * amplitude;
        ++i;
      }
    }
  } else {
    double lX = 0.0, lXNY = 0.0, lXNZ = 0.0, lXNYNZ = 0.0;
    int i = 0, lastY = -1;
    // the y terms only depend on yi: compute them once per call (identical values)
    std::vector<double> yyA((size_t)ySize), vA((size_t)ySize);
    std::vector<int> YA((size_t)ySize);
    for (int yi = 0; yi < ySize; ++yi) {
      double yy = (y + (double)yi) * yScale + yo;
      YA[yi] = jd2i(fastFloor(yy)) & 0xFF;
      yy -= fastFloor(yy);
      yyA[yi] = yy;
      vA[yi] = fade(yy);
    }
    for (int xi = 0; xi < xSize; ++xi) {
      double xx = (x + (double)xi) * xScale + xo;
      int X = jd2i(fastFloor(xx)) & 0xFF;
      xx -= fastFloor(xx);
      double u = fade(xx);
      for (int zi = 0; zi < zSize; ++zi) {
        double zz = (z + (double)zi) * zScale + zo;
        int Z = jd2i(fastFloor(zz)) & 0xFF;
        zz -= fastFloor(zz);
        double w = fade(zz);
        for (int yi = 0; yi < ySize; ++yi) {
          const double yy = yyA[yi], v = vA[yi];
          const int Y = YA[yi];
          if (yi == 0 || Y != lastY) {
            lastY = Y;
            int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
            int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
            lX = lerp(u, grad(p[AA], xx, yy, zz), grad(p[BA], xx - 1.0, yy, zz));
            lXNY = lerp(u, grad(p[AB], xx, yy - 1.0, zz), grad(p[BB], xx - 1.0, yy - 1.0, zz));
            lXNZ = lerp(u, grad(p[AA + 1], xx, yy, zz - 1.0), grad(p[BA + 1], xx - 1.0, yy, zz - 1.0));
            lXNYNZ = lerp(u, grad(p[AB + 1], xx, yy - 1.0, zz - 1.0), grad(p[BB + 1], xx - 1.0, yy - 1.0, zz - 1.0));
          }
          double lXY = lerp(v, lX, lXNY);
          double lXYNZ = lerp(v, lXNZ, lXNYNZ);
          double lXYZ = lerp(w, lXY, lXYNZ);
          out[i] = out[i] + lXYZ * amplitude;
          ++i;
        }
      }
    }
  }
}

// ---- SimplexNoise --------------------------------------------------------------------------------
namespace {
const int GRAD3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                          {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
inline int sfloor(double d) { return d <= 0.0 ? jd2i(d) - 1 : jd2i(d); }
inline double dot2(const int* g, double x, double y) { return (double)g[0] * x + (double)g[1] * y; }
inline double dot3(const int* g, double x, double y, double z) {
  return (double)g[0] * x + (double)g[1] * y + (double)g[2] * z;
}
const double& F2() { static const double v = 0.5 * (std::sqrt(3.0) - 1.0); return v; }
const double& G2() { static const double v = (3.0 - std::sqrt(3.0)) / 6.0; return v; }
}  // namespace

Simplex::Simplex(JavaRandom& r) { initPerm(this, p_, r); }

std::vector<Simplex> Simplex::genOctaves(int64_t seed, int n, int pre) {
  JavaRandom r(seed);
  skipOctaves(r, pre);
  std::vector<Simplex> v;
  v.reserve(n);
  for (int i = 0; i < n; ++i) v.emplace_back(r);
  return v;
}

double Simplex::getValue2(double x, double z) const {
  const double f2 = F2(), g2 = G2();
  const int* p = p_;
  double n0, n1, n2;
  double s = (x + z) * f2;
  int i = sfloor(x + s);
  int k = sfloor(z + s);
  double t = (double)jadd(i, k) * g2;
  double X0 = (double)i - t;
  double x0 = x - X0;
  double Z0 = (double)k - t;
  double z0 = z - Z0;
  int i1, k1;
  if (x0 > z0) { i1 = 1; k1 = 0; } else { i1 = 0; k1 = 1; }
  double x1 = x0 - (double)i1 + g2;
  double z1 = z0 - (double)k1 + g2;
  double x2 = x0 - 1.0 + 2.0 * g2;
  double z2 = z0 - 1.0 + 2.0 * g2;
  int ib = i & 0xFF, kb = k & 0xFF;
  int gi0 = p[ib + p[kb]] % 12;
  int gi1 = p[ib + i1 + p[kb + k1]] % 12;
  int gi2 = p[ib + 1 + p[kb + 1]] % 12;
  double t0 = 0.5 - x0 * x0 - z0 * z0;
  if (t0 < 0.0) n0 = 0.0; else { t0 *= t0; n0 = t0 * t0 * dot2(GRAD3[gi0], x0, z0); }
  double t1 = 0.5 - x1 * x1 - z1 * z1;
  if (t1 < 0.0) n1 = 0.0; else { t1 *= t1; n1 = t1 * t1 * dot2(GRAD3[gi1], x1, z1); }
  double t2 = 0.5 - x2 * x2 - z2 * z2;
  if (t2 < 0.0) n2 = 0.0; else { t2 *= t2; n2 = t2 * t2 * dot2(GRAD3[gi2], x2, z2); }
  return 70.0 * (n0 + n1 + n2);
}

double Simplex::getValue3(double x, double y, double z) const {
  const int* p = p_;
  double n0, n1, n2, n3;
  int i1, j1, k1, i2, j2, k2;
  double s = (x + y + z) * 0.3333333333333333;
  int i = sfloor(x + s), j = sfloor(y + s), k = sfloor(z + s);
  double t = (double)jadd(jadd(i, j), k) * 0.16666666666666666;
  double X0 = (double)i - t, Y0 = (double)j - t, Z0 = (double)k - t;
  double x0 = x - X0, y0 = y - Y0, z0 = z - Z0;
  if (x0 >= y0) {
    if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
    else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
  } else if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
  else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
  else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
  double x1 = x0 - (double)i1 + 0.16666666666666666, y1 = y0 - (double)j1 + 0.16666666666666666,
         z1 = z0 - (double)k1 + 0.16666666666666666;
  double x2 = x0 - (double)i2 + 0.3333333333333333, y2 = y0 - (double)j2 + 0.3333333333333333,
         z2 = z0 - (double)k2 + 0.3333333333333333;
  double x3 = x0 - 1.0 + 0.5, y3 = y0 - 1.0 + 0.5, z3 = z0 - 1.0 + 0.5;
  int ib = i & 0xFF, jb = j & 0xFF, kb = k & 0xFF;
  int gi0 = p[ib + p[jb + p[kb]]] % 12;
  int gi1 = p[ib + i1 + p[jb + j1 + p[kb + k1]]] % 12;
  int gi2 = p[ib + i2 + p[jb + j2 + p[kb + k2]]] % 12;
  int gi3 = p[ib + 1 + p[jb + 1 + p[kb + 1]]] % 12;
  double t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 < 0.0) n0 = 0.0; else { t0 *= t0; n0 = t0 * t0 * dot3(GRAD3[gi0], x0, y0, z0); }
  double t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 < 0.0) n1 = 0.0; else { t1 *= t1; n1 = t1 * t1 * dot3(GRAD3[gi1], x1, y1, z1); }
  double t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 < 0.0) n2 = 0.0; else { t2 *= t2; n2 = t2 * t2 * dot3(GRAD3[gi2], x2, y2, z2); }
  double t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 < 0.0) n3 = 0.0; else { t3 *= t3; n3 = t3 * t3 * dot3(GRAD3[gi3], x3, y3, z3); }
  return 32.0 * (n0 + n1 + n2 + n3);
}

void Simplex::addRegion2(double* out, double x, double z, int xSize, int zSize, double xScale, double zScale,
                         double amplitude) const {
  xScale /= 1.5;
  zScale /= 1.5;
  amplitude *= 0.55;
  int i = 0;
  for (int xi = 0; xi < xSize; ++xi) {
    double xx = (x + (double)xi) * xScale + xo;
    for (int zi = 0; zi < zSize; ++zi) {
      double zz = (z + (double)zi) * zScale + yo;
      out[i] = out[i] + getValue2(xx, zz) * amplitude;
      ++i;
    }
  }
}

// ---- WorleyNoise ---------------------------------------------------------------------------------
Worley::Worley(JavaRandom& r) {
  initPerm(this, p_, r);
  for (int i = 0; i < 256; ++i) {
    jitter_[i][0] = r.nextDouble();
    jitter_[i][1] = r.nextDouble();
    jitter_[i][2] = r.nextDouble();
  }
}

void Worley::getCenter(double x, double z, double& cx, double& cy) const {
  double ipx = std::floor(x), ipy = std::floor(z);
  double fpx = x - ipx, fpy = z - ipy;
  double md = std::numeric_limits<double>::infinity();
  int iix = jd2i(ipx), iiy = jd2i(ipy);
  for (int i = -1; i <= 1; ++i) {
    for (int j = -1; j <= 1; ++j) {
      double gx = (double)i, gy = (double)j;
      int h = hash(jadd(iix, jd2i(std::floor(gx))), jadd(iiy, jd2i(std::floor(gy))));
      double ox = jitter_[h & 0xFF][0], oy = jitter_[h & 0xFF][1];
      double rx = gx + ox, ry = gy + oy;
      rx = rx - fpx; ry = ry - fpy;
      double d = rx * rx + ry * ry;
      if (!(d < md)) continue;
      cx = ipx + gx + ox;
      cy = ipy + gy + oy;
      md = d;
    }
  }
}

void Worley::getValue(double x, double z, Result& res) const {
  double ipx = std::floor(x), ipy = std::floor(z);
  double fpx = x - ipx, fpy = z - ipy;
  double mgx = 0, mgy = 0, mrx = 0, mry = 0, mfx = 0, mfy = 0;
  double md = std::numeric_limits<double>::infinity();
  int index = 0;
  int iix = jd2i(ipx), iiy = jd2i(ipy);
  for (int i = -1; i <= 1; ++i) {
    for (int j = -1; j <= 1; ++j) {
      double gx = (double)i, gy = (double)j;
      int h = hash(jadd(iix, jd2i(std::floor(gx))), jadd(iiy, jd2i(std::floor(gy))));
      double ox = jitter_[h & 0xFF][0], oy = jitter_[h & 0xFF][1];
      double rx = gx + ox, ry = gy + oy;
      rx = rx - fpx; ry = ry - fpy;
      double d = rx * rx + ry * ry;
      if (!(d < md)) continue;
      mfx = ipx + gx + ox;
      mfy = ipy + gy + oy;
      md = d;
      mrx = rx; mry = ry;
      mgx = gx; mgy = gy;
      index = jadd(jd2i(ox * 10.0), jd2i(oy * 10.0));
    }
  }
  md = std::numeric_limits<double>::infinity();
  for (int i = -2; i <= 2; ++i) {
    for (int j = -2; j <= 2; ++j) {
      double gx = mgx + (double)i, gy = mgy + (double)j;
      int h = hash(jadd(iix, jd2i(std::floor(gx))), jadd(iiy, jd2i(std::floor(gy))));
      double ox = jitter_[h & 0xFF][0], oy = jitter_[h & 0xFF][1];
      double rx = gx + ox, ry = gy + oy;
      rx = rx - fpx; ry = ry - fpy;
      double px = mrx - rx, py = mry - ry;
      if (!(px * px + py * py > 1.0E-5)) continue;
      gx = mrx + rx; gy = mry + ry;
      gx = gx * 0.5; gy = gy * 0.5;
      ox = rx - mrx; oy = ry - mry;
      double inv = 1.0 / std::sqrt(ox * ox + oy * oy);
      ox = ox * inv; oy = oy * inv;
      md = jmin(md, gx * ox + gy * oy);
    }
  }
  res.cx = mfx;
  res.cy = mfy;
  res.distance = 1.0 - md;
  res.index = index;
}

}  // namespace bta
