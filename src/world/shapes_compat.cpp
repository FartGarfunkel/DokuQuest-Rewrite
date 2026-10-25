// world/shapes_compat.cpp — the gameplay code's pixel-box view of the shared block shapes (world/shapes.cpp).
#include "world/shapes.h"

namespace dq {

bool worldBoxes(const World& w, int x, int y, int z, BlockId id, int purpose, std::vector<PixBox>& out) {
  out.clear();
  u8 meta = w.getMeta(x, y, z);
  WorldShapeNb nb(w, x, y, z);
  static thread_local std::vector<ShapeBox> boxes;
  if (!shapeBoxes(id, meta, nb, purpose, boxes)) return false;
  float sink = (float)sinkOffset(id, nb), ox = 0, oz = 0;
  if (reg().tRender[id] == (u8)Render::Torch) {
    int m = meta & 7;
    if (torchSetBack((u8)m, nb) > 0) { ox = m == 1 ? -8.f : m == 2 ? 8.f : 0; oz = m == 3 ? -8.f : m == 4 ? 8.f : 0; }
  }
  for (auto& b : boxes) {
    if (purpose == ShapePurpose::Collide && b.nocol) continue;
    out.push_back({b.b[0] + ox, b.b[1] + sink, b.b[2] + oz, b.b[3] + ox, b.b[4] + sink, b.b[5] + oz});
  }
  return true;
}
bool hasShapeBoxes(BlockId id) { return reg().tShape[id] || !reg().block(id).str("shape").empty(); }
float liquidHeight(BlockId id, u8 meta, BlockId above) {
  if (above == id) return 1;
  if (reg().tPortal[id]) return 1;
  int lvl = meta & 7;
  if (meta & 8) return 0.9f;
  return std::max(0.12f, 0.875f - lvl * 0.105f);
}
bool waterAt(const World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (reg().tLiquid[id] == 1) return true;
  return reg().tWlog[id] && (w.getMeta(x, y, z) & WLOG);
}

} // namespace dq
