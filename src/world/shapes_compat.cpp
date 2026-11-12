// world/shapes_compat.cpp — the gameplay code's pixel-box view of the shared block shapes (world/shapes.cpp).
#include "world/shapes.h"
#include "world/carve_faces.h"

namespace dq {

bool hasShapeBoxes(BlockId id);

bool worldBoxes(const World& w, int x, int y, int z, BlockId id, int purpose, std::vector<PixBox>& out) {
  out.clear();
  u8 meta = w.getMeta(x, y, z);
  u8 carve = w.getCarve(x, y, z);
  if (carve && fullSolid(id)) {   // chisel geometry. A seat mark on a torch is not this.
    WorldShapeNb nb(w, x, y, z);
    static thread_local std::vector<ShapeBox> cboxes;
    if (!carveBoxes(cboxes, carve, nb)) return false;
    for (auto& b : cboxes) out.push_back({b.b[0], b.b[1], b.b[2], b.b[3], b.b[4], b.b[5]});
    if (purpose == ShapePurpose::Collide) {
      u8 coat = w.getCoat(x, y, z);
      if (coat & 0x0F) {
        float h = (coat & 0x10) ? 4.f : 2.f;
        for (int hx = 0; hx < 2; hx++) for (int hz = 0; hz < 2; hz++) {
          if (!(coat & colBit(hx, hz))) continue;
          int top = columnTopY(carve, hx, hz);
          if (top < 0) continue;
          if (top == 1 && reg().tSolid[w.getBlock(x, y + 1, z)]) continue;   // the cell above already holds the body
          float x0 = hx ? 8.f : 0.f, z0 = hz ? 8.f : 0.f, y0 = top ? 16.f : 8.f;
          out.push_back({x0, y0, z0, x0 + 8.f, y0 + h, z0 + 8.f});
        }
      }
    }
    return true;
  }
  WorldShapeNb nb(w, x, y, z);
  static thread_local std::vector<ShapeBox> boxes;
  if (!shapeBoxes(id, meta, nb, purpose, boxes)) return false;
  SeatShift sh = seatShiftOf(id, meta, carve, nb);
  float oy = 0, ox = 0, oz = 0;
  bool stretch = false;
  if (sh.micro) {
    ox = sh.x; oy = sh.y; oz = sh.z;
    stretch = sh.hang && sh.hy == 1;
  } else {
    oy = (float)sinkOffset(id, nb);
    if (reg().tRender[id] == (u8)Render::Torch) {
      int m = meta & 7;
      if (torchSetBack((u8)m, nb) > 0) { ox = m == 1 ? -8.f : m == 2 ? 8.f : 0; oz = m == 3 ? -8.f : m == 4 ? 8.f : 0; }
    }
    if (reg().block(id).str("shape") == "chain" && fullSolid(w.getBlock(x, y + 1, z)) && carveFaceDepth(w.getCarve(x, y + 1, z), 0, -1, 0) == 8) stretch = true;
  }
  for (auto& b : boxes) {
    if (purpose == ShapePurpose::Collide && b.nocol) continue;
    float y1 = b.b[4];
    if (stretch && b.b[4] >= 15.5f) y1 += 8.f;   // the top link reaches an upper underside
    out.push_back({b.b[0] + ox, b.b[1] + oy, b.b[2] + oz, b.b[3] + ox, y1 + oy, b.b[5] + oz});
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
  if (!(w.getMeta(x, y, z) & WLOG)) return false;
  return reg().tWlog[id] || (reg().tOpaque[id] && w.getCarve(x, y, z));
}

} // namespace dq
