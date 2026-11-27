// sim/dyntree.cpp — see dyntree.h (the growth signal, its growth logic, leaves branching out, the
// species' growth, the branch volume)
#include "sim/dyntree.h"
#include "sim/blockinfo.h"
#include "sim/rules.h"
#include "sim/gameplay.h"
#include "core/registry.h"
#include <cmath>
#include <map>

namespace dq {

namespace {
const ivec3 DIRS[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};   // down, up, north, south, west, east
constexpr int UP = 1, DOWN = 0;
constexpr float TAPER = 0.3f;
constexpr int MAX_R = 8, ENERGY = 16, LOWEST_BRANCH = 3, SECONDARY = 2;
constexpr int LEAF_ABOVE = 5;   // no new leaves, and no side branch, closer to the soil than this
u32 mix(u32 h) { h ^= h >> 16; h *= 0x7feb352d; h ^= h >> 15; h *= 0x846ca68b; h ^= h >> 16; return h; }

struct Signal {
  Level& lv; World& w; BlockId branch, leaves; u32 h; int energy = ENERGY, steps = 0; int dir = UP; float radius = 0; bool success = false, choked = false;
  ivec3 root;
  float rnd() { h = mix(h + 0x9e3779b9u); return (h & 0xFFFFFF) / 16777216.f; }
  BlockId at(ivec3 p) const { return w.getBlock(p.x, p.y, p.z); }
  bool ours(ivec3 p) const { return at(p) == branch && (w.getMeta(p.x, p.y, p.z) & DYN_BRANCH); }
  int rOf(ivec3 p) const { return dynRadius(w.getMeta(p.x, p.y, p.z)); }
  bool airy(ivec3 p) const { const BlockId b = at(p); return b == AIR || (reg().tReplaceable[b] && !reg().tLiquid[b]); }
  bool leafy(ivec3 p) const { return at(p) == leaves; }
  bool crown(ivec3 p) const { return p.y - root.y >= LEAF_ABOVE; }

