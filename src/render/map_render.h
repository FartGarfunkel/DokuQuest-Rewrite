// render/map_render.h — the pictures of paper maps (game/maps.js mapPaint: colours worked out from the tiles, so
// a resource pack recolours every map; shading by the lie of the land; water by its depth; N/E/S/W inked at the
// edges), the paper, the markers, and the map held in your hands (it comes up into view as you look down), drawn
// through HeldItemLayer::handsHook.
#pragma once
#include "sim/maps.h"
#include "render/content_draw.h"

namespace dq {

class GameSession;
constexpr int MAP_CELLS = 16;   // marker cells in the strip (see MapMarker::cell)

const std::vector<u8>& mapPicture(const PaperMap& m, bool force = false);   // RGBA 128 x 128
GLuint mapTexture(const PaperMap& m, bool force = false);
GLuint mapPaperTexture();       // 64 x 64, a darker rim
GLuint mapMarkTexture();        // 16 cells of 32 x 32 in a row
vec4 mapCellUV(int cell);
// the canvas drawing of one marker (maps.js mapMarker): arrow, dot, home, grave, village, dungeon, rift, pin
void mapMarker(Canvas& c, const std::string& kind, float x, float y, float sz, float rot, u32 col);
// a sheet of paper with the map on it and its markers, at M (view or render space), S across
void drawMapSheet(ContentDraw& cd, Level& lv, const PaperMap* m, const Player* viewer, const mat4& M, float S, vec2 light);
void registerHeldMap();         // installs the hands hook (once)

} // namespace dq
