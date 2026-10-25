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

} // namespace dq
