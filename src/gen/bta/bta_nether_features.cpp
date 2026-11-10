// BTA 8.0.1 WorldFeature* used by ChunkDecoratorNether, ported 1:1 (same Random call order).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include <cmath>
#include <cstdio>
#include <cstring>

#include "bta_nether_features.h"

namespace bta {
namespace feat {

namespace {
constexpr float PIF = 3.14159265358979323846f;
const int DX[6] = {0, 0, 0, 0, -1, 1}, DY[6] = {-1, 1, 0, 0, 0, 0}, DZ[6] = {0, 0, -1, 1, 0, 0};
inline bool surf(uint16_t id) { return hasF(id, T_NETHER_SURFACE); }
inline bool bsolid(uint16_t id) { return hasF(id, P_SOLID); }  // Block.solid()
inline bool isAcidId(uint16_t id) { return id == bid::ACID_STILL || id == bid::ACID_FLOWING; }
inline int nI(JavaRandom& r, int b) { return r.nextInt(b); }

// ---- JOML float helpers (Math.fma == a*b+c, two roundings: joml.useMathFma=false) ----
struct V3 { float x, y, z; };
inline float fmaF(float a, float b, float c) { return a * b + c; }
inline float jsinf(float a) { return (float)std::sin((double)a); }
inline float cosFromSin(float sin, float angle) {
  float cos = (float)std::sqrt((double)(1.0f - sin * sin));
  float a = angle + 1.5707964f;
  float b = a - (float)jf2i(a / (PIF * 2)) * (PIF * 2);
  if ((double)b < 0.0) b = PIF * 2 + b;
  if (b >= PIF) return -cos;
  return cos;
}
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 cross(V3 a, V3 v) {
  return {fmaF(a.y, v.z, -a.z * v.y), fmaF(a.z, v.x, -a.x * v.z), fmaF(a.x, v.y, -a.y * v.x)};
}
inline float dot(V3 a, V3 v) { return fmaF(a.x, v.x, fmaF(a.y, v.y, a.z * v.z)); }
inline V3 normalize(V3 a) {
  float s = 1.0f / (float)std::sqrt((double)fmaF(a.x, a.x, fmaF(a.y, a.y, a.z * a.z)));
  return {a.x * s, a.y * s, a.z * s};
}
}  // namespace

// ---- WorldFeatureRubyglassCrystalline ------------------------------------------------------------
namespace {
bool crystalHasSpace(DecoWorld& w, int px, int py, int pz, int base) {
  for (int y = py; y < py + base; ++y)
    for (int x = px - base; x < px + base; ++x)
      for (int z = pz - base; z < pz + base; ++z)
        if (w.get(x, y, z) == bid::RUBYGLASS_COLUMN) return false;
  return true;
}

void placeCrystal(DecoWorld& w, JavaRandom& r, bool ceiling, int minHeight, int heightMod, int px, int py, int pz) {
#ifdef BTA_RANDOM_DEBUG
  if (r.debugLog) printf("CRYSTAL state %llu at %d %d %d\n", (unsigned long long)r.rawSeed(), px, py, pz);
#endif
  float r0 = r.nextFloat();
  double rr = (double)r0 * (double)r0;
  int length = minHeight + mh::floor(rr * rr * (double)heightMod);  // Math.pow(r, 4.0)
  int base = length / 4;
  int tx = px, ty = ceiling ? py + base / 2 : py - base / 2, tz = pz;
  if (!crystalHasSpace(w, tx, ty, tz, base)) return;
  float minBase = -((float)base / 2.0f), maxBase = (float)base / 2.0f;
  float randX = r.nextFloat() * (float)(length / 16);
  float randZ = r.nextFloat() * (float)(length / 16);
  float fl = (float)length;
  V3 pts[9] = {{minBase - randX, 0.0f, minBase - randZ}, {maxBase + randX, 0.0f, minBase - randZ},
               {minBase - randX, 0.0f, maxBase + randZ}, {maxBase + randX, 0.0f, maxBase + randZ},
               {minBase - randX, fl, minBase - randZ},   {maxBase + randX, fl, minBase - randZ},
               {minBase - randX, fl, maxBase + randZ},   {maxBase + randX, fl, maxBase + randZ},
               {0.0f, 0.0f, 0.0f}};
  pts[8].y = fl + maxBase * 4.0f * (r.nextFloat() * 0.5f + 0.5f);
  float rx = ceiling ? PIF : 0.0f;
  float ry = 0.0f;
  // bytecode order (CFR shows it inlined the other way round): rx's nextDouble first, then ry's
  rx += (float)((r.nextDouble() - 0.5) * 1.5707963267948966) * 0.8f;
  ry += (float)(r.nextDouble() * 2.0 * 3.141592653589793);
  // new Matrix4f().rotateY(ry) -> rotationY
  float sy = jsinf(ry), cy = cosFromSin(sy, ry);
  float m00 = cy, m01 = 0, m02 = -sy, m10 = 0, m11 = 1, m12 = 0, m20 = sy, m21 = 0, m22 = cy;
  {  // .rotateX(rx) -> rotateXInternal
    float sx = jsinf(rx), cx = cosFromSin(sx, rx);
    float l10 = m10, l11 = m11, l12 = m12, l20 = m20, l21 = m21, l22 = m22;
    m20 = fmaF(l10, -sx, l20 * cx); m21 = fmaF(l11, -sx, l21 * cx); m22 = fmaF(l12, -sx, l22 * cx);
    m10 = fmaF(l10, cx, l20 * sx); m11 = fmaF(l11, cx, l21 * sx); m12 = fmaF(l12, cx, l22 * sx);
  }
  V3 tp[9];
  for (int i = 0; i < 9; ++i) {  // Vector4f(p, 1).mul(matrix) (affine)
    float x = pts[i].x, y = pts[i].y, z = pts[i].z, ww = 1.0f;
    tp[i].x = fmaF(m00, x, fmaF(m10, y, fmaF(m20, z, 0.0f * ww)));
    tp[i].y = fmaF(m01, x, fmaF(m11, y, fmaF(m21, z, 0.0f * ww)));
    tp[i].z = fmaF(m02, x, fmaF(m12, y, fmaF(m22, z, 0.0f * ww)));
  }
  static const int faces[9][3] = {{0, 1, 3}, {0, 1, 5}, {1, 3, 7}, {3, 2, 6}, {2, 0, 4},
                                  {4, 5, 8}, {5, 7, 8}, {7, 6, 8}, {6, 4, 8}};
  V3 nrm[9];
  float pd[9];
  for (int i = 0; i < 9; ++i) {
    V3 a = tp[faces[i][0]], b = tp[faces[i][1]], c = tp[faces[i][2]];
    nrm[i] = normalize(cross(sub(b, a), sub(c, a)));
    pd[i] = -dot(nrm[i], a);
  }
  V3 mn{INFINITY, INFINITY, INFINITY}, mx{-INFINITY, -INFINITY, -INFINITY}, cen{0, 0, 0};
  for (int i = 0; i < 9; ++i)
    for (int j = 0; j < 3; ++j) {
      V3 v = tp[faces[i][j]];
      mn.x = mn.x < v.x ? mn.x : v.x; mn.y = mn.y < v.y ? mn.y : v.y; mn.z = mn.z < v.z ? mn.z : v.z;
      mx.x = mx.x > v.x ? mx.x : v.x; mx.y = mx.y > v.y ? mx.y : v.y; mx.z = mx.z > v.z ? mx.z : v.z;
    }
  for (int i = 0; i < 9; ++i)
    for (int j = 0; j < 3; ++j) {
      V3 v = tp[faces[i][j]];
      cen.x = cen.x + v.x; cen.y = cen.y + v.y; cen.z = cen.z + v.z;
    }
  {
    float inv = 1.0f / (float)27;
    cen.x = cen.x * inv; cen.y = cen.y * inv; cen.z = cen.z * inv;
  }
  for (int i = 0; i < 9; ++i)
    if (dot(nrm[i], cen) + pd[i] > 0.0f) { nrm[i] = {-nrm[i].x, -nrm[i].y, -nrm[i].z}; pd[i] = -pd[i]; }
  float fd[9];
  for (int x = mh::floor(mn.x); x <= mh::floor(mx.x); ++x)
    for (int y = mh::floor(mn.y); y <= mh::floor(mx.y); ++y)
      for (int z = mh::floor(mn.z); z <= mh::floor(mx.z); ++z) {
        V3 p{(float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f};
        bool inside = true;
        for (int i = 0; i < 9; ++i) {
          fd[i] = -(dot(nrm[i], p) + pd[i]);
          if (fd[i] < 0.0f) { inside = false; break; }
        }
        if (!inside) continue;
        int wx = x + tx, wy = y + ty, wz = z + tz;
        uint16_t repl = w.get(wx, wy, wz);
        if (repl == bid::BEDROCK) continue;
        bool ore = r.nextInt(100) == 0;
        bool bed = false;
        float dp = fd[0] / (float)length;
        if (dp < 0.5f) {
          float inner = dp / 0.5f;
          if ((double)inner < (double)r.nextFloat() * 0.75) bed = true;
        }
        float minDist = 10.0f;
        for (int i = 1; i <= 4; ++i) minDist = jminf(fd[i], minDist);
        float distFromCenter = maxBase - minDist;
        float pctg = minDist / maxBase;
        uint16_t out;
        if ((double)distFromCenter > 0.5) out = bed ? bid::COBBLE_NETHERRACK_CRYSTALLINE : (ore ? bid::RUBYGLASS_NODE : bid::RUBYGLASS_COLUMN);
        else {
          float centerNoise = (float)std::sin((double)pctg * 3.141592653589793 * 3.0) * 0.5f;
          float fin = r.nextFloat() + centerNoise;
          out = (double)fin > 0.5 ? bid::RUBYGLASS_NODE : bid::RUBYGLASS_COLUMN;
        }
        w.setBlockTypeNotify(wx, wy, wz, out);
      }
}
}  // namespace

void rubyglassCrystalline(DecoWorld& w, JavaRandom& r, int px, int py, int pz, bool ceiling, int minH, int hMod) {
  int a = nI(r, 2), b = nI(r, 2);
  int tx = px + a - b;
  int c = nI(r, 2), d = nI(r, 2);
  int tz = pz + c - d;
  int ty = py;
  int floorY = -1, checks = 0, depth = 0;
  bool found = false, canGen = false;
  while (true) {
    ty = ceiling ? ty + 1 : ty - 1;
    if (checks > 64 || ty < 0 || ty >= 256) break;
    uint16_t b2 = w.get(tx, ty, tz);
    if (!found) {
      if (b2 == bid::COBBLE_NETHERRACK) { floorY = ty; found = true; }
    } else {
      ++depth;
      if (b2 != bid::COBBLE_NETHERRACK) break;
      if (depth >= 6) { canGen = true; break; }
    }
    ++checks;
  }
  uint16_t b3 = w.get(tx, ty, tz);
  if (canGen && (b3 == bid::COBBLE_NETHERRACK || b3 == bid::RUBYGLASS_CRYSTAL || b3 == bid::SOULSAND)) {
    floorY = ceiling ? floorY + 1 : floorY - 1;
    placeCrystal(w, r, ceiling, minH, hMod, tx, floorY, tz);
  }
}

// ---- WorldFeatureOre ----------------------------------------------------------------------------
void ore(DecoWorld& w, JavaRandom& r, int xs, int ys, int zs, const OreMap& map, int n) {
  float f = r.nextFloat() * PIF;
  float sf = mh::sin(f), cf = mh::cos(f);
  double xMax = (double)((float)(xs + 8) + sf * (float)n / 8.0f);
  double xMin = (double)((float)(xs + 8) - sf * (float)n / 8.0f);
  double zMax = (double)((float)(zs + 8) + cf * (float)n / 8.0f);
  double zMin = (double)((float)(zs + 8) - cf * (float)n / 8.0f);
  double yMax = (double)(ys + nI(r, 3) + 2);
  double yMin = (double)(ys - nI(r, 3) + 2);
  for (int l = 0; l <= n; ++l) {
    double d6 = xMax + (xMin - xMax) * (double)l / (double)n;
    double d7 = yMax + (yMin - yMax) * (double)l / (double)n;
    double d8 = zMax + (zMin - zMax) * (double)l / (double)n;
    double d9 = r.nextDouble() * (double)n / 16.0;
    double s = (double)(mh::sin((float)l * PIF / (float)n) + 1.0f);
    double d10 = s * d9 + 1.0;
    double d11 = s * d9 + 1.0;
    int x0 = mh::floor(d6 - d10 / 2.0), y0 = mh::floor(d7 - d11 / 2.0), z0 = mh::floor(d8 - d10 / 2.0);
    int x1 = mh::floor(d6 + d10 / 2.0), y1 = mh::floor(d7 + d11 / 2.0), z1 = mh::floor(d8 + d10 / 2.0);
    for (int x = x0; x <= x1; ++x) {
      double d12 = ((double)x + 0.5 - d6) / (d10 / 2.0);
      if (d12 * d12 >= 1.0) continue;
      for (int y = y0; y <= y1; ++y) {
        double d13 = ((double)y + 0.5 - d7) / (d11 / 2.0);
        if (d12 * d12 + d13 * d13 >= 1.0) continue;
        for (int z = z0; z <= z1; ++z) {
          double d14 = ((double)z + 0.5 - d8) / (d10 / 2.0);
          if (!(d12 * d12 + d13 * d13 + d14 * d14 < 1.0)) continue;
          uint16_t cur = w.get(x, y, z);
          if (cur == 0) continue;
          uint16_t to = 0;
          bool hit = false;
          for (int k = 0; k < map.n; ++k)
            if (map.from[k] == cur) { to = map.to[k]; hit = true; break; }
          if (!hit) continue;
          w.setBlockType(x, y, z, to);
        }
      }
    }
  }
}

// ---- WorldFeatureRubyglassGrowthPatch -------------------------------------------------------------
namespace {
bool growthSupport(DecoWorld& w, int x, int y, int z) {
  if (!w.normalCube(x, y, z)) return false;
  uint16_t b = w.get(x, y, z);
  if (b == bid::RUBYGLASS_GROWTH) return false;
  if (b == bid::RUBYGLASS_COLUMN || b == bid::RUBYGLASS_NODE || b == bid::COBBLE_NETHERRACK_CRYSTALLINE) return true;
  if (b == bid::COBBLE_NETHERRACK) {
    for (int dx = -1; dx <= 1; ++dx)
      for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
          if (w.get(x + dx, y + dy, z + dz) == bid::COBBLE_NETHERRACK_CRYSTALLINE) return true;
  }
  return false;
}
}  // namespace

void growthPatch(DecoWorld& w, JavaRandom& r, int px, int py, int pz, float tall) {
  for (int i1 = 0; i1 < 20; ++i1) {
    int a = nI(r, 4), b = nI(r, 4);
    int x1 = px + a - b;
    a = nI(r, 4); b = nI(r, 4);
    int y1 = py + a - b;
    a = nI(r, 4); b = nI(r, 4);
    int z1 = pz + a - b;
    if (!w.isAir(x1, y1, z1)) continue;
    for (int d = 0; d < 6; ++d) {  // Side.sides: BOTTOM(DOWN) TOP(UP) NORTH SOUTH WEST EAST
      if (!growthSupport(w, x1 - DX[d], y1 - DY[d], z1 - DZ[d])) continue;
      int rot = d;
      int ox = x1 + DX[d], oy = y1 + DY[d], oz = z1 + DZ[d];
      if (tall > 0.0f && w.isAir(ox, oy, oz) && r.nextFloat() < tall) {
        w.setBlockTypeData(x1, y1, z1, bid::RUBYGLASS_GROWTH, 8 | rot);
        w.setBlockTypeData(ox, oy, oz, bid::RUBYGLASS_GROWTH, 0x10 | rot);
        break;
      }
      w.setBlockTypeData(x1, y1, z1, bid::RUBYGLASS_GROWTH, 0 | rot);
      break;
    }
  }
}

// ---- WorldFeaturePerlinVein -----------------------------------------------------------------------
void perlinVein(DecoWorld& w, const ImprovedPerlin& noise, int sx, int sz, uint16_t place, uint16_t target,
                double scale, double thickness) {
  const int minY = 0, ySize = 255;
  std::vector<double> reg((size_t)16 * ySize * 16, 0.0);
  noise.addRegion3(reg.data(), sx, minY, sz, 16, ySize, 16, scale, scale, scale, 1.0);
  for (int xi = 0; xi < 16; ++xi) {
    int xb = xi * 16 * ySize;
    for (int zi = 0; zi < 16; ++zi) {
      int zb = xb + zi * ySize;
      for (int yi = 0; yi < ySize; ++yi) {
        double v = reg[zb + yi];
        if (std::fabs(v) > thickness) continue;
        int x = sx + xi, y = minY + yi, z = sz + zi;
        if (w.get(x, y, z) != target) continue;
        w.setBlockType(x, y, z, place);
      }
    }
  }
}

// ---- WorldFeatureWallClump (maxClumpSize 1: the growth loop never runs) ----------------------------
void wallClump(DecoWorld& w, int x, int y, int z, uint16_t place, uint16_t target) {
  if (w.get(x, y, z) != target) return;
  bool air = false;
  for (int d = 0; d < 6 && !air; ++d) air = w.isAir(x + DX[d], y + DY[d], z + DZ[d]);
  if (!air) return;
  w.setBlockType(x, y, z, place);
}

// ---- WorldFeatureNetherSpring ---------------------------------------------------------------------
void netherSpring(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t id) {
  if (surf(w.get(x, y + 1, z))) return;
  if (!w.isAir(x, y, z) && surf(w.get(x, y, z))) return;
  static const int off[5][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};
  int solid = 0, air = 0;
  for (auto& o : off) {
    uint16_t b = w.get(x + o[0], y + o[1], z + o[2]);
    if (surf(b)) ++solid;
    if (b == 0) ++air;
  }
  if (!(solid == 4 && air == 1)) return;
  if (id == bid::ACID_STILL || id == bid::ACID_FLOWING) {
    if (r.nextInt(50) == 0 && w.isAir(x, y + 1, z)) {
      w.setBlockTypeNotify(x, y, z, bid::GLOWSTONE);
      w.setBlockTypeNotify(x, y + 1, z, bid::FIRE_SULFURIC);
    }
    return;
  }
  w.setBlockTypeNotify(x, y, z, id);
  w.immediate = true;
  blockUpdateTick(w, x, y, z, id, r);
  w.immediate = false;
}

// ---- WorldFeatureLake ------------------------------------------------------------------------------
void lake(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t liquid) {
  x -= 8;
  z -= 8;
  while (y > 0 && w.isAir(x, y, z)) --y;
  y -= 4;
  bool sp[2048];
  std::memset(sp, 0, sizeof(sp));
  int l = r.nextInt(4) + 4;
  for (int i = 0; i < l; ++i) {
    double d = r.nextDouble() * 6.0 + 3.0;
    double d1 = r.nextDouble() * 4.0 + 2.0;
    double d2 = r.nextDouble() * 6.0 + 3.0;
    double d3 = r.nextDouble() * (16.0 - d - 2.0) + 1.0 + d / 2.0;
    double d4 = r.nextDouble() * (8.0 - d1 - 4.0) + 2.0 + d1 / 2.0;
    double d5 = r.nextDouble() * (16.0 - d2 - 2.0) + 1.0 + d2 / 2.0;
    for (int dx = 1; dx < 15; ++dx)
      for (int dz = 1; dz < 15; ++dz)
        for (int dy = 1; dy < 7; ++dy) {
          double d6 = ((double)dx - d3) / (d / 2.0);
          double d7 = ((double)dy - d4) / (d1 / 2.0);
          double d8 = ((double)dz - d5) / (d2 / 2.0);
          double d9 = d6 * d6 + d7 * d7 + d8 * d8;
          if (!(d9 < 1.0)) continue;
          sp[(dx * 16 + dz) * 8 + dy] = true;
        }
  }
  auto edge = [&](int dx, int dz, int dy) {
    return !sp[(dx * 16 + dz) * 8 + dy] &&
           ((dx < 15 && sp[((dx + 1) * 16 + dz) * 8 + dy]) || (dx > 0 && sp[((dx - 1) * 16 + dz) * 8 + dy]) ||
            (dz < 15 && sp[(dx * 16 + (dz + 1)) * 8 + dy]) || (dz > 0 && sp[(dx * 16 + (dz - 1)) * 8 + dy]) ||
            (dy < 7 && sp[(dx * 16 + dz) * 8 + (dy + 1)]) || (dy > 0 && sp[(dx * 16 + dz) * 8 + (dy - 1)]));
  };
  for (int dx = 0; dx < 16; ++dx)
    for (int dz = 0; dz < 16; ++dz)
      for (int dy = 0; dy < 8; ++dy) {
        if (!edge(dx, dz, dy)) continue;
        uint16_t b = w.get(x + dx, y + dy, z + dz);
        if (dy >= 4 && hasF(b, P_LIQUID)) return;
        if (dy < 4 && !hasF(b, P_MAT_SOLID) && b != liquid) return;
        if (b == bid::ICE) return;
      }
  for (int dx = 0; dx < 16; ++dx)
    for (int dz = 0; dz < 16; ++dz)
      for (int dy = 0; dy < 8; ++dy) {
        if (!sp[(dx * 16 + dz) * 8 + dy] || w.get(x + dx, y + dy, z + dz) == bid::BEDROCK) continue;
        w.setBlockTypeNotify(x + dx, y + dy, z + dz, dy < 4 ? liquid : 0);
      }
  // grass pass: needs DIRT below - never in the Nether
  if (matOf(liquid) == Mat::LAVA) {
    for (int dx = 0; dx < 16; ++dx)
      for (int dz = 0; dz < 16; ++dz)
        for (int dy = 0; dy < 8; ++dy) {
          if (!edge(dx, dz, dy)) continue;
          if (dy >= 4 && r.nextInt(2) == 0) continue;
          uint16_t b = w.get(x + dx, y + dy, z + dz);
          if (b == bid::BEDROCK || !hasF(b, P_MAT_SOLID)) continue;
          w.setBlockType(x + dx, y + dy, z + dz, bid::COBBLE_NETHERRACK);  // world.setBlock(.., fillerBlockId)
        }
  }
}

// ---- WorldFeatureTerrace ---------------------------------------------------------------------------
void terrace(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t fill, uint16_t wall, int minR, int rMod) {
  (void)y;
  int radius = minR + r.nextInt(rMod);
  int ccx = x + r.nextInt(8);
  int ccz = z + r.nextInt(8);
  auto canWall = [&](int cx, int cy, int cz) { return w.isAir(cx, cy + 1, cz) && surf(w.get(cx, cy, cz)); };
  auto canPool = [&](int cx, int cy, int cz) {
    if (!surf(w.get(cx, cy, cz))) return false;
    for (int d = 0; d < 6; ++d) {
      int qx = cx + DX[d], qy = cy + DY[d], qz = cz + DZ[d];
      if (d == 1 ? !w.isAir(qx, qy, qz) : (w.isAir(qx, qy, qz) || !surf(w.get(cx, cy, cz)))) return false;
    }
    return true;
  };
  for (int cx = ccx - radius; cx <= ccx + radius; ++cx)
    for (int cz = ccz - radius; cz <= ccz + radius; ++cz) {
      int xd = cx - ccx, zd = cz - ccz;
      float dist = (float)(xd * xd + zd * zd);
      for (int cy = 255; cy > 96 + 4; --cy) {
        if (w.isAir(cx, cy, cz)) continue;
        if (dist <= (float)(radius * radius) && canWall(cx, cy, cz)) w.setBlockTypeNotify(cx, cy, cz, wall);
        if (dist <= (float)((radius - 1) * (radius - 1)) && canPool(cx, cy, cz)) w.setBlockTypeNotify(cx, cy, cz, fill);
      }
    }
}

// ---- WorldFeaturePillar ----------------------------------------------------------------------------
namespace {
struct PillarBuf {
  bool b[17 * 17];
  void clear() { std::memset(b, 0, sizeof(b)); }
  void line(int x0, int x1, int y) { for (int x = x0; x <= x1; ++x) b[x * 17 + y] = true; }
  void plot(int cx, int cy, int x, int y) {
    line(cx - x, cx + x, cy - y);
    line(cx - x, cx + x, cy + y);
    line(cx - y, cx + y, cy - x);
    line(cx - y, cx + y, cy + x);
  }
  void circle(float r) {
    clear();
    if ((double)r <= 0.5) return;
    int ir = jf2i(r * 6.0f);
    int x = 0, y = ir, p = 1 - ir;
    plot(9, 9, x / 6, y / 6);
    while (x < y) {  // (bytecode; CFR's rendering of this loop is wrong)
      ++x;
      if (p < 0) p += 2 * x + 1;
      else { --y; p += 2 * (x - y) + 1; }
      plot(9, 9, x / 6, y / 6);
    }
  }
};
inline bool pillarEmpty(const DecoWorld& w, int x, int y, int z) {
  if (y < 0 || y >= 256) return false;
  return !bsolid(w.get(x, y, z));
}
bool exposedBase(const DecoWorld& w, int x, int y, int z, int radius) {
  for (int xi = -radius; xi < radius; ++xi)
    for (int zi = -radius; zi < radius; ++zi)
      if (!bsolid(w.get(x + xi, y, z + zi))) return true;
  return false;
}
}  // namespace

void pillar(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t blockId, bool doLava) {
  int x0 = r.nextInt(16) - 8 + x;
  int z0 = r.nextInt(16) - 8 + z;
  int y0 = y;
  while (pillarEmpty(w, x0, y0, z0)) --y0;
  int x1 = r.nextInt(2) - 2 + x;
  int z1 = r.nextInt(2) - 2 + z;
  int y1 = y;
  while (pillarEmpty(w, x1, y1, z1)) ++y1;
  y1 += 5;
  y0 -= 5;
  int length = y1 - y0;
  if (length >= 128 || length <= 5) return;
  int xd = x1 - x0, yd = y1 - y0, zd = z1 - z0;
  float dist = (float)std::sqrt((double)(xd * xd + yd * yd + zd * zd));
  float xn = (float)xd / dist, zn = (float)zd / dist;
  float A[3][3] = {{1.0f, xn, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  float B[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, zn, 1.0f}};
  float M[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) M[i][j] = A[i][0] * B[0][j] + A[i][1] * B[1][j] + A[i][2] * B[2][j];
  float rr = r.nextFloat();
  int initTop = jf2i(8.0f - (rr * 4.0f + 1.0f));
  rr = r.nextFloat();
  int initBottom = initTop;
  int initMin = jf2i(rr * (float)initBottom * 0.9f);
  if (exposedBase(w, x1, y1 + 1, z1, initTop) || exposedBase(w, x0, y0 - 1, z0, initBottom)) return;
  int skip = r.nextInt(8);
  if (skip == 0) { initTop = 0; initMin = 0; }
  else if (skip == 1) { initBottom = 0; initMin = 0; }
  PillarBuf buf;
  float buffered = (float)initBottom;
  buf.circle((float)initBottom);
  for (int dy = 0; dy <= length; ++dy) {
    float p = (float)dy / (float)length;
    if (skip == 0 && p > 0.5f) break;
    if (skip == 1 && p < 0.5f) continue;
    float lerped = ((float)initTop - (float)initBottom) * p + (float)initBottom;
    float radMod = (mh::cos(2.0f * p * PIF) / 2.0f + 0.5f) * 0.75f + 0.5f;
    float cur = (float)jf2i(lerped * radMod * 6.0f) / 6.0f;
    if (cur < (float)initMin) cur = (float)initMin;
    if (cur != buffered) { buf.circle(cur); buffered = cur; }
    for (int dx = -8; dx < 8; ++dx)
      for (int dz = -8; dz < 8; ++dz) {
        if (!buf.b[(dx + 8) * 17 + (dz + 8)]) continue;
        float v0 = (float)dx, v1 = (float)dy, v2 = (float)dz;
        float t0 = M[0][0] * v0 + M[0][1] * v1 + M[0][2] * v2;
        float t1 = M[1][0] * v0 + M[1][1] * v1 + M[1][2] * v2;
        float t2 = M[2][0] * v0 + M[2][1] * v1 + M[2][2] * v2;
        int xW = x0 + mh::floor((double)t0), yW = y0 + mh::floor((double)t1), zW = z0 + mh::floor((double)t2);
        uint16_t placed = (blockId == bid::COBBLE_NETHERRACK && r.nextInt(2) == 0) ? bid::NETHERRACK : blockId;
        w.setBlockTypeNotify(xW, yW, zW, placed);
      }
  }
  if (doLava) {
    int larger = initTop > initBottom ? initTop : initBottom;
    int num = 8 + r.nextInt(20);
    for (int i = 0; i < num; ++i) {
      int rx = r.nextInt(larger * 2 + 1);
      int rz = r.nextInt(larger * 2 + 1);
      int ry = r.nextInt(length);
      int qx = x - larger + rx, qy = y0 + ry, qz = z - larger + rz;
      if (w.get(qx, qy, qz) != bid::COBBLE_NETHERRACK) continue;
      w.setBlockTypeNotify(qx, qy, qz, bid::LAVA_FLOWING);
      w.immediate = true;
      blockUpdateTick(w, qx, qy, qz, bid::LAVA_FLOWING, r);
      w.immediate = false;
    }
  }
}

// ---- WorldFeatureBoulderMagma ------------------------------------------------------------------------
void boulderMagma(DecoWorld& w, JavaRandom& r, int px, int py, int pz) {
  int x = px, y = py, z = pz;
  while (w.isAir(x, y, z) && y - 1 > 0) --y;
  static const double MAPS[2][3] = {{1.5, 1.5, 1.0}, {1.0, 1.5, 1.0}};
  const double* map = MAPS[r.nextInt(2)];
  for (int qy = y - 2; qy < y + 2; ++qy)
    for (int qx = x - 2; qx < x + 2; ++qx)
      for (int qz = z - 2; qz < z + 2; ++qz) {
        uint16_t b = w.get(qx, qy, qz);
        if (b == 0 || surf(b)) continue;
        return;
      }
  auto plotBlock = [&](int a, int yy, int c) {  // plotBlock(x=a, y, z=c)
    for (int xx = x - c; xx <= x + c; ++xx) {
      w.setBlockType(xx, yy, z + a, bid::MAGMA);
      w.setBlockType(xx, yy, z - a, bid::MAGMA);
    }
  };
  for (int i = 0; i < 3; ++i) {
    double rad = map[i];
    int yy = y + i;
    int a = 0, c = jd2i(rad);
    double d = 3.0 - 2.0 * rad;
    plotBlock(c, yy, a); plotBlock(a, yy, c);
    while (c > a) {  // (bytecode order: x++ first; CFR's ternary rendering is wrong)
      ++a;
      if (d > 0.0) { --c; d = d + (double)(4 * (a - c)) + 10.0; }
      else d = d + (double)(4 * a) + 6.0;
      plotBlock(c, yy, a); plotBlock(a, yy, c);
    }
  }
  for (int qx = x - 2; qx < x + 2; ++qx)
    for (int qz = z - 2; qz < z + 2; ++qz)
      for (int qy = y - 4; qy < y + 4; ++qy) {
        if (w.isAir(qx, qy, qz) || r.nextInt(8) != 0) continue;
        w.setBlockType(qx, qy, qz, bid::ORE_NETHERCOAL_NETHERRACK);
      }
  w.setBlockType(x, y + 1, z, bid::BLOCK_NETHER_COAL);
}

// ---- WorldFeatureFloorVent(1, 2) -----------------------------------------------------------------------
void floorVent(DecoWorld& w, JavaRandom& r, int px, int py, int pz) {
  int x = px + r.nextInt(8), y = py;
  int z = pz + r.nextInt(8);
  while (w.isAir(x, y, z) && y > 0) --y;
  auto canPlace = [&](int cx, int cy, int cz) {
    uint16_t b = w.get(cx, cy, cz);
    if (b == 0 || hasF(b, T_IS_LAVA) || hasF(b, T_IS_WATER)) return false;
    return surf(b);
  };
  if (y <= 0 || !canPlace(x, y, z)) return;
  if (!(w.isAir(x, y + 1, z) && w.isAir(x + 1, y + 1, z) && w.isAir(x - 1, y + 1, z) && w.isAir(x, y + 1, z + 1) &&
        w.isAir(x, y + 1, z - 1)))
    return;
  int radius = 1 + r.nextInt(2);
  for (int cx = x - radius; cx <= x + radius; ++cx)
    for (int cz = z - radius; cz <= z + radius; ++cz) {
      int xd = cx - x, zd = cz - z;
      float dsq = (float)(xd * xd + zd * zd);
      if (dsq > (float)(radius * radius)) continue;
      for (int cy = y; cy > y - 4; --cy) {
        if (!(cy > 0 && canPlace(cx, cy, cz))) continue;
        if (cx == x && cy == y && cz == z) w.setBlockTypeNotify(cx, cy, cz, bid::THERMAL_VENT);
        else if (r.nextInt(4) != 0) w.setBlockTypeNotify(cx, cy, cz, bid::BASALT);
        else if (r.nextInt(20) == 0) {
          if (cy == y && w.isAir(cx, cy + 1, cz)) w.setBlockTypeNotify(cx, cy, cz, bid::THERMAL_VENT);
          else w.setBlockTypeNotify(cx, cy, cz, bid::BASALT);
        }
      }
    }
}

// ---- WorldFeatureSpireVent ------------------------------------------------------------------------------
void spireVent(DecoWorld& w, JavaRandom& r, int px, int py, int pz) {
  if (!w.isAir(px, py, pz)) return;
  int x = px, y = py, z = pz;
  for (int s = 0; w.isAir(x, y, z) && y - 1 > 0 && s < 64; ++s) --y;
  if (!surf(w.get(x, y, z))) return;
  for (int qy = y + 1; qy < y + 5; ++qy)
    for (int qx = x; qx < x + 2; ++qx)
      for (int qz = z; qz < z + 2; ++qz)
        if (!w.isAir(qx, qy, qz)) return;
  int hx = 0, hy = 0, hz = 0;
  for (int x1 = 0; x1 < 2; ++x1)
    for (int z1 = 0; z1 < 2; ++z1) {
      int h = r.nextInt(6) + 4;
      if (h > hy) { hx = x1; hy = h; hz = z1; }
      int oreRange = h / 3 > 1 ? h / 3 : 1;
      for (int y1 = 0; y1 < h; ++y1) {
        if (y1 < oreRange && r.nextInt(2) == 0) w.setBlockType(x + x1, y + y1, z + z1, bid::ORE_NETHERCOAL_BASALT);
        else w.setBlockType(x + x1, y + y1, z + z1, bid::BASALT);
      }
    }
  for (int y1 = 0; y1 < 2; ++y1) w.setBlockType(x + hx, y + hy + y1, z + hz, bid::THERMAL_VENT);
}

// ---- WorldFeatureBoulderGlowstone(GLOWSTONE, FIRE_SULFURIC, 1) ------------------------------------------
void boulderGlowstone(DecoWorld& w, JavaRandom& r, int px, int py, int pz) {
  int x = px, y = py, z = pz;
  for (int s = 0; (w.isAir(x, y, z) || isAcidId(w.get(x, y, z))) && y - 1 > 0 && s < 64; ++s) --y;
  if (!surf(w.get(x, y, z))) return;
  if (!isAcidId(w.get(x, y + 1, z))) return;
  const int radius = 1, maxH = radius * 2 + 2;
  for (int qy = y + 1; qy < y + maxH; ++qy) {
    uint16_t b = w.get(x, qy, z);
    if (b != 0 && !isAcidId(b)) return;
  }
  int cy = y + 1;
  for (int x1 = -radius; x1 <= radius; ++x1)
    for (int z1 = -radius; z1 <= radius; ++z1) {
      int dist = std::abs(x1) + std::abs(z1);
      int m = radius * 2 + 1 - dist;
      int h = r.nextInt(2) + (m > 1 ? m : 1);
      if (h <= 0) continue;
      for (int y1 = 0; y1 < h; ++y1) w.setBlockType(x + x1, cy + y1, z + z1, bid::GLOWSTONE);
      uint16_t q = w.get(x + x1, cy + h, z + z1);
      if ((q != 0 && !isAcidId(q)) || r.nextInt(2) != 0) continue;
      w.setBlockType(x + x1, cy + h, z + z1, bid::FIRE_SULFURIC);
    }
}

// ---- WorldFeatureRoofSpire -------------------------------------------------------------------------------
void roofSpire(DecoWorld& w, JavaRandom& r, int px, int py, int pz, uint16_t boulder) {
  int x = px, y = py, z = pz;
  while (w.isAir(x, y, z) && y < 245) ++y;
  bool randomize = boulder == bid::COBBLE_NETHERRACK;
  for (int qy = y - 3; qy > y - 9; --qy)
    for (int qx = x - 3; qx < x + 3; ++qx)
      for (int qz = z - 3; qz < z + 3; ++qz) {
        uint16_t b = w.get(qx, qy, qz);
        if (b != 0 && !surf(b)) return;
      }
  auto pick = [&]() -> uint16_t {
    if (randomize && r.nextInt(2) == 0) return bid::NETHERRACK;
    return boulder;
  };
  int hx = 0, hy = 0, hz = 0;
  for (int x1 = 0; x1 < 2; ++x1)
    for (int z1 = 0; z1 < 2; ++z1) {
      int h = r.nextInt(6) + 4;
      if (h > hy) { hx = x1; hy = h; hz = z1; }
      for (int y1 = 0; y1 < h; ++y1) {
        if (w.get(x + x1, y - y1, z + z1) == bid::BEDROCK) continue;
        w.setBlockType(x + x1, y - y1, z + z1, pick());
      }
    }
  for (int y1 = 0; y1 < 2; ++y1) {
    uint16_t b = pick();
    w.setBlockType(x + hx, y - (hy + y1), z + hz, b);
  }
}

// ---- WorldFeatureGlowstone -------------------------------------------------------------------------------
void glowstone(DecoWorld& w, JavaRandom& r, int x, int y, int z) {
  if (!w.isAir(x, y, z)) return;
  if (!bsolid(w.get(x, y + 1, z))) return;
  w.setBlockTypeNotify(x, y, z, bid::GLOWSTONE);
  for (int l = 0; l < 1500; ++l) {
    int a = nI(r, 8), b = nI(r, 8);
    int rx = x + a - b;
    int ry = y - nI(r, 12);
    a = nI(r, 8); b = nI(r, 8);
    int rz = z + a - b;
    if (!w.isAir(rx, ry, rz)) continue;
    int adj = 0;
    for (int d = 0; d < 6; ++d)
      if (w.get(rx + DX[d], ry + DY[d], rz + DZ[d]) == bid::GLOWSTONE) ++adj;
    if (adj != 1) continue;
    w.setBlockTypeNotify(rx, ry, rz, bid::GLOWSTONE);
  }
}

// ---- WorldFeatureFire ------------------------------------------------------------------------------------
void fire(DecoWorld& w, JavaRandom& r, int x, int y, int z, bool overrideVolcanic) {
  for (int i = 0; i < 32; ++i) {
    int a = nI(r, 8), b = nI(r, 8);
    int qx = x + a - b;
    a = nI(r, 4); b = nI(r, 4);
    int qy = y + a - b;
    a = nI(r, 8); b = nI(r, 8);
    int qz = z + a - b;
    if (!w.isAir(qx, qy, qz)) continue;
    uint16_t s = w.get(qx, qy - 1, qz);
    if (overrideVolcanic) {
      if (!surf(s)) continue;
      w.setBlockTypeNotify(qx, qy, qz, bid::FIRE);
      w.setBlockTypeNotify(qx, qy - 1, qz, bid::NETHERRACK);
      continue;
    }
    if (hasF(s, T_INF_BURN_SULF)) { w.setBlockTypeNotify(qx, qy, qz, bid::FIRE_SULFURIC); continue; }
    if (!hasF(s, T_INF_BURN)) continue;
    w.setBlockTypeNotify(qx, qy, qz, bid::FIRE);
  }
}

// ---- WorldFeatureNetherScatter* / SoulCatcherPatch ---------------------------------------------------------
namespace {
inline bool forbiddenSurface(uint16_t b) {
  return b == bid::RUBYGLASS_NODE || b == bid::RUBYGLASS_COLUMN || b == bid::RUBYGLASS_GROWTH;
}
bool resolveSite(const DecoWorld& w, int x, int y, int z, int& gy) {
  gy = y;
  for (int s = 0; w.isAir(x, gy, z) && gy > 0 && s < 96; ++s) --gy;
  if (gy <= 0) return false;
  if (forbiddenSurface(w.get(x, gy, z))) return false;
  return w.isAir(x, gy + 1, z);
}
inline bool flowerCanStay(const DecoWorld& w, int x, int y, int z) { return hasF(w.get(x, y - 1, z), P_SOLID_RENDER); }
}  // namespace

void emberAsh(DecoWorld& w, JavaRandom& r, int x, int y0, int z) {
  if (r.nextInt(4) < 1) return;
  int scanned = 0;
  for (int y = y0; y > 0 && scanned < 96; --y, ++scanned) {
    if (w.get(x, y, z) != bid::BLOCK_ASH) continue;
    if (!w.isAir(x, y + 1, z)) continue;
    int b = w.biome(x, y, z);
    if (b != BI_OLD_WORLD && b != BI_OLD_WORLD_DESERT) continue;
    if (!(w.get(x + 1, y, z) == bid::BLOCK_ASH && w.get(x - 1, y, z) == bid::BLOCK_ASH && w.get(x, y, z + 1) == bid::BLOCK_ASH &&
          w.get(x, y, z - 1) == bid::BLOCK_ASH))
      continue;
    w.setBlockType(x, y, z, bid::EMBER);
    if (layerAshCanPlaceAt(w, x, y + 1, z)) w.setBlockType(x, y + 1, z, bid::LAYER_ASH);
    return;
  }
}

void scatterBonePile(DecoWorld& w, JavaRandom& r, int x, int y, int z) {
  int gy;
  if (!resolveSite(w, x, y, z, gy)) return;
  uint16_t s = w.get(x, gy, z);
  if (s == bid::OBSIDIAN) return;
  if ((s == bid::BLOCK_ASH || s == bid::LAYER_ASH) && r.nextInt(8) != 0) return;
  if (!flowerCanStay(w, x, gy + 1, z)) return;
  w.setBlockType(x, gy + 1, z, bid::BONE_PILE);
}

void scatterBoulder(DecoWorld& w, int x, int y, int z, bool sulfuric) {
  int gy;
  if (!resolveSite(w, x, y, z, gy)) return;
  if (sulfuric) {
    uint16_t s = w.get(x, gy, z);
    if (s != bid::BRIMSAND && s != bid::BRIMSTONE && s != bid::BRIMTHAW) return;
  }
  if (!flowerCanStay(w, x, gy + 1, z)) return;
  w.setBlockType(x, gy + 1, z, sulfuric ? bid::BOULDER_SULFURIC : bid::BOULDER_MAGMATIC);
}

void soulCatcherPatch(DecoWorld& w, JavaRandom& r, int x, int y, int z) {
  int gy = y;
  while (w.isAir(x, gy, z) && gy > 0) --gy;
  if (gy <= 0) return;
  bool seed = w.get(x, gy, z) == bid::SOULSAND;
  int attempts = seed ? 28 : 6;
  for (int i = 0; i < attempts; ++i) {
    int a = nI(r, 3), b = nI(r, 3);
    int qx = x + a - b;
    a = nI(r, 2); b = nI(r, 2);
    int qy = gy + 1 + a - b;
    a = nI(r, 3); b = nI(r, 3);
    int qz = z + a - b;
    if (!w.isAir(qx, qy, qz)) continue;
    uint16_t s = w.get(qx, qy - 1, qz);
    bool over = s == bid::SOULSAND;
    if (s == bid::OBSIDIAN) continue;
    if (!over && r.nextInt(5) != 0) continue;
    if (!flowerCanStay(w, qx, qy, qz)) continue;
    w.setBlockType(qx, qy, qz, bid::SOUL_CATCHER);
  }
}

}  // namespace feat
}  // namespace bta
