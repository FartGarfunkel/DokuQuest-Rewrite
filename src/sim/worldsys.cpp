// sim/worldsys.cpp — fluids, random ticks, plant growth, furnaces, crowding.
#include "sim/world_life.h"
#include "sim/dyntree.h"
#include "sim/crafting.h"
#include "core/metals.h"
#include "sim/worldsys.h"
#include "sim/catchup.h"
#include "sim/cave_drips.h"
#include "sim/trommel.h"
#include "sim/gameplay.h"
#include "sim/blockfx.h"
#include "sim/rules.h"
#include "sim/weather.h"
#include "world/carve_faces.h"
#include "common/noise.h"
#include "gen/trees.h"
#include "gen/gendefs.h"
#include <random>
#include <regex>
#include <unordered_set>
#include <map>

namespace dq {

bool waterFormsInfiniteSourceAt(int y) {
  // y here is a stacked world coordinate. The Overworld sea is band-local 64, which
  // sits at y 576 in the usual layout, so a raw compare with 64 never filled the sea.
  if (landOf(dimOfY(y)) == DIM_OVERWORLD) return y <= dimBase(DIM_OVERWORLD) + SEA_LEVEL;
  return y <= SEA_LEVEL;
}

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

struct PosKey { int x, y, z; bool operator==(const PosKey& o) const { return x == o.x && y == o.y && z == o.z; } };
struct PosKeyHash { size_t operator()(const PosKey& k) const { return ((size_t)(u32)k.x * 73856093u) ^ ((size_t)(u32)k.y * 19349663u) ^ ((size_t)(u32)k.z * 83492791u); } };

static thread_local int t_quiet = 0;
QuietEdits::QuietEdits() { t_quiet++; }
QuietEdits::~QuietEdits() { t_quiet--; }
bool quietEdits() { return t_quiet > 0; }

// A cell that holds still water without being a liquid block: lanterns, and the empty octants of a chiseled block.
static bool waterloggedCell(const Registry& R, const World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (!id || R.tLiquid[id]) return false;
  if (!(w.getMeta(x, y, z) & WLOG)) return false;
  return R.tWlog[id] || (R.tOpaque[id] && w.getCarve(x, y, z) != 0);
}

// ---- fluids ---------------------------------------------------------------------------------
class FluidSystem : public WorldSystem {
public:
  explicit FluidSystem(Level* lv) : lv(lv) {}
  const char* name() const override { return "fluids"; }
  Level* lv;
  u64 tickNo = 0; double acc = 0;
  std::unordered_map<PosKey, u64, PosKeyHash> q;
  // what's due when: a list per tick, each in the order it came (like the original's Map). One list a tick is looked
  // at, not every waiting cell: fluid at the loaded area's edge waits there for its neighbours, and scanning all of it
  // each tick was 2.5 ms of a 33 ms tick on an i5-6300U
  std::map<u64, std::vector<PosKey>> buckets;
  void enqueue(const PosKey& k, u64 due) { if (q.emplace(k, due).second) buckets[due].push_back(k); }

  static int fluidMax(int kind, int y) { return kind == 1 ? 7 : kind == 3 ? 4 : dimOfY(y) == DIM_NETHER ? 6 : 3; }
  static int fluidRate(int kind, int y) { return kind == 1 ? 5 : kind == 3 ? 12 : dimOfY(y) == DIM_NETHER ? 10 : 28; }

  void schedule(World& w, int x, int y, int z) {
    if (!inWorldY(y)) return;
    BlockId id = w.getBlock(x, y, z);
    const Registry& R = reg();
    int kind = R.tLiquid[id] ? R.tLiquid[id] : (waterloggedCell(R, w, x, y, z) ? 1 : 0);
    if (!kind) return;
    PosKey k{x, y, z};
    enqueue(k, tickNo + fluidRate(kind, y));
  }
  void scheduleAround(World& w, int x, int y, int z) {
    schedule(w, x, y, z); schedule(w, x + 1, y, z); schedule(w, x - 1, y, z);
    schedule(w, x, y + 1, z); schedule(w, x, y - 1, z); schedule(w, x, y, z + 1); schedule(w, x, y, z - 1);
  }
  void onBlockChanged(World& w, int x, int y, int z, BlockId, BlockId) override { if (!t_quiet) scheduleAround(w, x, y, z); }
  void onChunkLoaded(World& w, Chunk& c) override { wake(w, c); }

