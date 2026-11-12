// render/creature_models.h — creature bodies as voxel models (Veloren / Cube World look). Every rig part
// (data/models.json) becomes a voxel box, 32 voxels to the block; its surface voxels take the part's face
// pictures — the original's code-painted tiles, or the resource pack's entity skin cut up box by box like
// render/packmobs.js — see-through pixels are carved out (a visual hull of the six pictures, so skeleton ribs
// and lion manes stay open), faces are recessed a voxel where their picture is dark (eyes, nostrils) and
// bigger boxes get their edges rounded. Built once per model (and pack), on the GL thread on first use.
#pragma once
#include "assets/voxel.h"
#include "sim/creature_rig.h"

namespace dq {

struct CreaturePartMesh {
  VoxelMeshGL gl;
  mat4 local{1};           // grid units -> part space (the box min..max)
  vec3 lo{0}, size{0};     // that box in part space (blocks)
  bool hidden = false;     // put away while the resource pack is on (packmobs.js `hide`) or nothing to draw
};
struct CreatureModelGL {
  std::string key, packSkin;   // packSkin: the pack picture used ("" = the original's tiles)
  std::vector<std::unique_ptr<CreaturePartMesh>> parts;   // same order as RigModel::parts
};
// The voxel model for a model key (MODEL_DEFS key: a creature type's model or variant). nullptr if unknown.
const CreatureModelGL* creatureModel(const std::string& key);
void creatureModelsReset();   // the tiles or the resource pack changed
// A free-standing voxel box from a tile (fauna, projectiles): size in voxels, picture per face like a part.
void buildTileBox(VoxelGrid& g, ivec3 n, const std::string& tile, bool glow = false);
// A creature's head as a mob head (placed or worn): its head part and everything fixed to it (eyes, horns, fronds),
// fitted into the unit cube standing on (0.5, 0, 0.5) and facing -z. Each entry: part index into CM->parts, matrix.
// false: that creature has no head (a slime, a fish).
bool creatureHeadParts(const std::string& mob, const CreatureModelGL*& CM, std::vector<std::pair<int, mat4>>& out);
// the same head as one voxel grid (CPU, any thread; for the mob head items): a block wide, standing on y 0, facing +z;
// voxel (x, y, z) covers (lo32 + (x, y, z)) / 32 in block units. false: no head.
struct TileSet; struct VoxelGrid;
bool creatureHeadGrid(const std::string& mob, const TileSet& ts, VoxelGrid& out, ivec3& lo32);

} // namespace dq
