// assets/icons.h — inventory icons for every item and block, for the UI (ImGui images).
// Each item's 3D voxel model (assets/item_models) is rendered into an icon atlas from a 3/4 view, lit;
// blocks are little voxel cubes (the classic isometric view) with their micro-voxel colours. The flat
// 32px sprites stay as the fallback (no GL FBO / shader) and raw tiles are available for GUI bits.
#pragma once
#include "common/common.h"
#include <unordered_map>

namespace dq {

struct TileSet;

struct IconRef {
  void* tex = nullptr;      // ImTextureID (GL texture name)
  vec2 uv0{0}, uv1{1};
  bool valid() const { return tex != nullptr; }
};

class ItemIcons {
public:
  // after the tile set (and resource pack) is loaded; needs a GL context. Also resets the item models.
  void build(const TileSet& tiles);
  IconRef get(const std::string& itemKey) const;
  IconRef tile(int tileLayer) const;        // a raw tile (block faces, GUI bits)
  void release();
  int iconSize() const { return size; }     // pixels per icon in the atlas
private:
  void render3D(const TileSet& tiles, std::vector<u8>& out);   // out = the finished atlas (empty if it failed)
  unsigned tex = 0;                          // item icons
  int cols = 32, size = 64, atlasW = 0, atlasH = 0;
  std::unordered_map<std::string, int> slot;   // item key -> slot
  unsigned tileTex = 0;                      // raw tiles
  int tileCols = 64, tileSize = 32, tileW = 0, tileH = 0, tileCount = 0;
};

ItemIcons& itemIcons();

} // namespace dq
