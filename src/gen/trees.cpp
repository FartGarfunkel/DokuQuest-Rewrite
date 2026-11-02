// gen/trees.cpp — trees (world/worldgen.js treesForChunk, treeParts, writeTree), exact ports.
#include "gen/overworld.h"
#include <functional>

namespace dq {

static const char* TREE_NAMES[TREE_KIND_COUNT] = {"none", "oak", "birch", "spruce", "pine", "jungle", "aspen", "azalea",
  "acacia", "dark_oak", "cherry", "mangrove", "crimson", "warped", "thorn", "palm"};
const char* treeKindName(u8 k) { return k < TREE_KIND_COUNT ? TREE_NAMES[k] : "none"; }
u8 treeKindFromName(const std::string& s) { for (u8 k = 1; k < TREE_KIND_COUNT; k++) if (s == TREE_NAMES[k]) return k; return TREE_OAK; }

void treeParts(const GenBlocks& B, i32 seed, const TreeSpec& t, std::vector<TreePart>& out) {
  Mulberry32 rnd(hash32(t.x, t.ySeed, t.z, seed ^ 77));
  auto put = [&](double x, double y, double z, BlockId id, bool onlyAir = false, int meta = -1) { out.push_back({x, y, z, id, onlyAir, meta}); };
  auto leafBall = [&](double cx, double cy, double cz, double r, BlockId id, double squash) {
    int ry = (int)std::ceil(r * squash), rr = (int)std::ceil(r);
    double den = (r * squash) * (r * squash); if (den == 0) den = 1;
    for (int dy = -ry; dy <= ry; dy++) for (int dz = -rr; dz <= rr; dz++) for (int dx = -rr; dx <= rr; dx++) {
      double d = (dx * dx + dz * dz) / (r * r) + (dy * dy) / den;
      if (d <= 1 - rnd() * 0.18) put(cx + dx, cy + dy, cz + dz, id, true);
    }
  };
  // a branch (the idea is Cubyz's, the code ours): slim branch blocks (world/shapes.cpp ShapeKind::Branch, they join
  // to logs, branches and leaves) winding out of the trunk, climbing a step every other block, a diagonal going x then
  // z; thick near the trunk and thin toward the tip (meta bit 0); now and then a leaf along the way; at the end it may
  // fork into two twigs; each tip vanishes into a clump of leaves (none on a bare tree: leafR 0). With no branch
  // block for that wood it falls back to sideways logs.
  static const int BX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, BZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  auto branchOf = [&](BlockId lg) -> BlockId {
    if (lg == B.log) return B.oak_branch; if (lg == B.birch_log) return B.birch_branch; if (lg == B.dark_oak_log) return B.dark_oak_branch;
    if (lg == B.jungle_log) return B.jungle_branch; if (lg == B.cherry_log) return B.cherry_branch; if (lg == B.acacia_log) return B.acacia_branch;
    if (lg == B.mangrove_log) return B.mangrove_branch;
    return 0;
  };
  std::function<void(int, int, int, int, int, BlockId, BlockId, double, bool, int)> grow;
  grow = [&](int bx, int by, int bz, int d, int n, BlockId lg, BlockId lf, double leafR, bool canFork, int done) {
    const BlockId br = branchOf(lg);
    const int dx = BX[d & 7], dz = BZ[d & 7];
    bool zTurn = rnd() < 0.5;
    for (int k = 0; k < n; k++) {
      int mx = dx, mz = dz;
      if (dx && dz) { if (zTurn) mx = 0; else mz = 0; zTurn = !zTurn; }
      bx += mx; bz += mz;
      const int thin = done + k >= 2 ? 1 : 0;
      if (br) put(bx, by, bz, br, false, thin); else put(bx, by, bz, lg, false, mx ? 1 : 2);
      if (br && thin && leafR > 0 && rnd() < 0.3) put(bx, by + 1, bz, lf, true);   // a leaf along the way
      if (k + 1 < n && (k % 2 == 1 || rnd() < 0.3)) { by++; if (br) put(bx, by, bz, br, false, thin); else put(bx, by, bz, lg, false, 0); }
    }
    if (canFork && n >= 3 && rnd() < 0.55) {
      grow(bx, by, bz, d + 1, 2, lg, lf, leafR * 0.85, false, done + n);
      grow(bx, by, bz, d + 7, 2, lg, lf, leafR * 0.85, false, done + n);
      return;
    }
    if (leafR > 0) leafBall(bx, by + 1, bz, leafR, lf, 0.6);
  };
  auto branch = [&](int bx, int by, int bz, int d, int n, BlockId lg, BlockId lf, double leafR, bool canFork) { grow(bx, by, bz, d, n, lg, lf, leafR, canFork, 0); };
  const int x = t.x, y = t.y, z = t.z;
  switch (t.kind) {
    case TREE_SPRUCE: {
      int H = 6 + jsFloor(t.h * 4);
      for (int k = 0; k < H; k++) put(x, y + k, z, B.spruce_log);
      if (t.bare) return;
      int r = 0;
      for (int yy = H; yy >= 2; yy--) {
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
          if (std::abs(dx) + std::abs(dz) > r + 1) continue;
          if (dx || dz || yy >= H) put(x + dx, y + yy, z + dz, B.spruce_leaves, true);
        }
        r = r >= 2 ? 1 : r + 1;
        if (yy == H) r = 1;
      }
      put(x, y + H, z, B.spruce_leaves, true); put(x, y + H + 1, z, B.spruce_leaves, true);
      return;
    }
    case TREE_JUNGLE: {
      if (t.h > 0.93 && !t.bare) {   // now and then a giant of the rainforest: a thick trunk far above the canopy, great limbs
        const int H = 18 + jsFloor(rnd() * 10);
        static const int AX[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        for (int k = 0; k < H; k++) for (auto& a : AX) put(x + a[0], y + k, z + a[1], B.jungle_log);
        for (int b = 0, nb = 4 + jsFloor(rnd() * 3), d0 = jsFloor(rnd() * 8); b < nb; b++) {
          const int d = (d0 + b * 3) & 7;
          branch(x + (BX[d] > 0 ? 1 : 0), y + jsFloor(H * (0.45 + 0.45 * b / nb)), z + (BZ[d] > 0 ? 1 : 0), d, 4 + jsFloor(rnd() * 3), B.jungle_log, B.jungle_leaves, 2.1, true);
        }
        leafBall(x + 0.5, y + H, z + 0.5, 4.2, B.jungle_leaves, 0.5);
        for (int d = 0; d < 4; d++) for (int k = 1; k < H - 3; k++) if (rnd() < 0.3) {   // vines down the sides of the trunk
          const int ox = DIRX[d] > 0 ? 2 : DIRX[d] < 0 ? -1 : (k & 1), oz = DIRZ[d] > 0 ? 2 : DIRZ[d] < 0 ? -1 : (k & 1);
          put(x + ox, y + k, z + oz, B.vine, true, (d + 2) & 3);
        }
        return;
      }
      if (B.cacao_leaves && (hash32(t.x, t.ySeed, t.z, seed ^ 911) & 1023) < 72 && !t.bare) {   // now and then a cacao tree (BTA): short, cocoa all up the trunk
        int Hc = 5 + jsFloor(t.h * 3);
        for (int k = 0; k < Hc; k++) put(x, y + k, z, B.jungle_log);
        leafBall(x, y + Hc - 1, z, 2.4, B.cacao_leaves, 0.75);
        for (int d = 0; d < 4; d++) for (int k = 1; k < Hc - 2; k++) if (rnd() < 0.5) put(x + DIRX[d], y + k, z + DIRZ[d], B.cocoa, true, (((d + 2) & 3) << 4) | jsFloor(rnd() * 3));
        return;
      }
      int H = 8 + jsFloor(t.h * 6);
      for (int k = 0; k < H; k++) put(x, y + k, z, B.jungle_log);
      if (t.bare) return;
      for (int b = 0, nb = 2 + jsFloor(rnd() * 3), d0 = jsFloor(rnd() * 8); b < nb; b++)
        branch(x, y + jsFloor(H * (0.5 + 0.4 * b / nb)), z, d0 + b * 3, 3 + jsFloor(rnd() * 3), B.jungle_log, B.jungle_leaves, 1.7, true);
      leafBall(x, y + H, z, 3.2, B.jungle_leaves, 0.55);
      for (int d = 0; d < 4; d++) for (int k = 1; k < H - 2; k++) {
        double q = rnd();
        if (q < 0.35) put(x + DIRX[d], y + k, z + DIRZ[d], B.vine, true);
        else if (q > 0.93 && k > 2) put(x + DIRX[d], y + k, z + DIRZ[d], B.cocoa, true, (((d + 2) & 3) << 4) | jsFloor(rnd() * 3));
      }
      return;
    }
    case TREE_PINE: {
      int H = 9 + jsFloor(t.h * 5);
      for (int k = 0; k < H; k++) put(x, y + k, z, B.pine_log);
      if (t.bare) return;
      for (int ly = H - 6; ly <= H; ly++) {
        int r = ly == H ? 0 : (H - ly) % 2 == 1 ? 2 : 1;
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
          if (std::abs(dx) + std::abs(dz) > r + (r > 1 ? 1 : 0)) continue;
          if (dx == 0 && dz == 0 && ly < H) continue;
          put(x + dx, y + ly, z + dz, B.pine_needles, true);
        }
      }
      put(x, y + H, z, B.pine_needles, true); put(x, y + H + 1, z, B.pine_needles, true);
      return;
    }
    case TREE_ASPEN: {
      int H = 7 + jsFloor(t.h * 4);
      for (int k = 0; k < H; k++) put(x, y + k, z, B.aspen_log);
      if (t.bare) return;
      leafBall(x, y + H - 1, z, 2.1, B.aspen_leaves, 1.3);
      return;
    }
    case TREE_AZALEA: {
      int H = 3 + jsFloor(t.h * 2);
      for (int k = 0; k < H; k++) put(x + (k == H - 1 && t.h > 0.5 ? 1 : 0), y + k, z, B.log);
      if (t.bare) return;
      const double balls[2][4] = {{(double)x, (double)(y + H), (double)z, 2.2}, {(double)(x + 1), (double)(y + H - 1), (double)(z + 1), 1.6}};
      for (const auto& bl : balls) {
        for (int dy = -1; dy <= 1; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
          if (dx * dx + dz * dz + dy * dy * 2 > bl[3] * bl[3]) continue;
          put(bl[0] + dx, bl[1] + dy, bl[2] + dz, rnd() < 0.3 ? B.flowering_azalea_leaves : B.azalea_leaves, true);
        }
      }
      return;
    }
    case TREE_ACACIA: {
      int H = 4 + jsFloor(t.h * 2), d = jsFloor(rnd() * 4);
      int tx = x, tz = z;
      for (int k = 0; k < H; k++) { if (k >= H - 2) { tx += DIRX[d]; tz += DIRZ[d]; } put(tx, y + k, tz, B.acacia_log); }
      if (t.bare) return;
      for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++)
        if (std::abs(dx) + std::abs(dz) <= 4 && !(std::abs(dx) == 3 && std::abs(dz) == 3)) put(tx + dx, y + H, tz + dz, B.acacia_leaves, true);
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) put(tx + dx, y + H + 1, tz + dz, B.acacia_leaves, true);
      return;
    }
    case TREE_THORN: {   // low and crooked, a flat ragged crown of thorn leaves
      int H = 2 + jsFloor(t.h * 2), d = jsFloor(rnd() * 4), tx = x, tz = z;
      for (int k = 0; k < H; k++) { if (k == H - 1) { tx += DIRX[d]; tz += DIRZ[d]; } put(tx, y + k, tz, B.acacia_log); }
      if (t.bare) return;
      BlockId lf = B.thorn_leaves ? B.thorn_leaves : B.acacia_leaves;
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++)
        if (std::abs(dx) + std::abs(dz) <= 3 && rnd() < 0.8) put(tx + dx, y + H, tz + dz, lf, true);
      put(tx, y + H + 1, tz, lf, true);
      return;
    }
    case TREE_PALM: {   // a ringed trunk leaning one way, a crown of fronds drooping all round
      BlockId lg = B.palm_log ? B.palm_log : B.jungle_log, lf = B.palm_leaves ? B.palm_leaves : B.jungle_leaves;
      int H = 5 + jsFloor(t.h * 4), d = jsFloor(rnd() * 4), tx = x, tz = z;
      for (int k = 0; k < H; k++) { if (k >= 2 && k % 3 == 2) { tx += DIRX[d]; tz += DIRZ[d]; } put(tx, y + k, tz, lg); }
      if (t.bare) return;
      const int top = y + H;
      put(tx, top, tz, lf, true);
      for (int a = 0; a < 8; a++) {   // eight fronds: out two, then down
        static const int FX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, FZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const int len = 3 + (rnd() < 0.5 ? 1 : 0);
        for (int k = 1; k <= len; k++) put(tx + FX[a] * k, top - (k >= 3 ? 1 : 0) - (k >= 4 ? 1 : 0), tz + FZ[a] * k, lf, true);
      }
      return;
    }
    case TREE_DARK_OAK: {
      int H = 6 + jsFloor(t.h * 3);
      static const int AX[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
      for (int k = 0; k < H; k++) for (auto& a : AX) put(x + a[0], y + k, z + a[1], B.dark_oak_log);
      for (int b = 0, nb = 3 + jsFloor(rnd() * 2), d0 = jsFloor(rnd() * 8); b < nb; b++) {
        const int d = (d0 + b * 2 + jsFloor(rnd() * 2)) & 7;
        branch(x + (BX[d] > 0 ? 1 : 0), y + H - 3 + jsFloor(rnd() * 2), z + (BZ[d] > 0 ? 1 : 0), d, 3 + jsFloor(rnd() * 2), B.dark_oak_log, B.dark_oak_leaves, t.bare ? 0 : 1.9, true);
      }
      if (t.bare) return;
      leafBall(x + 0.5, y + H - 1, z + 0.5, 3.6, B.dark_oak_leaves, 0.5);
      leafBall(x + 0.5, y + H + 0.5, z + 0.5, 2.4, B.dark_oak_leaves, 0.5);
      return;
    }
    case TREE_CHERRY: {
      int H = 4 + jsFloor(t.h * 2);
      for (int k = 0; k < H; k++) put(x, y + k, z, B.cherry_log);
      if (t.bare) return;
      for (int b = 0, nb = 2 + jsFloor(rnd() * 2), d0 = jsFloor(rnd() * 8); b < nb; b++)
        branch(x, y + H - 2 + (b & 1), z, d0 + b * (8 / nb), 3 + jsFloor(rnd() * 2), B.cherry_log, B.cherry_leaves, 2.0, true);
      leafBall(x, y + H + 1, z, 2.2, B.cherry_leaves, 0.6);
      return;
    }
    case TREE_MANGROVE: {
      int H = 5 + jsFloor(t.h * 3);
      for (int k = 0; k < H; k++) put(x, y + k, z, B.mangrove_log);
      for (int d = 0; d < 4; d++) if (rnd() < 0.8) {
        put(x + DIRX[d], y + 1, z + DIRZ[d], B.mangrove_log); put(x + DIRX[d] * 2, y, z + DIRZ[d] * 2, B.mangrove_log); put(x + DIRX[d] * 2, y - 1, z + DIRZ[d] * 2, B.mangrove_log);
      }
      if (t.bare) return;
      leafBall(x, y + H, z, 2.8, B.mangrove_leaves, 0.6);
      return;
    }
    case TREE_CRIMSON: case TREE_WARPED: {
      bool crim = t.kind == TREE_CRIMSON;
      BlockId stem = crim ? B.crimson_stem : B.warped_stem, cap = crim ? B.nether_wart_block : B.warped_wart_block;
      BlockId glow = B.shroomlight ? B.shroomlight : cap;
      int H = 4 + jsFloor(t.h * 4);
      for (int k = 0; k < H; k++) put(x, y + k, z, stem);
      for (int dy = -2; dy <= 0; dy++) {
        int r = 2 + (dy < 0 ? 0 : -1);
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++)
          if (dy == 0 || std::abs(dx) == r || std::abs(dz) == r) put(x + dx, y + H + dy, z + dz, rnd() < 0.08 ? glow : cap, true);
      }
      if (crim) for (int k = 0; k < 4; k++) {
        int dx = jsFloor(rnd() * 5) - 2, dz = jsFloor(rnd() * 5) - 2;
        if (std::abs(dx) < 2 && std::abs(dz) < 2) continue;
        int len = 1 + jsFloor(rnd() * 3);
        for (int j = 1; j <= len; j++) put(x + dx, y + H - 2 - j, z + dz, B.weeping_vines, true);
      }
      return;
    }
    default: break;
  }
  // oak & birch (big oaks now and then)
  bool birch = t.kind == TREE_BIRCH;
  BlockId lg = birch ? B.birch_log : B.log, leaf = birch ? B.birch_leaves : B.leaves;
  bool big = t.kind == TREE_OAK && t.h > 0.78;
  int H = big ? 7 + jsFloor(rnd() * 4) : (birch ? 5 : 4) + jsFloor(t.h * 3);
  for (int k = 0; k < H; k++) put(x, y + k, z, lg);
  if (big) {   // a great spreading oak: limbs all round from halfway up, forking, a clump on every tip
    for (int b = 0, nb = 3 + jsFloor(rnd() * 3), d0 = jsFloor(rnd() * 8); b < nb; b++)
      branch(x, y + H / 2 + jsFloor((H / 2 - 1) * (b + rnd()) / nb), z, d0 + b * 3, 3 + jsFloor(rnd() * 3), lg, leaf, t.bare ? 0 : 1.7, true);
    if (t.bare) return;
    leafBall(x, y + H, z, 2.7, leaf, 0.8);
    return;
  }
  // an ordinary oak or birch: now and then a limb or two under the crown
  if (t.h > (birch ? 0.75 : 0.5))
    for (int b = 0, nb = 1 + (rnd() < 0.5 ? 1 : 0), d0 = jsFloor(rnd() * 8); b < nb; b++)
      branch(x, y + H - 3 - b, z, d0 + b * 4, 2, lg, leaf, t.bare ? 0 : 1.4, false);
  if (t.bare) return;
  if (t.bees) { int d = jsFloor(rnd() * 4); put(x + DIRX[d], y + H - 4, z + DIRZ[d], B.bee_nest, true, d | 0x0c); }
  for (int ly = H - 3; ly <= H + 1; ly++) {
    int r = ly >= H ? 1 : 2;
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      if (std::abs(dx) == r && std::abs(dz) == r && (ly == H + 1 || hashFloat(x + dx, ly, z + dz, seed) < 0.5)) continue;
      if (dx == 0 && dz == 0 && ly < H) continue;
      put(x + dx, y + ly, z + dz, leaf, true);
    }
  }
}

