// assets/pottery3d.h — 3D voxel models for the immersive pottery (vessel, jug, pot, bowl, pan, spindle head, crucible,
// every mold with its cavity, filled molds showing their metal) and the firestarter.
#pragma once
#include "assets/voxel.h"
#include <string>

namespace dq {
// false: not one of ours (the caller builds it the usual way). metal: a filled mold's metal colour (nullptr: empty).
bool buildPotteryGrid(const std::string& key, VoxelGrid& g, VoxelFrame& fr, float& depth, bool& stand, const vec3* metal = nullptr);
}
