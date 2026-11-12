// assets/pack_models.h — Blockbench-style block models from the game's own pack (assets/minecraft/models/block/<name>.json:
// "textures" + "elements" of boxes with per-face UVs, an element rotation about an origin), loaded with the pack
// (assets/packs.cpp) and drawn by the world mesher for the blocks that use them (render/mesher.cpp: torches — placed,
// and the wall ones turned to their wall). Each texture a model names becomes a tile (the pack's picture, or ours of
// that name). Read-only once loaded; swapped whole when the pack changes.
#pragma once
#include "common/common.h"
#include <memory>

namespace dq {
class TileSet; class PackFS;

struct PackModelFace { int tile = -1; vec4 uv{0, 0, 16, 16}; };   // uv in the pack's 0..16 units
struct PackModelBox {
  vec3 from{0}, to{16};
  int rotAxis = -1; float rotAngle = 0; vec3 rotOrigin{8};   // axis 0 x, 1 y, 2 z (-1 none); degrees
  bool shade = true;
  PackModelFace face[6];   // 0 north (-z), 1 south (+z), 2 west (-x), 3 east (+x), 4 up, 5 down; tile -1 = no face
};
struct PackModel { std::vector<PackModelBox> boxes; };

// the pack's models for these blocks (by model name, e.g. "torch", "wall_torch"); their textures added to the tiles
void loadPackModels(TileSet& ts, const PackFS& P, const std::vector<std::string>& names);
const PackModel* packModel(const std::string& name);   // null: none (draw the block the game's own way)

} // namespace dq
