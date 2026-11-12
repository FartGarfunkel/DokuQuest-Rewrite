// world/rug.h — a carpet laid as a rug, the way carpet is used: not a block of its own in the room but the floor's
// covering, so a table, a chair, a bed or anything stands on it. It's the floor block's coat byte: bit 7, and which
// carpet in the low bits (the snow on a carved top is a carved block's coat and never sets bit 7). render/mesher.cpp
// draws it on the floor's top, in the cell above. The codes stay the same for saves: the sixteen colours in their usual
// order, then moss.
#pragma once
#include "core/registry.h"

namespace dq {

constexpr u8 RUG_BIT = 0x80;
inline bool isRugCoat(u8 coat) { return (coat & RUG_BIT) != 0; }
u8 rugCodeOf(BlockId carpet);           // the coat for a rug of this carpet; 0: it isn't one that can be a rug
BlockId rugBlockOf(u8 coat);            // the carpet a rug coat is (0: none)
bool rugFloor(BlockId id, u8 carve);    // can this block hold a rug: whole, uncarved, solid and opaque

} // namespace dq
