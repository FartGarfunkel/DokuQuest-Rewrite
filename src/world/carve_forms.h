// world/carve_forms.h — slabs and stairs are chiseled blocks now: the base block (stone, planks...) with octants carved
// away. The old slab/stairs blocks still exist in the data (old saves, structures, mods), and are turned into their
// chiseled form wherever they enter the world: generation, loaded chunks, and any setBlock.
#pragma once
#include "common/common.h"
#include "core/registry.h"

namespace dq {

class Chunk;

struct CarveForm { BlockId base = 0; u8 carve = 0; u8 meta = 0; };
// a stairs block's corner (Minecraft's "shape"), above its facing (0-3) and upside-down (4) bits: inner (the high half
// round the corner as well) or outer (only the corner's quarter high), to the facing's left (counter-clockwise) or right
constexpr u8 STAIR_CORNER = 0x08, STAIR_OUTER = 0x10, STAIR_RIGHT = 0x20;

void registerChiselForms();                   // after the registry loads: the slab/stairs table; hides their items and recipes
bool isSlabOrStairs(BlockId id);
CarveForm carveFormOf(BlockId id, u8 meta);   // base == 0: not a slab/stairs (or one we keep, e.g. waterlogged)
void convertSlabsStairs(Chunk& c);            // a whole chunk, band-local (generation, old saves)

// the carve mask helpers (bit (hx<<2)|(hy<<1)|hz set = that octant carved away)
inline constexpr u8 octBit(int hx, int hy, int hz) { return (u8)(1u << ((hx << 2) | (hy << 1) | hz)); }

} // namespace dq
