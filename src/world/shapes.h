// world/shapes.h — blocks that aren't full cubes (port of world/shapes.js).
// One function describes every non-cube block as a list of boxes in 1/16-block "pixels", and the
// same boxes drive drawing, collision and the selection outline (what you see is what you bump into).
// Static models (lanterns, campfires, furniture, anvils...) come from data/shapes.json (the
// original's SHAPES table, dumped by tools/port/jsdump.py).
//
// Meta bits are the original's (see the table at the top of world/shapes.js): bit 7 = waterlogged,
// slab 0 bottom 1 top 2..5 vertical N/E/S/W 6 double, stairs 0-1 facing 2 upside down, door 0-1
// facing 2 open 3 hinge right 4 upper, trapdoor 0-1 facing 2 open 3 top, gate 0-1 facing 2 open...
#pragma once
#include "world/world.h"

namespace dq {

constexpr u8 WLOG = 0x80;                        // waterlogged bit
constexpr int DIRX[4] = {0, 1, 0, -1};           // N, E, S, W
constexpr int DIRZ[4] = {-1, 0, 1, 0};

struct PixBox { float x0, y0, z0, x1, y1, z1; };
enum ShapePurpose : int { SHAPE_DRAW = 0, SHAPE_COLLIDE = 1, SHAPE_SELECT = 2 };

// One box of a static model (SHAPES[name]): b in pixels, tiles for its faces, nocol = no collision.
struct ModelBox { PixBox b; bool nocol = false; std::string side, end; };
const std::vector<ModelBox>* shapeModel(const std::string& name);   // nullptr if unknown

// Neighbour access for neighbour-dependent shapes (fences, stairs, walls). w may be null (an item
// drawn on its own: every neighbour reads as air).
struct ShapeNb {
  const World* w = nullptr; int x = 0, y = 0, z = 0;
  BlockId id(int dx, int dy, int dz) const { return w ? w->getBlock(x + dx, y + dy, z + dz) : AIR; }
  u8 meta(int dx, int dy, int dz) const { return w ? w->getMeta(x + dx, y + dy, z + dz) : 0; }
};

// The original's shapeBoxes(id, meta, nb, nm, purpose). Returns false for "null" (a full cube);
// true with `out` filled (possibly empty = nothing there for that purpose).
bool shapeBoxes(BlockId id, u8 meta, const ShapeNb& nb, int purpose, std::vector<PixBox>& out);
// worldBoxes: the same in the world, with the slab sink and torch set-back applied.
bool worldBoxes(const World& w, int x, int y, int z, BlockId id, int purpose, std::vector<PixBox>& out);
// T_SHAPE[id] || (def.shape && SHAPES[def.shape]): does this block have boxes at all?
bool hasShapeBoxes(BlockId id);

// helpers shared with gameplay
PixBox rotBox(PixBox b, int f);      // a north-facing box turned to face f (0..3)
PixBox flipBox(PixBox b);            // upside down
int stairShape(u8 meta, const ShapeNb& nb);   // 0 straight, 1/2 outer L/R, 3/4 inner L/R
float liquidHeight(BlockId id, u8 meta, BlockId above);   // surface height of a liquid cell (mesher.js)
inline bool isWaterlogged(BlockId id, u8 meta) { return reg().tWlog[id] && (meta & WLOG); }
bool waterAt(const World& w, int x, int y, int z);        // water, counting waterlogged blocks as still sources

} // namespace dq
