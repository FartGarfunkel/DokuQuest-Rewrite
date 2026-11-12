// assets/shield3d.h — the shield, modelled (not sculpted from a picture): four upright planks bowed out a little in an
// iron rim with its corners cut, two leather straps on the back that the forearm goes through. Sized as held (one grid
// voxel = 1/32 block; it is drawn at full size on the forearm: render/char_layer.cpp shieldPlacement).
#pragma once
#include "assets/voxel.h"

namespace dq {
bool buildShieldGrid(VoxelGrid& g, VoxelFrame& fr, float& depth);
// the middle of the straps' loops (where the forearm's axis runs, along model x), model space in blocks
vec3 shieldArmChannel();
}  // namespace dq
