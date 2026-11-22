// gen/surface_rules.cpp — see surface_rules.h
#include "gen/surface_rules.h"
#include "gen/density.h"
#include "gen/land_biomes.h"
#include "gen/village_pieces.h"
#include "common/noise.h"
#include <algorithm>
#include <cmath>

namespace dq {

namespace {
enum : u8 { C_BIOME, C_NOISE, C_VGRAD, C_YABOVE, C_WATER, C_TEMP, C_STEEP, C_NOT, C_HOLE, C_PRELIM, C_STONE, C_FALSE };
enum : u8 { R_BLOCK, R_SEQ, R_IF, R_BANDS };
constexpr BlockId NONE = 0xFFFF;
constexpr int MIN_Y = -64, TOP_Y = 319;   // (the imported Overworld's: the anchors count from these)

u64 mix(u64 z) { z += 0x9E3779B97F4A7C15ull; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
u64 hashStr(const std::string& s) { u64 h = 1469598103934665603ull; for (char ch : s) h = (h ^ (u8)ch) * 1099511628211ull; return h; }
double unit(u64 h) { return (double)(mix(h) >> 11) / (double)(1ull << 53); }   // [0, 1)
double posRand(u64 salt, int x, int y, int z) { return unit(salt ^ ((u64)(u32)x * 0x9E3779B1ull) ^ ((u64)(u32)y << 21) ^ ((u64)(u32)z * 0x85EBCA77ull << 7)); }
std::string strip(std::string s) { return s.rfind("main:", 0) == 0 ? s.substr(5) : s; }
} // namespace

int LandSurface::anchor(const json& j) const {
  if (j.contains("absolute")) return j["absolute"].get<int>();
  if (j.contains("above_bottom")) return MIN_Y + j["above_bottom"].get<int>();
  if (j.contains("below_top")) return TOP_Y - j["below_top"].get<int>();
  return 0;
}

int LandSurface::parseCond(const json& j) {
  Cond c;
  const std::string t = strip(j.value("type", std::string()));
  if (t == "biome") {
    c.t = C_BIOME; c.biomes.assign(bio_->count(), 0);
    for (auto& b : j["biome_is"]) { const int i = bio_->find(b.get<std::string>()); if (i >= 0) c.biomes[i] = 1; }
  } else if (t == "noise_threshold") {
    c.t = C_NOISE; c.noise = const_cast<DensityGraph*>(g_)->noise(j.value("noise", std::string()));
    c.lo = j.value("min_threshold", -1e308); c.hi = j.value("max_threshold", 1e308);
  } else if (t == "vertical_gradient") {
    c.t = C_VGRAD; c.a = anchor(j["true_at_and_below"]); c.b = anchor(j["false_at_and_above"]); c.salt = seed_ ^ (j.contains("random_salt") ? std::stoull(j["random_salt"].get<std::string>()) : hashStr(j.value("random_name", std::string())));   // (an imported gradient keeps the salt its name gave it)
  } else if (t == "y_above") {
    c.t = C_YABOVE; c.a = anchor(j["anchor"]); c.mul = j.value("surface_depth_multiplier", 0); c.add = j.value("add_stone_depth", false);
  } else if (t == "water") {
    c.t = C_WATER; c.offset = j.value("offset", 0); c.mul = j.value("surface_depth_multiplier", 0); c.add = j.value("add_stone_depth", false);
  } else if (t == "temperature") c.t = C_TEMP;
  else if (t == "steep") c.t = C_STEEP;
  else if (t == "hole") c.t = C_HOLE;
  else if (t == "above_preliminary_surface") c.t = C_PRELIM;
  else if (t == "not") { c.t = C_NOT; c.child = parseCond(j["invert"]); }
  else if (t == "stone_depth") {
    c.t = C_STONE; c.offset = j.value("offset", 0); c.add = j.value("add_surface_depth", false); c.range = j.value("secondary_depth_range", 0);
    c.ceiling = j.value("surface_type", std::string("floor")) == "ceiling";
  } else { DQ_LOG("surface rules: unknown condition %s", t.c_str()); c.t = C_FALSE; }
  conds_.push_back(std::move(c));
  return (int)conds_.size() - 1;
}

int LandSurface::parseRule(const json& j) {
  Rule r;
  const std::string t = strip(j.value("type", std::string()));
  if (t == "block") {
    r.t = R_BLOCK;
    const std::string nm = j["result_state"].value("Name", std::string());
    const std::string bare = nm.substr(nm.find(':') + 1);
    if (nm.rfind("main:", 0) != 0 && reg().blockIds.count(bare)) r.block = reg().blockId(bare);   // (an imported ground we have as a block of ours: tools/port/worldgen/ns_blocks.py)
    else if (auto it = subs_.find(nm); it != subs_.end()) r.block = blockFromState(json{{"Name", it->second}});   // (else ours that looks like it)
    else r.block = blockFromState(j["result_state"]);
    if (r.block == NONE) DQ_LOG("surface rules: no block for %s", j["result_state"].value("Name", std::string("?")).c_str());
  } else if (t == "sequence") { r.t = R_SEQ; for (auto& s : j["sequence"]) r.seq.push_back(parseRule(s)); }
  else if (t == "condition") { r.t = R_IF; r.cond = parseCond(j["if_true"]); r.then = parseRule(j["then_run"]); }
  else if (t == "bandlands") r.t = R_BANDS;
  else { DQ_LOG("surface rules: unknown rule %s", t.c_str()); r.t = R_BLOCK; r.block = NONE; }
  rules_.push_back(std::move(r));
  return (int)rules_.size() - 1;
}

bool LandSurface::load(const fs::path& dir, DensityGraph& g, const LandBiomes& biomes, u64 seed) {
  auto j = readJson(dir / "surface_rules.json");
  if (!j || !j->contains("base") || !biomes.ok()) return false;
  g_ = &g; bio_ = &biomes; seed_ = seed;
  if (auto sj = readJson(dir / "block_substitutes.json"); sj && sj->is_object()) for (auto& [k, v] : sj->items()) if (v.is_string()) subs_[k] = v.get<std::string>();
  nSurface = g.noise("main:surface"); nSecondary = g.noise("main:surface_secondary"); nClayOffset = g.noise("main:clay_bands_offset");
  base_ = parseRule((*j)["base"]);
  std::unordered_map<std::string, int> ns;
  for (auto& [k, v] : (*j)["namespaces"].items()) ns[k] = parseRule(v);
  byNs_.assign(biomes.count(), -1);
  for (int b = 0; b < biomes.count(); b++) {
    const std::string& n = biomes.info(b).name;
    if (auto it = ns.find(n.substr(0, n.find(':'))); it != ns.end()) byNs_[b] = it->second;
  }
  // the terracotta bands (with our random: the same kind of bands, not the imported data's exact ones)
  auto B = [](const char* n) { return blockFromState(json{{"Name", n}}); };
  const BlockId TC = B("terracotta"), OR = B("orange_terracotta"), YE = B("yellow_terracotta"), BR = B("brown_terracotta"),
                RE = B("red_terracotta"), WH = B("white_terracotta"), LG = B("light_gray_terracotta");
  u64 rs = mix(seed ^ hashStr("clay_bands"));
  auto nextInt = [&](int n) { rs = mix(rs); return (int)((rs >> 33) % (u64)n); };
  auto between = [&](int a, int b) { return a + nextInt(b - a + 1); };
  bands_.assign(192, TC);
  for (int i = 0; i < 192; i++) { i += nextInt(5) + 1; if (i < 192) bands_[i] = OR; }
  auto make = [&](int base, BlockId s) {
    const int n = between(6, 15);
    for (int k = 0; k < n; k++) { const int len = base + nextInt(3), at = nextInt(192); for (int m = 0; at + m < 192 && m < len; m++) bands_[at + m] = s; }
  };
  make(1, YE); make(2, BR); make(1, RE);
  const int whites = between(9, 15);
  for (int k = 0, i = 0; k < whites && i < 192; k++, i += nextInt(16) + 4) {
    bands_[i] = WH;
    if (i - 1 > 0 && nextInt(2)) bands_[i - 1] = LG;
    if (i + 1 < 192 && nextInt(2)) bands_[i + 1] = LG;
  }
  DQ_LOG("surface rules: %d rules, %d conditions", (int)rules_.size(), (int)conds_.size());
  return true;
}

void LandSurface::begin(Column& c, int x, int z, int prelim) const {
  c.x = x; c.z = z;
  c.surfaceDepth = (int)(g_->noiseValue(nSurface, x, 0, z) * 2.75 + 3.0 + posRand(seed_ ^ 0x5u, x, 0, z) * 0.25);
  c.secondary = g_->noiseValue(nSecondary, x, 0, z);
  c.minSurface = prelim + c.surfaceDepth - 8;
  c.biomeY = INT32_MIN;
}

int LandSurface::biomeOf(Column& c) const {
  if (c.biomeY != c.y) { c.biomeY = c.y; c.biome = c.biomeAt ? c.biomeAt(c.y) : -1; }
  return c.biome;
}

bool LandSurface::test(int ci, Column& c) const {
  const Cond& k = conds_[ci];
  switch (k.t) {
    case C_BIOME: { const int b = biomeOf(c); return b >= 0 && b < (int)k.biomes.size() && k.biomes[b]; }
    case C_NOISE: { const double v = g_->noiseValue(k.noise, c.x, 0, c.z); return v >= k.lo && v <= k.hi; }
    case C_VGRAD: {
      if (c.y <= k.a) return true;
      if (c.y >= k.b) return false;
      const double p = 1.0 - (double)(c.y - k.a) / (double)(k.b - k.a);   // (Mth.map(y, a, b, 1, 0))
      return posRand(k.salt, c.x, c.y, c.z) < p;
    }
    case C_YABOVE: return c.y + (k.add ? c.stoneAbove : 0) >= k.a + c.surfaceDepth * k.mul;
    case C_WATER: return c.waterHeight == INT32_MIN || c.y + (k.add ? c.stoneAbove : 0) >= c.waterHeight + k.offset + c.surfaceDepth * k.mul;
    case C_TEMP: { const int b = biomeOf(c); return bio_->coldEnoughToSnow(b, c.x, c.y, c.z); }
    case C_STEEP: return c.steep;
    case C_HOLE: return c.surfaceDepth <= 0;
    case C_PRELIM: return c.y >= c.minSurface;
    case C_NOT: return !test(k.child, c);
    case C_STONE: {
      const int d = k.ceiling ? c.stoneBelow : c.stoneAbove;
      if (k.ceiling && c.stoneBelow <= 1 && c.stoneAbove <= 1) return false;
      const int add = k.add ? c.surfaceDepth : 0;
      const int sec = k.range == 0 ? 0 : (int)((c.secondary + 1.0) / 2.0 * k.range);   // (Mth.map(secondary, -1, 1, 0, range))
      return d <= 1 + k.offset + add + sec;
    }
    default: return false;
  }
}

BlockId LandSurface::run(int ri, Column& c) const {
  const Rule& r = rules_[ri];
  switch (r.t) {
    case R_BLOCK: return r.block;
    case R_IF: return test(r.cond, c) ? run(r.then, c) : NONE;
    case R_SEQ: for (int s : r.seq) { const BlockId b = run(s, c); if (b != NONE) return b; } return NONE;
    case R_BANDS: {
      const int off = (int)std::lround(g_->noiseValue(nClayOffset, c.x, 0, c.z) * 4.0);
      return bands_[(((c.y + off) % 192) + 192) % 192];
    }
  }
  return NONE;
}

BlockId LandSurface::apply(Column& c) const {
  if (base_ < 0) return NONE;
  const int b = biomeOf(c);
  if (b >= 0 && b < (int)byNs_.size() && byNs_[b] >= 0) { const BlockId r = run(byNs_[b], c); if (r != NONE) return r; }
  return run(base_, c);
}

} // namespace dq
