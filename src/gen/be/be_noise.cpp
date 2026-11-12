// gen/be/be_noise.cpp — see be_noise.h. The two eval bodies are the jar's OpenSimplexNoise (CFR decompile of
// org/betterx/betterend/noise/OpenSimplexNoise.class), transcribed statement for statement so the doubles match.
// CFR mis-rendered two "ysv_ext0 = ysv_ext1 = ysb...; if (..) ysv_ext1 -= 1; else ysv_ext0 -= 1;" blocks in 3D eval
// (it hoisted the decrement out of the branch); they are written as in Kurt Spencer's original, which is what the
// class does: 36000 random 2D/3D evals over 9 seeds plus a lattice and java.util.Random draws compared bit for bit
// against the jar's class running on Java 25 (README.md, "Validation").
#include "gen/be/be_noise.h"

// keep the doubles Java-exact: no fused multiply-add
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace dq::be {

int32_t JavaRandom::nextInt(int32_t bound) {
  if (bound <= 0) return 0;
  int32_t r = next(31);
  const int32_t m = bound - 1;
  if ((bound & m) == 0) return int32_t((int64_t(bound) * int64_t(r)) >> 31);
  for (int32_t u = r; int64_t(u) - (r = u % bound) + m > INT32_MAX; u = next(31)) {}
  return r;
}

namespace {
const int8_t gradients2D[] = {5, 2, 2, 5, -5, 2, -2, 5, 5, -2, 2, -5, -5, -2, -2, -5};
const int8_t gradients3D[] = {-11, 4, 4, -4, 11, 4, -4, 4, 11, 11, 4, 4, 4, 11, 4, 4, 4, 11, -11, -4, 4, -4, -11, 4,
                              -4, -4, 11, 11, -4, 4, 4, -11, 4, 4, -4, 11, -11, 4, -4, -4, 11, -4, -4, 4, -11, 11, 4, -4,
                              4, 11, -4, 4, 4, -11, -11, -4, -4, -4, -11, -4, -4, -4, -11, 11, -4, -4, 4, -11, -4, 4, -4, -11};
}  // namespace

OpenSimplexNoise::OpenSimplexNoise(int64_t seed) {
  int16_t source[256];
  for (int i = 0; i < 256; ++i) source[i] = int16_t(i);
  uint64_t s = uint64_t(seed);
  const uint64_t A = 6364136223846793005ULL, C = 1442695040888963407ULL;
  s = s * A + C;
  s = s * A + C;
  s = s * A + C;
  for (int i = 255; i >= 0; --i) {
    s = s * A + C;
    int r = int((int64_t)(s + 31ULL) % int64_t(i + 1));
    if (r < 0) r += i + 1;
    perm[i] = source[r];
    permGradIndex3D[i] = int16_t(perm[i] % (int(sizeof(gradients3D)) / 3) * 3);
    source[r] = source[i];
  }
}

double OpenSimplexNoise::extrapolate(int xsb, int ysb, double dx, double dy) const {
  const int index = perm[(perm[xsb & 0xFF] + ysb) & 0xFF] & 0x0E;
  return double(gradients2D[index]) * dx + double(gradients2D[index + 1]) * dy;
}

double OpenSimplexNoise::extrapolate(int xsb, int ysb, int zsb, double dx, double dy, double dz) const {
  const int index = permGradIndex3D[(perm[(perm[xsb & 0xFF] + ysb) & 0xFF] + zsb) & 0xFF];
  return double(gradients3D[index]) * dx + double(gradients3D[index + 1]) * dy + double(gradients3D[index + 2]) * dz;
}

double OpenSimplexNoise::eval(double x, double y) const {
  double zins;
  double attn_ext;
  double dy_ext;
  double dx_ext;
  int ysv_ext;
  int xsv_ext;
  double dy2;
  double dx2;
  double attn2;
  double stretchOffset = (x + y) * -0.211324865405187;
  double xs = x + stretchOffset;
  double ys = y + stretchOffset;
  int xsb = fastFloor(xs);
  int ysb = fastFloor(ys);
  double squishOffset = (double)(xsb + ysb) * 0.366025403784439;
  double xb = (double)xsb + squishOffset;
  double yb = (double)ysb + squishOffset;
  double xins = xs - (double)xsb;
  double yins = ys - (double)ysb;
  double inSum = xins + yins;
  double dx0 = x - xb;
  double dy0 = y - yb;
  double value = 0.0;
  double dx1 = dx0 - 1.0 - 0.366025403784439;
  double dy1 = dy0 - 0.0 - 0.366025403784439;
  double attn1 = 2.0 - dx1 * dx1 - dy1 * dy1;
  if (attn1 > 0.0) {
    attn1 *= attn1;
    value += attn1 * attn1 * extrapolate(xsb + 1, ysb + 0, dx1, dy1);
  }
  dx2 = dx0 - 0.0 - 0.366025403784439;
  dy2 = dy0 - 1.0 - 0.366025403784439;
  attn2 = 2.0 - dx2 * dx2 - dy2 * dy2;
  if (attn2 > 0.0) {
    attn2 *= attn2;
    value += attn2 * attn2 * extrapolate(xsb + 0, ysb + 1, dx2, dy2);
  }
  if (inSum <= 1.0) {
    zins = 1.0 - inSum;
    if (zins > xins || zins > yins) {
      if (xins > yins) {
        xsv_ext = xsb + 1;
        ysv_ext = ysb - 1;
        dx_ext = dx0 - 1.0;
        dy_ext = dy0 + 1.0;
      } else {
        xsv_ext = xsb - 1;
        ysv_ext = ysb + 1;
        dx_ext = dx0 + 1.0;
        dy_ext = dy0 - 1.0;
      }
    } else {
      xsv_ext = xsb + 1;
      ysv_ext = ysb + 1;
      dx_ext = dx0 - 1.0 - 0.732050807568878;
      dy_ext = dy0 - 1.0 - 0.732050807568878;
    }
  } else {
    zins = 2.0 - inSum;
    if (zins < xins || zins < yins) {
      if (xins > yins) {
        xsv_ext = xsb + 2;
        ysv_ext = ysb + 0;
        dx_ext = dx0 - 2.0 - 0.732050807568878;
        dy_ext = dy0 + 0.0 - 0.732050807568878;
      } else {
        xsv_ext = xsb + 0;
        ysv_ext = ysb + 2;
        dx_ext = dx0 + 0.0 - 0.732050807568878;
        dy_ext = dy0 - 2.0 - 0.732050807568878;
      }
    } else {
      dx_ext = dx0;
      dy_ext = dy0;
      xsv_ext = xsb;
      ysv_ext = ysb;
    }
    ++xsb;
    ++ysb;
    dx0 = dx0 - 1.0 - 0.732050807568878;
    dy0 = dy0 - 1.0 - 0.732050807568878;
  }
  double attn0 = 2.0 - dx0 * dx0 - dy0 * dy0;
  if (attn0 > 0.0) {
    attn0 *= attn0;
    value += attn0 * attn0 * extrapolate(xsb, ysb, dx0, dy0);
  }
  attn_ext = 2.0 - dx_ext * dx_ext - dy_ext * dy_ext;
  if (attn_ext > 0.0) {
    attn_ext *= attn_ext;
    value += attn_ext * attn_ext * extrapolate(xsv_ext, ysv_ext, dx_ext, dy_ext);
  }
  return value / 47.0;
}

double OpenSimplexNoise::eval(double x, double y, double z) const {
  int c;
  double score;
  double attn_ext1;
  double dz_ext1;
  double dz_ext0;
  int zsv_ext1;
  int zsv_ext0;
  double dy_ext0;
  double dy_ext1;
  int ysv_ext0;
  int ysv_ext1;
  double dx_ext1;
  double dx_ext0;
  int xsv_ext1;
  int xsv_ext0;
  double stretchOffset = (x + y + z) * -0.16666666666666666;
  double xs = x + stretchOffset;
  double ys = y + stretchOffset;
  double zs = z + stretchOffset;
  int xsb = fastFloor(xs);
  int ysb = fastFloor(ys);
  int zsb = fastFloor(zs);
  double squishOffset = (double)(xsb + ysb + zsb) * 0.3333333333333333;
  double xb = (double)xsb + squishOffset;
  double yb = (double)ysb + squishOffset;
  double zb = (double)zsb + squishOffset;
  double xins = xs - (double)xsb;
  double yins = ys - (double)ysb;
  double zins = zs - (double)zsb;
  double inSum = xins + yins + zins;
  double dx0 = x - xb;
  double dy0 = y - yb;
  double dz0 = z - zb;
  double value = 0.0;
  if (inSum <= 1.0) {
    double dz3;
    double dy3;
    double dx3;
    double attn3;
    double dz2;
    double dy2;
    double dx2;
    double attn2;
    double dz1;
    double dy1;
    double dx1;
    double attn1;
    int aPoint = 1;
    double aScore = xins;
    int bPoint = 2;
    double bScore = yins;
    if (aScore >= bScore && zins > bScore) {
      bScore = zins;
      bPoint = 4;
    } else if (aScore < bScore && zins > aScore) {
      aScore = zins;
      aPoint = 4;
    }
    double wins = 1.0 - inSum;
    if (wins > aScore || wins > bScore) {
      c = bScore > aScore ? bPoint : aPoint;
      if ((c & 1) == 0) {
        xsv_ext0 = xsb - 1;
        xsv_ext1 = xsb;
        dx_ext0 = dx0 + 1.0;
        dx_ext1 = dx0;
      } else {
        xsv_ext0 = xsv_ext1 = xsb + 1;
        dx_ext0 = dx_ext1 = dx0 - 1.0;
      }
      if ((c & 2) == 0) {
        ysv_ext0 = ysv_ext1 = ysb;
        dy_ext0 = dy_ext1 = dy0;
        if ((c & 1) == 0) {
          --ysv_ext1;
          dy_ext1 += 1.0;
        } else {
          --ysv_ext0;
          dy_ext0 += 1.0;
        }
      } else {
        ysv_ext0 = ysv_ext1 = ysb + 1;
        dy_ext0 = dy_ext1 = dy0 - 1.0;
      }
      if ((c & 4) == 0) {
        zsv_ext0 = zsb;
        zsv_ext1 = zsb - 1;
        dz_ext0 = dz0;
        dz_ext1 = dz0 + 1.0;
      } else {
        zsv_ext0 = zsv_ext1 = zsb + 1;
        dz_ext0 = dz_ext1 = dz0 - 1.0;
      }
    } else {
      c = (aPoint | bPoint);
      if ((c & 1) == 0) {
        xsv_ext0 = xsb;
        xsv_ext1 = xsb - 1;
        dx_ext0 = dx0 - 0.6666666666666666;
        dx_ext1 = dx0 + 1.0 - 0.3333333333333333;
      } else {
        xsv_ext0 = xsv_ext1 = xsb + 1;
        dx_ext0 = dx0 - 1.0 - 0.6666666666666666;
        dx_ext1 = dx0 - 1.0 - 0.3333333333333333;
      }
      if ((c & 2) == 0) {
        ysv_ext0 = ysb;
        ysv_ext1 = ysb - 1;
        dy_ext0 = dy0 - 0.6666666666666666;
        dy_ext1 = dy0 + 1.0 - 0.3333333333333333;
      } else {
        ysv_ext0 = ysv_ext1 = ysb + 1;
        dy_ext0 = dy0 - 1.0 - 0.6666666666666666;
        dy_ext1 = dy0 - 1.0 - 0.3333333333333333;
      }
      if ((c & 4) == 0) {
        zsv_ext0 = zsb;
        zsv_ext1 = zsb - 1;
        dz_ext0 = dz0 - 0.6666666666666666;
        dz_ext1 = dz0 + 1.0 - 0.3333333333333333;
      } else {
        zsv_ext0 = zsv_ext1 = zsb + 1;
        dz_ext0 = dz0 - 1.0 - 0.6666666666666666;
        dz_ext1 = dz0 - 1.0 - 0.3333333333333333;
      }
    }
    double attn0 = 2.0 - dx0 * dx0 - dy0 * dy0 - dz0 * dz0;
    if (attn0 > 0.0) {
      attn0 *= attn0;
      value += attn0 * attn0 * extrapolate(xsb + 0, ysb + 0, zsb + 0, dx0, dy0, dz0);
    }
    dx1 = dx0 - 1.0 - 0.3333333333333333;
    dy1 = dy0 - 0.0 - 0.3333333333333333;
    dz1 = dz0 - 0.0 - 0.3333333333333333;
    attn1 = 2.0 - dx1 * dx1 - dy1 * dy1 - dz1 * dz1;
    if (attn1 > 0.0) {
      attn1 *= attn1;
      value += attn1 * attn1 * extrapolate(xsb + 1, ysb + 0, zsb + 0, dx1, dy1, dz1);
    }
    dx2 = dx0 - 0.0 - 0.3333333333333333;
    dy2 = dy0 - 1.0 - 0.3333333333333333;
    dz2 = dz1;
    attn2 = 2.0 - dx2 * dx2 - dy2 * dy2 - dz2 * dz2;
    if (attn2 > 0.0) {
      attn2 *= attn2;
      value += attn2 * attn2 * extrapolate(xsb + 0, ysb + 1, zsb + 0, dx2, dy2, dz2);
    }
    dx3 = dx2;
    dy3 = dy1;
    dz3 = dz0 - 1.0 - 0.3333333333333333;
    attn3 = 2.0 - dx3 * dx3 - dy3 * dy3 - dz3 * dz3;
    if (attn3 > 0.0) {
      attn3 *= attn3;
      value += attn3 * attn3 * extrapolate(xsb + 0, ysb + 0, zsb + 1, dx3, dy3, dz3);
    }
  } else if (inSum >= 2.0) {
    double attn0;
    double dz1;
    double dy1;
    double dx1;
    double attn1;
    double dz2;
    double dy2;
    double dx2;
    double attn2;
    int aPoint = 6;
    double aScore = xins;
    int bPoint = 5;
    double bScore = yins;
    if (aScore <= bScore && zins < bScore) {
      bScore = zins;
      bPoint = 3;
    } else if (aScore > bScore && zins < aScore) {
      aScore = zins;
      aPoint = 3;
    }
    double wins = 3.0 - inSum;
    if (wins < aScore || wins < bScore) {
      c = bScore < aScore ? bPoint : aPoint;
      if ((c & 1) != 0) {
        xsv_ext0 = xsb + 2;
        xsv_ext1 = xsb + 1;
        dx_ext0 = dx0 - 2.0 - 1.0;
        dx_ext1 = dx0 - 1.0 - 1.0;
      } else {
        xsv_ext0 = xsv_ext1 = xsb;
        dx_ext0 = dx_ext1 = dx0 - 1.0;
      }
      if ((c & 2) != 0) {
        ysv_ext0 = ysv_ext1 = ysb + 1;
        dy_ext0 = dy_ext1 = dy0 - 1.0 - 1.0;
        if ((c & 1) != 0) {
          ++ysv_ext1;
          dy_ext1 -= 1.0;
        } else {
          ++ysv_ext0;
          dy_ext0 -= 1.0;
        }
      } else {
        ysv_ext0 = ysv_ext1 = ysb;
        dy_ext0 = dy_ext1 = dy0 - 1.0;
      }
      if ((c & 4) != 0) {
        zsv_ext0 = zsb + 1;
        zsv_ext1 = zsb + 2;
        dz_ext0 = dz0 - 1.0 - 1.0;
        dz_ext1 = dz0 - 2.0 - 1.0;
      } else {
        zsv_ext0 = zsv_ext1 = zsb;
        dz_ext0 = dz_ext1 = dz0 - 1.0;
      }
    } else {
      c = (aPoint & bPoint);
      if ((c & 1) != 0) {
        xsv_ext0 = xsb + 1;
        xsv_ext1 = xsb + 2;
        dx_ext0 = dx0 - 1.0 - 0.3333333333333333;
        dx_ext1 = dx0 - 2.0 - 0.6666666666666666;
      } else {
        xsv_ext0 = xsv_ext1 = xsb;
        dx_ext0 = dx0 - 0.3333333333333333;
        dx_ext1 = dx0 - 0.6666666666666666;
      }
      if ((c & 2) != 0) {
        ysv_ext0 = ysb + 1;
        ysv_ext1 = ysb + 2;
        dy_ext0 = dy0 - 1.0 - 0.3333333333333333;
        dy_ext1 = dy0 - 2.0 - 0.6666666666666666;
      } else {
        ysv_ext0 = ysv_ext1 = ysb;
        dy_ext0 = dy0 - 0.3333333333333333;
        dy_ext1 = dy0 - 0.6666666666666666;
      }
      if ((c & 4) != 0) {
        zsv_ext0 = zsb + 1;
        zsv_ext1 = zsb + 2;
        dz_ext0 = dz0 - 1.0 - 0.3333333333333333;
        dz_ext1 = dz0 - 2.0 - 0.6666666666666666;
      } else {
        zsv_ext0 = zsv_ext1 = zsb;
        dz_ext0 = dz0 - 0.3333333333333333;
        dz_ext1 = dz0 - 0.6666666666666666;
      }
    }
    double dx3 = dx0 - 1.0 - 0.6666666666666666;
    double dy3 = dy0 - 1.0 - 0.6666666666666666;
    double dz3 = dz0 - 0.0 - 0.6666666666666666;
    double attn3 = 2.0 - dx3 * dx3 - dy3 * dy3 - dz3 * dz3;
    if (attn3 > 0.0) {
      attn3 *= attn3;
      value += attn3 * attn3 * extrapolate(xsb + 1, ysb + 1, zsb + 0, dx3, dy3, dz3);
    }
    dx2 = dx3;
    dy2 = dy0 - 0.0 - 0.6666666666666666;
    dz2 = dz0 - 1.0 - 0.6666666666666666;
    attn2 = 2.0 - dx2 * dx2 - dy2 * dy2 - dz2 * dz2;
    if (attn2 > 0.0) {
      attn2 *= attn2;
      value += attn2 * attn2 * extrapolate(xsb + 1, ysb + 0, zsb + 1, dx2, dy2, dz2);
    }
    dx1 = dx0 - 0.0 - 0.6666666666666666;
    dy1 = dy3;
    dz1 = dz2;
    attn1 = 2.0 - dx1 * dx1 - dy1 * dy1 - dz1 * dz1;
    if (attn1 > 0.0) {
      attn1 *= attn1;
      value += attn1 * attn1 * extrapolate(xsb + 0, ysb + 1, zsb + 1, dx1, dy1, dz1);
    }
    dx0 = dx0 - 1.0 - 1.0;
    dy0 = dy0 - 1.0 - 1.0;
    dz0 = dz0 - 1.0 - 1.0;
    attn0 = 2.0 - dx0 * dx0 - dy0 * dy0 - dz0 * dz0;
    if (attn0 > 0.0) {
      attn0 *= attn0;
      value += attn0 * attn0 * extrapolate(xsb + 1, ysb + 1, zsb + 1, dx0, dy0, dz0);
    }
  } else {
    double dz6;
    double dy6;
    double dx6;
    double attn6;
    double dz5;
    double dy5;
    double dx5;
    double attn5;
    double dz4;
    double dy4;
    double dx4;
    double attn4;
    double dz3;
    double dy3;
    double dx3;
    double attn3;
    double dz2;
    double dy2;
    double dx2;
    double attn2;
    bool bIsFurtherSide;
    int bPoint;
    double bScore;
    bool aIsFurtherSide;
    int aPoint;
    double aScore;
    double p1 = xins + yins;
    if (p1 > 1.0) {
      aScore = p1 - 1.0;
      aPoint = 3;
      aIsFurtherSide = true;
    } else {
      aScore = 1.0 - p1;
      aPoint = 4;
      aIsFurtherSide = false;
    }
    double p2 = xins + zins;
    if (p2 > 1.0) {
      bScore = p2 - 1.0;
      bPoint = 5;
      bIsFurtherSide = true;
    } else {
      bScore = 1.0 - p2;
      bPoint = 2;
      bIsFurtherSide = false;
    }
    double p3 = yins + zins;
    if (p3 > 1.0) {
      score = p3 - 1.0;
      if (aScore <= bScore && aScore < score) {
        aScore = score;
        aPoint = 6;
        aIsFurtherSide = true;
      } else if (aScore > bScore && bScore < score) {
        bScore = score;
        bPoint = 6;
        bIsFurtherSide = true;
      }
    } else {
      score = 1.0 - p3;
      if (aScore <= bScore && aScore < score) {
        aScore = score;
        aPoint = 1;
        aIsFurtherSide = false;
      } else if (aScore > bScore && bScore < score) {
        bScore = score;
        bPoint = 1;
        bIsFurtherSide = false;
      }
    }
    if (aIsFurtherSide == bIsFurtherSide) {
      if (aIsFurtherSide) {
        dx_ext0 = dx0 - 1.0 - 1.0;
        dy_ext0 = dy0 - 1.0 - 1.0;
        dz_ext0 = dz0 - 1.0 - 1.0;
        xsv_ext0 = xsb + 1;
        ysv_ext0 = ysb + 1;
        zsv_ext0 = zsb + 1;
        c = (aPoint & bPoint);
        if ((c & 1) != 0) {
          dx_ext1 = dx0 - 2.0 - 0.6666666666666666;
          dy_ext1 = dy0 - 0.6666666666666666;
          dz_ext1 = dz0 - 0.6666666666666666;
          xsv_ext1 = xsb + 2;
          ysv_ext1 = ysb;
          zsv_ext1 = zsb;
        } else if ((c & 2) != 0) {
          dx_ext1 = dx0 - 0.6666666666666666;
          dy_ext1 = dy0 - 2.0 - 0.6666666666666666;
          dz_ext1 = dz0 - 0.6666666666666666;
          xsv_ext1 = xsb;
          ysv_ext1 = ysb + 2;
          zsv_ext1 = zsb;
        } else {
          dx_ext1 = dx0 - 0.6666666666666666;
          dy_ext1 = dy0 - 0.6666666666666666;
          dz_ext1 = dz0 - 2.0 - 0.6666666666666666;
          xsv_ext1 = xsb;
          ysv_ext1 = ysb;
          zsv_ext1 = zsb + 2;
        }
      } else {
        dx_ext0 = dx0;
        dy_ext0 = dy0;
        dz_ext0 = dz0;
        xsv_ext0 = xsb;
        ysv_ext0 = ysb;
        zsv_ext0 = zsb;
        c = (aPoint | bPoint);
        if ((c & 1) == 0) {
          dx_ext1 = dx0 + 1.0 - 0.3333333333333333;
          dy_ext1 = dy0 - 1.0 - 0.3333333333333333;
          dz_ext1 = dz0 - 1.0 - 0.3333333333333333;
          xsv_ext1 = xsb - 1;
          ysv_ext1 = ysb + 1;
          zsv_ext1 = zsb + 1;
        } else if ((c & 2) == 0) {
          dx_ext1 = dx0 - 1.0 - 0.3333333333333333;
          dy_ext1 = dy0 + 1.0 - 0.3333333333333333;
          dz_ext1 = dz0 - 1.0 - 0.3333333333333333;
          xsv_ext1 = xsb + 1;
          ysv_ext1 = ysb - 1;
          zsv_ext1 = zsb + 1;
        } else {
          dx_ext1 = dx0 - 1.0 - 0.3333333333333333;
          dy_ext1 = dy0 - 1.0 - 0.3333333333333333;
          dz_ext1 = dz0 + 1.0 - 0.3333333333333333;
          xsv_ext1 = xsb + 1;
          ysv_ext1 = ysb + 1;
          zsv_ext1 = zsb - 1;
        }
      }
    } else {
      int c2;
      int c1;
      if (aIsFurtherSide) {
        c1 = aPoint;
        c2 = bPoint;
      } else {
        c1 = bPoint;
        c2 = aPoint;
      }
      if ((c1 & 1) == 0) {
        dx_ext0 = dx0 + 1.0 - 0.3333333333333333;
        dy_ext0 = dy0 - 1.0 - 0.3333333333333333;
        dz_ext0 = dz0 - 1.0 - 0.3333333333333333;
        xsv_ext0 = xsb - 1;
        ysv_ext0 = ysb + 1;
        zsv_ext0 = zsb + 1;
      } else if ((c1 & 2) == 0) {
        dx_ext0 = dx0 - 1.0 - 0.3333333333333333;
        dy_ext0 = dy0 + 1.0 - 0.3333333333333333;
        dz_ext0 = dz0 - 1.0 - 0.3333333333333333;
        xsv_ext0 = xsb + 1;
        ysv_ext0 = ysb - 1;
        zsv_ext0 = zsb + 1;
      } else {
        dx_ext0 = dx0 - 1.0 - 0.3333333333333333;
        dy_ext0 = dy0 - 1.0 - 0.3333333333333333;
        dz_ext0 = dz0 + 1.0 - 0.3333333333333333;
        xsv_ext0 = xsb + 1;
        ysv_ext0 = ysb + 1;
        zsv_ext0 = zsb - 1;
      }
      dx_ext1 = dx0 - 0.6666666666666666;
      dy_ext1 = dy0 - 0.6666666666666666;
      dz_ext1 = dz0 - 0.6666666666666666;
      xsv_ext1 = xsb;
      ysv_ext1 = ysb;
      zsv_ext1 = zsb;
      if ((c2 & 1) != 0) {
        dx_ext1 -= 2.0;
        xsv_ext1 += 2;
      } else if ((c2 & 2) != 0) {
        dy_ext1 -= 2.0;
        ysv_ext1 += 2;
      } else {
        dz_ext1 -= 2.0;
        zsv_ext1 += 2;
      }
    }
    double dx1 = dx0 - 1.0 - 0.3333333333333333;
    double dy1 = dy0 - 0.0 - 0.3333333333333333;
    double dz1 = dz0 - 0.0 - 0.3333333333333333;
    double attn1 = 2.0 - dx1 * dx1 - dy1 * dy1 - dz1 * dz1;
    if (attn1 > 0.0) {
      attn1 *= attn1;
      value += attn1 * attn1 * extrapolate(xsb + 1, ysb + 0, zsb + 0, dx1, dy1, dz1);
    }
    dx2 = dx0 - 0.0 - 0.3333333333333333;
    dy2 = dy0 - 1.0 - 0.3333333333333333;
    dz2 = dz1;
    attn2 = 2.0 - dx2 * dx2 - dy2 * dy2 - dz2 * dz2;
    if (attn2 > 0.0) {
      attn2 *= attn2;
      value += attn2 * attn2 * extrapolate(xsb + 0, ysb + 1, zsb + 0, dx2, dy2, dz2);
    }
    dx3 = dx2;
    dy3 = dy1;
    dz3 = dz0 - 1.0 - 0.3333333333333333;
    attn3 = 2.0 - dx3 * dx3 - dy3 * dy3 - dz3 * dz3;
    if (attn3 > 0.0) {
      attn3 *= attn3;
      value += attn3 * attn3 * extrapolate(xsb + 0, ysb + 0, zsb + 1, dx3, dy3, dz3);
    }
    dx4 = dx0 - 1.0 - 0.6666666666666666;
    dy4 = dy0 - 1.0 - 0.6666666666666666;
    dz4 = dz0 - 0.0 - 0.6666666666666666;
    attn4 = 2.0 - dx4 * dx4 - dy4 * dy4 - dz4 * dz4;
    if (attn4 > 0.0) {
      attn4 *= attn4;
      value += attn4 * attn4 * extrapolate(xsb + 1, ysb + 1, zsb + 0, dx4, dy4, dz4);
    }
    dx5 = dx4;
    dy5 = dy0 - 0.0 - 0.6666666666666666;
    dz5 = dz0 - 1.0 - 0.6666666666666666;
    attn5 = 2.0 - dx5 * dx5 - dy5 * dy5 - dz5 * dz5;
    if (attn5 > 0.0) {
      attn5 *= attn5;
      value += attn5 * attn5 * extrapolate(xsb + 1, ysb + 0, zsb + 1, dx5, dy5, dz5);
    }
    dx6 = dx0 - 0.0 - 0.6666666666666666;
    dy6 = dy4;
    dz6 = dz5;
    attn6 = 2.0 - dx6 * dx6 - dy6 * dy6 - dz6 * dz6;
    if (attn6 > 0.0) {
      attn6 *= attn6;
      value += attn6 * attn6 * extrapolate(xsb + 0, ysb + 1, zsb + 1, dx6, dy6, dz6);
    }
  }
  double attn_ext0 = 2.0 - dx_ext0 * dx_ext0 - dy_ext0 * dy_ext0 - dz_ext0 * dz_ext0;
  if (attn_ext0 > 0.0) {
    attn_ext0 *= attn_ext0;
    value += attn_ext0 * attn_ext0 * extrapolate(xsv_ext0, ysv_ext0, zsv_ext0, dx_ext0, dy_ext0, dz_ext0);
  }
  attn_ext1 = 2.0 - dx_ext1 * dx_ext1 - dy_ext1 * dy_ext1 - dz_ext1 * dz_ext1;
  if (attn_ext1 > 0.0) {
    attn_ext1 *= attn_ext1;
    value += attn_ext1 * attn_ext1 * extrapolate(xsv_ext1, ysv_ext1, zsv_ext1, dx_ext1, dy_ext1, dz_ext1);
  }
  return value / 103.0;
}

}  // namespace dq::be
