// render/journal_model.h — the journal held open at a menu, as a real book: the drawn book of the menus (ui/book.cpp,
// the pack's gui/book.png with the menus written on it) is its open spread, laid on two halves that rise from the spine
// and curve like paper; under them the page blocks (their cut edges showing), the leather covers with gold corners, the
// spine; and the bookmark ribbons as cloth strips coming out from between the pages, stirring in the air. Drawn in your
// own hands (render/item_layers.cpp) and in everyone else's (render/char_layer.cpp: the picture each player sends).
//
// The frame: the spread's centre on the spine at the origin, x across it (the right page toward +x as its reader sees
// it), y up the pages, z out of them toward the reader. w, h: the spread's size in the frame's units. bend: how much the
// halves rise from the spine and the paper curves (0 flat, as held to read in first person, 1 a book in someone's hands).
#pragma once
#include "assets/voxel.h"
#include <vector>

namespace dq {
class ContentDraw;

struct JournalPic {
  unsigned tex = 0;
  vec4 texRect{0, 0, 1, 1};      // the picture's top-left and bottom-right in texture coordinates (GL's FBO: flipped)
  vec4 spread{0, 0, 1, 1};       // the drawn book in the picture (u0, v0, u1, v1; 0..1 from the top left)
  std::vector<vec4> ribbons;     // each ribbon's showing part in the picture (same space)
};

// the leather, the page blocks and their edges, the gold corners (inside a VoxelRenderer pass)
void drawJournalBody(VoxelRenderer& VR, const mat4& frame, float w, float h, float bend, VoxelLight L, bool darkPages);
// the spread on the pages and the ribbons (inside a ContentDraw pass); time stirs the ribbons
// (oldPic: a page being turned — the pages as they were show on the side it lands on until it's down: drawJournalLeaf)
void drawJournalPages(ContentDraw& cd, const mat4& frame, float w, float h, float bend, const JournalPic& pic, double time, vec2 light, bool lit,
                      const JournalPic* oldPic = nullptr, int turnDir = 1);
// a page turning over the spine: t 0..1, dir +1 from the right page to the left (the next pages), -1 back. Its face is
// the old page it was, its back the new page it becomes, curling as it goes
void drawJournalLeaf(ContentDraw& cd, const mat4& frame, float w, float h, float bend, const JournalPic& oldPic, const JournalPic& newPic, float t, int dir, vec2 light, bool lit);
// "u0,v0,u1,v1;..." (the spread first, then the ribbons) for the network, and back
std::string journalLayoutString(const JournalPic& p);
void journalLayoutParse(const std::string& s, JournalPic& p);

} // namespace dq