void writeTreeParts(const GenBlocks& B, GenBuf& b, int cx, int cz, const TreeSpec& t, const std::vector<TreePart>& parts) {
  const int bx = cx * 16, bz = cz * 16;
  for (const TreePart& p : parts) {
    // the original indexes with |0 truncation: half-block leaf centres land on the block below/west, and a
    // fraction just outside the chunk on either side is dropped (same rule as the JS, so borders match it)
    double lx = p.x - bx, lz = p.z - bz;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || p.y < 1 || p.y >= DIM_H) continue;
    int i = colIdx(jsTrunc(lx), jsTrunc(p.y), jsTrunc(lz));
    BlockId cur = b.blocks[i];
    if (p.onlyAir && cur != AIR && cur != B.tall_grass && cur != B.fern) continue;
    if (p.id == B.vine) {
      if (cur != AIR) continue;
      double ddx = p.x - t.x, ddz = p.z - t.z;
      b.meta[i] = ddx == 1 ? 3 : ddx == -1 ? 1 : ddz == 1 ? 0 : 2;
    }
    if (!p.onlyAir && B.liquid[cur] && B.wlog[p.id]) b.meta[i] |= WLOG;
    BlockId id = p.id;
    if (t.mossy && ((id == B.dark_oak_log && B.mossy_dark_oak_log) || (id == B.mangrove_log && B.mossy_mangrove_log)) && hash32(jsTrunc(p.x), jsTrunc(p.y), jsTrunc(p.z), 77) % 100 < 45)
      id = id == B.dark_oak_log ? B.mossy_dark_oak_log : B.mossy_mangrove_log;
    if (t.gum && B.gum_log && id == B.birch_log) id = B.gum_log;
    if (t.gum && B.gum_leaves && id == B.birch_leaves) id = B.gum_leaves;
    if (t.seasonal && B.seasonal_leaves && id == B.cherry_leaves) id = B.seasonal_leaves;
    b.blocks[i] = id;
    if (p.meta >= 0) b.meta[i] = (u8)p.meta;
    if (B.axis[p.id] && p.meta < 0) b.meta[i] = 0;   // logs stand upright unless a branch turned them
  }
}

