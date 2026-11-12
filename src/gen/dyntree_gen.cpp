// gen/dyntree_gen.cpp — terrain 13: the generated trees are grown trees (Dynamic Trees; sim/dyntree.h). Each tree's
// own shape (gen/trees.cpp treeParts) is kept; its wood becomes its wood's branch block with the radius Dynamic Trees'
// pipe model gives it (a tip 1; each block sqrt of the areas of the branches it carries + 0.3, at most 8), and it stands
// in rooty soil, so it goes on growing. Pure: the tree's parts alone decide it, so every chunk it crosses agrees.
#include "gen/trees.h"
#include "sim/dyntree.h"
#include "core/registry.h"
#include <cmath>
#include <map>
#include <unordered_map>

namespace dq {

void dynTreeParts(const TreeSpec& t, std::vector<TreePart>& parts) {
  switch (t.kind) {   // the single-trunk trees (the 2x2 and the odd ones keep their logs)
    case TREE_OAK: case TREE_BIRCH: case TREE_SPRUCE: case TREE_PINE: case TREE_ASPEN: case TREE_ACACIA: case TREE_CHERRY: break;
    default: return;
  }
  if (t.gum || t.mossy) return;
  const Registry& R = reg();
  static const std::unordered_map<BlockId, BlockId> branchOf = [] {
    std::unordered_map<BlockId, BlockId> m;
    for (auto& b : reg().blocks) if (b.has("dynTree")) { if (BlockId l = reg().blockId(b.str("log"))) m[l] = b.id; m[b.id] = b.id; }
    return m;
  }();
  static const BlockId rooty = R.blockId("rooty_dirt");
  if (!rooty || branchOf.empty()) return;
  auto key = [](int x, int y, int z) { return ((i64)(x & 0xFFFFF) << 40) | ((i64)(y & 0xFFFFF) << 20) | (i64)(z & 0xFFFFF); };
  // the wood: the last solid part at each place
  std::unordered_map<i64, size_t> wood;
  for (size_t i = 0; i < parts.size(); i++) {
    const TreePart& p = parts[i];
    const i64 k = key((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z));
    if (branchOf.count(p.id) && !p.onlyAir) wood[k] = i;
    else if (!p.onlyAir) wood.erase(k);
  }
  const i64 rootK = key(t.x, t.y, t.z);
  if (!wood.count(rootK)) return;
  // the tree from its foot: each block's parent, through faces only (a branch shape joins only its six sides)
  static const int D[6][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
  std::vector<ivec3> order{ivec3(t.x, t.y, t.z)};
  std::unordered_map<i64, i64> parent{{rootK, rootK}};
  for (size_t q = 0; q < order.size(); q++) {
    const ivec3 c = order[q];
    for (auto& d : D) {
      const ivec3 n(c.x + d[0], c.y + d[1], c.z + d[2]);
      const i64 nk = key(n.x, n.y, n.z);
      if (wood.count(nk) && !parent.count(nk)) { parent[nk] = key(c.x, c.y, c.z); order.push_back(n); }
    }
  }
  // the pipe model from the tips down. A generated tree has no twigs in its crown (Dynamic Trees' trees are full of
  // them, and they thicken it): the leaves round each block stand in for them, half a twig's area each
  std::unordered_map<i64, float> area;
  {
    std::unordered_map<i64, int> leafAt;
    for (const TreePart& p : parts) if (p.onlyAir || !branchOf.count(p.id)) if (R.tLeaves[p.id]) leafAt[key((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z))] = 1;
    for (const ivec3& c : order) {
      int n = 0;
      for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (leafAt.count(key(c.x + dx, c.y + dy, c.z + dz))) n++;
      if (n) area[key(c.x, c.y, c.z)] += n * 0.5f;
    }
  }
  std::unordered_map<i64, float> rad;
  for (size_t q = order.size(); q-- > 0;) {
    const ivec3 c = order[q]; const i64 k = key(c.x, c.y, c.z);
    const float a = area.count(k) ? area[k] : 0.f;
    const float r = a > 0 ? std::min(8.f, std::sqrt(a) + 0.3f) : 1.f;
    rad[k] = r;
    if (k != rootK) area[parent[k]] += r * r;
  }
  for (auto& [k, r] : rad) {
    TreePart& p = parts[wood[k]];
    p.id = branchOf.at(p.id); p.meta = branchMeta((int)std::floor(r));
  }
  parts.push_back({(double)t.x, (double)(t.y - 1), (double)t.z, rooty, false, 4});   // rooty soil, half its life left
}

} // namespace dq
