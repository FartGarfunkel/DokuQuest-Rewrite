// assets/tileset.h — the colour of every micro-voxel. Each tile is a 32x32 grid: one colour per
// micro-voxel on a block face (blocks are 32x32x32 micro-voxels). The built-in set is the original
// game's code-painted Doku tiles (data/tiles.png); a resource pack (GeruDoku etc., dropped in
// userDir()/resourcepacks) replaces tiles pixel-for-voxel. `height` gives each micro-voxel its
// relief (0 = flush with the block surface, up to 255 = deepest recess) for the voxel shading.
#pragma once
#include "common/fs.h"

namespace dq {

// An animated tile: the frames (tile layers in this set) are copied into `layer` as time passes.
// frameTime is in milliseconds (a resource pack's .mcmeta "frametime" is in ticks: x 50). The built-in Doku
// animations come from data/tile_anims.* (render/tile_anim.cpp); a pack that replaces a tile drops or
// replaces its animation (dropTileAnim) and adds its own from the .mcmeta.
struct TileAnim { int layer = 0; std::vector<int> frames; int frameTime = 50; };

struct TileSet {
  int size = 32;                 // micro-voxels per block edge
  int count = 0;
  std::vector<u8> rgba;          // count * size*size * 4
  std::vector<u8> height;        // count * size*size
  std::vector<u8> emissive;      // count * size*size (glowing micro-voxels: lava, ores, torch flames), 0..255
  std::vector<u8> res;           // count: pixels across the source picture (16 = code-painted at 16 and doubled, 32 = full)
  std::unordered_map<std::string, int> index;   // tile name -> layer
  std::string packName;          // resource pack applied ("" = built-in)
  u8* tile(int i) { return rgba.data() + (size_t)i * size * size * 4; }
  const u8* tile(int i) const { return rgba.data() + (size_t)i * size * size * 4; }
  u32 avgColor(int i) const;     // 0xAABBGGRR, for LOD and particles
  std::vector<TileAnim> anims;   // animated tiles (water, lava, fire, portals...)
};
inline void dropTileAnim(TileSet& ts, int layer) { for (size_t i = ts.anims.size(); i-- > 0;) if (ts.anims[i].layer == layer) ts.anims.erase(ts.anims.begin() + i); }

bool loadBuiltinTiles(TileSet& ts);          // data/tiles.png + data/tiles.json
void deriveMicroVoxelHeights(TileSet& ts);   // relief from colour (mortar/cracks recess, bevels raise)
// resolves settings.resourcePack ("auto" = GeruDoku if present) and applies it (assets/packs.cpp)
std::string applyResourcePack(TileSet& ts, const std::string& choice);
std::vector<std::string> listResourcePacks();
int appendTile(TileSet& ts, const u8* rgba32x32);   // adds a tile (animation frames), returns its layer

// Biome colour from the active pack's colormap (textures/colormap/grass.png or foliage.png), Minecraft's
// lookup: x = (1 - temp), y = (1 - humid * temp). kind "grass"/"foliage"; ok = false when no pack colormap.
// Same packing as climateTint (r | g << 8 | b << 16). Safe to call from mesher threads.
u32 packTint(const char* kind, u8 temp, u8 humid, bool& ok);

// decode/encode PNG (stb)
bool loadPng(const fs::path& p, int& w, int& h, std::vector<u8>& rgba);
bool loadPngMem(const u8* data, size_t n, int& w, int& h, std::vector<u8>& rgba);
bool savePng(const fs::path& p, int w, int h, const u8* rgba);

} // namespace dq
