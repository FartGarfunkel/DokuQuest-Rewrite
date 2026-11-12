// Bit-exact port of java.util.Random (legacy LCG) plus the Java numeric helpers the BTA
// generator ports need (saturating double->int casts, Math.min/max NaN semantics, fdlibm log).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#ifdef BTA_RANDOM_DEBUG
#include <cstdio>
#endif

namespace bta {

// ---- Java casts / helpers ---------------------------------------------------------------------
// (int)double in Java: NaN -> 0, saturates at Integer.MIN/MAX_VALUE, otherwise truncates toward 0.
inline int32_t jd2i(double d) {
  if (d != d) return 0;
  if (d >= 2147483647.0) return INT32_MAX;
  if (d <= -2147483648.0) return INT32_MIN;
  return (int32_t)d;
}
inline int64_t jd2l(double d) {
  if (d != d) return 0;
  if (d >= 9223372036854775807.0) return INT64_MAX;
  if (d <= -9223372036854775808.0) return INT64_MIN;
  return (int64_t)d;
}
inline int32_t jf2i(float f) { return jd2i((double)f); }
// int / long arithmetic with Java wrap-around
inline int32_t jadd(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t jsub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t jmul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
inline int64_t ladd(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
inline int64_t lmul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
// java.lang.Math.min/max(double,double)
inline double jmin(double a, double b) {
  if (a != a) return a;
  if (a == 0.0 && b == 0.0) return std::signbit(b) ? b : a;
  return a <= b ? a : b;
}
inline double jmax(double a, double b) {
  if (a != a) return a;
  if (a == 0.0 && b == 0.0) return std::signbit(a) ? b : a;
  return a >= b ? a : b;
}
inline float jminf(float a, float b) {
  if (a != a) return a;
  if (a == 0.0f && b == 0.0f) return std::signbit(b) ? b : a;
  return a <= b ? a : b;
}
inline float jmaxf(float a, float b) {
  if (a != a) return a;
  if (a == 0.0f && b == 0.0f) return std::signbit(a) ? b : a;
  return a >= b ? a : b;
}

// StrictMath.log (fdlibm e_log.c), used by Random.nextGaussian. Bit-exact with Java.
inline double fdlibm_log(double x) {
  static const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10,
                      two54 = 1.80143985094819840000e+16, Lg1 = 6.666666666666735130e-01,
                      Lg2 = 3.999999999940941908e-01, Lg3 = 2.857142874366239149e-01,
                      Lg4 = 2.222219843214978396e-01, Lg5 = 1.818357216161805012e-01,
                      Lg6 = 1.531383769920937332e-01, Lg7 = 1.479819860511658591e-01;
  uint64_t bits; std::memcpy(&bits, &x, 8);
  int32_t hx = (int32_t)(bits >> 32); uint32_t lx = (uint32_t)bits;
  int32_t k = 0;
  if (hx < 0x00100000) {
    if (((hx & 0x7fffffff) | (int32_t)lx) == 0) return -std::numeric_limits<double>::infinity();   // (fdlibm: -two54 / 0; MSVC refuses a constant / 0)
    if (hx < 0) return std::numeric_limits<double>::quiet_NaN();                                // (fdlibm: (x - x) / 0)
    k -= 54; x *= two54;
    std::memcpy(&bits, &x, 8); hx = (int32_t)(bits >> 32);
  }
  if (hx >= 0x7ff00000) return x + x;
  k += (hx >> 20) - 1023;
  hx &= 0x000fffff;
  int32_t i = (hx + 0x95f64) & 0x100000;
  std::memcpy(&bits, &x, 8);
  bits = ((uint64_t)(uint32_t)(hx | (i ^ 0x3ff00000)) << 32) | (bits & 0xffffffffULL);
  std::memcpy(&x, &bits, 8);
  k += (i >> 20);
  double f = x - 1.0;
  if ((0x000fffff & (2 + hx)) < 3) {
    if (f == 0.0) { if (k == 0) return 0.0; double dk = (double)k; return dk * ln2_hi + dk * ln2_lo; }
    double R = f * f * (0.5 - 0.33333333333333333 * f);
    if (k == 0) return f - R;
    double dk = (double)k; return dk * ln2_hi - ((R - dk * ln2_lo) - f);
  }
  double s = f / (2.0 + f), dk = (double)k, z = s * s;
  i = hx - 0x6147a;
  double w = z * z;
  int32_t j = 0x6b851 - hx;
  double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
  double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
  i |= j;
  double R = t2 + t1;
  if (i > 0) {
    double hfsq = 0.5 * f * f;
    if (k == 0) return f - (hfsq - s * (hfsq + R));
    return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
  }
  if (k == 0) return f - s * (f - R);
  return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

// ---- java.util.Random -------------------------------------------------------------------------
class JavaRandom {
 public:
  static constexpr uint64_t kMult = 0x5DEECE66DULL, kAdd = 0xBULL, kMask = (1ULL << 48) - 1;
  JavaRandom() : JavaRandom(0) {}
  explicit JavaRandom(int64_t seed) { setSeed(seed); }
  void setSeed(int64_t seed) { seed_ = ((uint64_t)seed ^ kMult) & kMask; haveNextNextGaussian_ = false; }
  int32_t next(int bits) {
    seed_ = (seed_ * kMult + kAdd) & kMask;
    int32_t v = (int32_t)(uint32_t)(seed_ >> (48 - bits));
#ifdef BTA_RANDOM_DEBUG
    if (debugLog) std::printf("R %d %d\n", bits, v);
#endif
    return v;
  }
#ifdef BTA_RANDOM_DEBUG
  bool debugLog = false;
#endif
  int32_t nextInt() { return next(32); }
  // bound must be > 0 (Java throws otherwise; callers never pass <= 0 in valid BTA paths)
  int32_t nextInt(int32_t bound) {
    int32_t r = next(31);
    int32_t m = bound - 1;
    if ((bound & m) == 0) return (int32_t)(((int64_t)bound * (int64_t)r) >> 31);
    for (int32_t u = r; (int32_t)((uint32_t)u - (uint32_t)(r = u % bound) + (uint32_t)m) < 0; u = next(31)) {}
    return r;
  }
  int64_t nextLong() {
    int64_t hi = (int64_t)next(32);
    int64_t lo = (int64_t)next(32);
    return (int64_t)(((uint64_t)hi << 32) + (uint64_t)lo);
  }
  bool nextBoolean() { return next(1) != 0; }
  float nextFloat() { return (float)next(24) / (float)(1 << 24); }
  double nextDouble() {
    int64_t a = (int64_t)next(26);
    int64_t b = (int64_t)next(27);
    return (double)((a << 27) + b) * 0x1.0p-53;
  }
  double nextGaussian() {
    if (haveNextNextGaussian_) { haveNextNextGaussian_ = false; return nextNextGaussian_; }
    double v1, v2, s;
    do {
      v1 = 2 * nextDouble() - 1;
      v2 = 2 * nextDouble() - 1;
      s = v1 * v1 + v2 * v2;
    } while (s >= 1 || s == 0);
    double multiplier = std::sqrt(-2 * fdlibm_log(s) / s);
    nextNextGaussian_ = v2 * multiplier;
    haveNextNextGaussian_ = true;
    return v1 * multiplier;
  }
  uint64_t rawSeed() const { return seed_; }
  void setRawForTesting(uint64_t s) { seed_ = s & kMask; haveNextNextGaussian_ = false; }

 private:
  uint64_t seed_ = 0;
  double nextNextGaussian_ = 0;
  bool haveNextNextGaussian_ = false;
};

}  // namespace bta
