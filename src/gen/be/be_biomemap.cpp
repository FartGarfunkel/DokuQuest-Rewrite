// gen/be/be_biomemap.cpp — see be_biomemap.h. Everything below the biome table is a reconstruction of BCLib 2.x
// (HexBiomeMap, HexBiomeChunk, BiomePicker/WeighTree, WeightedList), which the BE jar does not contain.
#include "gen/be/be_biomemap.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace dq::be {

// Better End's values: data/betterend/worldgen/betterx/biome/<id>.json ("genChance", "terrainHeight", "parent",
// "edgeSize" — 0 everywhere). The vanilla rows are BCLib's wrappers (BiomeAPI.wrapNativeBiome): genChance 0.5 and
// END_HIGHLANDS edged by END_MIDLANDS 8 wide are recalled from BCLib's source [BCLib*]; terrainHeight 0.1 is
// BCLBiomeSettings' default.
const BiomeInfo kBiomes[B_COUNT] = {
    {"betterend:chorus_forest", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:blossoming_spires", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:foggy_mushroomland", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:glowing_grasslands", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:crystal_mountains", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:sulphur_springs", 1.0f, 0.0f, -1, -1, 0, K_LAND},
    {"betterend:umbrella_jungle", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:shadow_forest", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:ice_starfield", 0.25f, 0.1f, -1, -1, 0, K_VOID},
    {"betterend:amber_land", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:megalake", 1.0f, 0.0f, -1, -1, 0, K_LAND},
    {"betterend:lantern_woods", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:painted_mountains", 1.0f, 0.1f, B_DUST_WASTELANDS, -1, 0, K_LAND},
    {"betterend:neon_oasis", 0.5f, 0.1f, B_DUST_WASTELANDS, -1, 0, K_LAND},
    {"betterend:dragon_graveyards", 0.1f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:dry_shrubland", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:dust_wastelands", 1.0f, 1.5f, -1, -1, 0, K_LAND},
    {"betterend:umbra_valley", 1.0f, 0.1f, -1, -1, 0, K_LAND},
    {"betterend:megalake_grove", 1.0f, 0.0f, B_MEGALAKE, -1, 0, K_LAND},
    {"minecraft:the_end", 0.5f, 0.1f, -1, -1, 0, K_CENTER},
    {"minecraft:end_highlands", 0.5f, 0.1f, -1, B_END_MIDLANDS, 8, K_LAND},
    {"minecraft:end_midlands", 0.5f, 0.1f, -1, -1, 0, K_LAND},
    {"minecraft:small_end_islands", 0.5f, 0.1f, -1, -1, 0, K_VOID},
    {"minecraft:end_barrens", 0.5f, 0.1f, -1, -1, 0, K_BARRENS},
};

// ---------------------------------------------------------------- picker
void BiomePicker::add(int id) { ids_.push_back(id); }

void BiomePicker::finish() {
  cum_.clear();
  float sum = 0.0f;
  for (int id : ids_) cum_.push_back(sum += kBiomes[id].genChance);
  // every member's sub-biome list: itself at weight 1, then its children (registry order) at their genChance
  for (int id : ids_) {
    Sub& s = subs_[id];
    s.ids = {id};
    for (int c = 0; c < B_COUNT; ++c)
      if (kBiomes[c].parent == id) s.ids.push_back(c);
    float w = 0.0f;
    s.cum.clear();
    for (size_t i = 0; i < s.ids.size(); ++i) s.cum.push_back(w += (i == 0 ? 1.0f : kBiomes[s.ids[i]].genChance));
  }
}

int BiomePicker::pick(JavaRandom& r) const {
  if (ids_.empty()) return B_THE_END;
  const float v = r.nextFloat() * cum_.back();
  for (size_t i = 0; i < cum_.size(); ++i)
    if (v < cum_[i]) return ids_[i];
  return ids_.back();
}

int BiomePicker::subBiome(int id, JavaRandom& r) const {
  const Sub& s = subs_[id];
  const float v = r.nextFloat() * (s.cum.empty() ? 1.0f : s.cum.back());
  for (size_t i = 0; i < s.cum.size(); ++i)
    if (v <= s.cum[i]) return s.ids[i];
  return id;
}

// ---------------------------------------------------------------- hex chunk
namespace {
constexpr int SIDE = 32, SIDE_MASK = 31, SIZE = SIDE * SIDE, MAX_SIDE = SIZE - SIDE;
constexpr int SIDE_PRE = 4, SCALE_PRE = SIDE / SIDE_PRE, SIZE_PRE = SIDE_PRE * SIDE_PRE;
constexpr float RAD_INNER = 0.8660254f;  // sqrt(3)/2: a column is 1/RAD_INNER rows wide
constexpr uint8_t EMPTY = 0xFF;

// index = x << 5 | z, rows run along z; odd rows sit half a cell to -x (pointy-top hexes, "odd-r" offset)
const int NEIGHBOURS[2][6] = {
    {-SIDE, SIDE, -1, SIDE - 1, 1, SIDE + 1},   // even row
    {-SIDE, SIDE, -SIDE - 1, -1, -SIDE + 1, 1},  // odd row
};

void circle(uint8_t* buf, int center, uint8_t biome, uint8_t mask) {
  if (buf[center] == mask) buf[center] = biome;
  for (int off : NEIGHBOURS[center & 1]) {
    const int i = center + off;
    if (i >= 0 && i < SIZE && buf[i] == mask) buf[i] = biome;
  }
}

const float kEdgeX[8] = {0.0f, 0.70710677f, 1.0f, 0.70710677f, 0.0f, -0.70710677f, -1.0f, -0.70710677f};
const float kEdgeZ[8] = {1.0f, 0.70710677f, 0.0f, -0.70710677f, -1.0f, -0.70710677f, 0.0f, 0.70710677f};
constexpr float kSin = 0.38941834f, kCos = 0.92106099f;  // sin/cos of 0.4 rad
}  // namespace

HexBiomeMap::HexBiomeMap(int64_t seed, int size, const BiomePicker* picker, const HexMapOptions& o)
    : picker_(picker), opt_(o), scale_(float(size) / float(SIDE / SIDE_PRE)) {
  JavaRandom r(seed);
  noise0_ = OpenSimplexNoise(r.nextInt());
  noise1_ = OpenSimplexNoise(r.nextInt());
  iterations_ = std::clamp(int(std::ceil(std::log(double(scale_)) / std::log(2.0))), 1, 5);
  seed_ = int32_t(uint64_t(seed) & 0xFFFFFFFFu);
}

void HexBiomeMap::buildChunk(int cx, int cz, Chunk& out) const {
  JavaRandom random(beHash(seed_, cx, cz));
  uint8_t buffers[2][SIZE];
  std::fill(buffers[0], buffers[0] + SIZE, EMPTY);
  std::fill(buffers[1], buffers[1] + SIZE, EMPTY);
  for (int index = 0; index < SIZE_PRE; ++index) {  // 16 seeds, one per 8 x 8 block of cells
    const int px = (index >> 2) * SCALE_PRE + random.nextInt(SCALE_PRE);
    const int pz = (index & 3) * SCALE_PRE + random.nextInt(SCALE_PRE);
    circle(buffers[0], (px << 5) | pz, uint8_t(picker_->pick(random)), EMPTY);
  }
  bool hasEmpty = true;
  int bi = 0;
  while (hasEmpty) {  // each filled cell spreads into one random neighbour per pass
    const uint8_t* in = buffers[bi];
    bi ^= 1;
    uint8_t* outb = buffers[bi];
    hasEmpty = false;
    for (int index = SIDE; index < MAX_SIDE; ++index) {
      const int z = index & SIDE_MASK;
      if (z == 0 || z == SIDE_MASK) continue;
      if (in[index] != EMPTY) {
        outb[index] = in[index];
        const int side = index + NEIGHBOURS[z & 1][random.nextInt(6)];
        if (side >= 0 && side < SIZE && outb[side] == EMPTY) outb[side] = in[index];
      } else {
        hasEmpty = true;
      }
    }
  }
  uint8_t* b = buffers[bi];
  constexpr int preN = SIDE_MASK - 2;
  for (int i = 0; i < SIDE; ++i) {  // the rim copies the cells two in (keeps the row parity)
    b[(i << 5) | 0] = b[(i << 5) | 2];
    b[(0 << 5) | i] = b[(2 << 5) | i];
    b[(i << 5) | SIDE_MASK] = b[(i << 5) | preN];
    b[(SIDE_MASK << 5) | i] = b[(preN << 5) | i];
  }
  for (int index = 0; index < SIZE; ++index) {
    if (b[index] == EMPTY) {
      b[index] = uint8_t(picker_->pick(random));
    } else if (random.nextInt(opt_.subOneIn) == 0) {
      const uint8_t self = b[index];
      circle(b, index, uint8_t(picker_->subBiome(self, random)), self);
    }
  }
  std::copy(b, b + SIZE, out.b);
}

const HexBiomeMap::Chunk& HexBiomeMap::chunk(int cx, int cz) const {
  const uint64_t key = (uint64_t(uint32_t(cx)) << 32) | uint32_t(cz);
  {
    std::shared_lock lk(mx_);
    auto it = chunks_.find(key);
    if (it != chunks_.end()) return *it->second;
  }
  auto c = std::make_unique<Chunk>();
  buildChunk(cx, cz, *c);
  std::unique_lock lk(mx_);
  auto it = chunks_.find(key);
  if (it != chunks_.end()) return *it->second;
  return *chunks_.emplace(key, std::move(c)).first->second;  // chunks are never evicted: 1 KiB per ~1.2 x 1 km
}

int HexBiomeMap::rawBiome(double x, double z) const {
  double px = x / scale_ * RAD_INNER;  // column units
  double pz = z / scale_;              // row units
  const double rx = px * kCos - pz * kSin, rz = pz * kCos + px * kSin;
  px = rx;
  pz = rz;
  double wx = 0.0, wz = 0.0, f = 1.0;
  for (int i = 0; i < iterations_; ++i, f *= 2.0) {  // fractal warp down to about one block
    wx += noise0_.eval(px * f, pz * f) / f;
    wz += noise1_.eval(px * f, pz * f) / f;
  }
  const double sx = noise0_.eval(px * 0.25 + 517.3, pz * 0.25), sz = noise1_.eval(px * 0.25 + 517.3, pz * 0.25);
  px += wx * opt_.warp + sx * opt_.seamWarp;
  pz += wz * opt_.warp + sz * opt_.seamWarp;
  // nearest hex centre among the three candidate rows (even rows centred at col + 0.5, odd rows at col)
  const int row = int(std::floor(pz));
  int bestC = 0, bestR = row;
  double best = 1e30;
  for (int r = row - 1; r <= row + 1; ++r) {
    const double shift = (r & 1) ? 0.0 : 0.5;
    const int c = int(std::floor(px - shift + 0.5));
    const double du = (px - (c + shift)) / RAD_INNER, dv = pz - (r + 0.5);
    const double d = du * du + dv * dv;
    if (d < best) { best = d; bestC = c; bestR = r; }
  }
  const Chunk& ch = chunk(bestC >> 5, bestR >> 5);
  return ch.b[((bestC & SIDE_MASK) << 5) | (bestR & SIDE_MASK)];
}

int HexBiomeMap::biome(double x, double z) const {
  const int b = rawBiome(x, z);
  int edge = kBiomes[b].edge, size = kBiomes[b].edgeSize;
  if (edge < 0 && kBiomes[b].parent >= 0) {
    edge = kBiomes[kBiomes[b].parent].edge;
    size = kBiomes[kBiomes[b].parent].edgeSize;
  }
  if (edge < 0) return b;
  for (int i = 0; i < 8; ++i)
    if (rawBiome(x + size * kEdgeX[i], z + size * kEdgeZ[i]) != b) return edge;
  return b;
}

}  // namespace dq::be
