#include "world/region.h"
// tools/port/gentool.cpp — dq-gentool: terrain generator checks (not part of the game).
//   dq-gentool dump <request.json> <out.json>   per-column dumps for tools/port/gen_compare.py
//   dq-gentool bench <seed> [chunks]            time generate() per band and sample() per column
//   dq-gentool map <seed> <dim> <x0> <z0> <step> <n>   ASCII map of heights/biomes (quick look)
//   dq-gentool structs <request.json> <out.json>        structure plans and blocks for tools/port/struct_compare.py
//   dq-gentool stats <seed> <dim> <cx0> <cz0> <n>       block counts per 8-row band of y over n x n chunks (ores, caves...)
//   dq-gentool slice <seed> <dim> <x0> <z> <w> [y0 y1]  ASCII cutaway along x at z (one char per block)
#include "gen/bta/bta_nether.h"
#include "gen/nether_bta.h"
#include "core/init.h"
#include "gen/overworld.h"
#include "gen/dimgens.h"
#include "gen/structures_api.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <unordered_map>
#include <functional>
#include <algorithm>

using namespace dq;
#include <ctime>
// thread CPU time: the build machine is shared, wall time is meaningless there
#ifdef _WIN32
#include <chrono>
struct Clock { static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); } };
#else
struct Clock { static double now() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; } };
#endif

static std::string key(BlockId id) { return reg().block(id).key; }

static int dump(const char* reqPath, const char* outPath) {
  json req = json::parse(std::ifstream(reqPath));
  WorldGenOptions o; o.seed = seedFromString(req["seed"].get<std::string>());
  OverworldGen ow(o); SkylandsGen sky(o); EndGen end(o); DepthsGen dep(o);
  json out; out["seed"] = o.seed;
  for (auto& p : req["ow"]) {
    int cx = p[0], cz = p[1];
    Chunk ch(DIM_OVERWORLD, cx, cz); ow.generate(ch);
    json cols = json::array(), hist = json::object(), trees = json::array();
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      OwColumn c = ow.column(cx * 16 + x, cz * 16 + z); int i = z * 16 + x;
      cols.push_back({c.h, c.biome, key(ch.get(x, c.H, z)), key(ch.get(x, c.H + 1, z)), key(ch.get(x, c.H - 1, z)), ch.climate[i * 2], ch.climate[i * 2 + 1], c.H});
    }
    std::map<std::string, int> h;
    for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) h[key(ch.get(x, y, z))]++;
    for (auto& [k, n] : h) hist[k] = n;
    std::vector<TreeSpec> ts; ow.treesForChunk(cx, cz, ts);
    for (auto& t : ts) trees.push_back({t.x, t.ySeed, t.z, treeKindName(t.kind), t.h, t.bare, t.bees});
    out["ow"].push_back({{"cx", cx}, {"cz", cz}, {"cols", cols}, {"hist", hist}, {"trees", trees}});
  }
  if (req.contains("grid")) {
    int x0 = req["grid"][0], z0 = req["grid"][1], st = req["grid"][2], n = req["grid"][3];
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) { OwColumn c = ow.column(x0 + i * st, z0 + j * st); out["grid"].push_back({c.h, c.biome}); }
  }
  for (auto& p : req["sky"]) {
    int cx = p[0], cz = p[1];
    Chunk ch(DIM_AETHER, cx, cz); sky.generate(ch);
    BlockId sg = reg().blockId("sky_grass"), ss = reg().blockId("skystone"), dt = reg().blockId("dirt"), co = reg().blockId("aether_crystal_ore");
    json cols = json::array();
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int top = -1, bot = -1;
      for (int y = DIM_H - 1; y > 0; y--) { BlockId id = ch.get(x, y, z); if (id == sg && top < 0) top = y; if (id == ss || id == dt || id == sg || id == co) bot = y; }
      cols.push_back({top, bot});
    }
    out["sky"].push_back({{"cx", cx}, {"cz", cz}, {"cols", cols}});
  }
  for (auto& p : req["end"]) {
    int cx = p[0], cz = p[1];
    json cols = json::array();
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) { EndGen::Isle c; if (end.island(cx * 16 + x, cz * 16 + z, c)) cols.push_back({c.top, c.bot}); else cols.push_back(nullptr); }
    out["end"].push_back({{"cx", cx}, {"cz", cz}, {"cols", cols}});
  }
  for (auto& pl : end.pillars()) out["pillars"].push_back({pl.x, pl.z, pl.h, pl.rad, pl.cage});
  for (auto& p : req["depths"]) {
    int x = p[0], z = p[1];
    float D[DepthsGen::LAYERS]; dep.densityColumn(x, z, D);
    json s = json::array();
    for (int y = 0; y < 100; y++) {
      double bias = (y < 34 ? (34 - y) / 18.0 : 0) + (y > 80 ? (y - 80) / 12.0 : 0);
      s.push_back(y <= 0 || y >= 99 || D[y] + bias > 0.18 ? 1 : 0);
    }
    out["depths"].push_back({{"x", x}, {"z", z}, {"s", s}});
  }
  std::ofstream(outPath) << out.dump();
  return 0;
}

static int bench(const std::string& seedText, int n) {
  WorldGenOptions o; o.seed = seedFromString(seedText);
  if (const char* v = std::getenv("DQ_GENVER")) o.genVersion = atoi(v);
  if (std::getenv("DQ_NO_STRUCTS")) setStructuresEnabled(false);
  WorldGen wg(o);
  for (int d = 0; d < DIM_COUNT; d++) {
    DimGenerator* g = wg.forDim(d);
    // warm up
    { Chunk c(d, 1000, 1000); g->generate(c); }
    auto t0 = Clock::now();
    int side = std::max(1, (int)std::sqrt((double)n)), cnt = 0;
    for (int cz = 0; cz < side; cz++) for (int cx = 0; cx < side; cx++) {
      int ccx = d == DIM_END ? 20 + cx : cx, ccz = d == DIM_END ? cz : cz;   // the End: out on the islands
      if (const char* at = std::getenv("DQ_BENCH_AT")) { int ax = 0, az = 0; sscanf(at, "%d,%d", &ax, &az); ccx += ax >> 4; ccz += az >> 4; }
      Chunk c(d, ccx, ccz); g->generate(c); cnt++;
    }
    double ms = (Clock::now() - t0) / cnt;
    auto t1 = Clock::now(); ColumnSample s; long acc = 0;
    const int cols = 200000;
    for (int i = 0; i < cols; i++) { g->sample((i % 1000) * 7, (i / 1000) * 7, s); acc += s.height; }
    double us = (Clock::now() - t1) * 1000 / cols;
    printf("%-13s generate %.2f ms/chunk (%d chunks)   sample %.2f us/column (%ld)\n", dimName(d), ms, cnt, us, acc % 7);
  }
  return 0;
}

