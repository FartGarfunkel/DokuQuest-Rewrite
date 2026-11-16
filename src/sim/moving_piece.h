// sim/moving_piece.h — what a hinge or slider moves and where it goes (moving structures, stage 1: see
// docs/superpowers/specs/2026-11-16-moving-structures-design.md §2). The piece: the block glued to the machine's moving face
// and everything glued on from there (never the machine itself), at most 512 blocks. A hinge turns it in whole quarter turns
// about the machine's facing through the machine's middle (facing up or down: clockwise seen from above; facing east or
// west: a positive quarter takes up to south; north or south: up to east). A slider moves it `distance` blocks along its face.
#pragma once
#include "world/world.h"
#include <string>
#include <vector>

namespace dq {

struct Motion {
  int kind = 0;               // 0 hinge, 1 slider
  ivec3 machine{0}; int face = 2;   // the machine block and its moving face (0 east, 1 west, 2 up, 3 down, 4 south, 5 north)
  int quarters = 1;           // hinge: signed quarter turns
  int distance = 1;           // slider: blocks along the face
};
struct Piece { std::vector<ivec3> cells; };

constexpr int PIECE_MAX = 512;
bool gatherPiece(const World& w, const Motion& m, Piece& out, std::string* why);
ivec3 movedCell(const Motion& m, ivec3 cell, float fraction = 1.f);   // where a cell is `fraction` of the way through the move
int motionAxis(const Motion& m);   // 0 upright (about y), 1 about east-west, 2 about north-south
bool pathClear(const World& w, const Motion& m, const Piece& p, std::vector<ivec3>* breakable, ivec3* blockedAt);

} // namespace dq
