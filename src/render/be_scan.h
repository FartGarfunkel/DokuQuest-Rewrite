// render/be_scan.h — walk the block entities (piles, lecterns, chests...) near the camera, in the camera's band.
// fn(const Chunk&, json& entity, int x, int y, int z): x, y, z are GLOBAL block coordinates.
#pragma once
#include "render/frame.h"
#include "world/world.h"

namespace dq {

template <class F> void forBlockEntities(World& w, const FrameCtx& f, float radius, F&& fn) {
  int dim = f.dim, r = (int)std::ceil(radius / 16.f) + 1;
  int pcx = ifloor(f.cam.pos.x) >> 4, pcz = ifloor(f.cam.pos.z) >> 4;
  for (int cz = pcz - r; cz <= pcz + r; cz++) for (int cx = pcx - r; cx <= pcx + r; cx++) {
    Chunk* c = w.chunk(dim, cx, cz);
    if (!c || c->blockEntities.empty()) continue;
    for (auto& [idx, e] : c->blockEntities) {
      if (!e.is_object()) continue;
      fn(*c, e, cx * 16 + (idx & 15), c->baseY() + (idx >> 8), cz * 16 + ((idx >> 4) & 15));
    }
  }
}

} // namespace dq
