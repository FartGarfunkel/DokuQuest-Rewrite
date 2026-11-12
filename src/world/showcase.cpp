// world/showcase.cpp — the "showcase" world type: a flat Overworld with every block laid out on a
// grid (one per 3x3 cell, double-height plants and doors complete), plus groups that show shapes
// joining up (stairs corners, fences, walls, panes, slabs), every crop stage, a pool with a
// waterfall, a lava pool and a dark room lit by torches. For checking meshing, light and shading:
//   dokuquest --create Show --world-type showcase ...
// The grid starts at (0, 65, 0) (global y 577) and runs +x/+z; the groups lie at x < 0.
#include "gen/generator.h"
#include "world/shapes.h"

namespace dq {

namespace {
constexpr int FLOOR = 64;   // band-local y of the grass floor

class ShowcaseGen : public DimGenerator {
public:
  ShowcaseGen() {
    const Registry& R = reg();
    for (auto& b : R.blocks) if (b.id && b.render != Render::None) list.push_back(b.id);
    stone = R.blockId("stone"); grass = R.blockId("grass"); dirt = R.blockId("dirt"); water = R.blockId("water"); lava = R.blockId("lava");
    farmland = R.blockId("farmland"); torch = R.blockId("torch"); glass = R.blockId("glass"); planks = R.blockId("planks");
    stairs = R.blockId("oak_stairs"); fence = R.blockId("oak_fence"); wall = R.blockId("cobblestone_wall"); pane = R.blockId("glass_pane");
    slab = R.blockId("oak_slab"); gate = R.blockId("oak_fence_gate"); leaves = R.blockId("leaves"); log = R.blockId("log");
    for (auto& b : R.blocks) if (traits(b.id).crop && b.render == Render::Cross) crops.push_back(b.id);
  }
  // what stands at (x, band-local y, z)
  void at(int x, int y, int z, BlockId& id, u8& meta) const {
    id = AIR; meta = 0;
    if (y < FLOOR) { id = y == 0 ? stone : (y > FLOOR - 4 ? dirt : stone); return; }
    if (y == FLOOR) id = grass;
    // ---- the grid of every block
    if (x >= 0 && z >= 0 && x < 32 * 3 && (x % 3) == 0 && (z % 3) == 0) {
      size_t i = (size_t)(z / 3) * 32 + x / 3;
      if (i < list.size()) {
        BlockId b = list[i]; const BlockTraits& T = traits(b); const BlockDef& d = reg().block(b);
        if (y == FLOOR && T.crop && d.render == Render::Cross) { id = farmland; return; }
        if (y == FLOOR + 1) { id = b; meta = T.crop ? (u8)std::max<size_t>(T.stageTiles.size(), 1) - 1 : 0; if (d.render == Render::Liquid) meta = 0; return; }
        if (y == FLOOR + 2 && (T.dbl || T.door)) { id = b; meta = T.door ? 16 : 1; return; }
      }
      return;
    }
    if (x >= 0) return;
    // ---- groups at x < 0
    int gx = -x, gz = z;
    // crops: every stage of every crop, one crop per row
    if (gx >= 2 && gx < 2 + 9 && gz >= 0 && gz < (int)crops.size() * 2 && (gz % 2) == 0) {
      BlockId c = crops[gz / 2]; int stage = gx - 2; int n = (int)traits(c).stageTiles.size();
      if (stage < n) { if (y == FLOOR) id = farmland; else if (y == FLOOR + 1) { id = c; meta = (u8)stage; } }
      return;
    }
    // stairs: a ring of stairs making inner and outer corners
    if (gx >= 14 && gx < 19 && gz >= 0 && gz < 5 && y == FLOOR + 1) {
      int lx = gx - 14, lz = gz;
      if (lx == 0 || lx == 4 || lz == 0 || lz == 4) {
        id = stairs; meta = lz == 0 ? 0 : lz == 4 ? 2 : lx == 0 ? 1 : 3;
        if (lx == 0 && lz == 0) meta = 0; if (lx == 4 && lz == 4) meta = 2;
      }
      return;
    }
    // fences, walls and panes in lines with a corner and a post between
    if (gx >= 21 && gx < 28 && gz >= 0 && gz < 12 && y == FLOOR + 1) {
      int lx = gx - 21; int row = gz / 4, lz = gz % 4;
      BlockId k = row == 0 ? fence : row == 1 ? wall : pane;
      if ((lz == 0 && lx < 6) || (lx == 0 && lz < 3)) id = k;
      if (row == 0 && lz == 0 && lx == 6) { id = gate; meta = 1; }
      return;
    }
    // slabs: bottom, top, vertical against N/E/S/W, double; and a stack of the same
    if (gx >= 30 && gx < 44 && gz == 0 && y == FLOOR + 1 && (gx % 2) == 0) { id = slab; meta = (u8)((gx - 30) / 2); return; }
    // a pool with a waterfall pouring into it from a pillar
    if (gx >= 30 && gx < 40 && gz >= 4 && gz < 14) {
      int lx = gx - 30, lz = gz - 4;
      if (y == FLOOR && lx > 0 && lx < 9 && lz > 0 && lz < 9) { id = water; return; }
      if (y == FLOOR - 1 && lx > 0 && lx < 9 && lz > 0 && lz < 9) { id = water; return; }
      if (lx == 5 && lz == 0 && y > FLOOR && y <= FLOOR + 6) { id = stone; return; }
      if (lx == 5 && lz == 1 && y > FLOOR && y <= FLOOR + 6) { id = water; meta = y == FLOOR + 6 ? 0 : 8; return; }
      return;
    }
    // lava
    if (gx >= 42 && gx < 47 && gz >= 4 && gz < 9 && y == FLOOR) { id = lava; return; }
    // a dark stone room with a torch and a glowing block inside, a window on one side
    if (gx >= 50 && gx < 59 && gz >= 0 && gz < 9 && y > FLOOR && y <= FLOOR + 5) {
      int lx = gx - 50, lz = gz - 0; bool edge = lx == 0 || lx == 8 || lz == 0 || lz == 8 || y == FLOOR + 5;
      if (edge) { id = (lz == 8 && lx > 2 && lx < 6 && y == FLOOR + 2) ? glass : stone; return; }
      if (lx == 2 && lz == 2 && y == FLOOR + 1) { id = torch; return; }
      if (lx == 6 && lz == 6 && y == FLOOR + 1) { id = reg().blockId("glowstone"); return; }
      return;
    }
    // a little tree
    if (gx == 62 && gz == 3 && y > FLOOR && y <= FLOOR + 4) { id = log; return; }
    if (gx >= 60 && gx <= 64 && gz >= 1 && gz <= 5 && y >= FLOOR + 3 && y <= FLOOR + 5) { if (!(gx == 62 && gz == 3 && y < FLOOR + 5)) id = leaves; return; }
  }
  void generate(Chunk& c) override {
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int wx = c.worldX() + x, wz = c.worldZ() + z;
      for (int y = 0; y <= FLOOR + 8; y++) { BlockId id; u8 m; at(wx, y, wz, id, m); if (id) c.set(x, y, z, id, m); }
      c.climate[(z * 16 + x) * 2] = 160; c.climate[(z * 16 + x) * 2 + 1] = 175;
    }
  }
  void sample(int wx, int wz, ColumnSample& s) const override { s.height = FLOOR; s.top = grass; s.under = dirt; }
  int spawnHeight(int, int) const override { return FLOOR + 1; }
private:
  std::vector<BlockId> list, crops;
  BlockId stone = 0, grass = 0, dirt = 0, water = 0, lava = 0, farmland = 0, torch = 0, glass = 0, planks = 0, stairs = 0, fence = 0, wall = 0, pane = 0, slab = 0, gate = 0, leaves = 0, log = 0;
};
class EmptyGen : public DimGenerator {
public:
  void generate(Chunk&) override {}
  void sample(int, int, ColumnSample& s) const override { s.height = -1; }
};
} // namespace

void registerShowcaseWorld() {
  WorldGen::registerType("showcase", [](int dim, const WorldGenOptions&) -> std::unique_ptr<DimGenerator> {
    initBlockTraits();
    if (dim == DIM_OVERWORLD) return std::make_unique<ShowcaseGen>();
    return std::make_unique<EmptyGen>();
  });
}

} // namespace dq