static int map(const std::string& seedText, int dim, int x0, int z0, int step, int n) {
  WorldGenOptions o; o.seed = seedFromString(seedText);
  WorldGen wg(o); DimGenerator* g = wg.forDim(dim);
  for (int j = 0; j < n; j++) {
    std::string row;
    for (int i = 0; i < n; i++) {
      ColumnSample s; g->sample(x0 + i * step, z0 + j * step, s);
      if (dim == DIM_OVERWORLD) row += "OBPFDTM"[s.biome % 7];
      else row += s.height < 0 ? '.' : (char)('0' + std::min(9, s.height / 26));
    }
    printf("%s\n", row.c_str());
  }
  return 0;
}

// Structure plans in a box per band, and for some of them the blocks the structure put down: every position
// whose block differs from the same chunk generated without structures, where the new block is not one the
// structure-less terrain has anywhere in those chunks; y relative to the plan's base (S.y).
static std::unique_ptr<DimGenerator> makeGen(int dim, const WorldGenOptions& o) {
  switch (dim) {
    case DIM_OVERWORLD: return std::make_unique<OverworldGen>(o);
    case DIM_NETHER: return std::make_unique<DepthsGen>(o);
    case DIM_AETHER: return std::make_unique<SkylandsGen>(o);
    default: return std::make_unique<EndGen>(o);
  }
}
static int structs(const char* reqPath, const char* outPath) {
  json req = json::parse(std::ifstream(reqPath));
  WorldGenOptions o; o.seed = req["seed"].is_number() ? req["seed"].get<i32>() : seedFromString(req["seed"].get<std::string>());
  const int R = req["R"];
  std::unique_ptr<DimGenerator> on[DIM_COUNT], off[DIM_COUNT];
  for (int d = 0; d < DIM_COUNT; d++) on[d] = makeGen(d, o);
  setStructuresEnabled(false);
  for (int d = 0; d < DIM_COUNT; d++) off[d] = makeGen(d, o);
  setStructuresEnabled(true);
  json out; out["seed"] = o.seed;
  std::map<std::string, const StructurePlan*> byId;
  for (int d = 0; d < DIM_COUNT; d++) {
    const StructureIndex* idx = structureIndexOf(on[d].get());
    json L = json::array();
    for (int k : idx->typeList()) {
      const StructureType& T = structureTypes()[k];
      auto add = [&](const StructurePlan* S) {
        if (!S) return;
        L.push_back({T.key, S->id, S->x0, S->z0, S->x1, S->z1, S->y + S->hashDy, S->name, S->y});
        byId[std::to_string(d) + ":" + S->id] = S;
      };
      if (T.global) { std::vector<const StructurePlan*> v; idx->near(0, 0, 1 << 20, v, k); for (auto* S : v) add(S); continue; }
      const int n = (R + T.region - 1) / T.region;
      const int ox = req.value("cx", 0), oz = req.value("cz", 0);   // (round a point, not the origin)
      for (int rz = -n; rz < n; rz++) for (int rx = -n; rx < n; rx++) add(idx->at(k, rx + floordiv(ox, T.region), rz + floordiv(oz, T.region)));
    }
    out["list"][std::to_string(d)] = L;
  }
  for (auto& pk : req["picks"]) {
    const int d = pk["dim"]; const std::string id = pk["id"];
    auto it = byId.find(std::to_string(d) + ":" + id);
    json res{{"dim", d}, {"id", id}};
    if (it == byId.end()) { res["missing"] = true; out["picks"].push_back(res); continue; }
    const StructurePlan* S = it->second;
    const int cx0 = pk["c"][0], cz0 = pk["c"][1], cx1 = pk["c"][2], cz1 = pk["c"][3];
    std::vector<std::unique_ptr<Chunk>> A, Bc;
    std::set<std::string> natural;
    for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
      A.push_back(std::make_unique<Chunk>(d, cx, cz)); on[d]->generate(*A.back());
      Bc.push_back(std::make_unique<Chunk>(d, cx, cz)); off[d]->generate(*Bc.back());
      for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) natural.insert(key(Bc.back()->get(x, y, z)));
    }
    json blocks = json::array(); int ents = 0;
    for (size_t i = 0; i < A.size(); i++) {
      Chunk &a = *A[i], &b = *Bc[i];
      for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        BlockId ia = a.get(x, y, z);
        if (ia == b.get(x, y, z) || ia == AIR) continue;
        const std::string& k = key(ia);
        if (natural.count(k)) continue;
        blocks.push_back({a.cx * 16 + x, y - S->y, a.cz * 16 + z, k});
      }
      ents += (int)a.blockEntities.size();
    }
    res["base"] = S->y + S->hashDy; res["blocks"] = blocks; res["entities"] = ents; res["data"] = S->data;
    out["picks"].push_back(res);
  }
  std::ofstream(outPath) << out.dump();
  return 0;
}

// where a new world's player starts (client/session.cpp: first dry column along (r, r/2)), and its biome
static int spawns(const std::string& prefix, int n) {
  for (int k = 0; k < n; k++) {
    std::string st = prefix + std::to_string(k);
    WorldGenOptions o; o.seed = seedFromString(st);
    OverworldGen g(o);
    int sx = 0, sz = 0;
    for (int r = 0; r < 400; r += 16) { ColumnSample s; g.sample(r, r / 2, s); if (s.height > 0 && s.water < 0) { sx = r; sz = r / 2; break; } }
    OwColumn c = g.column(sx, sz);
    int maxH = 0; for (int dz = -96; dz <= 96; dz += 8) for (int dx = -96; dx <= 96; dx += 8) maxH = std::max(maxH, g.column(sx + dx, sz + dz).H);
    printf("%s %d %d %s H=%d maxH=%d temp=%.2f\n", st.c_str(), sx, sz, biomeName(c.biome), c.H, maxH, c.temp);
  }
  return 0;
}

