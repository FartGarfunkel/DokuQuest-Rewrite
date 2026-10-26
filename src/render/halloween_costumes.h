// render/halloween_costumes.h — October villager costumes (own design): a witch hat, a ghost sheet, skeleton face paint, a
// vampire collar (+ the dark cape). Small procedural voxel pieces in the villagers' own part space (model pixels, like the
// skin parts), so they sit on the head / body the way the pumpkin heads and capes do. Who wears what is a hash of the
// villager's id: nothing is stored, and a villager in a pumpkin never gets one.
#pragma once
#include "assets/char_model.h"

namespace dq {

enum class Costume : u8 { None, WitchHat, GhostSheet, SkullPaint, Vampire };
// hh = the villager's hash (render/villager_layer.cpp); about 35% of all villagers are dressed (never one with a pumpkin head)
Costume villagerCostume(u32 hh, bool pumpkinHead);

enum HwPiece { HW_WITCH_HAT, HW_SKULL_FACE, HW_SHEET_HEAD, HW_SHEET_BODY, HW_COLLAR, HW_PIECE_COUNT };
// A piece (built on first use, GL thread). Head pieces: part space of CP_HEAD (about the neck, y up, front = -z);
// HW_SHEET_BODY: CharRig::body space (about the hip).
const CharMesh& hwPiece(int piece);

} // namespace dq
