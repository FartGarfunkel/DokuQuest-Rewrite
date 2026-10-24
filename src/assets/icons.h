// assets/icons.h — inventory icons for every item and block, for the UI (ImGui images).
// The full version renders each item's 3D voxel model (and blocks as little voxel cubes) into an
// icon atlas; until then icons are the flat 32px sprites.
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
  void build(const TileSet& tiles);         // after the tile set (and resource pack) is loaded; needs a GL context
  IconRef get(const std::string& itemKey) const;
  IconRef tile(int tileLayer) const;        // a raw tile (block faces, GUI bits)
  void release();
private:
  unsigned tex = 0;
  int cols = 64, size = 32, atlasW = 0, atlasH = 0;
  std::unordered_map<std::string, int> slot;   // item key -> slot
  int tileBase = 0;                          // first slot holding raw tiles
  int tileCount = 0;
};

ItemIcons& itemIcons();

} // namespace dq