static int stats(const std::string& seedText, int dim, int cx0, int cz0, int n) {
  WorldGenOptions o; o.seed = seedFromString(seedText);
  WorldGen wg(o); DimGenerator* g = wg.forDim(dim);
  std::map<std::string, std::vector<long>> per;   // key -> count per 8-row band
  std::map<std::string, long> be;
  double ms = 0;
  for (int cz = cz0; cz < cz0 + n; cz++) for (int cx = cx0; cx < cx0 + n; cx++) {
    Chunk ch(dim, cx, cz);
    auto t0 = Clock::now(); g->generate(ch); ms += Clock::now() - t0;
    for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      auto& v = per[key(ch.get(x, y, z))]; if (v.empty()) v.assign(DIM_H / 8, 0); v[y / 8]++;
    }
    for (auto& [i, e] : ch.blockEntities) { std::string t = e.value("type", "?"); if (e.contains("mob")) t += ":" + e["mob"].get<std::string>(); be[t]++; }
  }
  printf("%d chunks, generate %.2f ms/chunk (thread cpu)\n", n * n, ms / (n * n));
  for (auto& [k, v] : per) {
    long tot = 0; for (long c : v) tot += c;
    printf("%-22s %9ld |", k.c_str(), tot);
    for (int b = 0; b < DIM_H / 8; b++) printf(v[b] ? " %ld" : " .", v[b]);
    printf("\n");
  }
  for (auto& [k, c] : be) printf("entity %-20s %ld\n", k.c_str(), c);
  return 0;
}

static int slice(const std::string& seedText, int dim, int x0, int z, int w, int y0, int y1) {
  WorldGenOptions o; o.seed = seedFromString(seedText);
  WorldGen wg(o); DimGenerator* g = wg.forDim(dim);
  std::map<int, std::unique_ptr<Chunk>> cache;
  auto at = [&](int x) -> Chunk& { int cx = floordiv(x, 16); auto& c = cache[cx]; if (!c) { c = std::make_unique<Chunk>(dim, cx, floordiv(z, 16)); g->generate(*c); } return *c; };
  std::map<std::string, char> sym = {{"air", ' '}, {"stone", '#'}, {"slate", '%'}, {"bedrock", '='}, {"water", '~'}, {"lava", 'L'}, {"dirt", 'd'},
    {"grass", '"'}, {"gravel", 'g'}, {"coal_ore", 'c'}, {"iron_ore", 'i'}, {"gold_ore", 'G'}, {"diamond_ore", 'D'}, {"redstone_ore", 'r'},
    {"lapis_ore", 'l'}, {"copper_ore", 'o'}, {"netherrack", '#'}, {"glowstone", '*'}, {"soul_sand", 's'}, {"nether_bricks", 'N'},
    {"cobblestone", 'C'}, {"mossy_cobblestone", 'M'}, {"monster_spawner", '!'}, {"chest", '$'}, {"granite", 'a'}, {"diorite", 'a'}, {"andesite", 'a'},
    {"ember_crust", '_'}, {"basalt", 'B'}, {"blackstone", 'K'}, {"weeping_vines", 'v'}, {"twisting_vines", 'v'}};
  for (int y = y1; y >= y0; y--) {
    printf("%3d ", y);
    for (int x = x0; x < x0 + w; x++) {
      Chunk& c = at(x); std::string k = key(c.get(floormod(x, 16), y, floormod(z, 16)));
      auto it = sym.find(k); putchar(it != sym.end() ? it->second : '?');
    }
    putchar('\n');
  }
  return 0;
}


