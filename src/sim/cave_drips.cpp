// sim/cave_drips.cpp — see cave_drips.h.
#include "sim/cave_drips.h"
#include "core/registry.h"

namespace dq {

namespace {
BlockId dripstoneId() { static const BlockId id = reg().blockId("pointed_dripstone"); return id; }
}

CaveDrip caveDripAt(const World& w, int x, int y, int z) {
  CaveDrip r;
  const BlockId ds = dripstoneId();
  if (!ds || w.getBlock(x, y, z) != ds || !(w.getMeta(x, y, z) & 1)) return r;   // (meta & 1: hanging)
  if (w.getBlock(x, y - 1, z) == ds) return r;   // not the tip
  int top = y;
  while (top - y < 24 && w.getBlock(x, top + 1, z) == ds && (w.getMeta(x, top + 1, z) & 1)) top++;
  r.length = top - y + 1;
  const BlockId root = w.getBlock(x, top + 1, z);   // the stone it hangs from
  if (!root || !reg().tSolid[root]) return r;
  const u8 liq = reg().tLiquid[w.getBlock(x, top + 2, z)];
  r.liquid = liq == 1 ? 1 : liq == 2 ? 2 : 0;
  return r;
}

void caveDripTick(World& w, int x, int y, int z, double rnd01) {
  const CaveDrip d = caveDripAt(w, x, y, z);
  if (!d.liquid) return;
  // a level about every two and a half minutes of water (lava: about every seven), quicker for a longer column
  const double each = (d.liquid == 1 ? 150.0 : 420.0) / (0.75 + 0.25 * std::min(d.length, 5));
  if (rnd01 >= 1.0 / each) return;
  static const BlockId cauldron = reg().blockId("cauldron");
  for (int k = 1; k <= 11; k++) {
    const BlockId b = w.getBlock(x, y - k, z);
    if (!b) continue;
    if (b != cauldron || !cauldron) return;   // the drops land on something else
    const u8 m = w.getMeta(x, y - k, z);
    const int lvl = cauldronLevel(m);
    if (lvl >= 3) return;
    if (lvl > 0 && cauldronLava(m) != (d.liquid == 2)) return;   // (water and lava don't share a cauldron)
    w.setBlock(x, y - k, z, cauldron, (u8)((m & ~7) | (d.liquid == 2 ? 4 : 0) | (lvl + 1)));
    return;
  }
}

} // namespace dq
