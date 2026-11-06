// assets/baked_colours.h — the colour of every tile and creature picture, carried inside the game.
// The bake is the 32×32 colours the voxels paint with. A launch that has the bake does not open those PNGs.
#pragma once
#include "assets/tileset.h"
#include <string>
#include <vector>

namespace dq {

struct BakedImage {
  std::string key;
  int w = 0, h = 0;
  std::vector<u8> rgba;
};

// Fills the tile set, the creature pictures and the biome colour maps from the bake linked into this
// program. False when this build has no bake: the caller reads the PNG files instead.
bool loadBakedColours(TileSet& ts);
// True after a bake has been read. Creature skins then paint from it with no pack open.
bool bakedColoursOn();
// Drops the picture table. The next tile load that misses the bake reads the PNG files.
void dropBakedPictures();
// A creature, armour or GUI picture from that same bake. False when the key was not baked.
bool bakedImage(const std::string& key, int& w, int& h, std::vector<u8>& rgba);

bool writeBakedColours(const fs::path& path, const TileSet& ts, const std::vector<BakedImage>& images,
                       int grassW, int grassH, const u8* grass, int foliageW, int foliageH, const u8* foliage);

} // namespace dq