// dq-gentool poi <seed> <out.bin> [regions]: with DQ_POI_ONLY=<key> set, the nearest such Soulrend place generated with the land
// round it (8 blocks), dumped for tools/port/poi_check.py: a JSON header line {x0,y0,z0,sx,sy,sz,palette,name,y,...} then sx*sy*sz u16
// block indices (x fastest, then z, then y)
namespace dq { void poiExpected(const StructurePlan& S, const std::function<void(int, int, int, BlockId)>& fn); }
static int poiDump(const std::string& seedText, const char* outPath, int regions) {
  WorldGenOptions o; o.seed = seedText.size() > 1 && seedText[0] == '#' ? (i32)std::stoll(seedText.substr(1)) : seedFromString(seedText);   // (#<n>: a world's own seed)
  WorldGen wg(o); DimGenerator* g = wg.forDim(DIM_OVERWORLD);
  int ax = 0, az = 0; if (const char* at = std::getenv("DQ_POI_AT")) sscanf(at, "%d,%d", &ax, &az);   // (the nearest to there)
  const StructurePlan* S = locateStructure(wg, DIM_OVERWORLD, "poi", ax, az, regions);
  if (!S) { fprintf(stderr, "none\n"); return 2; }
  const int M = 8, X0 = S->x0 - M, Z0 = S->z0 - M, X1 = S->x1 + M, Z1 = S->z1 + M;
  std::map<long long, std::unique_ptr<Chunk>> cs;
  for (int cz = floordiv(Z0, 16); cz <= floordiv(Z1, 16); cz++) for (int cx = floordiv(X0, 16); cx <= floordiv(X1, 16); cx++) {
    auto c = std::make_unique<Chunk>(DIM_OVERWORLD, cx, cz); g->generate(*c); cs[((long long)cx << 32) ^ (u32)cz] = std::move(c);
  }
  // the same chunks with no structures: a cell the structure didn't change is the land's own
  setStructuresEnabled(false);
  std::map<long long, std::unique_ptr<Chunk>> ns;
  { WorldGen wg0(o); DimGenerator* g0 = wg0.forDim(DIM_OVERWORLD);
    for (auto& [k, c] : cs) { auto n = std::make_unique<Chunk>(DIM_OVERWORLD, c->cx, c->cz); g0->generate(*n); ns[k] = std::move(n); } }
  setStructuresEnabled(true);
  auto nat = [&](int x, int y, int z) -> BlockId { auto it = ns.find(((long long)floordiv(x, 16) << 32) ^ (u32)floordiv(z, 16)); return it == ns.end() ? AIR : it->second->get(floormod(x, 16), y, floormod(z, 16)); };
  auto at = [&](int x, int y, int z) -> BlockId { auto it = cs.find(((long long)floordiv(x, 16) << 32) ^ (u32)floordiv(z, 16)); return it == cs.end() ? AIR : it->second->get(floormod(x, 16), y, floormod(z, 16)); };
  int top = 0;
  for (int x = X0; x <= X1; x++) for (int z = Z0; z <= Z1; z++) for (int y = DIM_H - 1; y > top; y--) if (at(x, y, z) != AIR) { top = y; break; }
  const int Y0 = std::max(1, S->y - 16), Y1 = std::min(DIM_H - 1, top + 2);
  const int sx = X1 - X0 + 1, sy = Y1 - Y0 + 1, sz = Z1 - Z0 + 1;
  std::map<BlockId, int> pal; json pl = json::array();
  std::vector<u16> v((size_t)sx * sy * sz); std::vector<u8> landm((size_t)sx * sy * sz);
  for (int y = 0; y < sy; y++) for (int z = 0; z < sz; z++) for (int x = 0; x < sx; x++) {
    const BlockId b = at(X0 + x, Y0 + y, Z0 + z);
    auto it = pal.find(b); if (it == pal.end()) { it = pal.emplace(b, (int)pl.size()).first; pl.push_back(key(b)); }
    v[((size_t)y * sz + z) * sx + x] = (u16)it->second;
    if (std::getenv("DQ_POI_RS") && reg().block(b).has("rs")) { auto c = cs.find(((long long)floordiv(X0 + x, 16) << 32) ^ (u32)floordiv(Z0 + z, 16));
      if (c != cs.end()) printf("rs %s meta %d at %d %d %d\n", key(b).c_str(), c->second->getMeta(floormod(X0 + x, 16), Y0 + y, floormod(Z0 + z, 16)), X0 + x, Y0 + y, Z0 + z); }
    landm[((size_t)y * sz + z) * sx + x] = b != AIR && b == nat(X0 + x, Y0 + y, Z0 + z);
  }
  // what its pieces put down that isn't there now (overwritten by a later piece, a tree, the land...): 2 in the mask
  int lost = 0, expected = 0; std::map<std::string, int> lostAs;
  // the last write to each cell as the pieces are built in order (one piece's air over another's blocks is the builder's own)
  std::unordered_map<long long, BlockId> want;
  poiExpected(*S, [&](int x, int y, int z, BlockId id) {
    if (x < X0 || x > X1 || z < Z0 || z > Z1 || y < Y0 || y > Y1) return;
    want[((long long)(y - Y0) * sz + (z - Z0)) * sx + (x - X0)] = id;
  });
  for (auto& [i, id] : want) {
    if (id == AIR) continue;
    expected++;
    const int x = (int)(i % sx), z = (int)(i / sx % sz), y = (int)(i / sx / sz);
    const BlockId got = at(X0 + x, Y0 + y, Z0 + z);
    if (got == id) continue;
    lost++; lostAs[key(id) + "->" + key(got)]++;
    landm[i] = 2;
  }
  std::vector<std::pair<int, std::string>> la; for (auto& [k, n] : lostAs) la.push_back({n, k}); std::sort(la.rbegin(), la.rend());
  json lj = json::array(); for (size_t i = 0; i < la.size() && i < 12; i++) lj.push_back({la[i].second, la[i].first});
  json h{{"expected", expected}, {"lost", lost}, {"lostAs", lj}, {"x0", X0}, {"y0", Y0}, {"z0", Z0}, {"sx", sx}, {"sy", sy}, {"sz", sz}, {"palette", pl}, {"name", S->name}, {"y", S->y},
         {"box", {S->x0, S->z0, S->x1, S->z1}}, {"data", S->data}};
  std::ofstream f(outPath, std::ios::binary); const std::string hs = h.dump() + "\n";
  f.write(hs.data(), hs.size()); f.write((const char*)v.data(), v.size() * 2); f.write((const char*)landm.data(), landm.size());
  return 0;
}


// dq-gentool vterrain <seed> <x0> <z0> <step> <n> <out.pgm>: terrain version 14's land (gen/density.h) seen from above:
// the surface of sloped_cheese per column, a greyscale height map (the sea at mid-grey's dark blue: 64 and below dark)
#include "gen/density.h"
static int vterrain(const std::string& seedText, int x0, int z0, int step, int n, const char* out) {
  DensityGraph g;
  if (!g.load(dataDir() / "vanilla_worldgen", (u64)(i64)seedFromString(seedText))) { fprintf(stderr, "no data/vanilla_worldgen\n"); return 1; }
  const int sc = g.find("overworld/sloped_cheese");
  const double t0 = Clock::now();
  std::vector<u8> img((size_t)n * n);
  int evals = 0, lo = 999, hi = -999;
  for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
    const double x = x0 + i * step, z = z0 + j * step;
    int top = -64;
    for (int y = 300; y >= -60; y -= 4) { evals++; if (g.eval(sc, x, y, z) > 0) { for (int k = 3; k >= 0; k--) { evals++; if (g.eval(sc, x, y + k, z) > 0) { top = y + k; break; } } break; } }
    lo = std::min(lo, top); hi = std::max(hi, top);
    img[(size_t)j * n + i] = (u8)std::clamp(top + 1, 0, 255);   // (our band y)
  }
  const double ms = Clock::now() - t0;
  printf("%d columns, %d evals, %.1f ms (%.2f us/eval), heights %d..%d (Minecraft y)\n", n * n, evals, ms, ms * 1000 / evals, lo, hi);
  FILE* f = fopen(out, "wb"); fprintf(f, "P5 %d %d 255\n", n, n); fwrite(img.data(), 1, img.size(), f); fclose(f);
  return 0;
}