  // A newly loaded chunk: liquid with somewhere to run starts flowing (and the edges of its neighbours).
  void wake(World& w, Chunk& c) {
    const Registry& R = reg();
    auto open = [&](BlockId id) { return id == AIR || (!R.tSolid[id] && !R.tLiquid[id] && !R.tWlog[id] && !R.tPortal[id]); };
    int bx = c.worldX(), bz = c.worldZ(), base = c.baseY();
    auto at = [&](int x, int y, int z) -> BlockId {   // band-local y, x/z may step one outside
      if (x >= 0 && x < 16 && z >= 0 && z < 16) return c.get(x, y, z);
      static const BlockId stone = reg().blockId("stone");
      return w.isLoaded(bx + x, base + y, bz + z) ? w.getBlock(bx + x, base + y, bz + z) : stone;
    };
    for (int sy = 0; sy < SECS; sy++) {
      const Section* s = c.section(sy);
      if (!s) continue;
      for (int ly = 0; ly < 16; ly++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        BlockId id = s->blocks[secIdx(x, ly, z)];
        if (!R.tLiquid[id]) continue;
        int y = sy * 16 + ly;
        if (y < 1 || y >= DIM_H - 1) continue;
        // running water that isn't fed any more settles too
        if (R.tLiquid[id] == 1 && s->meta[secIdx(x, ly, z)] == 8 && !R.tLiquid[c.get(x, y + 1, z)]) { schedule(w, bx + x, base + y, bz + z); continue; }
        bool leak = open(c.get(x, y - 1, z)) || open(at(x + 1, y, z)) || open(at(x - 1, y, z)) || open(at(x, y, z + 1)) || open(at(x, y, z - 1));
        if (R.tLiquid[id] == 1) {
          auto notch = [&](int nx, int ny, int nz, bool upper) {
            BlockId nid; u8 ncv, nm;
            if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && ny >= 0 && ny < DIM_H) {
              nid = c.get(nx, ny, nz); ncv = c.getCarve(nx, ny, nz); nm = c.getMeta(nx, ny, nz);
            } else if (w.isLoaded(bx + nx, base + ny, bz + nz)) {
              nid = w.getBlock(bx + nx, base + ny, bz + nz); ncv = w.getCarve(bx + nx, base + ny, bz + nz); nm = w.getMeta(bx + nx, base + ny, bz + nz);
            } else return false;
            if (!fullSolid(nid) || !ncv || (nm & WLOG)) return false;
            return !upper || (ncv & 0xCC) != 0;   // below: an empty upper octant. beside: any notch
          };
          if (notch(x, y - 1, z, true) || notch(x + 1, y, z, false) || notch(x - 1, y, z, false) || notch(x, y, z + 1, false) || notch(x, y, z - 1, false)) leak = true;
        }
        if (leak) schedule(w, bx + x, base + y, bz + z);
      }
    }
    // the neighbours' border columns facing this chunk
    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
      Chunk* n = w.chunk(c.dim, c.cx + dx, c.cz + dz);
      if (!n) continue;
      for (int y = 1; y < DIM_H - 1; y++) {
        if (!n->section(y >> 4)) { y |= 15; continue; }
        for (int k = 0; k < 16; k++) {
          int lx = dx == 1 ? 0 : dx == -1 ? 15 : k, lz = dz == 1 ? 0 : dz == -1 ? 15 : k;
          if (!R.tLiquid[n->get(lx, y, lz)]) continue;
          int ox = dx == 1 ? 15 : dx == -1 ? 0 : k, oz = dz == 1 ? 15 : dz == -1 ? 0 : k;
          if (open(c.get(ox, y, oz))) schedule(w, n->worldX() + lx, base + y, n->worldZ() + lz);
        }
      }
    }
  }

  bool canFlowInto(World& w, int x, int y, int z, int kind, int level) {
    const Registry& R = reg();
    BlockId id = w.getBlockPhys(x, y, z);
    if (id == AIR) return true;
    if (R.tWlog[id]) return false;
    if (R.tLiquid[id] == kind) { u8 m = w.getMeta(x, y, z); return (m & 8) == 0 && (m & 7) > level && (m & 7) != 0; }
    if (R.tLiquid[id]) return true;   // other liquid: a reaction
    return !R.tSolid[id] && !R.tPortal[id];   // plants and torches get washed away
  }
  void react(World& w, int x, int y, int z) { vec3 c(x + 0.5f, y + 0.8f, z + 0.5f); sfxAt(*lv, "fizz", c); burst(*lv, c, "white", 6, 0.5f); }
  void flowInto(World& w, int x, int y, int z, int kind, u8 meta) {
    const Registry& R = reg(); const KnownBlocks& B = KB();
    BlockId id = w.getBlock(x, y, z);
    if (fullSolid(id) && w.getCarve(x, y, z)) {
      if (kind == 1) {
        u8 m = w.getMeta(x, y, z), cv = w.getCarve(x, y, z);
        if (!(m & WLOG)) { w.setBlock(x, y, z, id, (u8)(m | WLOG)); w.setCarve(x, y, z, cv); }
      }
      return;   // never replace a chiseled block, and lava does not fill its notches
    }
    int other = R.tLiquid[id];
    if (other && other != kind) {
      u8 m = w.getMeta(x, y, z);
      if (other == 2) w.setBlock(x, y, z, (m & 7) == 0 && !(m & 8) ? B.obsidian : B.cobblestone, 0);
      else w.setBlock(x, y, z, B.stone, 0);
      react(w, x, y, z);
      return;
    }
    if (id && !R.tLiquid[id]) dropBlockItems(*lv, w, x, y, z, id, true, w.getMeta(x, y, z));
    static const BlockId acid = reg().blockId("acid");
    w.setBlock(x, y, z, kind == 1 ? B.water : kind == 3 && acid ? acid : B.lava, meta);
  }
  void update(World& w, int x, int y, int z) {
    const Registry& R = reg(); const KnownBlocks& B = KB();
    BlockId id = w.getBlock(x, y, z);
    int kind = R.tLiquid[id];
    u8 meta = w.getMeta(x, y, z);
    bool logged = !kind && waterloggedCell(R, w, x, y, z);   // a block holding water is a still source that only spreads
    if (logged) { kind = 1; meta = 0; }
    if (!kind) return;
    int max = fluidMax(kind, y);
    auto same = [&](int xx, int yy, int zz) { BlockId b = w.getBlock(xx, yy, zz); return R.tLiquid[b] == kind || (kind == 1 && waterloggedCell(R, w, xx, yy, zz)); };
    // lava touching water hardens
    if (kind == 2) {
      static const int N[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}};
      for (auto& n : N) if (R.tLiquid[w.getBlock(x + n[0], y + n[1], z + n[2])] == 1) {
        w.setBlock(x, y, z, (meta & 7) == 0 && !(meta & 8) ? B.obsidian : B.cobblestone, 0);
        react(w, x, y, z);
        return;
      }
    }
    bool isSource = (meta & 7) == 0 && !(meta & 8);
    if (!isSource) {
      // recompute this cell's level from its feeders
      int expected = 99; bool falling = false;
      if (!landRoofAt(y) && same(x, y + 1, z)) falling = true;   // (the land above never feeds this one: see below)
      else {
        int sources = 0, best = 99;
        for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
          if (!same(x + dx, y, z + dz)) continue;
          BlockId nb = w.getBlock(x + dx, y, z + dz);
          u8 m = R.tLiquid[nb] ? w.getMeta(x + dx, y, z + dz) : 0;
          int lvl = (m & 8) ? 0 : (m & 7);
          if ((m & 7) == 0 && !(m & 8)) sources++;
          best = std::min(best, lvl + 1);
        }
        expected = best;
        if (kind == 1 && sources >= 2 && waterFormsInfiniteSourceAt(y)) {
          BlockId below = w.getBlockPhys(x, y - 1, z);
          if (R.tSolid[below] || (same(x, y - 1, z) && (w.getMeta(x, y - 1, z) & 15) == 0)) expected = 0;
        }
      }
      if (falling) { if (meta != 8) { w.setBlock(x, y, z, id, 8); meta = 8; } }
      else if (expected > max) { w.setBlock(x, y, z, AIR, 0); return; }
      else if (expected != (meta & 7) || (meta & 8)) { w.setBlock(x, y, z, id, (u8)expected); meta = (u8)expected; }
    }
    // a land's floor: liquid never runs out of its land into the one below. The Skylands' waterfalls end in the cloud
    // layer (their spray shows there) instead of pouring on through and flooding the Overworld; the Depths' lava stays
    // out of the End. It doesn't spread along the floor either: the falls just end.
    if (landFloorAt(y)) return;
    // spread: a carved neighbour takes water into its empty octants. Down first, otherwise sideways. Only from still water
    // in its own block, as waterlogging works: running water (and water a block holds) filled them too, and each
    // one filled became a spring of its own, so a slab floor beside a waterfall flooded on and on.
    const bool fills = kind == 1 && isSource && !logged;
    if (fills) {
      for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        BlockId nid = w.getBlock(x + dx, y, z + dz);
        u8 ncv = w.getCarve(x + dx, y, z + dz), nm = w.getMeta(x + dx, y, z + dz);
        if (fullSolid(nid) && ncv && !(nm & WLOG)) { w.setBlock(x + dx, y, z + dz, nid, (u8)(nm | WLOG)); w.setCarve(x + dx, y, z + dz, ncv); }
      }
    }
    if (fills && inWorldY(y - 1)) {
      BlockId bel = w.getBlock(x, y - 1, z);
      u8 bcv = w.getCarve(x, y - 1, z), bm = w.getMeta(x, y - 1, z);
      if (fullSolid(bel) && bcv && !(bm & WLOG)) {
        w.setBlock(x, y - 1, z, bel, (u8)(bm | WLOG));
        w.setCarve(x, y - 1, z, bcv);
        bool held = (bcv & 0xCC) != 0xCC;   // a stair still has a high step under this cell
        if (!held && !same(x, y + 1, z)) { w.setBlock(x, y, z, AIR); return; }
      }
    }
    if (inWorldY(y - 1) && canFlowInto(w, x, y - 1, z, kind, 0) && !same(x, y - 1, z)) { flowInto(w, x, y - 1, z, kind, 8); return; }
    // resting on water: running water just feeds what's below; a source still spreads
    if (inWorldY(y - 1) && same(x, y - 1, z)) {
      BlockId bid = w.getBlock(x, y - 1, z);
      bool stillBelow = R.tLiquid[bid] ? (w.getMeta(x, y - 1, z) & 15) == 0 : true;
      if (!(isSource && stillBelow)) return;
    }
    int lvl = (meta & 8) ? 0 : (meta & 7);
    if (lvl >= max) return;
    BlockId below = w.getBlockPhys(x, y - 1, z);
    if (!R.tSolid[below] && !R.tLiquid[below] && below != AIR) return;
    int next = lvl + 1;
    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) if (canFlowInto(w, x + dx, y, z + dz, kind, next)) flowInto(w, x + dx, y, z + dz, kind, (u8)next);
  }
  void tick(World& w, double dt) override {
    acc += dt;
    int steps = 0;
    while (acc >= 0.05 && steps < 4) { acc -= 0.05; steps++; tickNo++; tickFluids(w); }
    if (acc > 0.2) acc = 0;
  }
  void tickFluids(World& w) {
    if (q.empty()) { buckets.clear(); return; }
    std::vector<PosKey> due;
    while (!buckets.empty() && buckets.begin()->first <= tickNo && due.size() < 300) {
      const u64 at = buckets.begin()->first; auto& list = buckets.begin()->second;
      size_t i = 0;
      for (; i < list.size() && due.size() < 300; i++) {
        auto it = q.find(list[i]);
        if (it == q.end() || it->second != at) continue;   // (gone, or waiting again for a later tick)
        due.push_back(list[i]); q.erase(it);
      }
      if (i < list.size()) { list.erase(list.begin(), list.begin() + (std::ptrdiff_t)i); break; }
      buckets.erase(buckets.begin());
    }
    for (auto& k : due) {
      if (!w.isLoaded(k.x, k.y, k.z)) continue;
      // wait for the chunks around before deciding how it runs
      if (!w.isLoaded(k.x + 1, k.y, k.z) || !w.isLoaded(k.x - 1, k.y, k.z) || !w.isLoaded(k.x, k.y, k.z + 1) || !w.isLoaded(k.x, k.y, k.z - 1)) {
        enqueue(k, tickNo + 20);
        continue;
      }
      update(w, k.x, k.y, k.z);
    }
  }
  void onChunkUnloading(World& w, Chunk& c) override {}
};
int fluidPending(World& w) { auto* f = w.system<FluidSystem>(); return f ? (int)f->q.size() : 0; }
void settleCarvedFluids(World& w, int x, int y, int z) {
  if (auto* f = w.system<FluidSystem>()) f->scheduleAround(w, x, y, z);
}

