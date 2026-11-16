// gen/density.cpp — see density.h. The data's algorithms (ImprovedNoise, PerlinNoise, NormalNoise, BlendedNoise,
// CubicSpline, the density function types the Overworld uses) written for this engine from how they behave.
#include "gen/density.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace dq {

namespace {
// our random source: splitmix64 (each noise and octave seeded by a hash of the world seed and its name)
struct Rng {
  u64 s;
  explicit Rng(u64 seed) : s(seed) {}
  u64 next() { u64 z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  double nextDouble() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
  int nextInt(int n) { return (int)(next() % (u64)n); }
};
u64 hashName(u64 seed, const std::string& s, int k = 0) {
  u64 h = 1469598103934665603ull ^ seed;
  for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ull; }
  h ^= (u64)(k + 1) * 0x9E3779B97F4A7C15ull; h *= 1099511628211ull;
  return h;
}
inline double wrap(double d) { return d - std::floor(d / 3.3554432E7 + 0.5) * 3.3554432E7; }
inline double smoothstep(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
inline double lerp(double t, double a, double b) { return a + t * (b - a); }
inline double clampedMap(double v, double a0, double a1, double b0, double b1) {
  const double t = std::clamp((v - a0) / (a1 - a0), 0.0, 1.0); return b0 + t * (b1 - b0);
}
const int GRAD[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                         {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
inline double gdot(int h, double x, double y, double z) { const int* g = GRAD[h & 15]; return g[0] * x + g[1] * y + g[2] * z; }

// ImprovedNoise: Perlin noise on a shuffled lattice, offset by a random amount
struct Improved {
  u8 p[256]; double xo, yo, zo;
  explicit Improved(Rng& r) {
    xo = r.nextDouble() * 256; yo = r.nextDouble() * 256; zo = r.nextDouble() * 256;
    for (int i = 0; i < 256; i++) p[i] = (u8)i;
    for (int i = 0; i < 256; i++) { const int j = r.nextInt(256 - i); std::swap(p[i], p[i + j]); }
  }
  int P(int i) const { return p[i & 255]; }
  double noise(double x, double y, double z, double yScale = 0, double yMax = 0) const {
    const double d = x + xo, e = y + yo, f = z + zo;
    const int i = (int)std::floor(d), j = (int)std::floor(e), k = (int)std::floor(f);
    const double g = d - i, h = e - j, l = f - k;
    double hh = 0;
    if (yScale != 0) hh = std::floor((yMax >= 0 && yMax < h ? yMax : h) / yScale + 1.0E-7) * yScale;
    const double hy = h - hh;
    const int n = P(i), o = P(i + 1), q = P(n + j), r = P(n + j + 1), s = P(o + j), t = P(o + j + 1);
    const double a1 = gdot(P(q + k), g, hy, l), a2 = gdot(P(s + k), g - 1, hy, l), a3 = gdot(P(r + k), g, hy - 1, l), a4 = gdot(P(t + k), g - 1, hy - 1, l);
    const double b1 = gdot(P(q + k + 1), g, hy, l - 1), b2 = gdot(P(s + k + 1), g - 1, hy, l - 1), b3 = gdot(P(r + k + 1), g, hy - 1, l - 1), b4 = gdot(P(t + k + 1), g - 1, hy - 1, l - 1);
    const double u = smoothstep(g), v = smoothstep(h), w = smoothstep(l);
    return lerp(w, lerp(v, lerp(u, a1, a2), lerp(u, a3, a4)), lerp(v, lerp(u, b1, b2), lerp(u, b3, b4)));
  }
};
} // namespace

struct DensityGraph::Perlin {
  std::vector<std::unique_ptr<Improved>> levels;   // lowest frequency first (null where the amplitude is 0)
  std::vector<double> amps;
  double inF = 1, valF = 1;
  Perlin(u64 seed, int firstOctave, const std::vector<double>& a) : amps(a) {
    const int n = (int)a.size();
    for (int i = 0; i < n; i++) {
      if (a[i] != 0) { Rng r(hashName(seed, "octave", firstOctave + i)); levels.push_back(std::make_unique<Improved>(r)); }
      else levels.push_back(nullptr);
    }
    inF = std::pow(2.0, firstOctave);
    valF = std::pow(2.0, n - 1) / (std::pow(2.0, n) - 1);
  }
  double value(double x, double y, double z) const {
    double v = 0, f = inF, vf = valF;
    for (size_t i = 0; i < levels.size(); i++, f *= 2, vf /= 2)
      if (levels[i]) v += amps[i] * vf * levels[i]->noise(wrap(x * f), wrap(y * f), wrap(z * f));
    return v;
  }
};

struct DensityGraph::Normal {
  std::unique_ptr<Perlin> a, b; double vf = 1;
  Normal(u64 seed, int firstOctave, const std::vector<double>& amps) {
    a = std::make_unique<Perlin>(seed, firstOctave, amps);
    b = std::make_unique<Perlin>(seed ^ 0x5DEECE66Dull * 31, firstOctave, amps);
    int lo = 1 << 30, hi = -(1 << 30);
    for (int i = 0; i < (int)amps.size(); i++) if (amps[i] != 0) { lo = std::min(lo, i); hi = std::max(hi, i); }
    vf = (1.0 / 6.0) / (0.1 * (1.0 + 1.0 / (hi - lo + 1)));
  }
  double value(double x, double y, double z) const {
    constexpr double F = 1.0181268882175227;
    return (a->value(x, y, z) + b->value(x * F, y * F, z * F)) * vf;
  }
};

// old_blended_noise: two limit noises lerped by a main noise, each octave's y quantised (the "smear")
struct DensityGraph::Blended {
  std::vector<std::unique_ptr<Improved>> minL, maxL, main;
  double xzMul, yMul, xzFac, yFac, smear;
  Blended(u64 seed, double xzScale, double yScale, double xzFactor, double yFactor, double smearMul) {
    for (int i = 0; i < 16; i++) { Rng r(hashName(seed, "blended_min", i)); minL.push_back(std::make_unique<Improved>(r)); }
    for (int i = 0; i < 16; i++) { Rng r(hashName(seed, "blended_max", i)); maxL.push_back(std::make_unique<Improved>(r)); }
    for (int i = 0; i < 8; i++) { Rng r(hashName(seed, "blended_main", i)); main.push_back(std::make_unique<Improved>(r)); }
    xzMul = 684.412 * xzScale; yMul = 684.412 * yScale; xzFac = xzFactor; yFac = yFactor; smear = yMul * smearMul;
  }
  double value(double x, double y, double z) const {
    const double d = x * xzMul, e = y * yMul, f = z * xzMul;
    const double g = d / xzFac, h = e / yFac, i = f / xzFac;
    const double k = smear / yFac;
    double n = 0, o = 1;
    for (int p = 0; p < 8; p++, o /= 2) n += main[p]->noise(wrap(g * o), wrap(h * o), wrap(i * o), k * o, h * o) / o;
    const double q = (n / 10.0 + 1.0) / 2.0;
    const bool hiOnly = q >= 1, loOnly = q <= 0;
    double l = 0, m = 0; o = 1;
    for (int r = 0; r < 16; r++, o /= 2) {
      const double s = wrap(d * o), t = wrap(e * o), u = wrap(f * o), v = smear * o;
      if (!hiOnly) l += minL[r]->noise(s, t, u, v, e * o) / o;
      if (!loOnly) m += maxL[r]->noise(s, t, u, v, e * o) / o;
    }
    const double a = l / 512.0, b = m / 512.0;
    return (q < 0 ? a : q > 1 ? b : lerp(q, a, b)) / 128.0;
  }
};

struct DensityGraph::Spline {
  int coord = -1;
  std::vector<double> loc, der;
  std::vector<double> val; std::vector<int> sub;   // a point's value: a constant, or (sub >= 0) a nested spline
};

namespace {
enum Op : u8 {
  O_CONST, O_ADD, O_MUL, O_MIN, O_MAX, O_ABS, O_SQUARE, O_CUBE, O_HALFNEG, O_QUARTNEG, O_SQUEEZE, O_CLAMP, O_YGRAD,
  O_NOISE, O_SHIFTED, O_SHIFT_A, O_SHIFT_B, O_SHIFT, O_RANGE, O_SPLINE, O_WEIRD, O_BLENDED, O_PASS, O_CACHE2D
};
std::atomic<u64> gSerial{1};
}

DensityGraph::DensityGraph() = default;
DensityGraph::~DensityGraph() = default;

static bool readJsonFile(const fs::path& p, json& out) {   // (through the data archive in a release)
  auto j = readJson(p); if (!j) return false;
  out = std::move(*j);
  return true;
}

bool DensityGraph::load(const fs::path& dir, u64 seed, const std::string& prefix) {
  dir_ = dir; seed_ = seed; prefix_ = prefix; serial_ = gSerial++;
  json ns;
  if (!readJsonFile(dir / "noise_settings" / "overworld.json", ns) || !ns.contains("noise_router")) return false;
  for (auto& [k, v] : ns["noise_router"].items()) router[k] = v;
  // the router's entries, parsed on demand by find()
  nodes.reserve(4096);
  // terrain version 14 only: more land than the base data gives, its continents raised a little (half our world was sea,
  // in islands; DQ_CONT_BIAS to try others). The land biomes' world (data/worldgen) is as its data makes it
  {
    const char* eb = std::getenv("DQ_CONT_BIAS");
    const double bias = eb ? std::atof(eb) : dir.filename() == "worldgen_base" ? 0.1 : 0.0;
    const int c = find("main:overworld/continents");
    if (c >= 0 && bias != 0) {
      Node k; k.op = O_CONST; k.c = bias; nodes.push_back(k);
      Node add; add.op = O_ADD; add.a = c; add.b = (int)nodes.size() - 1; nodes.push_back(add);
      named["main:overworld/continents"] = named["overworld/continents"] = (int)nodes.size() - 1;
    }
  }
  if (find("final_density") < 0) { nodes.clear(); return false; }
  return true;
}

int DensityGraph::find(const std::string& name) const {
  auto* self = const_cast<DensityGraph*>(this);
  if (auto it = named.find(name); it != named.end()) return it->second;
  int idx = -1;
  if (auto r = router.find(name); r != router.end()) idx = self->parse(r->second);
  else {
    // a named function: "overworld/x" (this world's own variant first, e.g. overworld_amplified/offset), or a plain one
    std::string rel = name;
    if (rel.rfind("main:", 0) == 0) rel = rel.substr(5);
    json j;
    bool got = false;
    if (rel.rfind("overworld/", 0) == 0 && prefix_ != "overworld") got = readJsonFile(dir_ / "density_function" / (prefix_ + rel.substr(9) + ".json"), j);
    if (!got) got = readJsonFile(dir_ / "density_function" / (rel + ".json"), j);
    if (!got) return -1;
    self->named[name] = -2;   // (being parsed: a cycle would read as zero)
    idx = self->parse(j);
  }
  self->named[name] = idx;
  return idx;
}

double DensityGraph::noiseValue(int n, double x, double y, double z) const { return n < 0 ? 0 : noises[n]->value(x, y, z); }

int DensityGraph::noiseRef(const std::string& id) {
  std::string rel = id.rfind("main:", 0) == 0 ? id.substr(5) : id;
  if (auto it = noiseIdx.find(rel); it != noiseIdx.end()) return it->second;
  json j; int first = -7; std::vector<double> amps{1.0};
  std::string file = rel; if (const size_t c = file.find(':'); c != std::string::npos) file[c] = '/';
  if (readJsonFile(dir_ / "noise" / (file + ".json"), j)) { first = j.value("firstOctave", -7); amps = j["amplitudes"].get<std::vector<double>>(); }
  noises.push_back(std::make_unique<Normal>(hashName(seed_, j.is_object() ? j.value("seedName", rel) : rel), first, amps));   // (an imported noise keeps the name its seed was made from)
  return noiseIdx[rel] = (int)noises.size() - 1;
}

int DensityGraph::parseSpline(const json& j) {
  auto sp = std::make_unique<Spline>();
  sp->coord = parse(j["coordinate"]);
  for (auto& pt : j["points"]) {
    sp->loc.push_back(pt.value("location", 0.0)); sp->der.push_back(pt.value("derivative", 0.0));
    const json& v = pt["value"];
    if (v.is_number()) { sp->val.push_back(v.get<double>()); sp->sub.push_back(-1); }
    else { sp->val.push_back(0); sp->sub.push_back(parseSpline(v)); }
  }
  splines.push_back(std::move(sp));
  return (int)splines.size() - 1;
}

int DensityGraph::parse(const json& j) {
  Node n;
  if (j.is_number()) { n.op = O_CONST; n.c = j.get<double>(); nodes.push_back(n); return (int)nodes.size() - 1; }
  if (j.is_string()) { const int r = find(j.get<std::string>()); if (r >= 0) return r; n.op = O_CONST; nodes.push_back(n); return (int)nodes.size() - 1; }
  std::string t = j.value("type", std::string());
  if (t.rfind("main:", 0) == 0) t = t.substr(5);
  auto arg = [&](const char* k) { return parse(j[k]); };
  if (t == "add" || t == "mul" || t == "min" || t == "max") {
    n.op = t == "add" ? O_ADD : t == "mul" ? O_MUL : t == "min" ? O_MIN : O_MAX; n.a = arg("argument1"); n.b = arg("argument2");
  } else if (t == "abs" || t == "square" || t == "cube" || t == "half_negative" || t == "quarter_negative" || t == "squeeze") {
    n.op = t == "abs" ? O_ABS : t == "square" ? O_SQUARE : t == "cube" ? O_CUBE : t == "half_negative" ? O_HALFNEG : t == "quarter_negative" ? O_QUARTNEG : O_SQUEEZE;
    n.a = arg("argument");
  } else if (t == "clamp") { n.op = O_CLAMP; n.a = arg("input"); n.c = j.value("min", -1e9); n.c2 = j.value("max", 1e9); }
  else if (t == "y_clamped_gradient") { n.op = O_YGRAD; n.c = j.value("from_y", 0); n.c2 = j.value("to_y", 1); n.c3 = j.value("from_value", 0.0); n.c4 = j.value("to_value", 1.0); }
  else if (t == "noise") { n.op = O_NOISE; n.noise = noiseRef(j.value("noise", std::string())); n.c = j.value("xz_scale", 1.0); n.c2 = j.value("y_scale", 1.0); }
  else if (t == "shifted_noise") {
    n.op = O_SHIFTED; n.noise = noiseRef(j.value("noise", std::string())); n.c = j.value("xz_scale", 1.0); n.c2 = j.value("y_scale", 1.0);
    n.a = arg("shift_x"); n.b = arg("shift_y"); n.d = arg("shift_z");
  } else if (t == "shift_a" || t == "shift_b" || t == "shift") {
    n.op = t == "shift_a" ? O_SHIFT_A : t == "shift_b" ? O_SHIFT_B : O_SHIFT; n.noise = noiseRef(j.value("argument", std::string()));
  } else if (t == "range_choice") {
    n.op = O_RANGE; n.a = arg("input"); n.c = j.value("min_inclusive", 0.0); n.c2 = j.value("max_exclusive", 0.0); n.b = arg("when_in_range"); n.d = arg("when_out_of_range");
  } else if (t == "spline") { n.op = O_SPLINE; n.spline = parseSpline(j["spline"]); }
  else if (t == "weird_scaled_sampler") {
    n.op = O_WEIRD; n.a = arg("input"); n.noise = noiseRef(j.value("noise", std::string()));
    n.c = j.value("rarity_value_mapper", std::string()) == "type_2" ? 2 : 1;
  } else if (t == "old_blended_noise") {
    n.op = O_BLENDED;
    blended.push_back(std::make_unique<Blended>(hashName(seed_, "blended"), j.value("xz_scale", 0.25), j.value("y_scale", 0.125), j.value("xz_factor", 80.0),
                                                j.value("y_factor", 160.0), j.value("smear_scale_multiplier", 8.0)));
    n.blended = (int)blended.size() - 1;
  } else if (t == "interpolated" || t == "blend_density" || t == "cache_once" || t == "cache_all_in_cell") { n.op = O_PASS; n.a = arg("argument"); }
  else if (t == "flat_cache" || t == "cache_2d") { n.op = O_CACHE2D; n.a = arg("argument"); }
  else if (t == "blend_alpha") { n.op = O_CONST; n.c = 1; }
  else { n.op = O_CONST; n.c = 0; }   // blend_offset, beardifier, end_islands, constants by another name: none here
  nodes.push_back(n);
  return (int)nodes.size() - 1;
}

double DensityGraph::evalSpline(int s, double x, double y, double z) const {
  const Spline& S = *splines[s];
  const double f = eval(S.coord, x, y, z);
  const int n = (int)S.loc.size() - 1;
  auto value = [&](int i) { return S.sub[i] >= 0 ? evalSpline(S.sub[i], x, y, z) : S.val[i]; };
  int i = -1;
  for (int k = 0; k <= n; k++) { if (S.loc[k] <= f) i = k; else break; }   // (the last point at or below f)
  if (i < 0) return value(0) + S.der[0] * (f - S.loc[0]);
  if (i == n) return value(n) + S.der[n] * (f - S.loc[n]);
  const double l0 = S.loc[i], l1 = S.loc[i + 1], k = (f - l0) / (l1 - l0);
  const double v0 = value(i), v1 = value(i + 1);
  const double p = S.der[i] * (l1 - l0) - (v1 - v0), q = -S.der[i + 1] * (l1 - l0) + (v1 - v0);
  return lerp(k, v0, v1) + k * (1 - k) * lerp(k, p, q);
}

double DensityGraph::eval(int ni, double x, double y, double z) const {
  if (ni < 0) return 0;
  const Node& n = nodes[ni];
  switch (n.op) {
    case O_CONST: return n.c;
    case O_ADD: return eval(n.a, x, y, z) + eval(n.b, x, y, z);
    case O_MUL: { const double a = eval(n.a, x, y, z); return a == 0 ? 0 : a * eval(n.b, x, y, z); }
    case O_MIN: return std::min(eval(n.a, x, y, z), eval(n.b, x, y, z));
    case O_MAX: return std::max(eval(n.a, x, y, z), eval(n.b, x, y, z));
    case O_ABS: return std::fabs(eval(n.a, x, y, z));
    case O_SQUARE: { const double a = eval(n.a, x, y, z); return a * a; }
    case O_CUBE: { const double a = eval(n.a, x, y, z); return a * a * a; }
    case O_HALFNEG: { const double a = eval(n.a, x, y, z); return a > 0 ? a : a * 0.5; }
    case O_QUARTNEG: { const double a = eval(n.a, x, y, z); return a > 0 ? a : a * 0.25; }
    case O_SQUEEZE: { const double a = std::clamp(eval(n.a, x, y, z), -1.0, 1.0); return a / 2.0 - a * a * a / 24.0; }
    case O_CLAMP: return std::clamp(eval(n.a, x, y, z), n.c, n.c2);
    case O_YGRAD: return clampedMap(y, n.c, n.c2, n.c3, n.c4);
    case O_NOISE: return noises[n.noise]->value(x * n.c, y * n.c2, z * n.c);
    case O_SHIFTED: return noises[n.noise]->value(x * n.c + eval(n.a, x, y, z), y * n.c2 + eval(n.b, x, y, z), z * n.c + eval(n.d, x, y, z));
    case O_SHIFT_A: return noises[n.noise]->value(x * 0.25, 0, z * 0.25) * 4.0;
    case O_SHIFT_B: return noises[n.noise]->value(z * 0.25, x * 0.25, 0) * 4.0;
    case O_SHIFT: return noises[n.noise]->value(x * 0.25, y * 0.25, z * 0.25) * 4.0;
    case O_RANGE: { const double v = eval(n.a, x, y, z); return v >= n.c && v < n.c2 ? eval(n.b, x, y, z) : eval(n.d, x, y, z); }
    case O_SPLINE: return evalSpline(n.spline, x, y, z);
    case O_WEIRD: {
      const double v = eval(n.a, x, y, z);
      const double e = n.c == 2 ? (v < -0.75 ? 0.5 : v < -0.5 ? 0.75 : v < 0.5 ? 1.0 : v < 0.75 ? 2.0 : 3.0)
                                : (v < -0.5 ? 0.75 : v < 0.0 ? 1.0 : v < 0.5 ? 1.5 : 2.0);
      return e * std::fabs(noises[n.noise]->value(x / e, y / e, z / e));
    }
    case O_BLENDED: return blended[n.blended]->value(x, y, z);
    case O_PASS: return eval(n.a, x, y, z);
    case O_CACHE2D: {
      // a function of x and z only: kept per thread for the last column it was asked about (each node its own)
      struct C { double x, z, v; };
      thread_local u64 tagSerial = 0; thread_local std::vector<C> cache;
      if (tagSerial != serial_) { tagSerial = serial_; cache.assign(nodes.size(), C{1e300, 1e300, 0}); }
      if ((size_t)ni >= cache.size()) cache.resize(nodes.size(), C{1e300, 1e300, 0});
      C& c = cache[ni];
      if (c.x == x && c.z == z) return c.v;
      const double v = eval(n.a, x, 0, z);
      c = {x, z, v};
      return v;
    }
  }
  return 0;
}

} // namespace dq
