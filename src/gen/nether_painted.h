// gen/nether_painted.h — terrain 13's painted Nether lands (docs/THIRD_PARTY_NOTICES.md):
// the Nether Jungle, Gravel Desert, Swampland, Magma Land, Soul Plain and Old Warped Woods, painted over the classic open
// Nether (gen/nether_exact.cpp) where a slow mask noise says so, each inside the classic land it suits (their own
// climate points: the jungle in warm crags, the swamp in the sulfur pools, the magma land among the volcanoes...).
// the classic generator is untouched (it is checked block for block): this is a pass after it, column by column, its
// blocks from tools/port/nether_biome_blocks.py. Terrain 12 and older never run it.
#pragma once
#include "common/common.h"

namespace nethergen { class NetherGen; }
namespace dq {
struct GenBuf;

enum BnLand : u8 { PN_NONE = 0, PN_JUNGLE, PN_GRAVEL_DESERT, PN_SWAMP, PN_MAGMA, PN_SOUL_PLAIN, PN_OLD_WARPED, PN_COUNT };
const char* pnLandName(int l);
u32 bnFogColour(int l);   // the painted land's fog (0xRRGGBB)

// the painted land over classic land `host` (gen/nether_lands.h NB_*) at temperature `temp`, or PN_NONE (pure: seed and place)
u8 pnLandFor(i32 seed, int x, int z, u8 host, double temp, bool v19 = false);   // v19 (terrain 19): the crags split by their own warmth
// where you stand (band-local y not needed: the lands are columns): PN_NONE below terrain 13
u8 netherBnLand(i32 seed, int genVersion, int x, int z);
// paints chunk (cx, cz) of the classic finished Nether (gen/nether_exact.cpp DepthsGen::generateExact, before the structures);
// trees: false near a structure (they'd poke through its walls)
void bnPaint(i32 seed, const nethergen::NetherGen& g, GenBuf& b, int cx, int cz, bool trees, bool strongHash = false);   // strongHash (terrain 19): no stripes

} // namespace dq