// ---- where the trees stand (WorldGen.treesForChunk, filtered off surface structures like structures.js) ----
void OverworldGen::treesForChunk(int cx, int cz, std::vector<TreeSpec>& list) const {
  using namespace vmap;
  Mulberry32 rnd(hash32(seed_, cx, cz, 3));
  OwColumn mid = column(cx * 16 + 8, cz * 16 + 8);
  bool jungle = mid.temp > 0.25 && mid.humid > 0.3, savanna = mid.biome == BIOME_PLAINS && mid.temp > 0.28 && mid.humid < 0.02;
  double density = mid.biome == BIOME_FOREST ? 7 : mid.biome == BIOME_PLAINS ? 0.6 : mid.biome == BIOME_TAIGA ? 4 : mid.biome == BIOME_MOUNTAINS ? 0.8 : 0;
  if (jungle) density = 9;
  if (savanna) density = 0.9;
  if (btaVariant(mid) == BV_MOSSMIRE) density = 3;   // a swampy wood
  if (btaVariant(mid) == BV_THORNSCRUB) density = 2.5;   // thick with thorn trees
  if (btaVariant(mid) == BV_HIGHLANDS) density = 6;      // close-grown spruce
  if (btaVariant(mid) == BV_SEASONAL) density = 5;       // the Seasonal Forest
  bool bare = (mid.biome == BIOME_FOREST || mid.biome == BIOME_PLAINS) && hashFloat(cx, cz, seed_, 67) < 0.025;
  int n = jsFloor(density) + (rnd() < std::fmod(density, 1.0) ? 1 : 0) + (bare ? 3 : 0);
  for (int i = 0; i < n; i++) {
    int x = cx * 16 + jsFloor(rnd() * 16), z = cz * 16 + jsFloor(rnd() * 16);
    OwColumn c = column(x, z);
    double r = rnd(), r2 = rnd();
    TreeSpec t; t.x = x; t.z = z; t.y = c.H + 1; t.ySeed = c.h + 1; t.h = r2;
    // palms on warm beaches
    if (c.biome == BIOME_BEACH && c.h > JS_SEA && c.temp > 0.22 && r < 0.3) { t.kind = TREE_PALM; list.push_back(t); continue; }
    if (c.h <= JS_SEA || c.biome == BIOME_DESERT || c.biome == BIOME_BEACH || c.biome == BIOME_OCEAN) {
      if (c.h >= JS_SEA - 2 && c.h <= JS_SEA && c.temp > 0.2 && c.humid > 0.25 && c.biome != BIOME_DESERT && r < 0.5) { t.kind = TREE_MANGROVE; list.push_back(t); }
      continue;
    }
    if (c.biome == BIOME_MOUNTAINS && c.h > 82) continue;
    const BtaVariant bv = btaVariant(c);
    if (bv == BV_GLACIER || (bv == BV_REDLANDS && r > 0.45) || (bv == BV_MEADOW && r > 0.3) || (bv == BV_TUNDRA && r > 0.08)) continue;   // open country
    u8 type = TREE_OAK;
    if (c.biome == BIOME_TAIGA || (c.biome == BIOME_MOUNTAINS && r < 0.7)) type = c.biome == BIOME_MOUNTAINS && c.humid > 0.15 && c.h < 78 && r < 0.25 ? TREE_CHERRY : r < 0.45 ? TREE_PINE : TREE_SPRUCE;
    else if (jungle) type = r < 0.7 ? TREE_JUNGLE : TREE_OAK;
    else if (savanna) type = TREE_ACACIA;
    else if (c.biome == BIOME_FOREST) type = r < 0.2 ? TREE_BIRCH : c.temp < 0 && r < 0.42 ? TREE_ASPEN : c.humid > 0.25 && r < 0.55 ? TREE_DARK_OAK : c.humid > 0.15 && r > 0.97 ? TREE_AZALEA : TREE_OAK;
    else if (c.biome == BIOME_PLAINS && c.temp < -0.05 && r < 0.4) type = TREE_ASPEN;
    if (bv == BV_REDLANDS) type = TREE_BIRCH;                              // pale-barked gums standing alone
    else if (bv == BV_MOSSMIRE) type = r < 0.6 ? TREE_DARK_OAK : TREE_MANGROVE;   // dark, mossy swamp trees
    else if (bv == BV_THORNSCRUB) type = r < 0.72 ? TREE_THORN : r < 0.86 ? TREE_PALM : TREE_ACACIA;   // dry thorn country, a palm here and there
    else if (bv == BV_HIGHLANDS) type = r < 0.85 ? TREE_SPRUCE : TREE_PINE;   // the boreal hills
    else if (bv == BV_TUNDRA) type = TREE_SPRUCE;                             // a lone stunted spruce
    else if (bv == BV_SEASONAL) type = r < 0.62 ? TREE_CHERRY : r < 0.85 ? TREE_OAK : TREE_BIRCH;   // the Seasonal Forest's cherry groves
    bool bees = (type == TREE_OAK || type == TREE_BIRCH || type == TREE_CHERRY) && !bare && rnd() < (bv == BV_MEADOW || bv == BV_SEASONAL ? 0.12 : c.biome == BIOME_PLAINS ? 0.08 : c.biome == BIOME_FOREST ? 0.04 : 0.02);
    t.kind = type; t.bare = bare; t.bees = bees; t.mossy = bv == BV_MOSSMIRE; t.gum = bv == BV_REDLANDS; t.seasonal = bv == BV_SEASONAL;
    list.push_back(t);
  }
  // no trees growing through the floors of surface structures
  if (structures.active()) list.erase(std::remove_if(list.begin(), list.end(), [&](const TreeSpec& t) { return structures.flagged(t.x, t.z, 3, SF_SURFACE); }), list.end());
}