class BlockRulesSystem : public WorldSystem {
public:
  explicit BlockRulesSystem(Level* lv) : lv(lv) {}
  Level* lv;
  int depth = 0;
  const char* name() const override { return "blockRules"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n) override {
    if (o == n || depth > 64 || t_quiet) return;   // (cascades are bounded; a falling column of sand is a chain of these)
    depth++;
    onBlockChangedRules(*lv, w, x, y, z, o, n);
    depth--;
  }
};

void attachWorldSystems(Level& lv, World& w) {
  if (w.system<FluidSystem>()) return;
  auto f = std::make_shared<FluidSystem>(&lv);
  w.addSystem(f);
  w.addSystem(std::make_shared<BlockRulesSystem>(&lv));
  for (auto& [k, c] : w.chunks()) f->wake(w, *c);
}

// ---- trees ------------------------------------------------------------------------------------
// The world generator registers hooks().growTree with its treeParts; this fallback grows a simple
// tree of the sapling's kind so saplings always grow.
bool growTree(Level& lv, World& w, int x, int y, int z, double r) {
  const Registry& R = reg();
  BlockId sap = w.getBlock(x, y, z);
  if (dynTreesOn(lv) && R.block(sap).has("branchBlock")) return plantDynTree(lv, w, x, y, z);   // terrain 13: it takes root and grows (sim/dyntree.h)
  std::string type = R.block(sap).str("tree", "oak");
  auto placeParts = [&](const std::vector<std::array<int, 6>>& parts) {
    for (auto& p : parts) {
      if (p[1] == y && p[0] == x && p[2] == z) continue;
      BlockId b = w.getBlock(p[0], p[1], p[2]);
      if (b && !bhas(b, BF::IsLeaves) && !R.tReplaceable[b] && R.block(b).key != "tall_grass"
          && !(R.tLiquid[b] == 1 && R.tWlog[(BlockId)p[3]])) return false;
    }
    w.setBlock(x, y, z, AIR);
    for (auto& p : parts) {
      BlockId b = w.getBlock(p[0], p[1], p[2]);
      const bool wet = R.tLiquid[b] == 1 && R.tWlog[(BlockId)p[3]];
      if (p[4] && b && !R.tReplaceable[b] && !wet) continue;
      u8 meta = (u8)p[5];
      if (wet) meta |= WLOG;
      w.setBlock(p[0], p[1], p[2], (BlockId)p[3], meta);
    }
    return true;
  };
  if (type == "cacao") {   // the cacao tree: a short jungle-wood trunk under its own leaves, cocoa pods all up the trunk
    BlockId lg = R.blockId("jungle_log"), lf = R.blockId("cacao_leaves"), pod = R.blockId("cocoa");
    if (!lg || !lf) return false;
    std::vector<std::array<int, 6>> cp;
    Mulberry32 m(hash32(x, y, z, 31));
    int H = 5 + (int)(r * 3);
    for (int k = 0; k < H; k++) cp.push_back({x, y + k, z, (int)lg, 0, 0});
    for (int ly = H - 2; ly <= H + 1; ly++) {
      int rad = ly >= H ? 1 : 2;
      for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
        if (std::abs(dx) == rad && std::abs(dz) == rad && (ly >= H || m() < 0.5)) continue;
        if (!dx && !dz && ly < H) continue;
        cp.push_back({x + dx, y + ly, z + dz, (int)lf, 1, 0});
      }
    }
    if (pod) for (int d = 0; d < 4; d++) for (int k = 1; k < H - 2; k++) if (m() < 0.45)
      cp.push_back({x + DIRX[d], y + k, z + DIRZ[d], (int)pod, 1, (((d + 2) & 3) << 4) | (int)(m() * 3)});
    return placeParts(cp);
  }
  if (hooks().growTree) return hooks().growTree(w, x, y, z, type, r);
  auto pick = [&](std::initializer_list<std::string> keys) { for (auto& k : keys) { BlockId id = R.blockId(k); if (id) return id; } return (BlockId)0; };
  BlockId log = pick({type + "_log", "log"}), leaves = pick({type + "_leaves", type + "_needles", "leaves"});
  if (!log || !leaves) return false;
  bool conifer = type == "spruce" || type == "pine";
  int H = (conifer ? 6 : 4) + (int)(r * (conifer ? 4 : 3));
  std::vector<std::array<int, 6>> parts;   // x,y,z,id,onlyAir,meta
  // the world generator's own shape of that kind of tree, where it has one (palms lean, acacias fork, jungles tower)
  const u8 kind = treeKindFromName(type);
  const bool genShape = kind != TREE_NONE && type == treeKindName(kind);
  if (genShape) {
    static GenBlocks GB; static bool gbOk = false;
    if (!gbOk) { GB.resolve(); gbOk = true; }
    const int base = dimBase(dimOfY(y));
    TreeSpec t; t.kind = kind; t.x = x; t.z = z; t.y = t.ySeed = y - base; t.h = r;
    std::vector<TreePart> tp;
    treeParts(GB, (i32)hash32(x, y, z, 91), t, tp);
    for (auto& p : tp) if (p.id) parts.push_back({(int)std::floor(p.x), (int)std::floor(p.y) + base, (int)std::floor(p.z), (int)p.id, p.onlyAir ? 1 : 0, p.meta >= 0 ? p.meta : 0});
  }
  else for (int k = 0; k < H; k++) parts.push_back({x, y + k, z, log, 0});
  if (genShape) {
  } else if (conifer) {
    int rad = 0;
    for (int yy = H; yy >= 2; yy--) {
      for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) if (std::abs(dx) + std::abs(dz) <= rad + 1 && (dx || dz || yy >= H)) parts.push_back({x + dx, y + yy, z + dz, leaves, 1});
      rad = rad >= 2 ? 1 : rad + 1;
      if (yy == H) rad = 1;
    }
    parts.push_back({x, y + H + 1, z, leaves, 1});
  } else {
    Mulberry32 m(hash32(x, y, z, 77));
    for (int ly = H - 3; ly <= H + 1; ly++) {
      int rad = ly >= H ? 1 : 2;
      for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
        if (std::abs(dx) == rad && std::abs(dz) == rad && (ly >= H || m() < 0.5)) continue;
        if (!dx && !dz && ly < H) continue;
        parts.push_back({x + dx, y + ly, z + dz, leaves, 1});
      }
    }
  }
  return placeParts(parts);
}

