// world/carve_faces.h — octants of a chiseled block (2x2x2 corners, 8 units each).
// A whole face or a clean slab still holds things in the middle. A face broken into smaller
// corners holds snow, water and placed items on the octant under the crosshair.
#pragma once
#include "common/common.h"

namespace dq {

// Bit 6 on a NON-full-solid block's carve byte: this attachment sits on one octant (low 3 bits).
// Full solids use that bit as real geometry, so a seat is never stored on one and never read from one.
constexpr u8 SEAT_MARK = 0x40;
inline bool seatMark(u8 carve) { return (carve & SEAT_MARK) != 0; }
inline u8 packSeat(int oct) { return (u8)(SEAT_MARK | (oct & 7)); }
inline int seatOctant(u8 carve) { return carve & 7; }

// carve: bit (hx<<2)|(hy<<1)|hz set = that corner is gone. Present means the micro-block is still there.
inline bool octPresent(u8 carve, int hx, int hy, int hz) {
  return ((carve >> ((hx << 2) | (hy << 1) | hz)) & 1) == 0;
}
// Highest remaining octant of one column: 1 upper, 0 lower, -1 empty.
inline int columnTopY(u8 carve, int hx, int hz) {
  if (octPresent(carve, hx, 1, hz)) return 1;
  if (octPresent(carve, hx, 0, hz)) return 0;
  return -1;
}
inline int colBit(int hx, int hz) { return 1 << ((hx << 1) | hz); }
// Bits (hx<<1)|hz for columns that still have a block in them.
inline u8 exposedCols(u8 carve) {
  u8 m = 0;
  for (int hx = 0; hx < 2; hx++) for (int hz = 0; hz < 2; hz++) if (columnTopY(carve, hx, hz) >= 0) m |= (u8)colBit(hx, hz);
  return m;
}

// Entry point nudged just inside the face, then which octant contains it.
// A stair tread at exactly 0.5 would otherwise count as the empty upper corner.
inline int aimedOctant(vec3 point, int bx, int by, int bz, int nx, int ny, int nz) {
  float fx = clampv(point.x - (float)bx - (float)nx * 0.001f, 0.f, 0.999f);
  float fy = clampv(point.y - (float)by - (float)ny * 0.001f, 0.f, 0.999f);
  float fz = clampv(point.z - (float)bz - (float)nz * 0.001f, 0.f, 0.999f);
  int hx = fx >= 0.5f, hy = fy >= 0.5f, hz = fz >= 0.5f;
  return (hx << 2) | (hy << 1) | hz;
}

// Pixel shift from the cell centre onto an octant centre (8 -> 4 or 12). Wall torches move on the
// face; floor things move in XZ and sink 8 onto a low step; a hang only moves in XZ (the chain
// stretch for an upper underside is applied by the caller).
inline void seatPixels(u8 carve, int facing, bool hang, bool wall, float& ox, float& oy, float& oz) {
  ox = oy = oz = 0;
  if (!seatMark(carve)) return;
  int o = carve & 7;
  int hx = (o >> 2) & 1, hy = (o >> 1) & 1, hz = o & 1;
  float dx = hx ? 4.f : -4.f, dy = hy ? 4.f : -4.f, dz = hz ? 4.f : -4.f;
  if (wall) {
    if (facing == 1 || facing == 2) { oy = dy; oz = dz; }
    else { ox = dx; oy = dy; }
    return;
  }
  ox = dx; oz = dz;
  if (hang) return;
  if (!hy) oy = -8.f;
}

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
