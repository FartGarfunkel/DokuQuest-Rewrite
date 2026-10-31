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

// the four octants on one side of the block
inline u8 carveLayer(int nx, int ny, int nz) { return nx > 0 ? 0xF0 : nx < 0 ? 0x0F : ny > 0 ? 0xCC : ny < 0 ? 0x33 : nz > 0 ? 0xAA : 0x55; }
// How far in from the cell's face a thing fixed to that face sits: 0 = the face is whole; 8 = that half is carved away
// and the far half is whole (a slab: torches, frames and sinking things sit on its inner face, half a block in);
// -1 = no whole face to hold on to.
inline int carveFaceDepth(u8 carve, int nx, int ny, int nz) {
  const u8 nearL = carveLayer(nx, ny, nz), farL = carveLayer(-nx, -ny, -nz);
  if (!(carve & nearL)) return 0;
  if ((carve & nearL) == nearL && !(carve & farL)) return 8;
  return -1;
}

} // namespace dq