static int vstats(const std::string& seedText) {
  DensityGraph g;
  if (!g.load(dataDir() / "vanilla_worldgen", (u64)(i64)seedFromString(seedText))) return 1;
  { const int C = g.find("overworld/continents"), E = g.find("overworld/erosion"), R = g.find("overworld/ridges_folded"), O = g.find("overworld/offset");
    for (int i = 0; i < 8; i++) { const double x = i * 517.0 - 2000, z = i * 311.0 + 100;
      printf("pt %.5f %.5f %.5f %.5f\n", g.eval(C, x, 0, z), g.eval(E, x, 0, z), g.eval(R, x, 0, z), g.eval(O, x, 0, z)); } }
  for (const char* nm : {"continents", "erosion", "ridges", "temperature", "vegetation", "overworld/offset", "overworld/factor"}) {
    const int id = g.find(nm); std::vector<double> v;
    for (int j = 0; j < 200; j++) for (int i = 0; i < 200; i++) v.push_back(g.eval(id, i * 41.0 - 4000, 64, j * 41.0 - 4000));
    std::sort(v.begin(), v.end());
    printf("%-18s p1 %6.3f p10 %6.3f p50 %6.3f p90 %6.3f p99 %6.3f\n", nm, v[v.size() / 100], v[v.size() / 10], v[v.size() / 2], v[v.size() * 9 / 10], v[v.size() * 99 / 100]);
  }
  return 0;
}

// dq-gentool owmap <seed text> <genVersion> <x0> <z0> <step> <n> <out.ppm>: the Overworld as the game makes it (columns:
// height and biome), a picture from above for checking terrain changes
static int owmap(const std::string& seedText, int ver, int x0, int z0, int step, int n, const char* out) {
  WorldGenOptions o; o.seed = seedFromString(seedText); o.genVersion = ver;
  OverworldGen g(o);
  const double t0 = Clock::now();
  std::vector<u8> img((size_t)n * n * 3);
  int water = 0;
  for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
    const OwColumn c = g.rawColumn(x0 + i * step, z0 + j * step);
    int r, gg, b;
    const int h = c.H - SEA_LEVEL;
    if (c.H < SEA_LEVEL) { water++; r = 20; gg = 60 + std::max(-40, h) ; b = 150 + std::max(-60, h * 2); }
    else {
      switch (c.biome) {
        case BIOME_BEACH: r = 220; gg = 210; b = 150; break;
        case BIOME_DESERT: r = 230; gg = 200; b = 120; break;
        case BIOME_TAIGA: r = 60; gg = 110; b = 80; break;
        case BIOME_FOREST: r = 40; gg = 120; b = 40; break;
        case BIOME_MOUNTAINS: r = 130; gg = 130; b = 130; break;
        default: r = 100; gg = 170; b = 70; break;
      }
      if (c.special == 1) { r = 150; gg = 90; b = 160; }
      const double k = 0.6 + std::min(1.0, h / 120.0) * 0.8;
      r = std::min(255, (int)(r * k)); gg = std::min(255, (int)(gg * k)); b = std::min(255, (int)(b * k));
      if (c.temp < -0.38 && c.biome != BIOME_MOUNTAINS) { r = (r + 230) / 2; gg = (gg + 230) / 2; b = (b + 240) / 2; }
    }
    u8* px = &img[((size_t)j * n + i) * 3]; px[0] = (u8)std::clamp(r, 0, 255); px[1] = (u8)std::clamp(gg, 0, 255); px[2] = (u8)std::clamp(b, 0, 255);
  }
  printf("owmap v%d: %d columns in %.0f ms, water %.0f%%\n", ver, n * n, Clock::now() - t0, 100.0 * water / (n * n));
  FILE* f = fopen(out, "wb"); fprintf(f, "P6 %d %d 255\n", n, n); fwrite(img.data(), 1, img.size(), f); fclose(f);
  return 0;
}

