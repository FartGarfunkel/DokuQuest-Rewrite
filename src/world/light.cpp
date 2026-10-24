#include "world/light.h"

namespace dq {

void initialChunkLight(Chunk& c) {
  const Registry& R = reg();
  bool hasSky = c.dim == DIM_OVERWORLD || c.dim == DIM_AETHER;
  u8 skyTop = hasSky ? 15 : (c.dim == DIM_END ? 6 : 0);
  for (int sy = 0; sy < SECS; sy++) c.airLight[sy] = (u8)(skyTop << 4);
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    int sky = skyTop;
    for (int y = DIM_H - 1; y >= 0; y--) {
      Section* s = c.section(y >> 4);
      if (!s) { if (sky < skyTop) c.ensure(y >> 4); else continue; s = c.section(y >> 4); }
      int i = secIdx(x, y & 15, z);
      BlockId id = s->blocks[i];
      if (id) { if (R.tOpaque[id]) sky = 0; else if (R.tAtt[id] || R.tLiquid[id] || R.tLeaves[id]) sky = std::max(0, sky - std::max<int>(1, R.tAtt[id])); }
      s->light[i] = (u8)(sky << 4 | R.metaLight(id, s->meta[i]));
    }
  }
  c.lightReady = true;
}

} // namespace dq
