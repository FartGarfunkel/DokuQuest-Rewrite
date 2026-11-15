// ui/world_canvas.h — pictures painted with the interface's own drawing (its fonts, item icons, shapes) onto a texture,
// to be shown in the world: the enchanting table's floating offers and the anvil's result card (ui/cinematic_table.cpp,
// render/station_cards.cpp), the pages of the books held, on lecterns and being written (ui/held_book.cpp). Each picture
// is kept by a key and painted again only when its version changes.
#pragma once
#include "common/common.h"
#include <functional>
#include <imgui.h>

namespace dq {

struct CanvasPic { unsigned tex = 0; int w = 0, h = 0; };
// the picture for `key` at version `ver` (w x h pixels); paint draws it (its top left at 0,0, transparent behind);
// painted again when the version changes. Call where GL may be used (a render layer, a screen's draw)
CanvasPic canvasPaint(const std::string& key, u64 ver, int w, int h, const std::function<void(ImDrawList*, ImVec2 size)>& paint);
void canvasForget(const std::string& keyPrefix);   // drop pictures no longer wanted

// the enchanting table's glyphs (the Standard Galactic Alphabet, drawn in strokes): one letter, or a run
// of them from a seed (n of them, wrapped in w)
void drawGlyph(ImDrawList* dl, int letter, ImVec2 at, float size, ImU32 col, float thick = 0);
void drawGlyphRun(ImDrawList* dl, u32 seed, int n, ImVec2 at, float size, float w, ImU32 col);

} // namespace dq
