// gen/be/be_terrain.cpp — see be_terrain.h. TerrainGenerator / IslandLayer / LayerOptions are ported from the jar
// (CFR decompile) statement for statement, float vs double as in Java. The SDF operators come from BCLib
// (org.betterx.bclib.sdf.*), which is not in the jar: SDFCappedCone (Inigo Quilez's capped cone), SDFSmoothUnion
// (polynomial smooth min), SDFScale, SDFTranslate and SDFRadialNoiseMap are written from BCLib 2.x as remembered and
// marked [BCLib*].
#include "gen/be/be_terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

// keep the float/double maths Java-exact: no fused multiply-add
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace dq::be {

namespace {
// BCLib MHelper.floor: x < 0 ? (int)(x - 1) : (int)x  (one below floor() at exact negative integers, like BE)
inline int mfloor(double x) { return x < 0 ? int(x - 1) : int(x); }
inline float fminJ(float a, float b) { return a < b ? a : b; }  // MHelper.min
inline float fmaxJ(float a, float b) { return a > b ? a : b; }  // MHelper.max
inline float clampJ(float v, float lo, float hi) { return v < lo ? lo : std::min(v, hi); }  // Mth.clamp

// Minecraft's Mth.sin / Mth.cos (65536-entry table)
float mthSinIdx(int i) { return float(std::sin(double(i & 65535) * 3.141592653589793 * 2.0 / 65536.0)); }
float mthSin(float f) { return mthSinIdx(int(f * 10430.378f)); }
float mthCos(float f) { return mthSinIdx(int(f * 10430.378f + 16384.0f)); }

// [BCLib*] SDFCappedCone: half-height h, radius r1 at y = -h, r2 at y = +h
float cappedCone(float x, float y, float z, float h, float r1, float r2) {
  const float qx = std::sqrt(x * x + z * z);  // MHelper.length
  const float k2x = r2 - r1;
  const float k2y = 2.0f * h;
  const float cax = qx - fminJ(qx, y < 0.0f ? r1 : r2);
  const float cay = std::fabs(y) - h;
  const float mlt = clampJ(((r2 - qx) * k2x + (h - y) * k2y) / (k2x * k2x + k2y * k2y), 0.0f, 1.0f);
  const float cbx = qx - r2 + k2x * mlt;
  const float cby = y - h + k2y * mlt;
  const float s = (cbx < 0.0f && cay < 0.0f) ? -1.0f : 1.0f;
  return s * std::sqrt(fminJ(cax * cax + cay * cay, cbx * cbx + cby * cby));
}

// [BCLib*] SDFSmoothUnion: h = clamp(0.5 + 0.5 (b - a) / r, 0, 1); lerp(h, b, a) - r h (1 - h)
inline float smoothUnion(float a, float b, float r) {
  const float h = clampJ(0.5f + 0.5f * (b - a) / r, 0.0f, 1.0f);
  return (b + h * (a - b)) - r * h * (1.0f - h);
}

struct Cone { float h, r1, r2, ty; };
// IslandLayer.makeCone(radiusBottom, radiusTop, height, minY): a capped cone of half-height height/2 moved up to span
// minY..minY+height
Cone makeCone(float radiusBottom, float radiusTop, float height, float minY) {
  const float hh = height * 0.5f;
  return {hh, radiusBottom, radiusTop, minY + hh};
}
inline float cone(const Cone& c, float x, float y, float z) { return cappedCone(x, y - c.ty, z, c.h, c.r1, c.r2); }

const Cone kCone1 = makeCone(0.0f, 0.4f, 0.2f, -0.3f);    // root spike
const Cone kCone2 = makeCone(0.4f, 0.5f, 0.1f, -0.1f);    // underside shoulder
const Cone kCone3 = makeCone(0.5f, 0.45f, 0.03f, 0.0f);   // rim
const Cone kCone4 = makeCone(0.45f, 0.0f, 0.02f, 0.03f);  // dome

constexpr int kMaxIslands = 10;  // 9 grid cells + the central island
}  // namespace

// ================================================================= IslandLayer
class BETerrain::Layer {
public:
  Layer(int32_t seed, const LayerOptions& o, const BETerrainOptions& g, int index)
      : seed_(seed), index_(index), o_(o), density_(seed), radial_(seed) {
    distance_ = std::clamp(o.distance, 1.0f, 8192.0f);
    scale_ = std::clamp(o.scale, 0.1f, 1024.0f);
    const float center = std::clamp(o.averageHeight, 0.0f, 1.0f);
    const float variation = std::clamp(o.heightVariation, 0.0f, 1.0f);
    threshold_ = 0.9999f - std::clamp(o.coverage, 0.0f, 1.0f) * 2.0f;
    minY_ = center - variation;
    maxY_ = center + variation;
    centerDist_ = int64_t(std::floor(float(g.voidRing) / distance_));  // BE: Mth.floor(1000 / distance)
    hasCentral_ = o.hasCentralIsland;
    globalCentral_ = g.centralIsland;
    islandDistChunk_ = g.voidRing >> 3;
    circleSqr_ = int64_t(g.voidRing) * g.voidRing;
    prune_ = g.pruneIslands;
    sin_ = mthSin(0.5f);
    cos_ = mthCos(0.5f);
  }

  float distance() const { return distance_; }

  // IslandLayer.updatePositions + the per-island SDFScale from getIsland
  int positions(double x, double z, int maxHeight, Island* out) const {
    int n = 0;
    const int ix = mfloor(x / double(distance_));
    const int iz = mfloor(z / double(distance_));
    for (int pox = -1; pox < 2; ++pox) {
      const int px = pox + ix;
      for (int poz = -1; poz < 2; ++poz) {
        const int pz = poz + iz;
        Island is;
        if (candidate(px, pz, maxHeight, is)) out[n++] = is;
      }
    }
    if (globalCentral_ && std::abs(ix) < islandDistChunk_ && std::abs(iz) < islandDistChunk_) {
      int k = 0;
      for (int i = 0; i < n; ++i)
        if (int64_t(out[i].x) * out[i].x + int64_t(out[i].z) * out[i].z >= circleSqr_) out[k++] = out[i];
      n = k;
      if (hasCentral_) out[n++] = {0, 64, 0, 1.3f, index_};
    }
    return n;
  }

  // one grid cell's island before the void-ring cut; false where the cell is skipped or the coverage noise drops it
  bool candidate(int px, int pz, int maxHeight, Island& is, bool* inRing = nullptr) const {
    if (inRing) *inRing = int64_t(px) * px + int64_t(pz) * pz <= centerDist_;
    if (int64_t(px) * px + int64_t(pz) * pz <= centerDist_) return false;
    JavaRandom r(beHash(seed_, px, pz));
    const float fx = (float(px) + r.nextFloat()) * distance_;
    const float fy = (minY_ + r.nextFloat() * (maxY_ - minY_)) * float(maxHeight);
    const float fz = (float(pz) + r.nextFloat()) * distance_;
    if (!(density_.eval(double(fx) * 0.01, double(fz) * 0.01) > double(threshold_))) return false;
    is.x = int(double(fx));
    is.y = int(double(fy));
    is.z = int(double(fz));
    is.layer = index_;
    if (is.x == 0 && is.z == 0) {
      is.s = 1.3f;
    } else {
      JavaRandom s(beHash(seed_, is.x, is.z));
      is.s = s.nextFloat() + 0.5f;
    }
    return true;
  }

  // IslandLayer.calculateSDF: min over the islands of SDFScale(s)(island)((p - centre) / scale).
  // Islands that provably cannot lower the minimum are skipped (same result, less work): every cone lies in the
  // cylinder r <= 0.5, y in [-0.3, 0.05], so each exact cone SDF (and their min) is >= the cylinder's signed distance;
  // the smooth unions take off at most r/4 (0.005 + 0.005 + 0.0025) and the radial noise at most 1.7 * intensity.
  float sdf(const Island* isl, int n, double x, double y, double z, float intensity, float radius) const {
    if (!prune_) return sdfReference(isl, n, x, y, z, intensity, radius);
    float lb[kMaxIslands + 1];
    float q[kMaxIslands + 1][3];
    int first = -1;
    const float slack = 0.0125f + 1.7f * intensity + 1e-3f;
    for (int i = 0; i < n; ++i) {
      const Island& c = isl[i];
      q[i][0] = float(x - double(c.x)) / scale_ / c.s;
      q[i][1] = float(y - double(c.y)) / scale_ / c.s;
      q[i][2] = float(z - double(c.z)) / scale_ / c.s;
      // signed distance to the cylinder (exact, negative inside); the island SDF can never be below it
      const float dr = std::sqrt(q[i][0] * q[i][0] + q[i][2] * q[i][2]) - 0.5f;
      const float dy = std::fabs(q[i][1] + 0.125f) - 0.175f;
      const float ro = std::max(dr, 0.0f), yo = std::max(dy, 0.0f);
      lb[i] = (std::min(std::max(dr, dy), 0.0f) + std::sqrt(ro * ro + yo * yo) - slack) * c.s;
      if (first < 0 || lb[i] < lb[first]) first = i;
    }
    float distance = 10.0f;
    for (int k = -1; k < n; ++k) {  // the most promising island first, then the rest
      const int i = k < 0 ? first : k;
      if (i < 0 || (k >= 0 && i == first) || lb[i] >= distance) continue;
      const Island& c = isl[i];
      const float d = unitIsland(q[i][0], q[i][1], q[i][2], intensity, radius, int16_t(c.x & 32767),
                                 int16_t(c.z & 32767)) * c.s;
      distance = fminJ(distance, d);
    }
    return distance;
  }

  // the unpruned loop, exactly as IslandLayer (kept for tests: sdfReference == sdf)
  float sdfReference(const Island* isl, int n, double x, double y, double z, float intensity, float radius) const {
    float distance = 10.0f;
    for (int i = 0; i < n; ++i) {
      const Island& c = isl[i];
      const float lx = float(x - double(c.x)) / scale_;
      const float ly = float(y - double(c.y)) / scale_;
      const float lz = float(z - double(c.z)) / scale_;
      const float d = unitIsland(lx / c.s, ly / c.s, lz / c.s, intensity, radius, int16_t(c.x & 32767),
                                 int16_t(c.z & 32767)) * c.s;
      distance = fminJ(distance, d);
    }
    return distance;
  }

  // IslandLayer's island SDF: smoothUnion(0.01)(RadialNoiseMap(smoothUnion(0.02)(cone3, cone4)), smoothUnion(0.02)(cone1, cone2))
  float unitIsland(float x, float y, float z, float intensity, float radius, int16_t offX, int16_t offZ) const {
    const float top = smoothUnion(cone(kCone3, x, y, z), cone(kCone4, x, y, z), 0.02f) +
                      radialNoise(x, z, intensity, radius, offX, offZ);
    const float bottom = smoothUnion(cone(kCone1, x, y, z), cone(kCone2, x, y, z), 0.02f);
    return smoothUnion(top, bottom, 0.01f);
  }

  // [BCLib*] SDFRadialNoiseMap displacement (added to the distance)
  float radialNoise(float x, float z, float intensity, float radius, int16_t offX, int16_t offZ) const {
    if (intensity == 0.0f) return 0.0f;
    const float px = x / radius;
    const float pz = z / radius;
    float distance = px * px + pz * pz;
    if (distance > 1.0f) return 0.0f;
    distance = 1.0f - std::sqrt(distance);
    const float nx = px * cos_ - pz * sin_;
    const float nz = pz * cos_ + px * sin_;
    distance *= radialSum(double(nx) * 0.75 + offX, double(nz) * 0.75 + offZ);
    return distance * intensity;
  }

  float radialSum(double x, double z) const {
    return float(radial_.eval(x, z)) + float(radial_.eval(x * 3.0 + 1000.0, z * 3.0)) * 0.5f +
           float(radial_.eval(x * 9.0 + 1000.0, z * 9.0)) * 0.2f;
  }

private:
  int32_t seed_;
  int index_;
  LayerOptions o_;
  OpenSimplexNoise density_;  // coverage
  OpenSimplexNoise radial_;   // SDFRadialNoiseMap.setSeed(seed)
  float distance_, scale_, threshold_, minY_, maxY_;
  int64_t centerDist_;
  bool hasCentral_, globalCentral_;
  int islandDistChunk_;
  int64_t circleSqr_;
  bool prune_ = true;
  float sin_, cos_;
};

struct BETerrain::Positions {
  Island isl[3][kMaxIslands];
  int n[3];
};

// ================================================================= BETerrain
BETerrain::BETerrain(int64_t worldSeed, const BETerrainOptions& opt) : opt_(opt) {
  JavaRandom random(worldSeed);  // TerrainGenerator.initNoise: LegacyRandomSource(seed), nextInt() x 5
  const int32_t sBig = random.nextInt(), sMed = random.nextInt(), sSmall = random.nextInt();
  large_ = std::make_unique<Layer>(sBig, opt_.big, opt_, 0);
  medium_ = std::make_unique<Layer>(sMed, opt_.medium, opt_, 1);
  small_ = std::make_unique<Layer>(sSmall, opt_.small, opt_, 2);
  noise1_ = OpenSimplexNoise(random.nextInt());
  noise2_ = OpenSimplexNoise(random.nextInt());

  // TerrainGenerator's static init: offsets within 3 quarts, weight |o| / 3 normalised
  float sum = 0.0f;
  int n = 0;
  float raw[29];
  for (int x = -3; x <= 3; ++x)
    for (int z = -3; z <= 3; ++z) {
      const float dist = std::sqrt(float(x * x + z * z)) / 3.0f;
      if (!(dist <= 1.0f)) continue;
      sum += dist;
      raw[n] = dist;
      offs_[n] = {int8_t(x), int8_t(z)};
      ++n;
    }
  for (int i = 0; i < 29; ++i) coef_[i] = raw[i] / sum;

  // BCLib's pools, in registry order (resource paths sorted: ResourceLocation compares the path first) [BCLib*]
  static const int kLandOrder[] = {B_AMBER_LAND, B_BLOSSOMING_SPIRES, B_CHORUS_FOREST, B_CRYSTAL_MOUNTAINS,
                                   B_DRAGON_GRAVEYARDS, B_DRY_SHRUBLAND, B_DUST_WASTELANDS, B_END_HIGHLANDS,
                                   B_END_MIDLANDS, B_FOGGY_MUSHROOMLAND, B_GLOWING_GRASSLANDS, B_LANTERN_WOODS,
                                   B_MEGALAKE, B_SHADOW_FOREST, B_SULPHUR_SPRINGS, B_UMBRA_VALLEY, B_UMBRELLA_JUNGLE};
  for (int id : kLandOrder)
    if (opt_.vanillaBiomes || id < B_LAND_COUNT) landPicker_.add(id);
  voidPicker_.add(B_ICE_STARFIELD);
  if (opt_.vanillaBiomes) voidPicker_.add(B_SMALL_END_ISLANDS);
  landPicker_.finish();
  voidPicker_.finish();
  landMap_ = std::make_unique<HexBiomeMap>(worldSeed, opt_.landBiomeSize, &landPicker_, opt_.hex);
  voidMap_ = std::make_unique<HexBiomeMap>(worldSeed, opt_.voidBiomeSize, &voidPicker_, opt_.hex);
}

BETerrain::~BETerrain() = default;

float BETerrain::layerDistance(int layer) const {
  return (layer == 0 ? large_ : layer == 1 ? medium_ : small_)->distance();
}

void BETerrain::warp(double x, double z, double& px, double& pz) const {
  const double d1 = noise1_.eval(x * 0.1, z * 0.1) * 20.0 + noise2_.eval(x * 0.2, z * 0.2) * 10.0 +
                    noise1_.eval(x * 0.4, z * 0.4) * 5.0;
  const double d2 = noise2_.eval(x * 0.1, z * 0.1) * 20.0 + noise1_.eval(x * 0.2, z * 0.2) * 10.0 +
                    noise2_.eval(x * 0.4, z * 0.4) * 5.0;
  px = x * 8.0 + d1;
  pz = z * 8.0 + d2;
}

void BETerrain::positions(double px, double pz, Positions& p) const {
  p.n[0] = large_->positions(px, pz, kNoiseH, p.isl[0]);
  p.n[1] = medium_->positions(px, pz, kNoiseH, p.isl[1]);
  p.n[2] = small_->positions(px, pz, kNoiseH, p.isl[2]);
}

float BETerrain::layersDensity(const Positions& p, double px, double py, double pz, float intensity,
                               float radius) const {
  float dist = -large_->sdf(p.isl[0], p.n[0], px, py, pz, intensity, radius);
  dist = dist > 1.0f ? dist : fmaxJ(dist, -medium_->sdf(p.isl[1], p.n[1], px, py, pz, intensity, radius));
  dist = dist > 1.0f ? dist : fmaxJ(dist, -small_->sdf(p.isl[2], p.n[2], px, py, pz, intensity, radius));
  return dist;
}

float BETerrain::islandDensity(double px, double py, double pz, float h) const {
  Positions p;
  positions(px, pz, p);
  return layersDensity(p, px, py, pz, h, 0.5f / (1.0f + h));
}

namespace {
inline float roughen(const OpenSimplexNoise& n1, const OpenSimplexNoise& n2, float dist, double px, double py,
                     double pz) {
  if (dist > -0.5f) {
    dist = float(double(dist) + (n1.eval(px * 0.01, py * 0.01, pz * 0.01) * 0.02 + 0.02));
    dist = float(double(dist) + (n2.eval(px * 0.05, py * 0.05, pz * 0.05) * 0.01 + 0.01));
    dist = float(double(dist) + (n1.eval(px * 0.1, py * 0.1, pz * 0.1) * 0.005 + 0.005));
  }
  return dist;
}
inline float topFade(float dist, double py) {
  constexpr float fadOutStart = float(BETerrain::kNoiseH) - 28.0f;  // 100
  if (py >= double(BETerrain::kNoiseH)) return -1.0f;
  if (py > double(fadOutStart)) {
    const double d = (py - double(fadOutStart)) / 27.0;
    return float(double(dist) + d * (-1.0 - double(dist)));  // Mth.lerp(d, dist, -1)
  }
  return dist;
}
}  // namespace

float BETerrain::density(double px, double py, double pz, float h) const {
  return topFade(roughen(noise1_, noise2_, islandDensity(px, py, pz, h), px, py, pz), py);
}

// ----------------------------------------------------------------- the fill
// TerrainGenerator.fillTerrainDensity for one corner column, given h = avgTerrainHeight * 0.5
void BETerrain::fillCorner(int cellX, int cellZ, float h, float out[33]) const {
  double px, pz;
  warp(double(cellX), double(cellZ), px, pz);
  Positions p;
  positions(px, pz, p);
  const float radius = 0.5f / (1.0f + h);
  for (int y = 0; y <= kNoiseH / 4; ++y) {
    const double py = double(y) * 4.0;
    out[y] = topFade(roughen(noise1_, noise2_, layersDensity(p, px, py, pz, h, radius), px, py, pz), py);
  }
}

void BETerrain::cornerColumn(int cellX, int cellZ, float out[33]) const {
  fillCorner(cellX, cellZ, avgTerrainHeight(cellX << 1, cellZ << 1) * 0.5f, out);
}

void BETerrain::chunkDensity(int cx, int cz, float* out) const {
  // the 11 x 11 quarts the nine corner columns average their terrainHeight over (each biome looked up once)
  const int q0x = cx * 4 - 3, q0z = cz * 4 - 3;
  float th[11][11];
  for (int i = 0; i < 11; ++i)
    for (int k = 0; k < 11; ++k) th[i][k] = kBiomes[biomeQuart(q0x + i, q0z + k)].terrainHeight;
  float col[3][3][33];
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 3; ++k) {
      const int ci = 3 + 2 * i, ck = 3 + 2 * k;  // the column's quart in the window
      float h = 0.0f;
      if (!(th[ci][ck] < 0.1f)) {
        for (int o = 0; o < 29; ++o) h += th[ci + offs_[o][0]][ck + offs_[o][1]] * coef_[o];
      }
      fillCorner(cx * 2 + i, cz * 2 + k, h * 0.5f, col[i][k]);
    }
  // vanilla NoiseChunk interpolation: along y, then x, then z
  for (int y = 0; y < 256; ++y) {
    float* row = out + (y << 8);
    const int by = y - opt_.yOffset;
    if (by < 0 || by >= kNoiseH) {
      std::fill(row, row + 256, -1.0f);
      continue;
    }
    const int cy = by >> 2;
    const double fy = double(by & 3) / 4.0;
    for (int z = 0; z < 16; ++z) {
      const int kz = z >> 3;
      const double fz = double(z & 7) / 8.0;
      for (int x = 0; x < 16; ++x) {
        const int kx = x >> 3;
        const double fx = double(x & 7) / 8.0;
        auto ly = [&](int a, int b) {
          const double v0 = col[a][b][cy], v1 = col[a][b][cy + 1];
          return v0 + fy * (v1 - v0);
        };
        const double a00 = ly(kx, kz), a10 = ly(kx + 1, kz), a01 = ly(kx, kz + 1), a11 = ly(kx + 1, kz + 1);
        const double b0 = a00 + fx * (a10 - a00), b1 = a01 + fx * (a11 - a01);
        row[(z << 4) | x] = float(b0 + fz * (b1 - b0));
      }
    }
  }
}

