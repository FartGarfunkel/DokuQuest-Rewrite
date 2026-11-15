// render/vehicle_parts.h — the box models the vehicles are built from (render/vehicle_layer.cpp: minecarts, rowboats,
// sailboats, the sailing ship, windmills, waterwheels), shared so their items can be the same 3D things in the hand,
// the bag and set down (assets/item_models.cpp) instead of a picture.
#pragma once
#include "common/common.h"
#include "assets/voxel.h"
#include <array>
#include <functional>

namespace dq {

struct TileSet;

// one box of a model: centre, half size, turned by R; tiles per face (+x -x +y -y +z -z), model units (a block = 1)
struct ModelPart { vec3 c{0}, h{0}; mat3 R{1}; std::array<std::string, 6> tile; };

// the parts as a voxel grid of part numbers (1-based) and the colour of each face from the parts' tiles; lo = the grid's
// corner in model units. false if there's nothing to build.
bool partsGrid(const std::vector<ModelPart>& parts, float vox, const TileSet& ts, VoxelGrid& g, vec3& lo, VoxelFaceColor& fc);

// the parts of a vehicle or machine item, sat on its base (y 0) and centred: minecarts (with what they carry), the
// rowboats, the sailboat, the sailing ship, the waterwheel, the windmill. Empty if the item isn't one of them.
std::vector<ModelPart> vehicleItemParts(const std::string& item);

// a rowboat's oar handles where the rower holds them, as render/vehicle_layer.cpp draws the oars (rel: the boat's place
// in render space): [0] the right oar's, [1] the left's. false: not a rowboat
class Boat;
bool boatOarGrips(const Boat& b, vec3 rel, vec3 out[2]);

} // namespace dq