// dq-gentool floaters <#seed|text> <x0> <z0> <chunks>: wood generated with nothing under or beside it but air, water or
// leaves (a tree's parts left hanging): each clump, where it is and what it's made of
static int floaters(const std::string& seedText, int x0, int z0, int nc) {
  WorldGenOptions o; o.seed = seedText.size() > 1 && seedText[0] == '#' ? (i32)std::stoll(seedText.substr(1)) : seedFromString(seedText);
  if (const char* v = std::getenv("DQ_GENVER")) o.genVersion = atoi(v);
  WorldGen wg(o); DimGenerator* g = wg.forDim(DIM_OVERWORLD);
  const int n = nc * 16; std::vector<BlockId> B((size_t)n * n * DIM_H);
  auto I = [&](int x, int y, int z) { return ((size_t)y * n + z) * n + x; };
  for (int cz = 0; cz < nc; cz++) for (int cx = 0; cx < nc; cx++) {
    Chunk c(DIM_OVERWORLD, (x0 >> 4) + cx, (z0 >> 4) + cz); g->generate(c);
    for (int y = 0; y < DIM_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) B[I(cx * 16 + x, y, cz * 16 + z)] = c.get(x, y, z);
  }
  std::vector<u8> kind(reg().blocks.size() + 1, 0);   // 1 wood, 2 leaves, 3 holds it up
  for (size_t i = 1; i < reg().blocks.size(); i++) {
    const std::string& k = reg().blocks[i].key;
    if (k.find("leaves") != std::string::npos || k.find("vine") != std::string::npos || k == "cocoa") kind[i] = 2;
    else if (k.find("_log") != std::string::npos || k == "log" || k.find("branch") != std::string::npos || k.find("_wood") != std::string::npos || k.find("roots") != std::string::npos) kind[i] = 1;
    else if (reg().tSolid[i] && !reg().tLiquid[i]) kind[i] = 3;
  }
  std::vector<u8> seen(B.size(), 0); int found = 0;
  for (int y = 1; y < DIM_H - 1; y++) for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
    if (seen[I(x, y, z)] || kind[B[I(x, y, z)]] != 1) continue;
    std::vector<std::array<int, 3>> st{{x, y, z}}, all; seen[I(x, y, z)] = 1; bool held = false;
    while (!st.empty()) {
      auto [a, b, c] = st.back(); st.pop_back(); all.push_back({a, b, c});
      for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {   // (a tree's parts join at corners too)
        if (!dx && !dy && !dz) continue;
        const int p = a + dx, q = b + dy, r = c + dz;
        if (p < 0 || r < 0 || p >= n || r >= n) { held = true; continue; }
        if (q < 0 || q >= DIM_H) continue;
        const u8 k = kind[B[I(p, q, r)]];
        if (k == 3 && (std::abs(dx) + std::abs(dy) + std::abs(dz) == 1)) held = true;
        else if ((k == 1 || k == 2) && !seen[I(p, q, r)]) { seen[I(p, q, r)] = 1; st.push_back({p, q, r}); }
      }
    }
    if (held) continue;
    std::map<std::string, int> what; for (auto& e : all) what[key(B[I(e[0], e[1], e[2])])]++;
    if (all.size() > 400) continue;   // (a wood's canopy reaching out of the area: can't tell)
    std::string w; for (auto& [k, c] : what) w += " " + k + "x" + std::to_string(c);
    if (found++ < 40) printf("floating %zu at %d,%d,%d:%s\n", all.size(), x0 + x, y + 512, z0 + z, w.c_str());
  }
  printf("%d floating clumps in %d chunks\n", found, nc * nc);
  // lone columns of land standing two or more blocks over every column round them, by the water
  std::vector<int> top((size_t)n * n, 0);
  for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) for (int y = DIM_H - 1; y > 0; y--) { const BlockId t = B[I(x, y, z)]; if (reg().tSolid[t] && reg().tOpaque[t] && !reg().tLiquid[t] && kind[t] == 3) { top[(size_t)z * n + x] = y; break; } }
  int spikes = 0;
  for (int z = 1; z < n - 1; z++) for (int x = 1; x < n - 1; x++) {
    const int h = top[(size_t)z * n + x]; if (h <= SEA_LEVEL) continue;
    int mx = 0; for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (dx || dz) mx = std::max(mx, top[(size_t)(z + dz) * n + x + dx]);
    if (mx > h - 2 || mx > SEA_LEVEL) continue;
    if (spikes++ < 12) printf("spike at %d,%d top %d: %s\n", x0 + x, z0 + z, h + 512, key(B[I(x, h, z)]).c_str());
  }
  printf("%d lone columns in the water\n", spikes);
  // tall stalks: a run of more than 8 of one block that isn't solid ground (a plant or a column feature gone too tall)
  std::map<std::string, int> tall; int shown = 0;
  for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
    int run = 0; BlockId last = 0;
    for (int y = 1; y < DIM_H; y++) {
      const BlockId t = B[I(x, y, z)];
      if (t && t == last && !reg().tOpaque[t] && !reg().tLiquid[t] && !reg().tLeaves[t] && kind[t] != 1) run++; else run = 1;
      last = t;
      if (run == 9) { tall[key(t)]++; if (shown++ < 8) printf("stalk of %s at %d,%d,%d\n", key(t).c_str(), x0 + x, y + 512, z0 + z); }
    }
  }
  for (auto& [k, c] : tall) printf("tall %s: %d\n", k.c_str(), c);
  // water held up by leaves or air, and stone resting on leaves (features set on the trees' crowns)
  int wetLeaves = 0, rockLeaves = 0; const BlockId WATER = reg().blockId("water");
  for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) for (int y = 2; y < DIM_H; y++) {
    const BlockId t = B[I(x, y, z)], u = B[I(x, y - 1, z)];
    if (t == WATER && (u == AIR || reg().tLeaves[u]) && y > SEA_LEVEL) { if (wetLeaves++ < 3) printf("water over %s at %d,%d,%d\n", key(u).c_str(), x0 + x, y + 512, z0 + z); }
    if (t && reg().tOpaque[t] && kind[t] == 3 && reg().tLeaves[u]) { if (rockLeaves++ < 3) printf("%s on leaves at %d,%d,%d\n", key(t).c_str(), x0 + x, y + 512, z0 + z); }
  }
  printf("%d water over leaves/air, %d ground blocks on leaves\n", wetLeaves, rockLeaves);
  return 0;
}

// dq-gentool topview <#seed|text> <x0> <z0> <chunks> <out.ppm>: real chunks generated, each column's top block's colour from
// above (shaded by height): what the land actually looks like
// terrain version 15: Soulrend's biomes over an area, as a picture (a colour per biome) and a count
static int biomemap(const std::string& seedText, int x0, int z0, int n, int step, const char* out) {
  WorldGenOptions o; o.seed = seedText.size() > 1 && seedText[0] == '#' ? (i32)std::stoll(seedText.substr(1)) : seedFromString(seedText);
  o.genVersion = std::getenv("DQ_GENVER") ? atoi(std::getenv("DQ_GENVER")) : 17;
  const char* by = std::getenv("DQ_BIOMEY");   // (a depth: Minecraft's y)
  WorldGen wg(o); auto* g = dynamic_cast<OverworldGen*>(wg.forDim(DIM_OVERWORLD));
  std::vector<u8> img((size_t)n * n * 3); std::map<std::string, int> count; std::map<std::string, std::pair<int, int>> where;
  for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
    const std::string b = by ? g->mcBiomeNameAt(x0 + i * step, atoi(by), z0 + j * step) : g->mcBiomeName(x0 + i * step, z0 + j * step); count[b]++;
    if (!where.count(b)) where[b] = {x0 + i * step, z0 + j * step};
    u32 h = 2166136261u; for (char c : b) h = (h ^ (u8)c) * 16777619u;
    u8* px = &img[((size_t)j * n + i) * 3]; px[0] = 60 + (h & 0xBF); px[1] = 60 + ((h >> 8) & 0xBF); px[2] = 60 + ((h >> 16) & 0xBF);
  }
  std::vector<std::pair<int, std::string>> v; for (auto& [k, c] : count) v.push_back({c, k}); std::sort(v.rbegin(), v.rend());
  for (auto& [c, k] : v) printf("%5.1f%% %s (at %d %d)\n", 100.0 * c / (n * n), k.c_str(), where[k].first, where[k].second);
  FILE* f = fopen(out, "wb"); fprintf(f, "P6 %d %d 255\n", n, n); fwrite(img.data(), 1, img.size(), f); fclose(f);
  return 0;
}

