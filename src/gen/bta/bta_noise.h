// Bit-exact C++ ports of the Better than Adventure 8.0.1 noise classes used by its Nether generator
// (net.minecraft.core.world.noise.*): ImprovedPerlinNoise, SimplexNoise, WorleyNoise, FractalNoise2D/3D,
// operator.Normalize2D, plus MathHelper's sin/cos lookup table and floor helpers.
// All math is IEEE double/float exactly as in Java; compile WITHOUT -ffast-math and without FMA
// contraction (the .cpp files carry pragmas; keep -ffp-contract=off if you add -march flags).
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "jrandom.h"

namespace bta {

// ---- net.minecraft.core.util.helper.MathHelper -------------------------------------------------
namespace mh {
float sin(float rad);  // SIN_TABLE[(int)(rad * 10430.38f) & 0xFFFF]
float cos(float rad);  // SIN_TABLE[(int)(rad * 10430.38f + 16384.0f) & 0xFFFF]
inline int floor(double d) { int32_t t = jd2i(d); return d >= (double)t ? t : t - 1; }
inline int floorf(float f) { int32_t t = jf2i(f); return f >= (float)t ? t : t - 1; }
inline double unsignedMod(double v, double n) { return v - std::floor(v / n) * n; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
const float* sinTable();  // 65536 entries
}  // namespace mh

// ---- ImprovedPerlinNoise ----------------------------------------------------------------------
class ImprovedPerlin {
 public:
  enum class Legacy { DEFAULT, ALPHA, BETA };
  ImprovedPerlin(JavaRandom& r, Legacy t = Legacy::DEFAULT);
  static std::vector<ImprovedPerlin> genOctaves(int64_t seed, int numOctaves, int preOctaves = 0,
                                                Legacy t = Legacy::DEFAULT);
  double getValue2(double x, double z) const { return getValue3(x, z, 0.0); }
  double getValue3(double x, double y, double z) const;
  // Noise2D.addRegion -> addRegion3(out, x, z, 0, xSize, zSize, 1, xScale, zScale, 1, amp)
  void addRegion2(double* out, double x, double z, int xSize, int zSize, double xScale, double zScale,
                  double amplitude) const {
    addRegion3(out, x, z, 0.0, xSize, zSize, 1, xScale, zScale, 1.0, amplitude);
  }
  void addRegion3(double* out, double x, double y, double z, int xSize, int ySize, int zSize,
                  double xScale, double yScale, double zScale, double amplitude) const;
  static double rangeMin();  // -sqrt(0.75)
  static double rangeMax();  //  sqrt(0.75)
  double xo, yo, zo;

 private:
  double fastFloor(double v) const;
  int p_[512];
  bool edgeY_, edgeZ_, farLands_;
};

// ---- SimplexNoise -----------------------------------------------------------------------------
class Simplex {
 public:
  explicit Simplex(JavaRandom& r);
  static std::vector<Simplex> genOctaves(int64_t seed, int numOctaves, int preOctaves = 0);
  double getValue2(double x, double z) const;
  double getValue3(double x, double y, double z) const;
  void addRegion2(double* out, double x, double z, int xSize, int zSize, double xScale, double zScale,
                  double amplitude) const;
  static double rangeMin() { return -1.0; }
  static double rangeMax() { return 1.0; }
  double xo, yo, zo;

 private:
  int p_[512];
};

// ---- WorleyNoise (2D cell query used by the floes, shelf and nether pillars) --------------------
class Worley {
 public:
  struct Result { double distance = 0; int32_t index = 0; double cx = 0, cy = 0; };
  explicit Worley(JavaRandom& r);
  void getValue(double x, double z, Result& out) const;
  // only the cell centre (first pass) - identical centre to getValue(), cheaper
  void getCenter(double x, double z, double& cx, double& cy) const;
  double xo, yo, zo;

 private:
  int hash(int x, int z) const { return p_[(int)(((uint32_t)x + (uint32_t)p_[z & 0xFF]) & 0xFFu)]; }
  int p_[512];
  double jitter_[256][3];
};

// ---- FractalNoise2D / FractalNoise3D (lacunarity = frequency mul, persistence = amplitude mul) --
template <class T>
class Fractal {
 public:
  Fractal() = default;
  explicit Fractal(std::vector<T> oct) : oct_(std::move(oct)) {}
  Fractal& setLacunarity(double l) { lac_ = l; return *this; }
  double lacunarity() const { return lac_; }
  // FractalNoise2D.getValue(x, z)
  double getValue2(double x, double z) const {
    double out = 0.0, f = 1.0, a = 1.0;
    for (const T& o : oct_) { out += o.getValue2(x * f, z * f) * a; f *= lac_; a *= per_; }
    return out;
  }
  double getValue3(double x, double y, double z) const {
    double out = 0.0, f = 1.0, a = 1.0;
    for (const T& o : oct_) { out += o.getValue3(x * f, y * f, z * f) * a; f *= lac_; a *= per_; }
    return out;
  }
  // Noise2D.getRegion -> FractalNoise2D.addRegion (zero-fills `out`, size xSize*zSize)
  void getRegion2(double* out, double x, double z, int xSize, int zSize, double xScale, double zScale) const {
    for (int i = 0; i < xSize * zSize; ++i) out[i] = 0.0;
    double f = 1.0, a = 1.0;
    for (const T& o : oct_) { o.addRegion2(out, x, z, xSize, zSize, xScale * f, zScale * f, a); f *= lac_; a *= per_; }
  }
  // Noise3D.getRegion -> FractalNoise3D.addRegion (zero-fills `out`, size xSize*ySize*zSize)
  void getRegion3(double* out, double x, double y, double z, int xSize, int ySize, int zSize, double xScale,
                  double yScale, double zScale) const {
    for (int i = 0; i < xSize * ySize * zSize; ++i) out[i] = 0.0;
    double f = 1.0, a = 1.0;
    for (const T& o : oct_) {
      o.addRegion3(out, x, y, z, xSize, ySize, zSize, xScale * f, yScale * f, zScale * f, a);
      f *= lac_; a *= per_;
    }
  }
  // FractalNoise2D.calculateRange()
  void range(double& mn, double& mx) const {
    mn = 0.0; mx = 0.0; double a = 1.0;
    for (size_t i = 0; i < oct_.size(); ++i) { mn += T::rangeMin() * a; mx += T::rangeMax() * a; a *= per_; }
  }
  const std::vector<T>& octaves() const { return oct_; }

 private:
  std::vector<T> oct_;
  double lac_ = 0.5, per_ = 2.0;
};

// operator.Normalize2D over a FractalNoise2D<ImprovedPerlin>
class Normalize2D {
 public:
  explicit Normalize2D(Fractal<ImprovedPerlin> n) : n_(std::move(n)) { n_.range(mn_, mx_); }
  double getValue2(double x, double z) const {
    double r = mx_ - mn_;
    double v = n_.getValue2(x, z);
    double vo = v - mn_;
    return vo / r;
  }

 private:
  Fractal<ImprovedPerlin> n_;
  double mn_ = 0, mx_ = 0;
};

}  // namespace bta