// ---- random ticks --------------------------------------------------------------------------------
class RandomTickCache : public WorldSystem {
public:
  const char* name() const override { return "randomTicks"; }
  std::unordered_map<ChunkKey, std::unordered_set<int>, ChunkKeyHash> tickables;   // colIdx of T_TICK blocks
  void onBlockChanged(World& w, int x, int y, int z, BlockId, BlockId n) override {
    if (!reg().tTick[n]) return;
    auto it = tickables.find({dimOfY(y), x >> 4, z >> 4});
    if (it != tickables.end()) it->second.insert(colIdx(x & 15, y & 255, z & 15));
  }
  void onChunkUnloading(World& w, Chunk& c) override { tickables.erase(c.key()); }
  std::unordered_set<int>& scan(Chunk& c) {
    auto& s = tickables[c.key()];
    const Registry& R = reg();
    for (int sy = 0; sy < SECS; sy++) {
      const Section* sec = c.section(sy);
      if (!sec) continue;
      for (int i = 0; i < 4096; i++) if (R.tTick[sec->blocks[i]]) { int ly = i >> 8, z = (i >> 4) & 15, x = i & 15; s.insert(colIdx(x, sy * 16 + ly, z)); }
    }
    return s;
  }
};

bool waterNear(World& w, int x, int y, int z, int r) {
  for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) for (int dy = 0; dy <= 1; dy++) if (waterAt(w, x + dx, y + dy, z + dz)) return true;
  return false;
}
float bandDaylight(Level& lv, int y) {
  switch (dimOfY(y)) { case DIM_NETHER: return 0.22f; case DIM_SKYLANDS: return 1; case DIM_END: return 0.7f; default: return daylight(lv); }
}
bool stemFruit(World& w, int x, int y, int z, const BlockDef& d) {
  BlockId fruit = reg().blockId(d.props["crop"].value("fruit", std::string()));
  if (!fruit) return false;
  int ks[4] = {0, 1, 2, 3};
  std::shuffle(ks, ks + 4, std::mt19937((unsigned)(rnd() * 1e9)));
  auto& soil = soilIds();
  for (int k : ks) {
    int fx = x + DIRX[k], fz = z + DIRZ[k];
    if (w.getBlock(fx, y, fz) == fruit) return false;
    if (w.getBlock(fx, y, fz) == AIR && std::find(soil.begin(), soil.end(), w.getBlock(fx, y - 1, fz)) != soil.end()) { w.setBlock(fx, y, fz, fruit, 0); return true; }
  }
  return false;
}
static void grassTick(Level& lv, World& w, int x, int y, int z, BlockId id) {
  const Registry& R = reg(); const KnownBlocks& B = KB();
  BlockId above = w.getBlock(x, y + 1, z);
  if ((R.tOpaque[above] && R.tSolid[above] && !bhas(above, BF::IsLog)) || R.tLiquid[above]) { if (rule(lv, "grassSpread")) w.setBlock(x, y, z, B.dirt, 0); return; }
  if (!rule(lv, "grassSpread")) return;
  // four tries at bare dirt in the 3 x 5 x 3 around (one up, three down), each needing light and air above
  static const BlockId snowy = R.blockId("snowy_grass");
  for (int t = 0; t < 4; t++) {
    int tx = x + (int)std::floor(rnd() * 3) - 1, ty = y + (int)std::floor(rnd() * 5) - 3, tz = z + (int)std::floor(rnd() * 3) - 1;
    if (w.getBlock(tx, ty, tz) != B.dirt) continue;
    BlockId a = w.getBlock(tx, ty + 1, tz);
    if ((R.tOpaque[a] && R.tSolid[a]) || R.tLiquid[a]) continue;
    u8 l = w.getLight(tx, ty + 1, tz);
    if (std::max(l >> 4, l & 15) < 9) continue;
    w.setBlock(tx, ty, tz, id == snowy && snowy ? (BlockId)R.blockId("grass") : id, 0);
  }
}
static void growExtra(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta) {
  const Registry& R = reg(); const KnownBlocks& B = KB();
  const BlockDef& d = R.block(id);
  if (lifeRandomTick(lv, w, x, y, z, id, meta)) return;   // mushrooms, vines (sim/world_life.h)
  if (R.tGrassy[id]) { grassTick(lv, w, x, y, z, id); return; }
  if (d.has("crop") && d.props["crop"].contains("fruit")) { if ((meta & 15) >= d.props["crop"].value("stages", 1) - 1 && rnd() < 1.0 / 340) stemFruit(w, x, y, z, d); return; }
  if (id == B.bamboo) {
    if (rnd() > 1.0 / 205) return;
    int h = 1; while (h < 16 && w.getBlock(x, y - h, z) == id) h++;
    if (w.getBlock(x, y + 1, z) == AIR && h < 12 + (int)(hash32(x, 0, z, 3) % 5)) w.setBlock(x, y + 1, z, id, 0);
    return;
  }
  if (id == B.twisting_vines) { if (rnd() < 1.0 / 680 && w.getBlock(x, y + 1, z) == AIR) { int h = 1; while (h < 26 && w.getBlock(x, y - h, z) == id) h++; if (h < 25) w.setBlock(x, y + 1, z, id, 0); } return; }
  if (id == B.weeping_vines || id == B.cave_vines) {
    if (rnd() < 1.0 / 680 && w.getBlock(x, y - 1, z) == AIR) { int h = 1; while (h < 26 && bhas(w.getBlock(x, y + h, z), BF::Hangs)) h++; if (h < 25) w.setBlock(x, y - 1, z, id == B.cave_vines && rnd() < 0.15 ? B.cave_vines_lit : id, 0); }
    else if (id == B.cave_vines && rnd() < 1.0 / 60) w.setBlock(x, y, z, B.cave_vines_lit, meta);
    return;
  }
  if (d.has("seasonalForm")) {   // the Seasonal Forest: its leaves turn with the year, a few at a time
    static const BlockId F[4] = {R.blockId("seasonal_blossom"), R.blockId("seasonal_leaves"), R.blockId("seasonal_leaves_autumn"), R.blockId("seasonal_leaves_winter")};
    float ph = seasonPhase(lv);
    if (ph < 0) ph = (float)std::fmod(lv.info.day / 7.0, 4.0);   // with world seasons off the grove keeps its own year
    const int want = (int)ph & 3, now = (int)d.num("seasonalForm", 1);
    if (want != now && F[want] && rnd() < 0.04) w.setBlock(x, y, z, F[want], meta);
    return;
  }
  if (d.has("regrows")) {   // emberglass regrows: now and then a fresh cluster on top, in the Depths
    static const BlockId cl = R.blockId(d.str("regrows"));
    if (cl && dimOfY(y) == DIM_NETHER && w.getBlock(x, y + 1, z) == AIR && rnd() < 0.02) w.setBlock(x, y + 1, z, cl, 0);
    return;
  }
  if (d.has("igneous")) {   // igneous emberstone: a fire on top that never goes out, and wet sponges beside it dry
    static const BlockId fireId = R.blockId("fire"), wet = R.blockId("wet_sponge"), dry = R.blockId("sponge");
    if (fireId && w.getBlock(x, y + 1, z) == AIR) w.setBlock(x, y + 1, z, fireId, 0);
    if (wet && dry) for (int dz = -2; dz <= 2; dz++) for (int dy = -1; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
      if (w.getBlock(x + dx, y + dy, z + dz) == wet) { w.setBlock(x + dx, y + dy, z + dz, dry, 0); lv.fx->particles("poof", vec3(x + dx, y + dy, z + dz) + vec3(0.5f), 6); }
    return;
  }
  if (d.has("hive")) { if ((meta & 0x0c) != 0x0c && rnd() < 1.0 / 140) w.setBlock(x, y, z, id, meta + 4); return; }
  if (d.has("movingBlock")) { bool anim = false; for (auto& e : lv.entities()) if (e->type == "piston_anim") anim = true; if (!anim) w.setBlock(x, y, z, AIR); return; }
  if (id == B.budding_amethyst) {
    if (rnd() > 1.0 / 340) return;
    int k = (int)std::floor(rnd() * 6);
    if (k != 2) return;   // buds are drawn standing up, so they only grow on top
    BlockId nid = w.getBlock(x, y + 1, z);
    BlockId next = nid == AIR ? B.small_amethyst_bud : nid == B.small_amethyst_bud ? B.medium_amethyst_bud : nid == B.medium_amethyst_bud ? B.large_amethyst_bud : nid == B.large_amethyst_bud ? B.amethyst_cluster : (BlockId)0;
    if (next) w.setBlock(x, y + 1, z, next, 0);
  }
  // puddles dry up once the rain stops and a storm's snow melts away: the weather's own block tick (game/blocks2.js growExtra2)
  static const BlockId puddleId = R.blockId("puddle"), snowLayerId = R.blockId("snow_layer"), iceId = R.blockId("ice");
  if (id == puddleId || id == snowLayerId || id == iceId) { if (WeatherSystem* ws = weatherOf(lv)) ws->blockTick(lv, w, x, y, z, id, meta); return; }
}

bool saplingStep(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta) {
  const Registry& R = reg();
  if (!(meta & 8)) { w.setBlock(x, y, z, id, meta | 8); return true; }
  const std::string fl = R.block(id).str("fruitLeaves");
  const bool grew = growTree(lv, w, x, y, z, rnd());
  if (grew && !fl.empty()) if (BlockId f = R.blockId(fl)) {   // a fruit tree: its leaves bear fruit
    const BlockId oak = R.blockId("leaves");
    for (int dy = 1; dy <= 9; dy++) for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (w.getBlock(x + dx, y + dy, z + dz) == oak) w.setBlock(x + dx, y + dy, z + dz, f, w.getMeta(x + dx, y + dy, z + dz));
  }
  return grew;
}

// Farmland's moisture (meta & 7): water within four blocks (or rain falling on it) keeps it at 7; away from water it
// dries a step at a time, and dry farmland with nothing planted on it goes back to dirt. Crops on moist farmland grow
// twice as fast as on dry.
bool farmlandMoist(World& w, int x, int y, int z) {
  static const BlockId fl = reg().blockId("farmland");
  return fl && w.getBlock(x, y, z) == fl && ((w.getMeta(x, y, z) & 7) > 0 || waterNear(w, x, y, z, 4));
}
bool farmlandTick(Level& lv, World& w, int x, int y, int z, u8 meta) {
  static const BlockId fl = reg().blockId("farmland"), dirt = reg().blockId("dirt");
  const int m = meta & 7;
  WeatherSystem* ws = weatherOf(lv);
  const bool rainedOn = ws && ws->kind != "clear" && ws->precipAt(w, x + 0.5f, y + 1.0f, z + 0.5f) == 1 && ws->topAt(w, x, z) <= y + 1;
  if (waterNear(w, x, y, z, 4) || rainedOn) { if (m != 7) { w.setBlock(x, y, z, fl, (u8)((meta & ~7) | 7)); return true; } return false; }
  if (m > 0) { w.setBlock(x, y, z, fl, (u8)((meta & ~7) | (m - 1))); return true; }
  const BlockId above = w.getBlock(x, y + 1, z);
  if (above == AIR || !bhas(above, BF::Crop)) { if (dirt) w.setBlock(x, y, z, dirt, 0); return true; }
  return false;
}

void trampleFarmland(Level& lv, World& w, int x, int y, int z) {
  static const BlockId fl = reg().blockId("farmland"), dirt = reg().blockId("dirt");
  if (!fl || !dirt || w.getBlock(x, y, z) != fl) return;
  const BlockId above = w.getBlock(x, y + 1, z);
  if (above && bhas(above, BF::Crop)) { dropBlockItems(lv, w, x, y + 1, z, above, false, w.getMeta(x, y + 1, z)); w.setBlock(x, y + 1, z, AIR, 0); }
  w.setBlock(x, y, z, dirt, 0);
  lv.fx->material("gravel", "dig", vec3(x + 0.5f, y + 1, z + 0.5f));
}

void randomTicks(Level& lv, World& w, float px, float pz, int slice, int slices) {
  const double RT = 68.27;
  const Registry& R = reg(); const KnownBlocks& B = KB();
  auto* cache = w.system<RandomTickCache>();
  if (!cache) { auto c = std::make_shared<RandomTickCache>(); cache = c.get(); w.addSystem(c); }
  int pcx = ifloor(px) >> 4, pcz = ifloor(pz) >> 4;
  int scanned = 0;
  const double tickSpeed = std::clamp(ruleNum(lv, "randomTickSpeed"), 0.0, 100.0);
  std::vector<Chunk*> list;
  const int simR = simDistance(lv);   // (beyond it, the catch-up makes up the time: sim/catchup.h)
  for (auto& [k, c] : w.chunks()) if (std::abs(k.cx - pcx) <= simR && std::abs(k.cz - pcz) <= simR) {
    if (slices > 1 && ((k.cx * 7 + k.cz * 13) % slices + slices) % slices != slice) continue;   // another tick's share
    list.push_back(c.get());
  }
  const int scanCap = slices > 1 ? 2 : 24;   // (first sight of a chunk: its tickable blocks found, a few a call)
  for (Chunk* c : list) {
    auto it = cache->tickables.find(c->key());
    if (it == cache->tickables.end()) { if (scanned++ > scanCap) continue; cache->scan(*c); it = cache->tickables.find(c->key()); }
    std::vector<int> idx(it->second.begin(), it->second.end());
    int base = c->baseY();
    const int reps = (int)tickSpeed + (rnd() < tickSpeed - (int)tickSpeed ? 1 : 0);   // randomTickSpeed: 2 = twice as fast
    for (int rep = 0; rep < reps; rep++)
    for (int i : idx) {
      int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
      BlockId id = c->get(lx, ly, lz);
      int x = c->worldX() + lx, y = base + ly, z = c->worldZ() + lz;
      if (!R.tTick[id]) { cache->tickables[c->key()].erase(i); continue; }
      u8 meta = c->getMeta(lx, ly, lz);
      const BlockDef& d = R.block(id);
      if (d.key == "pointed_dripstone") { if (meta & 1) caveDripTick(w, x, y, z, rnd()); continue; }
      if (id == B.farmland) { if (rnd() < 1.0 / 20) farmlandTick(lv, w, x, y, z, meta); continue; }   // (moisture: about every 20 s)   // (sim/cave_drips.h: a cauldron under it fills)
      if (bhas(id, BF::Tree)) {   // saplings: two stages, each a 1 in 7 chance per random tick
        if (rnd() < 1 / (RT * 7)) saplingStep(lv, w, x, y, z, id, meta);
      } else if (id == B.berry_bush_empty) { if (rnd() < 1 / (RT * 5)) w.setBlock(x, y, z, B.berry_bush); }
      else if (d.has("refill")) { if (rnd() < 1 / (RT * 5)) if (BlockId f = R.blockId(d.str("refill"))) w.setBlock(x, y, z, f); }   // a picked berry bush bears again
      else if (bhas(id, BF::Crop)) {
        int m = meta & 15, stages = d.props["crop"].value("stages", 1);
        if (m >= stages - 1) { if (d.props["crop"].contains("fruit")) growExtra(lv, w, x, y, z, id, meta); continue; }
        if (!d.has("soil") && !d.has("cocoa")) {
          u8 l = w.getLight(x, y, z);
          if (std::max(l >> 4, l & 15) < 9 && (l >> 4) * bandDaylight(lv, y) < 9) continue;
        }
        bool wet = d.has("soil") || d.has("cocoa") || farmlandMoist(w, x, y - 1, z) || waterNear(w, x, y - 1, z, 4);
        if (seasonOf(lv) == 3 && skyLandOf(dimOfY(y)) == DIM_OVERWORLD && w.realm == REALM_MAIN && rnd() < 0.7) continue;   // winter: crops out in the cold grow slowly
        if (rnd() < (wet ? 1 / (RT * 4) : 1 / (RT * 8))) w.setBlock(x, y, z, id, (u8)((meta & ~15) | (m + 1)));
      } else if (id == B.sugar_cane || id == B.cactus || id == B.kelp) {
        if (rnd() > (id == B.kelp ? 0.14 / RT : 1 / (RT * 16))) continue;
        int h = 1; while (h < 4 && w.getBlock(x, y - h, z) == id) h++;
        BlockId above = w.getBlock(x, y + 1, z);
        if (h < (id == B.kelp ? 8 : 3) && (id == B.kelp ? R.tLiquid[above] == 1 : above == AIR)) w.setBlock(x, y + 1, z, id, id == B.kelp ? WLOG : 0);
      } else if (d.has("rootySoil")) {   // a growing tree's soil: one growth signal now and then (the growing trees: sim/dyntree.h)
        if (dynTreesOn(lv) && rnd() < 1 / (RT * 0.5)) growDynTree(lv, w, x, y, z, (u32)(rnd() * 4294967295.0));
      } else if (d.has("netherReed") || d.has("netherCactus")) {   // the classic Nether: reeds grow by lava, cacti on their own, 3 tall
        if (rnd() > 1 / (RT * 16)) continue;
        int h = 1; while (h < 4 && w.getBlock(x, y - h, z) == id) h++;
        if (h >= 3 || w.getBlock(x, y + 1, z) != AIR) continue;
        if (d.has("netherReed")) {
          bool lava = false;
          for (auto [a, b] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) if (R.tLiquid[w.getBlock(x + a, y - h, z + b)] == 2) lava = true;
          if (!lava && rnd() < 0.5) continue;   // (by lava twice as fast: the classic Nether's fertile ground)
        }
        w.setBlock(x, y + 1, z, id, 0);
      } else if (bhas(id, BF::Fire)) {
        BlockId below = w.getBlock(x, y - 1, z);
        if (below && (below == B.emberstone || below == B.magma_block || below == B.soul_soil || below == B.ash || R.block(below).has("igneous"))) continue;
        if (rnd() < 0.3) w.setBlock(x, y, z, AIR);
      } else if (d.has("oxidizes")) { if (rnd() < 1.0 / 2400) w.setBlock(x, y, z, R.blockId(d.str("oxidizes")), meta); }
      else growExtra(lv, w, x, y, z, id, meta);
    }
    // grass creeping: four random columns per chunk each second (about once a minute for each; the
    // randomTickSpeed rule scales it), looking down through leaves, logs and plants to the ground under trees too
    for (int k = 0, n = (int)std::lround(4 * tickSpeed); k < n; k++) {
      int col = ((int)(rnd() * 16)) | (((int)(rnd() * 16)) << 4);
      int cx = col & 15, cz = col >> 4;
      int y = std::min(c->maxY, DIM_H - 1);
      while (y > 0) { BlockId b = c->get(cx, y, cz); if (R.tSolid[b] && !R.tLeaves[b] && !bhas(b, BF::IsLog)) break; y--; }
      BlockId id = c->get(cx, y, cz);
      static const BlockId snowyG = R.blockId("snowy_grass");
      if (R.tGrassy[id] || (snowyG && id == snowyG)) grassTick(lv, w, c->worldX() + cx, base + y, c->worldZ() + cz, id);
    }
  }
}

// ---- furnaces ---------------------------------------------------------------------------------
float fuelValue(const std::string& key) { auto* d = reg().item(key); return d ? (float)d->num("fuel", 0) : 0; }
static bool oreLike(const std::string& k, const std::string& out) {
  static const std::regex a("_ore$|_ore_piece$|^crushed_|^raw_|_blend$|_dust$|^ancient_debris$"), b("_ingot$|_shard$|_nugget$");
  return std::regex_search(k, a) || std::regex_search(out, b);
}
std::string smeltResult(const std::string& in, const std::string& kind) {
  if (recipeGate().immersive) {   // sim/immersive.h: metal comes from vessels, the forge and the bloomery, not the furnace
    if (in == "steel_blend") return kind == "blast" ? "pig_iron" : "";   // pig iron, and only from the blast furnace
    if (!metalOfOreBlock(in).empty() || unitsOf(in) > 0) return "";
  }
  auto it = reg().smelting.find(in);
  if (it == reg().smelting.end()) return "";
  const std::string& out = it->second;
  if (kind == "smoker" || kind == "blast") {
    static const std::unordered_set<std::string> FOOD = {"raw_meat", "venison", "rabbit", "potato", "kelp", "flour", "beef", "porkchop", "chicken", "mutton"};
    static const std::unordered_set<std::string> ORE = {"iron_ore", "gold_ore", "copper_ore", "tin_ore", "crushed_iron", "crushed_gold", "crushed_copper", "crushed_tin", "cinderite_ore", "quartz_ore", "nether_gold_ore", "redstone_ore", "ancient_debris"};
    auto* od = reg().item(out);
    bool food = FOOD.count(in) || (od && od->has("food"));          // itemfix.js: every raw food smokes
    bool ore = ORE.count(in) || (!food && oreLike(in, out));         // and every ore blasts
    if (kind == "smoker" && !food) return "";
    if (kind == "blast" && !ore) return "";
  }
  return out;
}
bool tickFurnace(json& e, double dt, const std::string& kind) {
  if (!e.contains("items") || !e["items"].is_array()) e["items"] = json::array({nullptr, nullptr, nullptr});
  json& items = e["items"];
  while (items.size() < 3) items.push_back(nullptr);
  // a hands-on furnace (sim/hands.h) has four spots to cook on and four for what's done: slots 0, 3, 4, 5 and 2, 6, 7, 8
  // (the classic three — input, fuel, output — are the first of each, so the screen and hoppers see what they always did)
  const bool multi = items.size() >= 9;
  static const int IN[4] = {0, 3, 4, 5}, OUT[4] = {2, 6, 7, 8};
  const int spots = multi ? 4 : 1;
  ItemStack fuel = ItemStack::fromJson(items[1]);
  if (kind == "smoker" || kind == "blast") dt *= 2;
  // what each spot makes, and whether there's room for it among what's done
  std::string result[4]; bool can[4] = {false, false, false, false}; int active = 0;
  auto room = [&](const std::string& r) {
    for (int k = 0; k < spots; k++) { const ItemStack o = ItemStack::fromJson(items[OUT[k]]); if (o.empty() || (o.id == r && o.n < maxStackOf(r))) return true; }
    return false;
  };
  for (int k = 0; k < spots; k++) {
    const ItemStack in = ItemStack::fromJson(items[IN[k]]);
    result[k] = in.empty() ? "" : smeltResult(in.id, kind);
    can[k] = !result[k].empty() && room(result[k]);
    active += can[k];
  }
  bool changed = false;
  double burn = e.value("burn", 0.0);
  json cooks = e.contains("cooks") && e["cooks"].is_array() ? e["cooks"] : json::array({0.0, 0.0, 0.0, 0.0});
  while (cooks.size() < 4) cooks.push_back(0.0);
  cooks[0] = e.value("cook", 0.0);
  if (burn <= 0 && active > 0 && !fuel.empty() && fuelValue(fuel.id) > 0) {
    burn = fuelValue(fuel.id) * SMELT_TIME; e["burnMax"] = burn; e["burnId"] = fuel.id;   // (what burns: drawn in the fire, burning down: render/furnace_layer.cpp)
    if (fuel.id == "lava_bucket") items[1] = ItemStack("bucket", 1).toJson();
    else { fuel.n--; items[1] = fuel.n <= 0 ? json() : fuel.toJson(); }
    changed = true;
  }
  if (burn > 0) {
    burn = std::max(0.0, burn - dt * std::max(1, active));   // (hands-on: the more cooking, the faster the fire goes)
    for (int k = 0; k < spots; k++) {
      double cook = cooks[k].get<double>();
      if (!can[k]) { cooks[k] = 0.0; continue; }
      const double was = cook;
      cook += dt;
      // the stages the furnace's front shows (render/furnace_layer.cpp: ore glowing, food turned cooked) reach players too
      if (k == 0) for (double at : {SMELT_TIME * 0.3, SMELT_TIME * 0.55}) if (was < at && cook >= at) changed = true;
      if (cook >= SMELT_TIME && room(result[k])) {
        cook = 0;
        ItemStack in = ItemStack::fromJson(items[IN[k]]); in.n--; items[IN[k]] = in.n <= 0 ? json() : in.toJson();
        for (int o = 0; o < spots; o++) {   // (the hands-on placeOneOutput: onto the same, else the first free)
          ItemStack out = ItemStack::fromJson(items[OUT[o]]);
          if (!out.empty() && !(out.id == result[k] && out.n < maxStackOf(result[k]))) continue;
          if (out.empty()) { bool same = false; for (int q = o + 1; q < spots; q++) { const ItemStack x = ItemStack::fromJson(items[OUT[q]]); if (x.id == result[k] && x.n < maxStackOf(result[k])) { same = true; break; } } if (same) continue; }
          if (out.empty()) items[OUT[o]] = ItemStack(result[k], 1).toJson(); else { out.n++; items[OUT[o]] = out.toJson(); }
          break;
        }
        changed = true;
      }
      cooks[k] = cook;
    }
    if (burn <= 0) changed = true;
  } else for (int k = 0; k < spots; k++) { const double c = cooks[k].get<double>(); if (c > 0) cooks[k] = std::max(0.0, c - dt * 2); }
  e["burn"] = burn; e["cook"] = cooks[0];
  if (multi) e["cooks"] = cooks;
  return changed;
}
void tickFurnaces(Level& lv, double dt) {
  double& acc = levelSlot<double>(lv, "furnaceAcc");
  acc += dt;
  if (acc <= 0.25) return;
  double step = acc; acc = 0;
  const Registry& R = reg();
  for (auto& [r, wp] : lv.allRealms()) {
    World& w = *wp;
    std::vector<std::tuple<int, int, int, BlockId, BlockId, u8>> swaps;
    std::vector<ivec3> changed;
    for (auto& [k, c] : w.chunks()) {
      if (c->blockEntities.empty()) continue;
      for (auto& [i, e] : c->blockEntities) {
        if (!e.is_object() || e.value("type", "") != "furnace") continue;
        int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
        BlockId id = c->get(lx, ly, lz);
        const BlockInfo& bi = binfo(id);
        if (bi.furnace.empty()) continue;
        int x = c->worldX() + lx, y = c->baseY() + ly, z = c->worldZ() + lz;
        recipeGate().immersive = w.immersive;
        if (bi.furnace == "trommel" ? tickTrommel(lv, w, r, e, step, ivec3(x, y, z), c->getMeta(lx, ly, lz)) : tickFurnace(e, step, bi.furnace)) changed.push_back({x, y, z});
        bool lit = e.value("burn", 0.0) > 0;
        const BlockDef& d = R.block(id);
        std::string unlitKey = d.str("unlit", d.key == "lit_furnace" ? "furnace" : d.key);
        std::string litKey = unlitKey == "furnace" ? "lit_furnace" : "lit_" + unlitKey;
        BlockId unlit = R.blockId(unlitKey), litId = R.blockId(litKey);
        if (lit && id == unlit && litId) swaps.push_back({x, y, z, id, litId, c->getMeta(lx, ly, lz)});
        else if (!lit && id == litId && unlit) swaps.push_back({x, y, z, id, unlit, c->getMeta(lx, ly, lz)});
      }
    }
    // keep the block entity: NoNotify keeps it (and skips the fluid/support reactions, like noFluid)
    for (auto& [x, y, z, from, to, m] : swaps) {   // the block entity goes along (setBlock drops it when the id changes)
      json* be = w.blockEntity(x, y, z); json keep = be ? *be : json();
      QuietEdits q;
      w.setBlock(x, y, z, to, m);
      if (!keep.is_null()) w.setBlockEntity(x, y, z, std::move(keep));
    }
    for (auto& c : changed) w.blockEntityChanged(c.x, c.y, c.z);   // saved, and synced to players (net/)
  }
}

// ---- crowd: bodies don't share space ------------------------------------------------------------
void crowdPush(Level& lv, float dt) {
  if (dt <= 0) return;
  struct Body { Entity* e; float r, h, mass; bool player; };
  std::vector<Body> bodies;
  for (auto& ep : lv.entities()) {
    Entity* e = ep.get();
    if (e->removed || e->ghost) continue;
    if (e->isPlayer()) {
      Player* p = static_cast<Player*>(e);
      if (p->dead || p->riding || p->sleeping || (p->flying && p->creative())) continue;
      bodies.push_back({e, 0.3f, p->sneaking ? 1.5f : 1.8f, 0.65f, true});
    } else if (isMob(*e) && e->hp > 0 && !e->riding && !e->data.value("noPush", false) && !e->data.contains("riddenBy")) {
      float r = e->w > 0 ? e->w : 0.3f, h = e->h > 0 ? e->h : 1;
      bodies.push_back({e, r, h, std::max(0.05f, r * r * h * 4), false});
    }
  }
  if (bodies.size() < 2) return;
  for (size_t i = 0; i < bodies.size(); i++) for (size_t j = i + 1; j < bodies.size(); j++) {
    Body& b = bodies[i]; Body& o = bodies[j];
    if (b.player && o.player) continue;
    Entity* a = b.e; Entity* c = o.e;
    if (a->realm != c->realm) continue;
    if (std::fabs(a->pos.x - c->pos.x) > 2.5f || std::fabs(a->pos.z - c->pos.z) > 2.5f) continue;
    if (a->pos.y + b.h <= c->pos.y + 0.05f || c->pos.y + o.h <= a->pos.y + 0.05f) continue;   // one above the other
    float dx = c->pos.x - a->pos.x, dz = c->pos.z - a->pos.z, d = std::hypot(dx, dz);
    float mn = (b.r + o.r) * 0.92f;
    if (d >= mn) continue;
    float depth = (mn - d) / mn;
    if (d < 0.001f) { float ang = (float)rnd() * TAU; dx = std::cos(ang); dz = std::sin(ang); d = 1; }
    float nx = dx / d, nz = dz / d;
    float tot = b.mass + o.mass, sa = o.mass / tot, sc = b.mass / tot;
    float dv = std::min(6.f, depth * 30) * dt;
    float shift = std::min(0.15f, depth * mn * 0.5f) * std::min(1.f, dt * 20);
    World& w = lv.world(a->realm);
    auto nudge = [&](Body& bb, float ux, float uz, float amt, float v) {
      Entity* e = bb.e;
      if (e->data.value("pinned", false)) return;
      e->vel.x += ux * v; e->vel.z += uz * v;
      if (!bb.player && amt > 0) {
        float x = e->pos.x + ux * amt, z = e->pos.z + uz * amt, r = bb.r;   // (its whole width: shoved a tenth into a fence post it had climbed it)
        if (!collides(w, x - r, e->pos.y + 0.02, z - r, x + r, e->pos.y + std::min(bb.h, 1.8f) - 0.02, z + r, true)) { e->pos.x = x; e->pos.z = z; }   // (a creature never shoved over a pen's posts)
      }
    };
    nudge(b, -nx, -nz, shift * sa, dv * sa * (b.player ? 0.6f : 1));
    nudge(o, nx, nz, shift * sc, dv * sc * (o.player ? 0.6f : 1));
  }
}

} // namespace dq