  // leaves where there's air, and only up in the crown (the trunk stays bare)
  bool needLeaves(ivec3 p) {
    if (!crown(p)) return leafy(p);
    if (airy(p)) { w.setBlock(p.x, p.y, p.z, leaves, 0); return true; }
    return leafy(p);
  }
  // DynamicLeavesBlock.branchOut: a new twig where a leaf would grow, with no other branch beside it; leaves round it
  void branchOut(ivec3 p, int fromDir) {
    if (!airy(p) && !leafy(p)) { success = false; return; }
    for (int d = 0; d < 6; d++) if (d != (fromDir ^ 1) && ours(p + DIRS[d])) { success = false; return; }
    w.setBlock(p.x, p.y, p.z, branch, branchMeta(1));
    for (int d = 0; d < 6; d++) needLeaves(p + DIRS[d]);
    // (a full model would fill the canopy by its leaves' hydration cells; a small round cluster here does it at once)
    for (int dy = -1; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      const int r2 = dx * dx + dz * dz + (dy > 0 ? dy * dy * 2 : dy * dy * 3);
      if (r2 > 5 || (r2 >= 4 && rnd() < 0.4f)) continue;
      const ivec3 q = p + ivec3(dx, dy, dz);
      if (!crown(q) || !airy(q)) continue;
      w.setBlock(q.x, q.y, q.z, leaves, 0);
    }
    radius = SECONDARY; success = true;
  }
  // GrowthLogicKit.populateDirectionProbabilityMap + selectNewDirection
  int choose(ivec3 p, int originDir) {
    int prob[6] = {};
    prob[UP] = dir != DOWN ? 2 : 0;          // the species' up probability
    prob[dir] += 1;                          // the way it was going
    for (int d = 0; d < 6; d++) if (d != originDir) {
      const ivec3 q = p + DIRS[d];
      prob[d] += ours(q) ? rOf(q) + 2 : leafy(q) ? 2 : airy(q) ? 1 : 0;
    }
    prob[originDir] = 0;
    if (p.y - root.y <= LOWEST_BRANCH) for (int d = 0; d < 6; d++) if (d != UP) prob[d] = 0;   // the trunk: straight up at first
    for (int d = 0; d < 6; d++) if (d != UP && (p + DIRS[d]).y - root.y < LEAF_ABOVE) prob[d] = 0;   // never grow back down to the ground
    int total = 0; for (int v : prob) total += v;
    if (!total) return -1;
    int pick = (int)(rnd() * total);
    for (int d = 0; d < 6; d++) { if (pick < prob[d]) return d; pick -= prob[d]; }
    return UP;
  }
  // BasicBranchBlock.growSignal
  void grow(ivec3 p) {
    if (--energy < 0) return;   // (signal.step: out of energy)
    steps++;
    const int originDir = dir ^ 1;
    const int rHere = rOf(p);
    const int target = choose(p, originDir);
    if (target < 0) return;
    dir = target;
    const ivec3 q = p + DIRS[target];
    if (ours(q)) grow(q);
    else if (leafy(q)) branchOut(q, target);
    else if (airy(q)) {
      // up the trunk the branch grows on. A side twig below the crown only tries a leaf, and that leaf
      // is refused, so nothing grows down onto the trunk or the ground.
      if (rHere == 1 && !crown(q) && target != UP) success = needLeaves(q);
      else branchOut(q, target);
    }
    // the pipe model, back down the way it came
    float area = radius * radius;
    for (int d = 0; d < 6; d++) if (d != originDir && d != target) { const ivec3 n = p + DIRS[d]; if (ours(n)) { const int r = rOf(n); area += (float)(r * r); } }
    if (!choked) {
      radius = std::clamp(std::sqrt(area) + TAPER, (float)rHere, (float)MAX_R);
      const int want = (int)std::floor(radius);
      if (want > rHere) w.setBlock(p.x, p.y, p.z, branch, branchMeta(want));
    }
  }
};
}  // namespace

int branchRadius(BlockId id, u8 meta) {
  if (!bhas(id, BF::IsLog)) return 0;
  const BlockDef& b = reg().block(id);
  if (!b.has("branch")) return 8;
  if (meta & DYN_BRANCH) return dynRadius(meta);
  return (meta & 1) ? 3 : 4;
}

bool dynTreesOn(Level& lv) { return lv.info.genVersion >= 13 && rule(lv, "dynamicTrees"); }

bool plantDynTree(Level& lv, World& w, int x, int y, int z) {
  const Registry& R = reg();
  const BlockDef& sap = R.block(w.getBlock(x, y, z));
  const BlockId branch = R.blockId(sap.str("branchBlock")), rooty = R.blockId("rooty_dirt");
  if (!branch || !rooty) return false;
  const BlockId leaves = R.blockId(R.block(branch).str("leaves", "leaves"));
  const BlockId below = w.getBlock(x, y - 1, z);
  if (!R.tSolid[below] || R.tLiquid[below]) return false;
  w.setBlock(x, y - 1, z, rooty, 15);                 // rooty soil, full of life
  w.setBlock(x, y, z, branch, branchMeta(1));         // the sapling's twig
  auto leaf = [&](int a, int b, int c) { const BlockId k = w.getBlock(a, b, c); if (k == AIR || (R.tReplaceable[k] && !R.tLiquid[k])) w.setBlock(a, b, c, leaves, 0); };
  leaf(x, y + 1, z);                                  // one leaf on the twig; none down at the ground beside it
  return true;
}

bool growDynTree(Level& lv, World& w, int x, int y, int z, u32 seed) {
  const Registry& R = reg();
  // only with the land round it there (BlockRooty: isSurroundedByLoadedChunks)
  for (int dx : {-8, 8}) for (int dz : {-8, 8}) if (!w.isLoaded(x + dx, y, z + dz)) return false;
  const u8 meta = w.getMeta(x, y, z);
  const BlockId trunk = w.getBlock(x, y + 1, z);
  if (!R.block(trunk).has("dynTree") || !(w.getMeta(x, y + 1, z) & DYN_BRANCH)) {   // no tree on it: dirt again
    w.setBlock(x, y, z, R.blockId("dirt"), 0);
    return false;
  }
  const int life = meta & 15;
  if (life <= 0) return false;
  Signal s{lv, w, trunk, R.blockId(R.block(trunk).str("leaves", "leaves")), mix(seed ^ 0xD7EEu)};
  s.root = ivec3(x, y, z);
  s.dir = UP;
  s.grow(ivec3(x, y + 1, z));
  // leaves that an earlier growth left down the trunk come off (the crown stays at LEAF_ABOVE and up)
  for (int dy = 1; dy < LEAF_ABOVE; dy++) for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
    const int lx = x + dx, ly = y + dy, lz = z + dz;
    if (R.tLeaves[w.getBlock(lx, ly, lz)]) w.setBlock(lx, ly, lz, AIR, 0);
  }
  // the soil spends its life (Species.grow: soilLongevity)
  if (s.rnd() < (s.success ? 1.f / 8 : 1.f / 128)) w.setBlock(x, y, z, w.getBlock(x, y, z), (u8)((meta & ~15) | (life - 1)));
  // now and then a seed drops from the canopy (Species.seedDrop): it takes root where it lands (sim/gameplay.cpp)
  if (s.success && s.rnd() < 0.04f && !lv.isRemote()) {
    std::string sapKey;
    for (auto& b : R.blocks) if (b.str("branchBlock") == R.block(trunk).key) { sapKey = b.drop.empty() ? b.key : b.drop; break; }
    if (!sapKey.empty() && R.item(sapKey)) dropItem(lv, w.realm, ItemStack(sapKey, 1), vec3(x + 0.5f + (s.rnd() - 0.5f) * 6, y + 4.f + s.steps, z + 0.5f + (s.rnd() - 0.5f) * 6), true);
  }
  return s.success;
}

void dynTreeYield(const std::vector<std::pair<BlockId, u8>>& parts, std::map<std::string, int>& logs, int& sticks) {
  std::map<std::string, int> volume;   // voxels (a log 4096)
  for (auto& [id, meta] : parts) {
    const BlockDef& b = reg().block(id);
    const std::string lg = b.str("log", b.drop.empty() ? b.key : b.drop);
    const int r = branchRadius(id, meta);
    volume[lg] += r * r * 64;
  }
  int rest = 0;
  for (auto& [k, v] : volume) { if (v / 4096) logs[k] += v / 4096; rest += v % 4096; }
  sticks += rest * 8 / 4096;
}

} // namespace dq
