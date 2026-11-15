// gen/nether_bn.h — terrain 13's Nether lands after BetterNether (github.com/quiqueck/BetterNether, MIT; docs/CREDITS.md):
// its Nether Jungle, Gravel Desert, Swampland, Magma Land, Soul Plain and Old Warped Woods, painted over BTA's open
// Nether (gen/nether_exact.cpp) where a slow mask noise says so, each inside the BTA land it suits (BetterNether's own
// climate points: the jungle in warm crags, the swamp in the sulfur pools, the magma land among the volcanoes...).
// BTA's own generator is untouched (it is checked block for block): this is a pass after it, column by column, its
// blocks from tools/port/betternether_blocks.py. Terrain 12 and older never run it.
#pragma once
#include "common/common.h"

namespace nethergen { class NetherGen; }
namespace dq {
struct GenBuf;

enum BnLand : u8 { BN_NONE = 0, BN_JUNGLE, BN_GRAVEL_DESERT, BN_SWAMP, BN_MAGMA, BN_SOUL_PLAIN, BN_OLD_WARPED, BN_COUNT };
const char* bnLandName(int l);
u32 bnFogColour(int l);   // BetterNether's fog for the land (0xRRGGBB)

// the BetterNether land over BTA land `host` (gen/nether_lands.h NB_*) at temperature `temp`, or BN_NONE (pure: seed and place)
u8 bnLandFor(i32 seed, int x, int z, u8 host, double temp, bool v19 = false);   // v19 (terrain 19): the crags split by their own warmth
// where you stand (band-local y not needed: the lands are columns): BN_NONE below terrain 13
u8 netherBnLand(i32 seed, int genVersion, int x, int z);
// paints chunk (cx, cz) of BTA's finished Nether (gen/nether_exact.cpp DepthsGen::generateExact, before the structures);
// trees: false near a structure (they'd poke through its walls)
void bnPaint(i32 seed, const nethergen::NetherGen& g, GenBuf& b, int cx, int cz, bool trees, bool strongHash = false);   // strongHash (terrain 19): no stripes

} // namespace dq