// The original only checks the ground when the tree's base is in the chunk being made, so a tree on bad
// ground next to a border could leave its crown in the neighbour. Here every chunk asks the same
// question of the column itself (top block + caves), so both sides always agree.
bool OverworldGen::treeGroundOk(const TreeSpec& t) const {
  OwColumn c = column(t.x, t.z);
  if (t.y - 1 != c.H) return false;
  SurfaceMat m = surface(c, t.x, t.z);
  BlockId g = m.top;   // the carvers leave a planted tree's trunk column alone (gen/underground.cpp)
  bool soil = g == B.grass || g == B.snowy_grass || g == B.dirt || g == B.podzol || g == B.coarse_dirt || g == B.mud ||
              g == B.dirt_with_dry_grass || g == B.dirt_with_coniferous_litter || g == B.dirt_with_rainforest_litter;
  return soil || (t.kind == TREE_MANGROVE && (g == B.sand || g == B.clay || g == B.gravel || g == B.mud)) || (t.kind == TREE_PALM && (g == B.sand || g == B.red_sand));
}

void OverworldGen::placeTrees(GenBuf& b, int cx, int cz) const {
  std::vector<TreeSpec> list; std::vector<TreePart> parts;
  for (int ncz = cz - 1; ncz <= cz + 1; ncz++) for (int ncx = cx - 1; ncx <= cx + 1; ncx++) {
    list.clear(); treesForChunk(ncx, ncz, list);
    for (const TreeSpec& t : list) writeTree(b, cx, cz, t, parts);
  }
}

