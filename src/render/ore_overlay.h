// render/ore_overlay.h — every ore takes on the rock round it: an ore block is drawn as the natural rock beside it
// (the most of its six neighbours), with the ore's own specks over it. The specks are each ore picture's pixels that
// aren't the stone it was painted on (found by matching the picture against the stones; an ore with no stone in its
// picture, as ancient debris, is left as it is). Nothing in the world changes: the block, its drops and saves are the
// ore's; only how it looks follows its rock.
#pragma once
#include "core/registry.h"
#include <memory>
#include <vector>

namespace dq {
struct TileSet;
void buildOreOverlays(TileSet& ts);   // after the pack and the player's tiles, before the relief is baked (client/app.cpp)
// by block id: the ore's specks (a tile layer, -1: not an ore drawn this way), the stone its picture was painted on
// (drawn as it is when that's its rock), natural rock an ore can take on. A snapshot: take it once per section.
struct OreTables {
  std::vector<int> overlay; std::vector<BlockId> base; std::vector<u8> host;
  int specks(BlockId id) const { return id < overlay.size() ? overlay[id] : -1; }
  BlockId baseOf(BlockId id) const { return id < base.size() ? base[id] : 0; }
  bool isHost(BlockId id) const { return id < host.size() && host[id]; }
};
std::shared_ptr<const OreTables> oreTables();
}
