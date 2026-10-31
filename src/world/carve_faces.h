// world/carve_faces.h — is one face of a (possibly chiseled) block whole? Torches, lanterns, ladders, signs and plants
// only fix to a whole face, so a carved notch can't leave them floating.
#pragma once
#include "common/common.h"

namespace dq {

// carve: the block's octant mask (bit (hx<<2)|(hy<<1)|hz set = carved away). (nx,ny,nz): the face's outward normal,
// one axis only. True when all four octants on that side are still there.
inline bool carveFaceFull(u8 carve, int nx, int ny, int nz) {
  if (!carve) return true;
  for (int o = 0; o < 8; o++) {
    if (!((carve >> o) & 1)) continue;
    int hx = (o >> 2) & 1, hy = (o >> 1) & 1, hz = o & 1;
    if ((nx > 0 && hx) || (nx < 0 && !hx) || (ny > 0 && hy) || (ny < 0 && !hy) || (nz > 0 && hz) || (nz < 0 && !hz)) return false;
  }
  return true;
}

} // namespace dq
