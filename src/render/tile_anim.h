// render/tile_anim.h — animated tiles (water, lava, fire, portals, torch flames, furnaces...).
// The original painted extra frames in code (render/doku.js ANIMS) and swapped them into the atlas;
// here the built-in frames ship as data/tile_anims.png/.json, become extra layers of the TileSet,
// and TileAnimator copies the current frame into the animated layer of the GPU tile array.
#pragma once
#include "assets/tileset.h"
#include "render/frame.h"

namespace dq {

void addBuiltinTileAnims(TileSet& ts);   // call right after loadBuiltinTiles (before a resource pack)

class TileAnimator {
public:
  void update(const TileSet& ts, const TileTextures& tex, double timeSec);
  void reset() { cur.clear(); }
private:
  std::vector<int> cur;
  const void* lastData = nullptr;
};

} // namespace dq
