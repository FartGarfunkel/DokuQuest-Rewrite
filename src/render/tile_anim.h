// render/tile_anim.h — animated tiles (water, lava, fire, portals, torch flames, furnaces...).
// The original painted extra frames in code (render/doku.js ANIMS) and swapped them into the atlas;
// here the built-in frames ship as data/tile_anims.png/.json, become extra layers of the TileSet,
// and TileAnimator points the animated layer at its current frame in a small layer map (tileAnimMap) that the terrain
// shaders read. (It used to copy each new frame into the GPU tile array: on Intel's driver a write into that array
// while the GPU drew from it could hold the game for seconds.)
#pragma once
#include "assets/tileset.h"
#include "render/frame.h"

namespace dq {

void addBuiltinTileAnims(TileSet& ts);   // call right after loadBuiltinTiles (before a resource pack)

GLuint tileAnimMap();   // the current layer map (R16UI, 64 x 64): bound by TileTextures::bind as uAnimMap

class TileAnimator {
public:
  void update(const TileSet& ts, const TileTextures& tex, double timeSec);
  void reset() { cur.clear(); }
private:
  std::vector<int> cur;
  const void* lastData = nullptr;
};

} // namespace dq