// ----------------------------------------------------------------- land / void and biomes
bool BETerrain::isLandCell(int cellX, int cellZ) const {
  const uint64_t key = (uint64_t(uint32_t(cellX >> 7)) << 32) | uint32_t(cellZ >> 7);
  std::shared_ptr<LandSection> sec;
  {
    std::shared_lock lk(landMx_);
    auto it = land_.find(key);
    if (it != land_.end()) sec = it->second;
  }
  if (!sec) {
    std::unique_lock lk(landMx_);
    auto it = land_.find(key);
    if (it != land_.end()) {
      sec = it->second;
    } else {
      if (land_.size() > 512) land_.clear();  // 16 KiB each; holders keep theirs alive
      sec = std::make_shared<LandSection>();
      for (auto& v : sec->v) v.store(0, std::memory_order_relaxed);
      land_.emplace(key, sec);
    }
  }
  std::atomic<uint8_t>& slot = sec->v[((cellX & 127) << 7) | (cellZ & 127)];
  const uint8_t cached = slot.load(std::memory_order_relaxed);
  if (cached) return cached > 1;

  // TerrainGenerator.isLand: the warp at the cell centre, y = 0, 4, .. 124, any density > -0.01
  double px, pz;
  warp(double(cellX) + 0.5, double(cellZ) + 0.5, px, pz);
  Positions p;
  positions(px, pz, p);
  const float h = opt_.isLandHeight, radius = 0.5f / (1.0f + h);
  bool result = false;
  for (int y = 0; y < (kNoiseH + 3) / 4; ++y) {
    const double py = double(y) * 4.0;
    const float dist = roughen(noise1_, noise2_, layersDensity(p, px, py, pz, h, radius), px, py, pz);
    if (double(dist) > -0.01) {
      result = true;
      break;
    }
  }
  slot.store(result ? 2 : 1, std::memory_order_relaxed);
  return result;
}

