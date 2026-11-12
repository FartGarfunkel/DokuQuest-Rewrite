// assets/gear3d.h — heroes and townsfolk in the round, the Hytale way (port of render/gear3d.js, the
// shape data; the characters workstream poses and draws them).
//
// A skin is flat: over it every hero wears real shapes — hair with volume, beards that hang, pointed ears,
// horns, tusks and tails, hoods and hats, robes whose skirts swing with the legs, sleeves, tabards, fur
// collars, sashes and cloaks — and worn armour grows pauldrons, a crest, a gorget, tassets and cuffs.
// The look travels inside the skin itself (gearRead: a spare corner of the 64x64 picture).
//
// Shapes are six-sided blocks given by their eight corners in model pixels (feet at y 0, the top of the
// head at 32, the chest toward -z, the right arm out along +x), each tied to a body part (it moves with
// that part, about gearPivots()) and a material (its colour: gearColor()). Voxelise with
// VoxelPainter::hexa (assets/voxel_paint.h) or paintGearShapes().
#pragma once
#include "assets/voxel.h"
#include <map>
#include <array>
#include <optional>

namespace dq {

struct TileSet;
class VoxelPainter;

struct GearLook {
  std::string species = "human", hair = "bald", beard = "none", horns = "none", ears = "round", outfit, hat = "none";
  bool hood = false, cloak = false, tusks = false, snout = false, tail = false, scales = false, nose = false, slim = false;
  bool robe = false, apron = false, tabard = false, helmHide = false, feather = false, elder = false, gloves = false, beltPick = false;
  vec3 hairCol{80, 50, 30}, beardCol{80, 50, 30}, hornCol{70, 60, 50}, skin{220, 180, 140};
  std::array<vec3, 4> cols{vec3(122, 106, 74), vec3(90, 122, 90), vec3(74, 74, 58), vec3(58, 36, 20)};
  vec3 extra{90, 122, 90}, hatCol{122, 106, 74};
  std::string shape, code;   // the bytes that decide the shapes / everything (seeds)
};
// the look a 64x64 skin carries (nullopt: a plain skin)
std::optional<GearLook> gearRead(const u8* rgba64x64);
// ... and written into a skin (the voxel character creator)
void gearWrite(u8* rgba64x64, const GearLook& L);

struct GearShape {
  std::string part;                    // head neck body skirtF skirtB armR foreR armL foreL legR shinR legL shinL cape capeLo tail
  std::string mat;                     // hair beard main trim legs leather fur gold horn skin bone leaf hat cloak feather extra (armour: am, at)
  std::array<vec3, 8> pts;             // corner (cx, cy, cz) is pts[cx*4 + cy*2 + cz]
  std::map<int, std::string> fm;       // faces painted with another material (face 5 = -z ... as the original's fm)
};
// The look's own shapes. aw: arm width (4 classic, 3 slim). helm / chest: a helmet or chestplate is worn
// (hair, hoods and hats go under a helmet; shoulder pieces, belts and cuffs under a chestplate).
std::vector<GearShape> gearShapes(const GearLook& L, float aw, bool helm, bool chest);
// Armour's extra pieces by kind (plate, mail, soft) and slot (0 helmet .. 3 boots); materials am / at
std::string gearArmorClass(const std::string& mat);
std::vector<GearShape> gearArmorShapes(const std::string& cls, int slot, float aw);
// where each part turns (model pixels)
std::map<std::string, vec3> gearPivots(float aw);
// height and breadth by species ([1, 1] = human)
vec2 gearStature(const std::string& species);
// the colour of a material at a point (a little weave or grain from the game's own pictures, like gearPaint)
u32 gearColor(const GearLook& L, const std::string& mat, vec3 p, const TileSet* ts = nullptr);
// paint shapes into a painter whose model space is in model pixels (shapes of one part, or all of them)
void paintGearShapes(VoxelPainter& P, const std::vector<GearShape>& S, const GearLook& L, const TileSet* ts = nullptr, const std::string& part = "");

} // namespace dq
