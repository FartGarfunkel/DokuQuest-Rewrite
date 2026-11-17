// sim/glue.h — glue between two blocks' faces (moving structures: docs/superpowers/specs/2026-11-16-moving-structures-design.md).
// A hinge or slider moves what's glued to its moving face and everything glued on from there. Each block keeps a mask of
// its glued faces (bit f = face f: 0 east +x, 1 west -x, 2 up, 3 down, 4 south +z, 5 north -z) in its block entity's
// "glue"; a face is glued only when both blocks say so, so breaking either one ends it.
#pragma once
#include "sim/level.h"

namespace dq {

bool glueable(BlockId id);                                // not air, liquids, fire or unbreakable blocks
u8 glueMask(const World& w, ivec3 p);
bool glued(const World& w, ivec3 a, int face);
bool setGlue(World& w, ivec3 a, int face, bool on);       // both sides; false when either block can't take glue
ivec3 faceDir(int face);
void registerGlue();                                      // using glue on a face sticks it (again: unsticks it)

} // namespace dq