bool BETerrain::isLand(int x, int z) const { return isLandCell(x >> 3, z >> 3); }

int BETerrain::biomeQuart(int qx, int qz) const {
  const int64_t posX = int64_t(qx) * 4, posZ = int64_t(qz) * 4;
  const int64_t r2 = int64_t(opt_.voidRing) * opt_.voidRing;
  const int64_t dist = std::llabs(posX) + std::llabs(posZ) > r2 ? r2 + 1 : posX * posX + posZ * posZ;
  const bool land = isLandCell(qx >> 1, qz >> 1);  // EndLandBiomeDecider.suggestType
  if (dist <= r2) return land ? B_THE_END : B_END_BARRENS;
  return land ? landMap_->biome(double(posX), double(posZ)) : voidMap_->biome(double(posX), double(posZ));
}

int BETerrain::biome(int x, int z) const { return biomeQuart(x >> 2, z >> 2); }

int BETerrain::land(int x, int z, int* vanillaKind) const {
  const int b = biome(x, z);
  int vk = V_NONE, r = b;
  if (b == B_THE_END) { vk = V_THE_END; r = -2; }
  else if (b >= B_LAND_COUNT) { vk = b - B_END_HIGHLANDS; r = -1; }
  if (vanillaKind) *vanillaKind = vk;
  return r;
}

