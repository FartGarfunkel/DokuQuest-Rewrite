// tools/port/gentool.cpp — dq-gentool: terrain generator checks (not part of the game).
//   dq-gentool dump <request.json> <out.json>   per-column dumps for tools/port/gen_compare.py
//   dq-gentool bench <seed> [chunks]            time generate() per band and sample() per column
//   dq-gentool map <seed> <dim> <x0> <z0> <step> <n>   ASCII map of heights/biomes (quick look)
//   dq-gentool structs <request.json> <out.json>        structure plans and blocks for tools/port/struct_compare.py
#include "core/init.h"
#include "gen/overworld.h"
#include "gen/dimgens.h"
#include "gen/structures_api.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>

using namespace dq;
#include <ctime>
// thread CPU time: the build machine is shared, wall time is meaningless there
struct Clock { static double now() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; } };

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
  WorldGen wg(o);
  for (int d = 0; d < DIM_COUNT; d++) {
    DimGenerator* g = wg.forDim(d);
    // warm up
    { Chunk c(d, 1000, 1000); g->generate(c); }
    auto t0 = Clock::now();
    int side = std::max(1, (int)std::sqrt((double)n)), cnt = 0;
    for (int cz = 0; cz < side; cz++) for (int cx = 0; cx < side; cx++) {
      int ccx = d == DIM_END ? 20 + cx : cx, ccz = d == DIM_END ? cz : cz;   // the End: out on the islands
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
  WorldGenOptions o; o.seed = seedFromString(req["seed"].get<std::string>());
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
      for (int rz = -n; rz < n; rz++) for (int rx = -n; rx < n; rx++) add(idx->at(k, rx, rz));
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

int main(int argc, char** argv) {
  CoreOptions co; co.server = true; co.userDirOverride = "/tmp/dq-gentool";
  if (!initCore(co)) { fprintf(stderr, "initCore failed (run from the build dir: data/ next to the exe)\n"); return 1; }
  std::string m = argc > 1 ? argv[1] : "";
  if (m == "dump" && argc >= 4) return dump(argv[2], argv[3]);
  if (m == "bench" && argc >= 3) return bench(argv[2], argc > 3 ? atoi(argv[3]) : 64);
  if (m == "spawns" && argc >= 4) return spawns(argv[2], atoi(argv[3]));
  if (m == "structs" && argc >= 4) return structs(argv[2], argv[3]);
  if (m == "map" && argc >= 8) return map(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));
  fprintf(stderr, "dq-gentool dump <req.json> <out.json> | bench <seed> [chunks] | map <seed> <dim> <x0> <z0> <step> <n>\n");
  return 2;
}