static int topview(const std::string& seedText, int x0, int z0, int nc, const char* out) {
  WorldGenOptions o; o.seed = seedText.size() > 1 && seedText[0] == '#' ? (i32)std::stoll(seedText.substr(1)) : seedFromString(seedText);
  if (const char* v = std::getenv("DQ_GENVER")) o.genVersion = atoi(v);
  WorldGen wg(o); DimGenerator* g = wg.forDim(DIM_OVERWORLD);
  const int n = nc * 16; std::vector<u8> img((size_t)n * n * 3); std::map<std::string, int> tops;
  std::map<BlockId, u32> colCache;
  auto colour = [&](BlockId b) -> u32 {
    auto it = colCache.find(b); if (it != colCache.end()) return it->second;
    const std::string k = key(b); u32 c = 0x808080;
    struct { const char* w; u32 c; } T[] = {{"water", 0x3060c0}, {"ice", 0xa0c0f0}, {"leaves", 0x2f7a2a}, {"grass", 0x5aa040}, {"sand", 0xdccf90}, {"stone", 0x8a8a8a},
      {"gravel", 0x9a9088}, {"dirt", 0x7a5a3a}, {"snow", 0xf0f0f0}, {"log", 0x6b4a2a}, {"clay", 0xa0a8b8}, {"mud", 0x4a3a30}, {"podzol", 0x6a4a28},
      {"terracotta", 0xb06a40}, {"mycelium", 0x806878}, {"moss", 0x507a30}, {"andesite", 0x888888}, {"granite", 0x9a6a5a}, {"diorite", 0xc8c8c8}};
    for (auto& t : T) if (k.find(t.w) != std::string::npos) { c = t.c; break; }
    return colCache[b] = c;
  };
  for (int cz = 0; cz < nc; cz++) for (int cx = 0; cx < nc; cx++) {
    Chunk c(DIM_OVERWORLD, (x0 >> 4) + cx, (z0 >> 4) + cz); g->generate(c);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int y = DIM_H - 1; BlockId b = AIR;
      for (; y > 0; y--) { b = c.get(x, y, z); if (b != AIR && reg().block(b).render != Render::Cross) break; }
      tops[key(b)]++;
      const u32 col = colour(b); const float k = 0.55f + std::clamp((y - 50) / 120.f, 0.f, 1.f) * 0.6f;
      u8* px = &img[(((size_t)cz * 16 + z) * n + cx * 16 + x) * 3];
      px[0] = (u8)std::min(255.f, ((col >> 16) & 255) * k); px[1] = (u8)std::min(255.f, ((col >> 8) & 255) * k); px[2] = (u8)std::min(255.f, (col & 255) * k);
    }
  }
  std::vector<std::pair<int, std::string>> v; for (auto& [k2, c2] : tops) v.push_back({c2, k2}); std::sort(v.rbegin(), v.rend());
  for (size_t i = 0; i < v.size() && i < 14; i++) printf("%s %.1f%%  ", v[i].second.c_str(), 100.0 * v[i].first / (n * n)); printf("\n");
  FILE* f = fopen(out, "wb"); fprintf(f, "P6 %d %d 255\n", n, n); fwrite(img.data(), 1, img.size(), f); fclose(f);
  return 0;
}

static int surfgrid(const std::string& seedText, int x0, int z0, int w) {
  WorldGenOptions o; o.seed = seedText.size() > 1 && seedText[0] == '#' ? (i32)std::stoll(seedText.substr(1)) : seedFromString(seedText);
  if (const char* v = std::getenv("DQ_GENVER")) o.genVersion = atoi(v);
  WorldGen wg(o); DimGenerator* g = wg.forDim(DIM_OVERWORLD);
  std::map<std::string, char> sym; const char* syms = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::map<std::pair<int,int>, Chunk*> cs; const char* save = std::getenv("DQ_SAVE"); RegionStore* rs = save ? new RegionStore(save) : nullptr; int fromSave = 0;
  auto chunkAt = [&](int x, int z) { auto k = std::make_pair(x >> 4, z >> 4); auto it = cs.find(k); if (it != cs.end()) return it->second;
    Chunk* c = new Chunk(DIM_OVERWORLD, k.first, k.second); std::vector<u8> d;
    if (rs && rs->load({DIM_OVERWORLD, k.first, k.second}, d) && decodeChunk(*c, d.data(), d.size())) fromSave++; else g->generate(*c); return cs[k] = c; };
  std::vector<std::string> rows;
  for (int z = z0; z < z0 + w; z++) { std::string r;
    for (int x = x0; x < x0 + w; x++) { Chunk* c = chunkAt(x, z); static const char* sy = std::getenv("DQ_SLICEY"); int y = sy ? atoi(sy) : DIM_H - 1; BlockId b = sy ? c->get(x & 15, y, z & 15) : AIR;
      if (!sy) for (; y > 0; y--) { b = c->get(x & 15, y, z & 15); if (b != AIR && reg().block(b).render != Render::Cross && key(b).find("leaves") == std::string::npos && key(b).find("snow") != 0) break; }
      std::string k = key(b); if (!sym.count(k)) sym[k] = syms[sym.size() % 62]; r += sym[k]; }
    rows.push_back(r); }
  for (auto& [k, c] : sym) printf("%c=%s ", c, k.c_str()); printf("\n(%d chunks from the save)\n", fromSave);
  for (auto& r : rows) printf("%s\n", r.c_str());
  if (const char* col = std::getenv("DQ_COLUMN")) { const int qx = atoi(col), qz = atoi(strchr(col, ',') + 1); Chunk* c = chunkAt(qx, qz);
    for (int y = 100; y > 50; y--) printf("%d %s\n", y, key(c->get(qx & 15, y, qz & 15)).c_str()); }
  return 0;
}