void OverworldGen::writeTree(GenBuf& b, int cx, int cz, const TreeSpec& t, std::vector<TreePart>& parts) const {
  // quick reject: no part of any tree reaches more than 6 blocks from its base
  if (t.x + 6 < cx * 16 || t.x - 6 > cx * 16 + 15 || t.z + 6 < cz * 16 || t.z - 6 > cz * 16 + 15) return;
  if (!treeGroundOk(t)) return;
  parts.clear(); treeParts(B, seed_, t, parts);
  writeTreeParts(B, b, cx, cz, t, parts);
}

void OverworldGen::trees(int cx, int cz, std::vector<TreeSample>& out) const {
  std::vector<TreeSpec> list; std::vector<TreePart> parts;
  treesForChunk(cx, cz, list);
  const Registry& R = reg();
  for (const TreeSpec& t : list) {
    if (!treeGroundOk(t)) continue;
    parts.clear(); treeParts(B, seed_, t, parts);
    TreeSample s{t.x, t.y, t.z, t.kind, t.bare, {}};
    for (const TreePart& p : parts) {
      if (!p.id) continue;
      bool leaves = R.tLeaves[p.id], logb = B.axis[p.id];
      if (!leaves && !logb) continue;
      if (t.bare && leaves) continue;
      int dx = jsTrunc(p.x) - t.x, dz = jsTrunc(p.z) - t.z, py = jsTrunc(p.y);
      TreeColumn* c = nullptr;
      for (auto& q : s.cols) if (q.dx == dx && q.dz == dz) { c = &q; break; }
      if (!c) { s.cols.push_back({(i8)dx, (i8)dz, -1, 999, 0, 0}); c = &s.cols.back(); }
      if (py + 1 > c->top) c->top = (i16)(py + 1);
      if (leaves) { if (py < c->leafBottom) c->leafBottom = (i16)py; if (!c->leaves) c->leaves = p.id; }
      else if (!c->log) c->log = p.id;
    }
    out.push_back(std::move(s));
  }
}

} // namespace dq
