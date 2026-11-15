// gen/biome_features.cpp — see biome_features.h. The features' rules follow the imported data's format (placement
// modifiers, the feature types named on each), the randomness our own.
#include "gen/biome_features.h"
#include "gen/gendefs.h"
#include "gen/land_biomes.h"
#include "gen/village_pieces.h"
#include "common/noise.h"
#include "world/chunk.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include <mutex>
#include <set>

namespace dq {
using namespace vmap;

namespace {
enum : u8 { I_CONST, I_UNIFORM, I_BIASED, I_CLAMPED, I_NORMAL, I_WEIGHTED };
enum : u8 { P_SIMPLE, P_WEIGHTED, P_NOISE, P_THRESHOLD, P_RULES };
enum : u8 { B_TRUE, B_BLOCKS, B_FLUIDS, B_SURVIVE, B_REPLACEABLE, B_SOLID, B_STURDY, B_ALL, B_ANY, B_NOT, B_FALSE };
enum : u8 { M_COUNT, M_RARITY, M_SQUARE, M_HEIGHTMAP, M_BIOME, M_FILTER, M_WATERDEPTH, M_RELATIVE, M_NOISECOUNT, M_NOISEBASED, M_OFFSET,
            M_HEIGHT, M_SCAN, M_NONE };
enum : u8 { F_NONE, F_PATCH, F_SIMPLE, F_SELECT, F_SIMPLESELECT, F_BOOL, F_COLUMN, F_VEGPATCH, F_VINES, F_SEAGRASS, F_KELP, F_PICKLE,
            F_BAMBOO, F_DISK, F_ROCK, F_ICEBERG, F_BLUEICE, F_FREEZE, F_BIGBLOCK, F_POND };
enum : u8 { HM_WORLD, HM_OCEAN, HM_MOTION, HM_NOLEAVES };
constexpr BlockId OUT = 0xFFFF;
constexpr int DATA_MIN_Y = -64, DATA_TOP_Y = 319;   // (the imported data's Overworld: height anchors count from these)

std::string strip(const std::string& s) { return s.rfind("main:", 0) == 0 ? s.substr(5) : s; }
std::string full(const std::string& s) { return s.find(':') == std::string::npos ? "main:" + s : s; }
u64 mix(u64 z) { z += 0x9E3779B97F4A7C15ull; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
int anchorY(const json& j) {   // the imported data's y
  if (j.is_object()) {
    if (j.contains("absolute")) return j["absolute"].get<int>();
    if (j.contains("above_bottom")) return DATA_MIN_Y + j["above_bottom"].get<int>();
    if (j.contains("below_top")) return DATA_TOP_Y - j["below_top"].get<int>();
  }
  return 0;
}
const json& val(const json& j) { return j.is_object() && j.contains("value") && j["value"].is_object() ? j["value"] : j; }   // (an int provider's bounds: in "value" or not)
const SimplexNoise& infoNoise() { static const SimplexNoise n(2345); return n; }   // (Biome.BIOME_INFO_NOISE)
} // namespace

struct BiomeFeatures::Rng {
  u64 s;
  explicit Rng(u64 seed) : s(seed) {}
  u64 next() { s += 0x9E3779B97F4A7C15ull; u64 z = s; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  int nextInt(int n) { return n <= 1 ? 0 : (int)((next() >> 33) % (u64)n); }
  int between(int a, int b) { return b <= a ? a : a + nextInt(b - a + 1); }
  float nextFloat() { return (float)((next() >> 40) * (1.0 / 16777216.0)); }
  double nextDouble() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
  bool nextBool() { return next() & 1; }
  double gaussian() { const double u = std::max(1e-12, nextDouble()), v = nextDouble(); return std::sqrt(-2 * std::log(u)) * std::cos(6.283185307179586 * v); }
};

struct BiomeFeatures::Ctx {
  GenBuf& b; int bx, bz; const u16* biome; const std::function<bool(int, int)>& builtOn;
  int step = 0, top = 0;
  const u8* treeish = nullptr;   // (our trees' blocks: logs, branches, leaves)   // (the feature being run: its step and placed index, for the biome filter)
  int hm[4][256]; u8 dirty[256];
  Ctx(GenBuf& b, int bx, int bz, const u16* bi, const std::function<bool(int, int)>& bo) : b(b), bx(bx), bz(bz), biome(bi), builtOn(bo) { std::memset(dirty, 1, sizeof dirty); stamp = nextStamp(); }
  bool in(int x, int y, int z) const { return x >= bx && x < bx + 16 && z >= bz && z < bz + 16 && y >= 0 && y < DIM_H; }
  BlockId get(int x, int y, int z) const { return in(x, y, z) ? b.blocks[colIdx(x - bx, y, z - bz)] : OUT; }
  int col(int x, int z) const { return std::clamp(z - bz, 0, 15) * 16 + std::clamp(x - bx, 0, 15); }   // (outside: its own edge)
  int biomeAt(int x, int z) const { return biome[col(x, z)]; }
  int height(int type, int x, int z) {   // the first free block over the column's top of the kind (band y)
    const int c = col(x, z);
    if (dirty[c]) {
      const Registry& R = reg(); const int lx = c & 15, lz = c >> 4;
      int got = 0, h[4] = {0, 0, 0, 0};
      for (int y = DIM_H - 1; y >= 0 && got != 15; y--) {
        const BlockId id = b.blocks[colIdx(lx, y, lz)];
        if (id == AIR || (treeish && treeish[id])) continue;   // (our trees are planted first: the ground is under them when the features run)
        if (!(got & 1)) { h[HM_WORLD] = y + 1; got |= 1; }
        const bool solid = R.tSolid[id], liquid = R.tLiquid[id];
        if (!(got & 2) && solid && !liquid) { h[HM_OCEAN] = y + 1; got |= 2; }
        if (!(got & 4) && (solid || liquid)) { h[HM_MOTION] = y + 1; got |= 4; }
        if (!(got & 8) && (solid || liquid) && !R.tLeaves[id]) { h[HM_NOLEAVES] = y + 1; got |= 8; }
      }
      for (int k = 0; k < 4; k++) hm[k][c] = h[k];
      dirty[c] = 0;
    }
    return hm[type][c];
  }
  bool set(int x, int y, int z, BlockId id, u8 meta) {
    if (!in(x, y, z)) return false;
    const int i = colIdx(x - bx, y, z - bz);
    if (b.blocks[i] != id) stamp++;   // (every remembered test result is stale now)
    b.blocks[i] = id; b.meta[i] = meta; dirty[col(x, z)] = 1;
    return true;
  }
  // block predicates' results, remembered until a block changes (the cliff features test each block dozens of times)
  struct Memo { u64 key; u32 stamp; u8 v; };
  static constexpr int MEMO = 1 << 15;
  std::vector<Memo>& memo() { static thread_local std::vector<Memo> m(MEMO, Memo{~0ull, 0, 0}); return m; }
  u32 stamp = 1;
  static u32 nextStamp() { static thread_local u32 s = 0x10000; return s += 0x10000; }
};

// ---- loading ----------------------------------------------------------------------------------------------
int BiomeFeatures::parseInt(const json& j) {
  IntP p;
  if (j.is_number()) { p.t = I_CONST; p.a = j.get<int>(); }
  else if (j.is_object()) {
    const std::string t = strip(j.value("type", std::string("constant")));
    const json& v = val(j);
    if (t == "constant") { p.t = I_CONST; p.a = v.is_object() && v.contains("value") ? v["value"].get<int>() : j.value("value", 0); }
    else if (t == "uniform") { p.t = I_UNIFORM; p.a = v.value("min_inclusive", 0); p.b = v.value("max_inclusive", 0); }
    else if (t == "biased_to_bottom") { p.t = I_BIASED; p.a = v.value("min_inclusive", 0); p.b = v.value("max_inclusive", 0); }
    else if (t == "clamped") { p.t = I_CLAMPED; p.src = parseInt(v["source"]); p.a = v.value("min_inclusive", 0); p.b = v.value("max_inclusive", 0); }
    else if (t == "clamped_normal") { p.t = I_NORMAL; p.mean = v.value("mean", 0.f); p.dev = v.value("deviation", 0.f); p.a = v.value("min_inclusive", 0); p.b = v.value("max_inclusive", 0); }
    else if (t == "weighted_list") { p.t = I_WEIGHTED; for (auto& e : j["distribution"]) { const int i = parseInt(e["data"]); p.wl.push_back({i, e.value("weight", 1)}); } }
    else { p.t = I_CONST; p.a = 0; }
  }
  ints_.push_back(std::move(p));
  return (int)ints_.size() - 1;
}

int BiomeFeatures::sampleInt(int i, Rng& r) const {
  if (i < 0) return 0;
  const IntP& p = ints_[i];
  switch (p.t) {
    case I_CONST: return p.a;
    case I_UNIFORM: return r.between(p.a, p.b);
    case I_BIASED: return p.a + r.nextInt(r.nextInt(p.b - p.a + 1) + 1);
    case I_CLAMPED: return std::clamp(sampleInt(p.src, r), p.a, p.b);
    case I_NORMAL: return std::clamp((int)std::floor(p.mean + r.gaussian() * p.dev), p.a, p.b);
    case I_WEIGHTED: {
      int tot = 0; for (auto& w : p.wl) tot += w.second;
      int k = r.nextInt(std::max(1, tot));
      for (auto& w : p.wl) { if (k < w.second) return sampleInt(w.first, r); k -= w.second; }
      return p.wl.empty() ? 0 : sampleInt(p.wl.back().first, r);
    }
  }
  return 0;
}

BiomeFeatures::State BiomeFeatures::state(const json& j) {
  State s;
  if (!j.is_object()) return s;
  static std::mutex mx; static std::unordered_map<std::string, BlockId> memo;   // (blockFromState logs each block it lacks: once)
  const std::string k = j.dump();
  { std::lock_guard<std::mutex> lk(mx); if (auto it = memo.find(k); it != memo.end()) { s.id = it->second; } else { s.id = blockFromState(j); memo[k] = s.id; } }
  if (s.id != OUT) {
    // a plant's own stages and props we don't keep; the pickles' count is ours
    const std::string pk = j.contains("Properties") && j["Properties"].contains("pickles") ? j["Properties"]["pickles"].get<std::string>() : "";
    if (!pk.empty()) s.meta = (u8)std::clamp(std::atoi(pk.c_str()) - 1, 0, 3);
  }
  return s;
}

void BiomeFeatures::tagInto(const std::string& tag, std::vector<u8>& m, int depth) {
  auto it = tags_.find(full(tag));
  if (it == tags_.end() || depth > 8) return;
  for (const std::string& v : it->second) {
    if (!v.empty() && v[0] == '#') { tagInto(v.substr(1), m, depth + 1); continue; }
    const State s = state(json{{"Name", v}});
    if (s.id != OUT && s.id < m.size()) m[s.id] = 1;
  }
}

std::vector<u8> BiomeFeatures::blockSet(const json& j) {
  std::vector<u8> m(reg().blocks.size() + 1, 0);
  auto one = [&](const std::string& n) {
    if (!n.empty() && n[0] == '#') { tagInto(n.substr(1), m); return; }
    const State s = state(json{{"Name", n}});
    if (s.id != OUT && s.id < m.size()) m[s.id] = 1;
    if (strip(n) == "air" || strip(n) == "cave_air") m[AIR] = 1;
  };
  if (j.is_string()) one(j.get<std::string>());
  else if (j.is_array()) for (auto& e : j) if (e.is_string()) one(e.get<std::string>());
  return m;
}

int BiomeFeatures::parseProv(const json& j) {
  Prov p;
  const std::string t = strip(j.value("type", std::string(j.contains("fallback") ? "rule_based_state_provider" : "simple_state_provider")));
  if (t == "rule_based_state_provider") {   // (the disks': no type given)
    p.t = P_RULES; p.fallback = !j.contains("fallback") || j["fallback"].is_null() ? -1 : parseProv(j["fallback"]);
    for (auto& r : j["rules"]) p.rules.push_back({parsePred(r["if_true"]), parseProv(r["then"])});
  }
  else if (t == "simple_state_provider" || t == "rotated_block_provider") { p.t = P_SIMPLE; p.states.push_back(state(j["state"])); }
  else if (t == "weighted_state_provider") { p.t = P_WEIGHTED; for (auto& e : j["entries"]) { p.states.push_back(state(e["data"])); p.weights.push_back(e.value("weight", 1)); p.total += e.value("weight", 1); } }
  else if (t == "noise_provider" || t == "dual_noise_provider") { p.t = P_NOISE; p.seed = (u64)j.value("seed", (i64)0); p.scale = j.value("scale", 1.0) * (t == "dual_noise_provider" ? 1.0 : 1.0); for (auto& e : j["states"]) p.states.push_back(state(e)); }
  else if (t == "noise_threshold_provider") {
    p.t = P_THRESHOLD; p.seed = (u64)j.value("seed", (i64)0); p.scale = j.value("scale", 1.0); p.threshold = j.value("threshold", 0.0); p.high = j.value("high_chance", 0.0);
    p.def = state(j["default_state"]); for (auto& e : j["low_states"]) p.low.push_back(state(e)); for (auto& e : j["high_states"]) p.highS.push_back(state(e));
  }
  else if (t == "randomized_int_state_provider") { const int s = parseProv(j["source"]); provs_.push_back(provs_[s]); return (int)provs_.size() - 1; }
  else if (j.contains("fallback")) {   // (rule_based_state_provider: the disks')
    p.t = P_RULES; p.fallback = j["fallback"].is_null() ? -1 : parseProv(j["fallback"]);
    for (auto& r : j["rules"]) p.rules.push_back({parsePred(r["if_true"]), parseProv(r["then"])});
  } else { p.t = P_SIMPLE; p.states.push_back(State{}); DQ_LOG("features: unknown state provider %s", t.c_str()); }
  provs_.push_back(std::move(p));
  return (int)provs_.size() - 1;
}

BiomeFeatures::State BiomeFeatures::provide(int i, Rng& r, int x, int y, int z, Ctx& c) const {
  const Prov& p = provs_[i];
  auto noise = [&](u64 seed, double scale) {
    static thread_local std::unordered_map<u64, std::unique_ptr<SimplexNoise>> ns;
    auto& n = ns[seed]; if (!n) n = std::make_unique<SimplexNoise>((u32)(seed ^ (seed >> 32)));
    return n->noise3(x * scale, (y - 1) * scale, z * scale);
  };
  switch (p.t) {
    case P_SIMPLE: return p.states.empty() ? State{} : p.states[0];
    case P_WEIGHTED: { int k = r.nextInt(std::max(1, p.total)); for (size_t s = 0; s < p.states.size(); s++) { if (k < p.weights[s]) return p.states[s]; k -= p.weights[s]; } return p.states.back(); }
    case P_NOISE: {
      if (p.states.empty()) return State{};
      const double v = noise(p.seed, p.scale);
      return p.states[std::clamp((int)((1.0 + v) / 2.0 * p.states.size()), 0, (int)p.states.size() - 1)];
    }
    case P_THRESHOLD: {
      const double v = noise(p.seed, p.scale);
      if (v < p.threshold) return p.low.empty() ? p.def : p.low[r.nextInt((int)p.low.size())];
      return r.nextFloat() < p.high && !p.highS.empty() ? p.highS[r.nextInt((int)p.highS.size())] : p.def;
    }
    case P_RULES:
      for (auto& [pr, pv] : p.rules) if (test(pr, c, x, y, z)) return provide(pv, r, x, y, z, c);
      return p.fallback >= 0 ? provide(p.fallback, r, x, y, z, c) : State{};
  }
  return State{};
}

int BiomeFeatures::parsePred(const json& j) {
  Pred p;
  const std::string t = strip(j.value("type", std::string("true")));
  if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() == 3) { p.ox = j["offset"][0]; p.oy = j["offset"][1]; p.oz = j["offset"][2]; }
  if (t == "true" || t == "inside_world_bounds" || t == "unobstructed") p.t = B_TRUE;
  else if (t == "matching_blocks") { p.t = B_BLOCKS; p.mask = blockSet(j["blocks"]); }
  else if (t == "matching_block_tag") { p.t = B_BLOCKS; p.mask = blockSet("#" + j.value("tag", std::string())); }
  else if (t == "matching_fluids") {
    p.t = B_FLUIDS; p.mask.assign(2, 0);
    auto one = [&](const std::string& f) { if (f.find("water") != std::string::npos) p.mask[0] = 1; if (f.find("lava") != std::string::npos) p.mask[1] = 1; };
    if (j["fluids"].is_string()) one(j["fluids"]); else for (auto& f : j["fluids"]) one(f);
  }
  else if (t == "would_survive") { p.t = B_SURVIVE; p.st = state(j["state"]); }
  else if (t == "replaceable") p.t = B_REPLACEABLE;
  else if (t == "solid") p.t = B_SOLID;
  else if (t == "has_sturdy_face") { p.t = B_STURDY; const std::string d = j.value("direction", std::string("up")); p.dir = d == "up" ? 0 : d == "down" ? 1 : 2; }
  else if (t == "all_of" || t == "any_of") { p.t = t == "all_of" ? B_ALL : B_ANY; for (auto& k : j["predicates"]) p.kids.push_back(parsePred(k)); }
  else if (t == "not") { p.t = B_NOT; p.kids.push_back(parsePred(j["predicate"])); }
  else { p.t = B_FALSE; DQ_LOG("features: unknown block predicate %s", t.c_str()); }
  preds_.push_back(std::move(p));
  return (int)preds_.size() - 1;
}

bool BiomeFeatures::test(int i, Ctx& c, int x, int y, int z) const {
  const Pred& p = preds_[i];
  x += p.ox; y += p.oy; z += p.oz;
  const Registry& R = reg();
  switch (p.t) {
    case B_TRUE: return true;
    case B_FALSE: return false;
    case B_BLOCKS: { const BlockId id = c.get(x, y, z); return id != OUT && id < p.mask.size() && p.mask[id]; }
    case B_FLUIDS: { const BlockId id = c.get(x, y, z); return id != OUT && ((p.mask[0] && id == water_) || (p.mask[1] && R.tLiquid[id] && id != water_)); }
    case B_SURVIVE: return p.st.id != OUT && survives(p.st.id, c, x, y, z);
    case B_REPLACEABLE: { const BlockId id = c.get(x, y, z); return id != OUT && (id == AIR || R.tReplaceable[id] || id == water_); }
    case B_SOLID: { const BlockId id = c.get(x, y, z); return id != OUT && R.tSolid[id] && !R.tLiquid[id]; }
    case B_STURDY: { const BlockId id = c.get(x, y, z); return id != OUT && R.tSolid[id] && R.tOpaque[id]; }
    case B_ALL: for (int k : p.kids) if (!test(k, c, x, y, z)) return false; return true;
    case B_ANY: for (int k : p.kids) if (test(k, c, x, y, z)) return true; return false;
    case B_NOT: return !test(p.kids[0], c, x, y, z);
  }
  return false;
}

// canSurvive, for the kinds of block the features place: plants on soil (sand for the desert's), cactus on sand
// alone, sugar cane by water, lily pads on water, sea plants under it, vines on a wall
bool BiomeFeatures::survives(BlockId id, Ctx& c, int x, int y, int z) const {
  const Registry& R = reg();
  const BlockDef& d = R.block(id);
  const BlockId here = c.get(x, y, z), below = c.get(x, y - 1, z);
  if (here == OUT || below == OUT) return false;
  auto in = [&](const std::vector<u8>& m, BlockId b) { return b < m.size() && m[b]; };
  if (d.has("onWater")) return below == water_ && (here == AIR || R.tReplaceable[here]);
  if (id == seagrass_ || id == kelp_ || id == pickle_) return here == water_ && (below == id || (R.tSolid[below] && !R.tLiquid[below] && below != ice_));
  if (id == vine_) { for (int k = 0; k < 4; k++) { const BlockId w = c.get(x + DIRX[k], y, z + DIRZ[k]); if (w != OUT && R.tSolid[w] && R.tOpaque[w]) return true; } return false; }
  if (id == cactus_) {
    for (int k = 0; k < 4; k++) { const BlockId w = c.get(x + DIRX[k], y, z + DIRZ[k]); if (w == OUT || (w != AIR && R.tSolid[w])) return false; }
    return below == cactus_ || in(sand_, below);
  }
  if (id == cane_) {
    if (below == cane_) return true;
    if (!in(dirt_, below) && !in(sand_, below)) return false;
    for (int k = 0; k < 4; k++) { const BlockId w = c.get(x + DIRX[k], y - 1, z + DIRZ[k]); if (w == water_ || w == ice_) return true; }
    return false;
  }
  if (id == bamboo_) return below == bamboo_ || in(dirt_, below) || in(sand_, below) || below == R.blockId("gravel");
  if (!d.has("needsSupport")) return true;
  if (here != AIR && here != water_ && !R.tReplaceable[here]) return false;
  if (here == water_ && !R.tWlog[id]) return false;
  if (in(plantable_, id)) return in(dirt_, below) || in(sand_, below) || in(terracotta_, below);   // (the dry lands' plants)
  if (d.key.find("mushroom") != std::string::npos) return R.tSolid[below] && R.tOpaque[below];
  return in(dirt_, below);
}

int BiomeFeatures::placedRef(const std::string& id) {
  const std::string k = full(id);
  if (auto it = placedIds_.find(k); it != placedIds_.end()) return it->second;
  placedIds_[k] = -1;   // (a loop guard)
  const size_t c = k.find(':');
  auto j = readJson(dir_ / "placed_feature" / k.substr(0, c) / (k.substr(c + 1) + ".json"));
  int i = -1;
  try { i = j ? parsePlaced(*j) : -1; } catch (const std::exception& e) { DQ_LOG("features: %s left out (%s)", k.c_str(), e.what()); }
  placedIds_[k] = i;
  return i;
}

int BiomeFeatures::confRef(const std::string& id) {
  const std::string k = full(id);
  if (auto it = confIds_.find(k); it != confIds_.end()) return it->second;
  confIds_[k] = -1;
  const size_t c = k.find(':');
  auto j = readJson(dir_ / "configured_feature" / k.substr(0, c) / (k.substr(c + 1) + ".json"));
  int i = -1;
  try { i = j ? parseConf(*j) : -1; } catch (const std::exception& e) { DQ_LOG("features: %s left out (%s)", k.c_str(), e.what()); }
  confIds_[k] = i;
  return i;
}

int BiomeFeatures::parsePlaced(const json& j) {
  if (j.is_string()) return placedRef(j.get<std::string>());
  Placed P;
  const json& f = j.contains("feature") ? j["feature"] : j;
  P.conf = f.is_string() ? confRef(f.get<std::string>()) : parseConf(f);
  if (j.contains("placement")) for (auto& m : j["placement"]) {
    Mod M;
    const std::string t = strip(m.value("type", std::string()));
    if (t == "count") { M.t = M_COUNT; M.ip = parseInt(m["count"]); }
    else if (t == "rarity_filter") { M.t = M_RARITY; M.f = 1.0f / std::max(1, m.value("chance", 1)); }
    else if (t == "in_square") M.t = M_SQUARE;
    else if (t == "heightmap") {
      M.t = M_HEIGHTMAP; const std::string h = m.value("heightmap", std::string("WORLD_SURFACE_WG"));
      M.hm = h.rfind("OCEAN_FLOOR", 0) == 0 ? HM_OCEAN : h == "MOTION_BLOCKING" ? HM_MOTION : h == "MOTION_BLOCKING_NO_LEAVES" ? HM_NOLEAVES : HM_WORLD;
    }
    else if (t == "biome") M.t = M_BIOME;
    else if (t == "block_predicate_filter") { M.t = M_FILTER; M.pred = parsePred(m["predicate"]); }
    else if (t == "surface_water_depth_filter") { M.t = M_WATERDEPTH; M.a = m.value("max_water_depth", 0); }
    else if (t == "surface_relative_threshold_filter") {
      M.t = M_RELATIVE; const std::string h = m.value("heightmap", std::string("WORLD_SURFACE_WG"));
      M.hm = h.rfind("OCEAN_FLOOR", 0) == 0 ? HM_OCEAN : h == "MOTION_BLOCKING" ? HM_MOTION : h == "MOTION_BLOCKING_NO_LEAVES" ? HM_NOLEAVES : HM_WORLD;
      M.a = m.value("min_inclusive", -100000); M.b = m.value("max_inclusive", 100000);
    }
    else if (t == "noise_threshold_count") { M.t = M_NOISECOUNT; M.f = m.value("noise_level", 0.f); M.a = m.value("below_noise", 0); M.b = m.value("above_noise", 0); }
    else if (t == "noise_based_count") { M.t = M_NOISEBASED; M.f = m.value("noise_to_count_ratio", 0.f); M.g = m.value("noise_factor", 1.f); M.h = m.value("noise_offset", 0.f); }
    else if (t == "random_offset") { M.t = M_OFFSET; M.ip = parseInt(m["xz_spread"]); M.ip2 = parseInt(m["y_spread"]); }
    else if (t == "height_range") {
      M.t = M_HEIGHT; const json& h = m["height"];
      const std::string ht = strip(h.value("type", std::string("constant")));
      if (ht == "constant") { M.dir = 0; M.a = M.b = anchorY(h.contains("value") ? h["value"] : h); }
      else { M.dir = ht == "trapezoid" ? 1 : ht == "uniform" ? 0 : 2; M.a = anchorY(h["min_inclusive"]); M.b = anchorY(h["max_inclusive"]); M.f = (float)h.value("plateau", 0); M.ip = h.value("inner", 1); }
    }
    else if (t == "environment_scan") {
      M.t = M_SCAN; M.dir = m.value("direction_of_search", std::string("down")) == "up" ? 1 : -1; M.a = m.value("max_steps", 1);
      M.pred = parsePred(m["target_condition"]); M.pred2 = m.contains("allowed_search_condition") ? parsePred(m["allowed_search_condition"]) : -1;
    }
    else { M.t = M_NONE; P.ok = false; }   // (count_on_every_layer, carving_mask...: the Nether's and the caves', not here)
    if (M.t == M_OFFSET || M.t == M_SQUARE) P.moves = true;
    P.mods.push_back(M);
  }
  placed_.push_back(std::move(P));
  return (int)placed_.size() - 1;
}

int BiomeFeatures::parseConf(const json& j) {
  Conf C;
  const std::string t = j.value("type", std::string());
  const json& g = j.contains("config") ? j["config"] : json::object();
  const std::string tt = strip(t);
  if (tt == "random_patch" || tt == "flower" || tt == "no_bonemeal_flower" || t == "nature:leveled_random_patch_feature") {
    C.t = F_PATCH; C.a = g.value("tries", 128); C.b = g.value("xz_spread", 7); C.c = g.value("y_spread", 3); C.d = parsePlaced(g["feature"]);
  }
  else if (tt == "simple_block") { C.t = F_SIMPLE; C.prov = parseProv(g["to_place"]); }
  else if (tt == "random_selector") {
    C.t = F_SELECT; for (auto& e : g["features"]) { C.kids.push_back(parsePlaced(e["feature"])); C.chances.push_back(e.value("chance", 0.f)); }
    C.a = parsePlaced(g["default"]);
  }
  else if (tt == "simple_random_selector") { C.t = F_SIMPLESELECT; for (auto& e : g["features"]) C.kids.push_back(parsePlaced(e)); }
  else if (tt == "random_boolean_selector") { C.t = F_BOOL; C.a = parsePlaced(g["feature_true"]); C.b = parsePlaced(g["feature_false"]); }
  else if (tt == "block_column") {
    C.t = F_COLUMN; C.flag = g.value("direction", std::string("up")) != "down"; C.pred = parsePred(g["allowed_placement"]); C.h = g.value("prioritize_tip", false) ? 1.f : 0.f;
    for (auto& l : g["layers"]) C.layers.push_back({parseInt(l["height"]), parseProv(l["provider"])});
  }
  else if (tt == "vegetation_patch" || tt == "waterlogged_vegetation_patch") {
    C.t = F_VEGPATCH; C.c = tt == "waterlogged_vegetation_patch";
    const json& rp = g["replaceable"]; C.mask = blockSet(rp.is_string() && rp.get<std::string>()[0] != '#' ? json("#" + rp.get<std::string>()) : rp);
    C.prov = parseProv(g["ground_state"]); C.a = parsePlaced(g["vegetation_feature"]); C.flag = g.value("surface", std::string("floor")) == "ceiling";
    C.ia = parseInt(g["depth"]); C.f = g.value("extra_bottom_block_chance", 0.f); C.b = g.value("vertical_range", 1); C.g = g.value("vegetation_chance", 0.f);
    C.ib = parseInt(g["xz_radius"]); C.h = g.value("extra_edge_column_chance", 0.f);
  }
  else if (tt == "vines") C.t = F_VINES;
  else if (tt == "seagrass") { C.t = F_SEAGRASS; C.f = g.value("probability", 0.f); }
  else if (tt == "kelp") C.t = F_KELP;
  else if (tt == "sea_pickle") { C.t = F_PICKLE; C.ia = parseInt(g["count"]); }
  else if (tt == "bamboo") { C.t = F_BAMBOO; C.f = g.value("probability", 0.f); }
  else if (tt == "disk") { C.t = F_DISK; C.prov = parseProv(g["state_provider"]); C.pred = parsePred(g["target"]); C.ia = parseInt(g["radius"]); C.a = g.value("half_height", 1); }
  else if (tt == "forest_rock") { C.t = F_ROCK; C.st = state(g["state"]); }
  else if (tt == "iceberg") { C.t = F_ICEBERG; C.st = state(g["state"]); }
  else if (tt == "blue_ice") C.t = F_BLUEICE;
  else if (tt == "freeze_top_layer") C.t = F_FREEZE;
  else if (t == "nature:large_pumpkin_feature") { C.t = F_BIGBLOCK; C.prov = parseProv(g["state_provider"]); }
  else if (t == "nature:water_delta_feature") { C.t = F_POND; C.st = state(g["contents"]); C.st2 = state(g["rim"]); C.ia = parseInt(g["size"]); C.ib = parseInt(g["rim_size"]); }
  else if (t == "nature:turnip_root_feature" && g.contains("feature")) { C.t = F_SIMPLESELECT; C.kids.push_back(parsePlaced(g["feature"])); }   // (its turnip, without the roots)
  else C.t = F_NONE;   // (trees are ours; ores, lakes, springs, fossils, geodes, dungeons, the caves' and the Nether's are left out)
  confs_.push_back(std::move(C));
  return (int)confs_.size() - 1;
}

int BiomeFeatures::intMax(int i) const {   // the largest size an int provider gives
  if (i < 0) return 0;
  const IntP& p = ints_[i];
  switch (p.t) {
    case I_CONST: return std::abs(p.a);
    case I_WEIGHTED: { int m = 0; for (auto& w : p.wl) m = std::max(m, intMax(w.first)); return m; }
    case I_CLAMPED: return std::min(std::max(std::abs(p.a), std::abs(p.b)), intMax(p.src));
    default: return std::max(std::abs(p.a), std::abs(p.b));
  }
}

int BiomeFeatures::reachOf(int f, int depth) const {   // how far from its origin a feature may set blocks (-1: it places nothing)
  if (f < 0 || depth > 12) return -1;
  const Conf& C = confs_[f];
  auto kid = [&](int p) {
    if (p < 0 || !placed_[p].ok) return -1;
    int r = reachOf(placed_[p].conf, depth + 1);
    if (r < 0) return -1;
    for (const Mod& m : placed_[p].mods) if (m.t == M_OFFSET) r += intMax(m.ip); else if (m.t == M_SQUARE) r += 16;
    return r;
  };
  switch (C.t) {
    case F_NONE: return -1;
    case F_PATCH: { const int r = kid(C.d); return r < 0 ? -1 : r + C.b; }
    case F_SELECT: case F_SIMPLESELECT: case F_BOOL: {
      int r = -1; std::vector<int> ks = C.kids; if (C.a >= 0) ks.push_back(C.a); if (C.b >= 0 && C.t == F_BOOL) ks.push_back(C.b);
      for (int k : ks) { const int kr = kid(k); if (kr < 0) return -1; r = std::max(r, kr); }   // (a choice with a tree in it: our trees')
      return r;
    }
    case F_VEGPATCH: return intMax(C.ib) + 2 + std::max(0, kid(C.a));
    case F_PICKLE: case F_SEAGRASS: return 8;
    case F_BAMBOO: return 5;
    case F_DISK: return intMax(C.ia) + 1;
    case F_ROCK: return 4;
    case F_ICEBERG: return 16;
    case F_BLUEICE: return 8;
    case F_FREEZE: return 0;
    case F_POND: return 16;
    case F_BIGBLOCK: return 2;
    default: return 1;
  }
}

bool BiomeFeatures::load(const fs::path& dir, const LandBiomes& biomes, u64 seed) {
  dir_ = dir; bio_ = &biomes; seed_ = seed;
  if (auto t = readJson(dir / "block_tags.json"); t && t->is_object()) for (auto& [k, v] : t->items()) tags_[k] = v.get<std::vector<std::string>>();
  else return false;
  const Registry& R = reg();
  water_ = R.blockId("water"); ice_ = R.blockId("ice"); packed_ = R.blockId("packed_ice"); blueIce_ = R.blockId("blue_ice"); snow_ = R.blockId("snow_layer");
  snowBlock_ = R.blockId("snow_block"); grass_ = R.blockId("grass"); snowyGrass_ = R.blockId("snowy_grass"); podzol_ = R.blockId("podzol");
  seagrass_ = R.blockId("seagrass"); kelp_ = R.blockId("kelp"); pickle_ = R.blockId("sea_pickle");
  bamboo_ = R.blockId("bamboo"); cactus_ = R.blockId("cactus"); cane_ = R.blockId("sugar_cane"); vine_ = R.blockId("vine");
  const size_t n = R.blocks.size() + 1;
  dirt_.assign(n, 0); sand_.assign(n, 0); terracotta_.assign(n, 0); plantable_.assign(n, 0);
  tagInto("main:dirt", dirt_); tagInto("main:sand", sand_); tagInto("main:terracotta", terracotta_);
  dirt_.resize(n); sand_.resize(n); terracotta_.resize(n);
  for (const char* k : {"snowy_grass", "farmland", "dirt_with_dry_grass", "dirt_with_coniferous_litter", "dirt_with_rainforest_litter", "scorched_dirt", "peat"}) if (BlockId id = R.blockId(k)) dirt_[id] = 1;
  for (const char* k : {"silver_sand", "desert_sand", "red_sand"}) if (BlockId id = R.blockId(k)) sand_[id] = 1;
  for (size_t i = 1; i < R.blocks.size(); i++) {
    const std::string& k = R.blocks[i].key;
    for (const char* w : {"succulent", "scorched", "beach_grass", "dead_bush", "dry_grass", "spinifex", "marram"}) if (k.find(w) != std::string::npos) plantable_[i] = 1;
  }
  // each biome's features by step; the steps' order: as first listed
  for (int s = 0; s < STEPS; s++) has_[s].assign(biomes.count(), {});
  std::set<int> seen[STEPS];
  for (int b = 0; b < biomes.count(); b++) {
    const std::string& name = biomes.info(b).name;
    const size_t c = name.find(':');
    auto j = readJson(dir / "biome" / name.substr(0, c) / (name.substr(c + 1) + ".json"));
    if (!j || !j->contains("features")) continue;
    const json& F = (*j)["features"];
    for (int s = 0; s < STEPS && s < (int)F.size(); s++) {
      for (auto& e : F[s]) {
        if (!e.is_string()) continue;
        const int p = placedRef(e.get<std::string>());
        if (p < 0) continue;
        if (has_[s][b].size() <= (size_t)p) has_[s][b].resize(p + 1, 0);
        has_[s][b][p] = 1;
        if (seen[s].insert(p).second) order_[s].push_back(p);
      }
    }
  }
  // a placed feature that places nothing (a tree's, an ore's...) isn't run at all
  int run = 0, total = 0;
  for (int s = 0; s < STEPS; s++) for (int p : order_[s]) { total++; if (placed_[p].ok && reachOf(placed_[p].conf) >= 0) run++; else placed_[p].ok = false; }
  if (std::getenv("DQ_FEATLIST")) for (int s = 0; s < STEPS; s++) for (int p : order_[s]) for (auto& [id, ix] : placedIds_) if (ix == p) DQ_LOG("feature %d %s %s", s, placed_[p].ok ? "run" : "left", id.c_str());
  for (auto& C : confs_) C.reach = 0;
  for (size_t f = 0; f < confs_.size(); f++) confs_[f].reach = reachOf((int)f);
  loaded_ = true;
  DQ_LOG("features: %d of %d biome features placed by us (%zu placed, %zu configured)", run, total, placed_.size(), confs_.size());
  return true;
}

// ---- placing ----------------------------------------------------------------------------------------------
bool BiomeFeatures::setState(Ctx& c, int x, int y, int z, State s) const {
  if (s.id == OUT) return false;
  const Registry& R = reg();
  u8 meta = s.meta;
  if (R.tWlog[s.id] && c.get(x, y, z) == water_) meta |= 0x80;
  if (s.id != AIR && R.block(s.id).has("double")) {
    if (c.get(x, y + 1, z) != AIR) return false;
    c.set(x, y + 1, z, s.id, 1);
    meta = 0;
  }
  return c.set(x, y, z, s.id, meta);
}

bool BiomeFeatures::placePlaced(int pi, Ctx& c, Rng& r, int x, int y, int z) const {
  if (pi < 0) return false;
  const Placed& P = placed_[pi];
  if (!P.ok || P.conf < 0) return false;
  struct Pos { int x, y, z; };
  static thread_local std::vector<Pos> bufs[16], bufs2[16]; static thread_local int depth = 0;
  if (depth >= 15) return false;
  std::vector<Pos>& cur = bufs[depth];
  std::vector<Pos>& nxt = bufs2[depth];   // (each nesting level its own: the features below may place their own)
  depth++;
  cur.clear(); cur.push_back({x, y, z});
  for (const Mod& m : P.mods) {
    nxt.clear();
    for (const Pos& p : cur) {
      switch (m.t) {
        case M_COUNT: { const int n = sampleInt(m.ip, r); for (int k = 0; k < n; k++) nxt.push_back(p); break; }
        case M_RARITY: if (r.nextFloat() < m.f) nxt.push_back(p); break;
        case M_SQUARE: { const int ax = r.nextInt(16) + p.x, az = r.nextInt(16) + p.z; nxt.push_back({ax, p.y, az}); break; }
        case M_HEIGHTMAP: { const int h = c.height(m.hm, p.x, p.z); if (h > 0) nxt.push_back({p.x, h, p.z}); break; }
        case M_BIOME: { const int b = c.biomeAt(p.x, p.z); if (b >= 0 && b < (int)has_[c.step].size() && (size_t)c.top < has_[c.step][b].size() && has_[c.step][b][c.top]) nxt.push_back(p); break; }
        case M_FILTER: {
          const int lx = p.x - c.bx + 16, lz = p.z - c.bz + 16;
          if (lx < 0 || lx > 47 || lz < 0 || lz > 47 || p.y < 0 || p.y >= DIM_H) { if (test(m.pred, c, p.x, p.y, p.z)) nxt.push_back(p); break; }
          const u64 key = ((u64)m.pred << 24) | ((u64)lx << 16) | ((u64)lz << 8) | (u64)p.y;
          Ctx::Memo& e = c.memo()[(size_t)(mix(key) & (Ctx::MEMO - 1))];
          if (e.key != key || e.stamp != c.stamp) { e.key = key; e.stamp = c.stamp; e.v = test(m.pred, c, p.x, p.y, p.z); }
          if (e.v) nxt.push_back(p);
          break;
        }
        case M_WATERDEPTH: if (c.height(HM_WORLD, p.x, p.z) - c.height(HM_OCEAN, p.x, p.z) <= m.a) nxt.push_back(p); break;
        case M_RELATIVE: { const int h = c.height(m.hm, p.x, p.z); if (p.y >= h + m.a && p.y <= h + m.b) nxt.push_back(p); break; }
        case M_NOISECOUNT: { const double d = infoNoise().noise2(p.x / 200.0, p.z / 200.0); const int n = d < m.f ? m.a : m.b; for (int k = 0; k < n; k++) nxt.push_back(p); break; }
        case M_NOISEBASED: { const double d = infoNoise().noise2(p.x / (double)m.g, p.z / (double)m.g); const int n = (int)std::ceil((d + m.h) * m.f); for (int k = 0; k < n; k++) nxt.push_back(p); break; }
        case M_OFFSET: nxt.push_back({p.x + sampleInt(m.ip, r), p.y + sampleInt(m.ip2, r), p.z + sampleInt(m.ip, r)}); break;
        case M_HEIGHT: {
          int my;
          if (m.dir == 0) my = r.between(m.a, m.b);
          else if (m.dir == 1) { const int i = m.b - m.a, pl = (int)m.f; if (pl >= i) my = r.between(m.a, m.b); else { const int j = (i - pl) / 2, k = i - j; my = m.a + r.between(0, k) + r.between(0, j); } }
          else my = m.a + r.nextInt(r.nextInt(std::max(1, m.b - m.a - m.ip + 1)) + m.ip);   // (BiasedToBottomHeight)
          const int y2 = my + 1;
          if (y2 >= 1 && y2 < DIM_H) nxt.push_back({p.x, y2, p.z});
          break;
        }
        case M_SCAN: {
          int sy = p.y;
          if (m.pred2 >= 0 && !test(m.pred2, c, p.x, sy, p.z)) break;
          bool found = false;
          for (int k = 0; k < m.a; k++) {
            if (test(m.pred, c, p.x, sy, p.z)) { found = true; break; }
            sy += m.dir;
            if (sy < 1 || sy >= DIM_H) break;
            if (m.pred2 >= 0 && !test(m.pred2, c, p.x, sy, p.z)) break;
          }
          if (found || (sy >= 1 && sy < DIM_H && test(m.pred, c, p.x, sy, p.z))) nxt.push_back({p.x, sy, p.z});
          break;
        }
        default: break;
      }
    }
    cur.swap(nxt);
    if (cur.empty()) break;
  }
  bool any = false;
  for (size_t k = 0; k < cur.size(); k++) { const Pos p = cur[k]; any |= placeConf(P.conf, c, r, p.x, p.y, p.z); }
  depth--;
  return any;
}

bool BiomeFeatures::placeConf(int fi, Ctx& c, Rng& r, int x, int y, int z) const {
  if (fi < 0) return false;
  const Conf& C = confs_[fi];
  const Registry& R = reg();
  // nothing it does can reach this chunk: skip it (its randomness is its own, so the chunks round it agree either way)
  if (C.reach >= 0 && (x + C.reach < c.bx || x - C.reach > c.bx + 15 || z + C.reach < c.bz || z - C.reach > c.bz + 15)) return false;
  switch (C.t) {
    case F_NONE: return false;
    case F_PATCH: {
      int placed = 0; const int xz = C.b + 1, yy = C.c + 1;
      for (int k = 0; k < C.a; k++) {
        const int px = x + r.nextInt(xz) - r.nextInt(xz), py = y + r.nextInt(yy) - r.nextInt(yy), pz = z + r.nextInt(xz) - r.nextInt(xz);
        Rng sub(r.next());   // (each try its own randomness: the same patch whichever chunk is being made)
        if ((px < c.bx - 1 || px > c.bx + 16 || pz < c.bz - 1 || pz > c.bz + 16) && C.d >= 0 && !placed_[C.d].moves && placed_[C.d].conf >= 0 && confs_[placed_[C.d].conf].reach <= 1) continue;   // (it can't reach in)
        if (placePlaced(C.d, c, sub, px, py, pz)) placed++;
      }
      return placed > 0;
    }
    case F_SIMPLE: {
      const State s = provide(C.prov, r, x, y, z, c);
      if (s.id == OUT || !c.in(x, y, z)) return false;
      if (s.id != AIR && !survives(s.id, c, x, y, z)) return false;
      return setState(c, x, y, z, s);
    }
    case F_SELECT:
      for (size_t k = 0; k < C.kids.size(); k++) if (r.nextFloat() < C.chances[k]) return placePlaced(C.kids[k], c, r, x, y, z);
      return placePlaced(C.a, c, r, x, y, z);
    case F_SIMPLESELECT: return C.kids.empty() ? false : placePlaced(C.kids[r.nextInt((int)C.kids.size())], c, r, x, y, z);
    case F_BOOL: return placePlaced(r.nextBool() ? C.a : C.b, c, r, x, y, z);
    case F_COLUMN: {
      if (c.builtOn(x, z)) return false;
      const int n = (int)C.layers.size(); int hs[8] = {0}, tot = 0;
      for (int i = 0; i < n && i < 8; i++) { hs[i] = sampleInt(C.layers[i].first, r); tot += hs[i]; }
      if (!tot) return false;
      const int d = C.flag ? 1 : -1;
      for (int m = 0, py = y; m < tot; m++, py += d) {
        if (!test(C.pred, c, x, py, z)) {   // (BlockColumnFeature.truncate: from the bottom, or from the tip)
          int o = tot - m;
          if (C.h > 0) { for (int q = 0; q < n && o > 0; q++) { const int k = std::min(hs[q], o); o -= k; hs[q] -= k; } }
          else { for (int q = n - 1; q >= 0 && o > 0; q--) { const int k = std::min(hs[q], o); o -= k; hs[q] -= k; } }
          break;
        }
      }
      int py = y;
      for (int i = 0; i < n && i < 8; i++) for (int p = 0; p < hs[i]; p++, py += d) {
        const State s = provide(C.layers[i].second, r, x, py, z, c);
        if (s.id != OUT) c.set(x, py, z, s.id, s.meta);
      }
      return true;
    }
    case F_VEGPATCH: {
      const int rx = sampleInt(C.ib, r) + 1, rz = sampleInt(C.ib, r) + 1;
      const int dir = C.flag ? 1 : -1;
      struct P3 { int x, y, z; }; std::vector<P3> ground;
      auto repl = [&](BlockId b) { return b != OUT && b < C.mask.size() && C.mask[b]; };
      for (int i = -rx; i <= rx; i++) for (int j = -rz; j <= rz; j++) {
        const bool ex = i == -rx || i == rx, ez = j == -rz || j == rz, corner = ex && ez, edge = (ex || ez) && !corner;
        if (corner || (edge && (C.h == 0 || r.nextFloat() > C.h))) continue;
        const int px = x + i, pz = z + j; int py = y;
        for (int k = 0; c.get(px, py, pz) == AIR && k < C.b; k++) py += dir;
        for (int k = 0; c.get(px, py, pz) != AIR && c.get(px, py, pz) != OUT && k < C.b; k++) py -= dir;
        const BlockId g = c.get(px, py + dir, pz);
        if (c.get(px, py, pz) == AIR && g != OUT && R.tSolid[g] && R.tOpaque[g]) {
          const int depth = sampleInt(C.ia, r) + (C.f > 0 && r.nextFloat() < C.f ? 1 : 0);
          int gy = py + dir; bool ok = true;
          for (int k = 0; k < depth; k++) {
            const State s = provide(C.prov, r, px, gy, pz, c);
            const BlockId cur = c.get(px, gy, pz);
            if (s.id != cur) { if (!repl(cur)) { ok = k != 0; break; } c.set(px, gy, pz, s.id, s.meta); gy += dir; }
          }
          if (ok) ground.push_back({px, py + dir, pz});
        }
      }
      if (C.c) {   // (waterlogged: the patch's inner ground becomes water)
        std::vector<P3> wet;
        for (const P3& p : ground) {
          bool exposed = false;
          for (int k = 0; k < 4 && !exposed; k++) { const BlockId w = c.get(p.x + DIRX[k], p.y, p.z + DIRZ[k]); if (w == OUT || !(R.tSolid[w] || w == water_)) exposed = true; }
          const BlockId u = c.get(p.x, p.y - 1, p.z); if (u == OUT || !R.tSolid[u]) exposed = true;
          if (!exposed) wet.push_back(p);
        }
        for (const P3& p : wet) c.set(p.x, p.y, p.z, water_, 0);
        ground = wet;
      }
      for (const P3& p : ground) if (C.g > 0 && r.nextFloat() < C.g) placePlaced(C.a, c, r, p.x, C.c ? p.y : p.y - dir, p.z);
      return !ground.empty();
    }
    case F_VINES: {
      if (c.get(x, y, z) != AIR) return false;
      for (int k = 0; k < 4; k++) { const BlockId w = c.get(x + DIRX[k], y, z + DIRZ[k]); if (w != OUT && R.tSolid[w] && R.tOpaque[w]) return c.set(x, y, z, vine_, (u8)k); }
      return false;
    }
    case F_SEAGRASS: {
      const int px = x + r.nextInt(8) - r.nextInt(8), pz = z + r.nextInt(8) - r.nextInt(8);
      const int py = c.height(HM_OCEAN, px, pz);
      r.nextDouble();
      const BlockId sg = R.blockId("seagrass");
      if (c.get(px, py, pz) == water_ && survives(sg, c, px, py, pz)) return c.set(px, py, pz, sg, 0x80);
      return false;
    }
    case F_KELP: {
      const int py0 = c.height(HM_OCEAN, x, z);
      const BlockId kelp = R.blockId("kelp");
      if (c.get(x, py0, z) != water_) return false;
      const int l = 1 + r.nextInt(10); int n = 0;
      for (int m = 0, py = py0; m <= l; m++, py++) {
        if (c.get(x, py, z) == water_ && c.get(x, py + 1, z) == water_) { c.set(x, py, z, kelp, 0x80); n++; } else break;
      }
      return n > 0;
    }
    case F_PICKLE: {
      const int cnt = sampleInt(C.ia, r); int n = 0;
      const BlockId pk = R.blockId("sea_pickle");
      for (int j = 0; j < cnt; j++) {
        const int px = x + r.nextInt(8) - r.nextInt(8), pz = z + r.nextInt(8) - r.nextInt(8);
        const int py = c.height(HM_OCEAN, px, pz); const u8 pickles = (u8)r.nextInt(4);
        if (c.get(px, py, pz) == water_ && survives(pk, c, px, py, pz)) { c.set(px, py, pz, pk, 0x80 | pickles); n++; }
      }
      return n > 0;
    }
    case F_BAMBOO: {
      if (c.get(x, y, z) != AIR || !survives(bamboo_, c, x, y, z) || c.builtOn(x, z)) return false;
      const int j = r.nextInt(12) + 5;
      if (r.nextFloat() < C.f) {
        const int k = r.nextInt(4) + 1;
        for (int l = x - k; l <= x + k; l++) for (int m = z - k; m <= z + k; m++) {
          if ((l - x) * (l - x) + (m - z) * (m - z) > k * k) continue;
          const int py = c.height(HM_WORLD, l, m) - 1; const BlockId g = c.get(l, py, m);
          if (g != OUT && g < dirt_.size() && dirt_[g]) c.set(l, py, m, podzol_, 0);
        }
      }
      for (int l = 0, py = y; l < j && c.get(x, py, z) == AIR; l++, py++) c.set(x, py, z, bamboo_, 0);
      return true;
    }
    case F_DISK: {
      const int rad = sampleInt(C.ia, r); bool any = false;
      for (int px = x - rad; px <= x + rad; px++) for (int pz = z - rad; pz <= z + rad; pz++) {
        if ((px - x) * (px - x) + (pz - z) * (pz - z) > rad * rad) continue;
        for (int py = y + C.a; py > y - C.a; py--) if (test(C.pred, c, px, py, pz)) { const State s = provide(C.prov, r, px, py, pz, c); if (s.id != OUT) { c.set(px, py, pz, s.id, s.meta); any = true; } }
      }
      return any;
    }
    case F_ROCK: {   // ForestRockFeature: down to the ground, three blobs
      int py = y;
      while (py > 3) { const BlockId u = c.get(x, py - 1, z); if (u == OUT) return false; if (u != AIR && (dirt_[u] || u == R.blockId("stone") || R.tOpaque[u])) break; py--; }
      if (py <= 3) return false;
      int px = x, pz = z;
      for (int k = 0; k < 3; k++) {
        const int i = r.nextInt(2), j = r.nextInt(2), l = r.nextInt(2);
        const float f = (i + j + l) * 0.333f + 0.5f;
        for (int ax = px - i; ax <= px + i; ax++) for (int ay = py - j; ay <= py + j; ay++) for (int az = pz - l; az <= pz + l; az++)
          if ((ax - px) * (ax - px) + (ay - py) * (ay - py) + (az - pz) * (az - pz) <= f * f) c.set(ax, ay, az, C.st.id, 0);
        px += -1 + r.nextInt(2); py -= r.nextInt(2); pz += -1 + r.nextInt(2);
      }
      return true;
    }
    case F_ICEBERG: {   // (IcebergFeature, its shape simplified: a tapering ice mountain at the sea, its foot below the water)
      const int sea = SEA_LEVEL;
      const bool tall = r.nextDouble() > 0.7;
      const int h = tall ? r.between(14, 30) : r.between(3, 8), base = r.between(3, 6) + (tall ? 3 : 0), depth = std::min(h, 10) + r.nextInt(4);
      const double ang = r.nextDouble() * 6.283, ex = 1.0 + r.nextDouble() * 0.6;
      for (int dy = -depth; dy <= h; dy++) {
        const double t = dy >= 0 ? 1.0 - (double)dy / (h + 1) : 1.0 - (double)(-dy) / (depth + 1) * 0.6;
        const double rr = base * (dy >= 0 ? std::pow(t, tall ? 1.6 : 1.0) : t);
        const int ri = (int)std::ceil(rr * ex) + 1;
        for (int ax = -ri; ax <= ri; ax++) for (int az = -ri; az <= ri; az++) {
          const double u = ax * std::cos(ang) + az * std::sin(ang), v = -ax * std::sin(ang) + az * std::cos(ang);
          const double d = (u / ex) * (u / ex) + v * v;
          if (d > rr * rr) continue;
          const int py = sea + dy; const BlockId cur = c.get(x + ax, py, z + az);
          if (cur == OUT || (cur != AIR && cur != water_ && cur != ice_)) continue;
          const bool top = dy >= 0 && (dy == h || d > (rr - 1) * (rr - 1)) && r.nextFloat() < 0.6f && dy > 0;
          c.set(x + ax, py, z + az, top && C.st.id == packed_ ? snowBlock_ : C.st.id, 0);
        }
      }
      return true;
    }
    case F_BLUEICE: {
      if (y > SEA_LEVEL - 1) return false;
      const BlockId at = c.get(x, y, z), up = c.get(x, y + 1, z);
      if (at != water_ && at != packed_) return false;
      bool near = up == packed_ || up == ice_;
      for (int k = 0; k < 4 && !near; k++) near = c.get(x + DIRX[k], y, z + DIRZ[k]) == packed_;
      if (!near) return false;
      c.set(x, y, z, blueIce_, 0);
      for (int k = 0; k < 200; k++) {
        const int ax = x + r.nextInt(5) - r.nextInt(5), ay = y - r.nextInt(3) * 0 - r.nextInt(3) + 1, az = z + r.nextInt(5) - r.nextInt(5);
        const BlockId cur = c.get(ax, ay, az);
        if (cur == water_ || cur == packed_ || cur == ice_) {
          bool touch = false;
          for (int d = 0; d < 4 && !touch; d++) touch = c.get(ax + DIRX[d], ay, az + DIRZ[d]) == blueIce_;
          if (touch || c.get(ax, ay + 1, az) == blueIce_ || c.get(ax, ay - 1, az) == blueIce_) c.set(ax, ay, az, blueIce_, 0);
        }
      }
      return true;
    }
    case F_FREEZE: {   // SnowAndFreezeFeature: the chunk's columns, ice on still water and snow on the ground where it snows
      if (!c.in(x, y, z) && !c.in(x, 1, z)) return false;
      for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
        const int wx = c.bx + lx, wz = c.bz + lz;
        const int top = c.height(HM_MOTION, wx, wz);
        if (top <= 1 || top >= DIM_H) continue;
        const int b = c.biomeAt(wx, wz);
        if (b < 0 || !bio_->coldEnoughToSnow(b, wx, top - 2, wz)) continue;
        const BlockId below = c.get(wx, top - 1, wz);
        if (below == water_) { bool edge = false; for (int k = 0; k < 4; k++) { const BlockId w = c.get(wx + DIRX[k], top - 1, wz + DIRZ[k]); if (w != water_ && w != ice_ && w != OUT) edge = true; } if (!edge || r.nextInt(4)) c.set(wx, top - 1, wz, ice_, 0); continue; }
        if (c.get(wx, top, wz) == AIR && below != OUT && R.tSolid[below] && R.tOpaque[below] && below != ice_ && below != packed_ && below != blueIce_) {
          c.set(wx, top, wz, snow_, 0);
          if (below == grass_ && snowyGrass_) c.set(wx, top - 1, wz, snowyGrass_, 0);
        } else if (c.get(wx, top, wz) == AIR && below != OUT && R.tLeaves[below]) c.set(wx, top, wz, snow_, 0);
      }
      return true;
    }
    case F_BIGBLOCK: {   // (the large pumpkins and stone lumps: a 2x2x2 block on the ground)
      int py = y; while (py > 2 && c.get(x, py - 1, z) == AIR) py--;
      const BlockId g = c.get(x, py - 1, z);
      if (g == OUT || !(g < dirt_.size() && (dirt_[g] || sand_[g])) || c.builtOn(x, z)) return false;
      for (int ax = 0; ax < 2; ax++) for (int az = 0; az < 2; az++) for (int ay = 0; ay < 2; ay++) {
        const State s = provide(C.prov, r, x + ax, py + ay, z + az, c);
        const BlockId cur = c.get(x + ax, py + ay, z + az);
        if (cur == AIR || (cur != OUT && R.tReplaceable[cur])) c.set(x + ax, py + ay, z + az, s.id, s.meta);
      }
      return true;
    }
    case F_POND: {   // (the ponds and deltas: a round of still water in the ground, a rim of its mud or stone)
      const int sz = std::max(2, sampleInt(C.ia, r) / 2), rim = sampleInt(C.ib, r);
      const int gy = c.height(HM_OCEAN, x, z) - 1;
      if (gy < SEA_LEVEL - 2 || gy > SEA_LEVEL + 40) return false;
      for (int ax = -sz - rim; ax <= sz + rim; ax++) for (int az = -sz - rim; az <= sz + rim; az++) {
        const double d = std::sqrt(ax * ax + az * az) + r.nextDouble() * 0.8;
        const int px = x + ax, pz = z + az; const int top = c.height(HM_OCEAN, px, pz) - 1;
        if (std::abs(top - gy) > 1) continue;
        const BlockId g = c.get(px, gy, pz);
        if (g == OUT || !R.tSolid[g] || R.tLiquid[g]) continue;
        if (d <= sz) {
          bool closed = true;
          for (int k = 0; k < 4; k++) { const BlockId w = c.get(px + DIRX[k], gy, pz + DIRZ[k]); if (w == AIR || (w != OUT && R.tReplaceable[w] && w != water_)) closed = false; }
          if (closed) { c.set(px, gy, pz, C.st.id, 0); for (int yy = gy + 1; yy < gy + 3; yy++) if (c.get(px, yy, pz) != OUT && c.get(px, yy, pz) != AIR && R.tReplaceable[c.get(px, yy, pz)]) c.set(px, yy, pz, AIR, 0); }
        } else if (d <= sz + rim && C.st2.id != OUT) c.set(px, gy, pz, C.st2.id, 0);
      }
      return true;
    }
  }
  return false;
}

void BiomeFeatures::decorate(GenBuf& b, int cx, int cz, const u16* biome, const std::function<bool(int, int)>& builtOn, bool underTrees) const {
  if (!loaded_) return;
  Ctx c(b, cx * 16, cz * 16, biome, builtOn);
  static const std::vector<u8> TREE = [] {
    std::vector<u8> v(reg().blocks.size() + 1, 0);
    for (size_t i = 1; i < reg().blocks.size(); i++) {
      const std::string& k = reg().blocks[i].key;
      if (reg().tLeaves[i] || k == "log" || k.find("_log") != std::string::npos || k.find("branch") != std::string::npos || k.find("_wood") != std::string::npos) v[i] = 1;
    }
    return v;
  }();
  if (underTrees) c.treeish = TREE.data();
  std::vector<u8> present(bio_->count(), 0);
  for (int i = 0; i < 256; i++) if (biome[i] < present.size()) present[biome[i]] = 1;
  std::vector<int> pres; for (int i = 0; i < (int)present.size(); i++) if (present[i]) pres.push_back(i);
  for (int s = 0; s < STEPS; s++) {
    for (int pf : order_[s]) {
      const Placed& P = placed_[pf];
      if (!P.ok) continue;
      bool here = false;
      for (int bi : pres) if ((size_t)pf < has_[s][bi].size() && has_[s][bi][pf]) { here = true; break; }
      if (!here) continue;
      c.step = s; c.top = pf;
      const int reach = confs_[P.conf].reach;
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        if ((dx || dz) && reach < 1) continue;
        const int ox = (cx + dx) * 16, oz = (cz + dz) * 16;
        // too far to reach this chunk from anywhere in its own
        if (ox + 15 + reach < c.bx || ox - reach > c.bx + 15 || oz + 15 + reach < c.bz || oz - reach > c.bz + 15) continue;
        Rng r(mix(seed_ ^ mix(((u64)(u32)(cx + dx) << 32) | (u32)(cz + dz)) ^ ((u64)pf * 0x9E3779B97F4A7C15ull) ^ ((u64)s << 56)));
        static const bool prof = std::getenv("DQ_FEATPROF");
        if (prof) {
          static std::mutex pm; static std::map<int, double> tm; static int calls = 0;
          const auto t0 = std::chrono::steady_clock::now();
          placePlaced(pf, c, r, ox, 0, oz);
          const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
          std::lock_guard<std::mutex> lk(pm); tm[pf] += us;
          if (++calls % 3000 == 0) {
            std::vector<std::pair<double, int>> v; for (auto& [k, t] : tm) v.push_back({t, k});
            std::sort(v.rbegin(), v.rend());
            double tot = 0; for (auto& e : v) tot += e.first; DQ_LOG("featprof total %.0f ms", tot / 1000);
            std::string nm; for (int i = 0; i < 12 && i < (int)v.size(); i++) { for (auto& [id, ix] : placedIds_) if (ix == v[i].second) { nm = id; break; } DQ_LOG("featprof %s %.0f ms", nm.c_str(), v[i].first / 1000); }
          }
        } else placePlaced(pf, c, r, ox, 0, oz);
      }
    }
  }
}

} // namespace dq
