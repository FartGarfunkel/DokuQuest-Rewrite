// sim/rotate.cpp — see rotate.h.
#include "sim/rotate.h"
#include "sim/blockinfo.h"
#include "core/registry.h"
#include "world/shapes.h"

namespace dq {

u8 rotateMetaY(BlockId id, u8 m, int quarters) {
  const Registry& R = reg(); const BlockDef& b = R.block(id); int k = R.tShape[id];
  static const int H2D6[4] = {2, 5, 3, 4}; static const int D62H[6] = {-1, -1, 0, 2, 3, 1};
  const int q = ((quarters % 4) + 4) % 4;
  for (int i = 0; i < q; i++) {
    if (k == ShapeKind::Stairs || k == ShapeKind::Gate || k == ShapeKind::Door || k == ShapeKind::Trapdoor || k == ShapeKind::Ladder || k == ShapeKind::Bed || k == ShapeKind::Frame || b.has("facingModel") || (bhas(id, BF::FacingFront) && !b.has("f6"))) m = (u8)((m & ~3) | ((m + 1) & 3));
    else if (k == ShapeKind::Slab && (m & 7) >= 2 && (m & 7) <= 5) m = (u8)((m & ~7) | (2 + (((m & 7) - 2 + 1) & 3)));
    else if (b.has("axisBlock") || k == ShapeKind::Rod) { int a = m & 3; if (a == 1) m = (u8)((m & ~3) | 2); else if (a == 2) m = (u8)((m & ~3) | 1); }
    else if (b.render == Render::Torch && (m & 7)) { static const int t[5] = {0, 3, 4, 2, 1}; m = (u8)((m & ~7) | t[m & 7]); }
    else if (b.has("f6")) { int d = m & 7; if (d >= 2) m = (u8)((m & ~7) | H2D6[(D62H[d] + 1) & 3]); }
  }
  return m;
}

u8 rotateMetaAbout(BlockId id, u8 m, int axis, int quarters) {
  const Registry& R = reg(); const BlockDef& b = R.block(id); const int k = R.tShape[id];
  const int q = ((quarters % 4) + 4) % 4;
  // six-way facings (0 down, 1 up, 2 north, 3 south, 4 west, 5 east): the four round the axis in order
  static const int RING_X[4] = {1, 3, 0, 2}, RING_Z[4] = {1, 5, 0, 4};   // up, south, down, north / up, east, down, west
  const int* ring = axis == 1 ? RING_X : RING_Z;
  for (int i = 0; i < q; i++) {
    if (b.has("axisBlock") || k == ShapeKind::Rod) {   // 0 upright, 1 east-west, 2 north-south: upright <-> across the axis
      const int a = m & 3, across = axis == 1 ? 2 : 1;
      if (a == 0) m = (u8)((m & ~3) | across); else if (a == across) m = (u8)(m & ~3);
    } else if (b.has("f6")) {
      const int d = m & 7;
      for (int j = 0; j < 4; j++) if (ring[j] == d) { m = (u8)((m & ~7) | ring[(j + 1) & 3]); break; }
    }
  }
  return m;
}

namespace {
// each of the 8 corner bits sent where the turn takes it (bits: x<<2 | y<<1 | z)
u8 mapCorners(u8 c, int q, int (*f)(int x, int y, int z)) {
  q = ((q % 4) + 4) % 4;
  for (int i = 0; i < q; i++) {
    u8 o = 0;
    for (int b = 0; b < 8; b++) if (c >> b & 1) o |= (u8)(1 << f(b >> 2 & 1, b >> 1 & 1, b & 1));
    c = o;
  }
  return c;
}
int cornY(int x, int y, int z) { return ((1 - z) << 2) | (y << 1) | x; }    // east -> south
int cornX(int x, int y, int z) { return (x << 2) | ((1 - z) << 1) | y; }    // up -> south
int cornZ(int x, int y, int z) { return (y << 2) | ((1 - x) << 1) | z; }    // up -> east
}

u8 rotateCarveY(u8 c, int quarters) { return mapCorners(c, quarters, cornY); }
u8 rotateCarveAbout(u8 c, int axis, int quarters) { return mapCorners(c, quarters, axis == 1 ? cornX : cornZ); }

} // namespace dq
