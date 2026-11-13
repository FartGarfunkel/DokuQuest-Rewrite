// assets/pack_models.h — Blockbench-style block models from the game's own pack (assets/minecraft/models/block/<name>.json:
// "textures" + "elements" of boxes with per-face UVs, an element rotation about an origin), loaded with the pack
// (assets/packs.cpp) and drawn by the world mesher for the blocks that use them (render/mesher.cpp: torches — placed,
// and the wall ones turned to their wall). Each texture a model names becomes a tile (the pack's picture, or ours of
// that name). Read-only once loaded; swapped whole when the pack changes.
#pragma once
#include <functional>
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
// under: the lid's underside; floor: the base's top (the inside, seen with the lid up) — per half and facing like top
struct DoubleChestTiles { int front[2] = {-1, -1}, back[2] = {-1, -1}, top[2][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}},
                          under[2][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, floor[2][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}}; };
// which half of a large chest the one at world (wx, wz) facing fd (meta & 3) is: 0 its partner on its right (as you face
// its front), 1 on its left, -1 none; same(dx, dz): the same chest facing the same way there. (A row of three or more
// pairs up by the row's world position.) The mesher and the open chest (render/chest_layer.cpp) both ask this.
int doubleChestHalf(int fd, int wx, int wz, const std::function<bool(int, int)>& same);
void loadDoubleChests(TileSet& ts, const PackFS& P);
const DoubleChestTiles* doubleChestFor(int singleFrontTile);
// the painted chests and the party chest drawn from the chest's own pictures (the pack's or the game's), recoloured: a painted
// one's wood takes its dye, the party chest's metal turns gold; their large-chest pictures too. After the pack and the
// player's own tiles are in (client/app.cpp reloadTiles).
void paintChestTiles(TileSet& ts);

// furnaces, smokers and blast furnaces with their openings hollowed: every dark pixel of the unlit front picture is a hole
// FURNACE_DEPTH (of 16) deep into the block; through the holes, the front picture itself on the back wall (the lit one's fire
// when it burns). body: the block (front, the holes' walls, floors and ceilings, sides, top, bottom); back: the back wall,
// drawn bright when the furnace is lit. Facing north (-z); keyed by the block's front tile. After the tiles are final.
constexpr float FURNACE_DEPTH = 5.f;
struct FurnaceModel { PackModel body, back; float floorY = 0, floorX0 = 6, floorX1 = 10; };   // the main opening's floor (of 16: its height, from x0 to x1, model x), where the fuel lies
void buildFurnaceModels(TileSet& ts);
const FurnaceModel* furnaceModelFor(int frontTile);

} // namespace dq
