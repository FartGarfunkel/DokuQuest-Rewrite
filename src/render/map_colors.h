// render/map_colors.h — the colour of a block seen from above (game/maps.js mapColorOf / mapRGB): the average of its
// top tile (so a resource pack recolours every map), grass / leaves / water tinted by the climate. Used by the paper
// maps (render/map_render.cpp) and by the launcher's server map and isometric view (launcher/server_views.cpp).
// Main thread only (one cache, rebuilt when the tile set changes).
#pragma once
#include "assets/tileset.h"
#include "core/registry.h"

namespace dq {

std::string mapTileStamp(const TileSet& ts);                       // changes when the tiles do (pack, count)
int mapColorKind(const TileSet& ts, BlockId id);                    // 0 plain, 1 grass tint, 2 foliage tint, 3 water
void mapRGB(const TileSet& ts, BlockId id, u8 climate, float* out); // climate = temperature << 4 | humidity (4 bits each)

} // namespace dq
