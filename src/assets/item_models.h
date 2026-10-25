// assets/item_models.h — every item is a real 3D voxel model (render/models.js itemMesh with the
// original's later layers: items3d.js, sculpt.js, models3d.js).
//
//  * block items are their block as a micro-voxel model (32 voxels per block edge): cubes, shapes
//    (stairs, slabs, fences...), placed models (lanterns, campfires...) and the 3D torch;
//  * tools, weapons, food, gems... are built from parts like the original's models (a pickaxe is a round
//    handle with a wrapped grip, a collar and a curved head; a sword a pommel, grip, crossguard and a
//    bevelled blade), every part coloured from where it lies over the item's picture — the resource
//    pack's sprite when there is one, so pack items keep their look but stay voxels;
//  * potions, buckets, bowls, books, armour pieces and arrows have models of their own (sculpt.js);
//  * bows, fishing rods, ingots/bars and backpacks are built from parts (items3d.js);
//  * everything else is sculpted from its picture: each pixel stands as thick as the part of the
//    picture it belongs to is wide (blades thin, handles square, apples round).
//
// Models keep the original mesh frames (x, y across -0.5..0.5 for pictures; blocks centred on the origin,
// a block wide), so the original's hand, drop and frame transforms apply unchanged: draw with
//   model matrix = <the original's transform> * m.matrix().
#pragma once
#include "assets/voxel.h"

namespace dq {

struct TileSet;
class ThreadPool;

struct VoxelModel {
  enum Kind : u8 { Flat = 0, Block = 1 };
  std::string key;
  Kind kind = Flat;            // the original's im.kind: 'block' (block items) or 'flat' (pictures and part models)
  bool torch3d = false;        // the 3D torch (held upright)
  bool stand = false;          // stands on its foot (potions, buckets...)
  float depth = 0.04f;         // half its thickness, model units (how far it sticks out of a frame / off the ground)
  VoxelFrame frame;            // grid -> model space
  ivec3 size{0};               // grid size (voxels)
  VoxelMeshData data;          // CPU mesh (dropped after upload)
  VoxelMeshGL gl;
  bool built = false;          // finished building (it may still be empty: an item without a picture)
  bool ready() const { return gl.ready(); }
  mat4 matrix() const { return frame.matrix(); }
};

// The model for an item key (GL thread). The first call queues it on a worker; until it is ready the
// returned model has ready() == false (draw nothing or a fallback). Extra keys like the original's:
// "bow_pulling_0..2", "fishing_rod_cast", "fishing_rod_tp".
const VoxelModel& itemModel(const std::string& key);
// Build on this thread, CPU only (no GL) — used by the icon renderer and the workers.
bool buildItemModel(const std::string& key, VoxelModel& out);
// The tiles (or the resource pack) changed: drop every model; later builds read this tile set (copied).
void itemModelsReset(const TileSet& ts, ThreadPool* pool);
// Pump finished worker builds to the GPU (itemModel() does this too). Call once per frame.
void itemModelsUpdate();

bool isHandheld(const std::string& key);   // skins.js isHandheld: tools held by the handle, sticks, rods, staffs
float itemSize(const std::string& key);    // sculpt.js itemSize: how big each thing is next to the others

} // namespace dq
