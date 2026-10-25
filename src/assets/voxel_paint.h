// assets/voxel_paint.h — building voxel models from shapes and pictures (the voxel versions of the
// original's MeshBuilder helpers: cbox/partSeg/sseg/sball/cring from render/sculpt.js, models3d.js and
// items3d.js, and gear3d.js's eight-corner blocks). Shapes are given in MODEL space (the original mesh's
// coordinates); the painter fills every voxel whose centre lies inside, at least one voxel thick.
#pragma once
#include "assets/voxel.h"

namespace dq {

struct TileSet;

// one opaque pixel of an item picture (render/sculpt.js spriteInfo)
struct SpritePx { int i, j; int r, g, b; float lum, sat; };
// an item picture read for building: R pixels across, its opaque pixels, and the picture "filled out"
// past its edges with its dark outline replaced (sculpt.js modelFill) — every part of a 3D model takes
// its colour from where it lies over that fill, so a resource pack's art comes through in 3D.
struct SpriteInfo {
  int R = 16, tile = -1;
  std::vector<SpritePx> px;
  std::vector<u8> grid;        // R*R: 1 = opaque
  std::vector<u32> raw;        // R*R: the picture (voxRGBA, 0 = clear)
  std::vector<u32> fill;       // R*R: filled picture
  float lo = 255, hi = 0, edge = 0;   // luminance range; edge = the outline's threshold
  static SpriteInfo of(const TileSet& ts, int tile);
  bool empty() const { return px.empty(); }
  // pixel under a model-space point (x, y in -0.5..0.5, y up)
  int pi(float x) const { return (int)std::floor((x + 0.5f) * R); }
  int pj(float y) const { return (int)std::floor((0.5f - y) * R); }
  u32 fillAt(float x, float y) const;
  u32 rawAt(float x, float y) const;
  // sculpt.js sampler(): the nearest non-outline pixel to (x, y) among those passing ok (else any)
  const SpritePx* sample(float x, float y, const std::function<bool(const SpritePx&)>& ok = nullptr) const;
  const SpritePx* common(const std::function<bool(const SpritePx&)>& ok = nullptr) const;   // spriteCommon
  const SpritePx* best(const std::function<float(const SpritePx&)>& score, const std::function<bool(const SpritePx&)>& ok = nullptr) const;
  float tPos(const SpritePx& p) const { return ((p.i + 0.5f) / R + 1 - (p.j + 0.5f) / R) / 2; }   // along the diagonal
};

// colour of a tile pixel as a voxel: alpha < 128 -> 0; the 253 "tint me" marker takes the original's
// default item tint (TINT_GRASS * 1.3); emissive pixels glow
u32 tileVoxel(const TileSet& ts, int tile, int x, int y);   // x, y in 0..size-1

struct PaintPt { vec3 p; float t = 0, s = 0; };   // model-space point; along (0..1) / across (-1..1) a segment
using PaintFn = std::function<u32(const PaintPt&)>;

class VoxelPainter {
public:
  VoxelGrid g;
  VoxelFrame frame;
  VoxelPainter(ivec3 size, vec3 origin, float voxel) : g(size) { frame.origin = origin; frame.voxel = voxel; }
  // axis-aligned box lo..hi
  void box(vec3 lo, vec3 hi, const PaintFn& fn);
  // partSeg: a bar from a to b in the picture's plane (x, y), w wide, d deep (along z, centred on a.z).
  // bevel > 0 thins it toward its long edges (blades).
  void seg(vec3 a, vec3 b, float w, float d, const PaintFn& fn, float bevel = 0);
  // gear3d gSegPts: a square rod in 3D from p0 to p1, widths w0 -> w1 ([across, deep])
  void rod(vec3 p0, vec3 p1, vec2 w0, vec2 w1, const PaintFn& fn, vec3 ref = vec3(0, 0, 1));
  // a convex six-sided block from its eight corners P[cx*4 + cy*2 + cz] (gear3d shapes)
  void hexa(const std::array<vec3, 8>& P, const PaintFn& fn);
  // the voxel containing model point p
  ivec3 cell(vec3 p) const { return ivec3(glm::floor((p - frame.origin) / frame.voxel)); }
  vec3 centre(ivec3 c) const { return frame.origin + (vec3(c) + 0.5f) * frame.voxel; }
private:
  template <class Inside> void fillAABB(vec3 lo, vec3 hi, Inside inside, const PaintFn& fn);
};

} // namespace dq
