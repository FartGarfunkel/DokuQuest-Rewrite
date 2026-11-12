// assets/pack_models.h — Blockbench-style block models from the game's own pack (assets/minecraft/models/block/<name>.json:
// "textures" + "elements" of boxes with per-face UVs, an element rotation about an origin), loaded with the pack
// (assets/packs.cpp) and drawn by the world mesher for the blocks that use them (render/mesher.cpp: torches — placed,
// and the wall ones turned to their wall). Each texture a model names becomes a tile (the pack's picture, or ours of
// that name). Read-only once loaded; swapped whole when the pack changes.
#pragma once
#include "common/common.h"
#include <memory>
#include <unordered_map>

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

// random looks of a block's picture (the pack's assets/dokuquest/variants.json: picture -> [[picture, weight]...], the
// picture itself among them): the mesher picks one per block by where it is. baseTiles: the pack picture -> our tile.
void loadTileVariants(TileSet& ts, const PackFS& P, const std::unordered_map<std::string, int>& baseTiles);
struct TileVariants { std::vector<int> tiles; std::vector<u32> cum; u32 total = 0; };
const TileVariants* tileVariants(int tile);

// a large chest drawn as one (the pack's entity/chest/<kind>_left / _right halves): per half (0 = the one on your left
// as you face it) its front, its back, and its lid's top turned for each facing (meta & 3); keyed by the single chest's
// front tile
struct DoubleChestTiles { int front[2] = {-1, -1}, back[2] = {-1, -1}, top[2][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}}; };
void loadDoubleChests(TileSet& ts, const PackFS& P);
const DoubleChestTiles* doubleChestFor(int singleFrontTile);

} // namespace dq