int main(int argc, char** argv) {
  CoreOptions co; co.server = true; co.userDirOverride = "/tmp/dq-gentool";
  if (!initCore(co)) { fprintf(stderr, "initCore failed (run from the build dir: data/ next to the exe)\n"); return 1; }
  std::string m = argc > 1 ? argv[1] : "";
  if (m == "biomemap" && argc >= 8) return biomemap(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), argv[7]);
  if (m == "floaters" && argc >= 6) return floaters(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
  if (m == "surfgrid" && argc >= 6) return surfgrid(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
  if (m == "topview" && argc >= 7) return topview(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), argv[6]);
  if (m == "owmap" && argc >= 9) return owmap(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]), argv[8]);
  if (m == "vstats" && argc >= 3) return vstats(argv[2]);
  if (m == "vterrain" && argc >= 8) return vterrain(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), argv[7]);
  if (m == "poi" && argc >= 4) return poiDump(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 48);
  if (m == "dump" && argc >= 4) return dump(argv[2], argv[3]);
  if (m == "stats" && argc >= 7) return stats(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
  if (m == "slice" && argc >= 7) return slice(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), argc > 7 ? atoi(argv[7]) : 0, argc > 8 ? atoi(argv[8]) : 120);
  if (m == "bench" && argc >= 3) return bench(argv[2], argc > 3 ? atoi(argv[3]) : 64);
  if (m == "spawns" && argc >= 4) return spawns(argv[2], atoi(argv[3]));
  if (m == "structs" && argc >= 4) return structs(argv[2], argv[3]);
  if (m == "nether" && argc >= 3) {   // dq-gentool nether <seed>: where each of the BTA Nether's lands lies (and a coarse map)
    NetherShape S(seedFromString(argv[2]));
    int found[NB_COUNT][3]; for (auto& f : found) f[0] = INT32_MIN;
    for (int z = -1600; z <= 1600; z += 64) {
      std::string row;
      for (int x = -1600; x <= 1600; x += 32) {
        NetherCol c = S.column(x, z);
        row += "CVSXO"[c.biome];
        if (c.mix > 0.98f && found[c.biome][0] == INT32_MIN && std::abs(x) < 1500 && std::abs(z) < 1500) { found[c.biome][0] = x; found[c.biome][1] = z; found[c.biome][2] = c.floor; }
      }
      printf("%s\n", row.c_str());
    }
    for (int b = 0; b <= NB_OLDWORLD; b++) printf("%s: %d %d floor %d\n", netherBiomeName(b), found[b][0], found[b][1], found[b][2]);
    for (int k = 1; k <= 2; k++) {   // a crater's lava, a pool's acid
      bool done = false;
      for (int z = -1500; z <= 1500 && !done; z += 4) for (int x = -1500; x <= 1500 && !done; x += 4) { NetherCol c = S.column(x, z); if (c.pool == k) { printf("%s at %d %d top %d floor %d\n", k == 1 ? "crater" : "acid pool", x, z, c.poolTop, c.floor); done = true; } }
    }
    return 0;
  }
  if (m == "end" && argc >= 3) {   // dq-gentool end <seed>: where each outer End land (Better End's biomes) first lies, near the origin
    const i32 seed = seedFromString(argv[2]);
    WorldGenOptions o; o.seed = seed; WorldGen wg(o);
    auto* eg = dynamic_cast<EndGen*>(wg.forDim(DIM_END));
    int fx[19], fz[19], ft[19]; for (int i = 0; i < 19; i++) fx[i] = INT32_MIN;
    for (int r = 400; r <= 3000; r += 40) for (int a = 0; a < 64; a++) {
      const int x = (int)std::lround(std::cos(a / 64.0 * 6.2832) * r), z = (int)std::lround(std::sin(a / 64.0 * 6.2832) * r);
      const int l = endLandAt(seed, x, z);
      if (l < 0 || l > 18 || fx[l] != INT32_MIN) continue;
      EndGen::Isle c; if (!eg || !eg->island(x, z, c)) continue;
      fx[l] = x; fz[l] = z; ft[l] = c.top;
    }
    for (int i = 0; i < 19; i++) printf("land %2d: %d %d top %d\n", i, fx[i], fz[i], fx[i] == INT32_MIN ? 0 : ft[i]);
    return 0;
  }
  if (m == "ncol" && argc >= 5) {   // dq-gentool ncol <seed> <x> <z>: the Nether's landing there and its real column
    WorldGenOptions o; o.seed = seedFromString(argv[2]);
    DepthsGen g(o); const int x = atoi(argv[3]), z = atoi(argv[4]);
    printf("spawnHeight %d\n", g.spawnHeight(x, z));
    Chunk c(DIM_NETHER, floordiv(x, 16), floordiv(z, 16)); g.generate(c);
    for (int y = 175; y >= 90; y--) { BlockId b = c.get(x & 15, y, z & 15); if (b) printf("%d %s\n", y, reg().block(b).key.c_str()); }
    return 0;
  }
  if (m == "nether8" && argc >= 3) {   // dq-gentool nether8 <seed>: BTA's Nether lands across 4096 blocks (the terrain 8 map)
    bta::NetherGen g((int64_t)seedFromString(argv[2]));
    int cnt[bta::NBIO_COUNT] = {0}, tot = 0;
    for (int z = -2048; z <= 2048; z += 64) {
      std::string row;
      for (int x = -2048; x <= 2048; x += 32) { const int b = g.biomeAt(x, 100, z); row += "OPFSCUVD"[b]; cnt[b]++; tot++; }
      printf("%s\n", row.c_str());
    }
    for (int b = 0; b < bta::NBIO_COUNT; b++) printf("%-20s %5.1f%%\n", bta::netherBiomeName(b), 100.0 * cnt[b] / tot);
    return 0;
  }
  if (m == "map" && argc >= 8) return map(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));
  fprintf(stderr, "dq-gentool dump <req.json> <out.json> | bench <seed> [chunks] | map <seed> <dim> <x0> <z0> <step> <n>\n");
  return 2;
}
