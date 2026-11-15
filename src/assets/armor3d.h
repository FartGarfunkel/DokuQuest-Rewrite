// assets/armor3d.h — worn armour from 3D armour sets (.geo.json geometry + picture, the imported fantasy armour, brought into
// the pack's assets/dokuquest/armor3d/ by the armour import tool in tools/port/). Each of the game's metals is drawn as one of the sets,
// its grey metal recoloured to the metal (cloth, leather and trims keep their own colours): the pack's own armour pictures
// still go first where it has them (leather, chainmail). Built per body part in that part's frame, like the pack shells.
#pragma once
#include "assets/voxel.h"
#include <map>
#include <string>

namespace dq {

class PackFS;

int loadArmor3d(const PackFS& P);                    // the sets in the pack (and the game's own pack); returns how many
bool armor3dHas(const std::string& mat);             // a 3D set for this metal?
// the voxels of one slot (0 helmet .. 3 boots) for a metal, by our body part ("head", "body", "armR_u", "armR_d", "armL_u",
// "armL_d", "legR_u", "legR_d", "legL_u", "legL_d"): a grid and its low corner in that part's frame (skin px from its pivot)
struct Armor3dPart { VoxelGrid g; vec3 lo{0}; };
// tint: the piece's colour (its icon's average; x < 0 keeps the set's own)
std::map<std::string, Armor3dPart> armor3dSlot(const std::string& mat, int slot, float armW, int vpp, vec3 tint = vec3(-1));

} // namespace dq
