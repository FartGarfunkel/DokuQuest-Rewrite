// render/book_model.h — a book that opens and turns its pages, drawn in the world: the journal every character wears on
// the belt and holds open in both hands at their menus (render/char_layer.cpp, item_layers.cpp), the enchanting table's
// floating book (render/magic_fx.cpp).
//
// Its frame: the spine along local z (centred on the origin), the open pages facing local +y, the right half toward +x
// and the left toward -x. open 0 = shut (both halves along +y, the page blocks meeting), 1 = open nearly flat. leaf
// 0..1 = a page turning over (dir +1 right to left, -1 back), < 0 none. Sizes in the frame's units: half width 6/16,
// length 10/16 (the classic book) times scale.
#pragma once
#include "assets/voxel.h"

namespace dq {

// open > 1: lying open flat (the journal held up to read its pages: ui/book_ui.h)
void drawBook(VoxelRenderer& VR, const mat4& frame, VoxelLight L, float open, float leaf, int dir, float scale, u32 coverRgb);
void setBookPagesDark(bool dark);   // dark pages (Options "Dark Book Pages"), as the menus in them
// the open pages' area in the book's frame (flat open): x from -hw to hw, z from -hl to hl, its surface at y = top
void bookPageArea(float scale, float& hw, float& hl, float& top);

} // namespace dq