void BETerrain::chunkLand(int cx, int cz, int8_t out[256], int8_t* vanillaKind) const {
  for (int qz = 0; qz < 4; ++qz)
    for (int qx = 0; qx < 4; ++qx) {
      int vk = V_NONE;
      const int l = land(cx * 16 + qx * 4, cz * 16 + qz * 4, &vk);
      for (int z = qz * 4; z < qz * 4 + 4; ++z)
        for (int x = qx * 4; x < qx * 4 + 4; ++x) {
          out[(z << 4) | x] = int8_t(l);
          if (vanillaKind) vanillaKind[(z << 4) | x] = int8_t(vk);
        }
    }
}

double BETerrain::terrainHeight(int land) {
  if (land < 0 || land >= B_COUNT) return 0.1;
  return kBiomes[land].terrainHeight;
}

float BETerrain::avgTerrainHeight(int qx, int qz) const {
  if (kBiomes[biomeQuart(qx, qz)].terrainHeight < 0.1f) return 0.0f;
  float depth = 0.0f;
  for (int i = 0; i < 29; ++i) depth += kBiomes[biomeQuart(qx + offs_[i][0], qz + offs_[i][1])].terrainHeight * coef_[i];
  return depth;
}

// ----------------------------------------------------------------- introspection
int BETerrain::islandsNear(double px, double pz, Island* out, int maxOut) const {
  Positions p;
  positions(px, pz, p);
  int n = 0;
  for (int l = 0; l < 3; ++l)
    for (int i = 0; i < p.n[l] && n < maxOut; ++i) out[n++] = p.isl[l][i];
  return n;
}

float BETerrain::islandOwnDensity(const Island& is, double px, double py, double pz, float h) const {
  const Layer& L = is.layer == 0 ? *large_ : is.layer == 1 ? *medium_ : *small_;
  return -L.sdf(&is, 1, px, py, pz, h, 0.5f / (1.0f + h));
}

BETerrain::LayerStats BETerrain::layerStats(int layer, int ix0, int iz0, int ix1, int iz1) const {
  const Layer& L = layer == 0 ? *large_ : layer == 1 ? *medium_ : *small_;
  LayerStats s{0, 0};
  const int64_t r2 = int64_t(opt_.voidRing) * opt_.voidRing;
  for (int ix = ix0; ix < ix1; ++ix)
    for (int iz = iz0; iz < iz1; ++iz) {
      bool ring = false;
      Island is;
      const bool kept = L.candidate(ix, iz, kNoiseH, is, &ring);
      if (ring) continue;
      ++s.candidates;
      if (kept && int64_t(is.x) * is.x + int64_t(is.z) * is.z >= r2) ++s.kept;
    }
  return s;
}

}  // namespace dq::be
