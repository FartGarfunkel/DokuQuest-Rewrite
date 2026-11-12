// assets/item_build.h — internal to the item model builders (item_models.cpp, item_parts.cpp).
#pragma once
#include "assets/item_models.h"
#include "assets/voxel_paint.h"
#include "assets/tileset.h"
#include "core/registry.h"
#include <regex>
#include <unordered_map>

namespace dq {

// a model being built: the painter (model-space shapes -> voxels), the item's picture, the tiles
struct ItemBuild {
  const TileSet& ts;
  VoxelPainter& P;
  const SpriteInfo& I;
  const std::string& key;
  const ItemDef* def;
  float depth = 0.04f;
  bool stand = false;
  VoxelModel::Kind kind = VoxelModel::Flat;
  int tile(const std::string& name) const { auto it = ts.index.find(name == "white_wool" ? "wool" : name); return it == ts.index.end() ? 0 : it->second; }
};

// std::regex_search with the pattern compiled once per thread (patterns are string literals)
inline bool rxSearch(const std::string& s, const char* pattern) {
  thread_local std::unordered_map<const char*, std::regex> cache;
  auto it = cache.find(pattern);
  if (it == cache.end()) it = cache.emplace(pattern, std::regex(pattern)).first;
  return std::regex_search(s, it->second);
}

// which part model an item gets ("" = none): models3d.js model3dKind, sculpt.js modelKind
std::string model3dKind(const std::string& key, const ItemDef* def);
std::string modelKind(const std::string& key, const ItemDef* def);
bool buildModel3d(const std::string& kind, ItemBuild& b);   // T3D families (tools, weapons, food, gems...)
bool buildModelM3d(const std::string& kind, ItemBuild& b);  // potion, bucket, bowl, book, armor, arrow
// items3d.js: ingots/bars, bows ("bow", "bow_pulling_N"), fishing rods, backpacks. false = not one of them
bool buildItems3d(ItemBuild& b);

} // namespace dq
