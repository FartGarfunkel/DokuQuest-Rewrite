// assets/pack_armor.h — worn armour in the resource pack's own pictures (port of render/packarmor.js).
//
// Minecraft paints armour as two pictures laid out like an old 64x32 skin: layer 1 holds the helmet,
// the chestplate (body and both arms) and the boots, layer 2 the leggings (the belt on the body and both
// legs). When the pack has them, worn armour is built from the boxes below, a pixel (half a pixel for
// the leggings) out from the body the way Minecraft does it, painted with the pack's pictures; leather
// takes its dye with the pack's undyed overlay on top. Our own metals keep their own look.
//
// This module provides the data (pictures, boxes) and a voxeliser; the characters workstream draws it.
#pragma once
#include "assets/voxel.h"

namespace dq {

class PackFS;

struct ArmorLayer { int w = 0, h = 0; std::vector<u8> rgba; };   // a 64x32-layout picture (w = 64 * scale)
// a box of worn armour in its body part's frame, in skin pixels (packarmor.js packArmorMeshes):
// the Minecraft box unwrap at (u, v) of size w x h x d; slice = the part of the side faces' height used
struct ArmorPiece { std::string name; int layer; vec3 lo, hi; int u, v, w, h, d; bool mirror; vec2 slice{0, 1}; };

int loadPackArmor(const PackFS& P);     // called by applyResourcePack; returns the number of materials found
void resetPackArmor();
bool packArmorHas(const std::string& mat);                 // leather, iron, gold, diamond, cinderite, copper
std::string armorMatOf(const std::string& itemKey);        // "iron_chestplate" -> "iron" (skins.js armorMat)
// the picture for a material and layer (1 or 2); leather is dyed (dye "RRGGBB", "" = undyed) with its overlay on top
const ArmorLayer* packArmorLayer(const std::string& mat, int layer, const std::string& dye = "");
// does the pack's chestplate leave the forearms bare? (then the cuff is cut from its body: cuffB)
bool packArmorBareArms(const std::string& mat);
// helmet, chest, waist, pad, padL, cuff, cuffL, thigh, thighL, knee, kneeL, boot, bootL (cuff = cuffB when bareArms)
const std::vector<ArmorPiece>& packArmorPieces(bool bareArms = false);

// colour of a skin box's face (0..5 = +x -x +y -y +z -z) at (s, t) in 0..1 of that face (addSkinBox's
// mapping), from a picture of TW x TH layout units (64 x 32 for armour, 64 x 64 for skins); 0 = clear
u32 skinBoxColor(const ArmorPiece& p, const u8* rgba, int W, int H, int TW, int TH, int face, float s, float t);
// fill the shell of a skin box into g, starting at grid cell `at`, vpp voxels per skin pixel
void paintSkinBox(VoxelGrid& g, ivec3 at, const ArmorPiece& p, const u8* rgba, int W, int H, int TW, int TH, int vpp);

} // namespace dq
