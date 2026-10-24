// gen/trees.h — every kind of tree of the original (world/worldgen.js treeParts), shared by world
// generation and saplings growing.
#pragma once
#include "gen/gendefs.h"

namespace dq {

enum TreeKind : u8 { TREE_NONE = 0, TREE_OAK, TREE_BIRCH, TREE_SPRUCE, TREE_PINE, TREE_JUNGLE, TREE_ASPEN, TREE_AZALEA,
  TREE_ACACIA, TREE_DARK_OAK, TREE_CHERRY, TREE_MANGROVE, TREE_CRIMSON, TREE_WARPED, TREE_KIND_COUNT };
const char* treeKindName(u8 kind);       // "oak", "dark_oak", ... (the original's type strings)
u8 treeKindFromName(const std::string& s);

struct TreeSpec {
  int x = 0, y = 0, z = 0;   // base (the lowest log), world x/z, band-local y
  int ySeed = 0;             // the y its shape is seeded with (the original's world y; = y for saplings)
  u8 kind = TREE_OAK;
  double h = 0;              // 0..1 size roll
  bool bare = false, bees = false;
};
struct TreePart { double x, y, z; BlockId id; bool onlyAir; int meta; };   // meta -1 = leave as is
// Every block of a tree, in the original's order (some leaf balls sit on half-block centres).
void treeParts(const GenBlocks& B, i32 seed, const TreeSpec& t, std::vector<TreePart>& out);
// Writes parts into a band buffer of chunk (cx, cz) the way WorldGen.writeTree does (no ground check).
void writeTreeParts(const GenBlocks& B, GenBuf& b, int cx, int cz, const TreeSpec& t, const std::vector<TreePart>& parts);

} // namespace dq
