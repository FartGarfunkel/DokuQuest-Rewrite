// Internal: WorldFeature* ports used by ChunkDecoratorNether.
#pragma once
#include "bta_nether_world.h"

namespace bta {
namespace feat {
struct OreMap { int n; uint16_t from[4]; uint16_t to[4]; };
void rubyglassCrystalline(DecoWorld& w, JavaRandom& r, int x, int y, int z, bool ceiling, int minH, int hMod);
void ore(DecoWorld& w, JavaRandom& r, int x, int y, int z, const OreMap& map, int n);
void growthPatch(DecoWorld& w, JavaRandom& r, int x, int y, int z, float tallChance);
void perlinVein(DecoWorld& w, const ImprovedPerlin& noise, int sx, int sz, uint16_t place, uint16_t target, double scale,
                double thickness);
void wallClump(DecoWorld& w, int x, int y, int z, uint16_t place, uint16_t target);
void netherSpring(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t id);
void lake(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t liquid);
void terrace(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t fill, uint16_t wall, int minR, int rMod);
void pillar(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t block, bool doLava);
void boulderMagma(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void floorVent(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void spireVent(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void boulderGlowstone(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void roofSpire(DecoWorld& w, JavaRandom& r, int x, int y, int z, uint16_t boulder);
void glowstone(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void fire(DecoWorld& w, JavaRandom& r, int x, int y, int z, bool overrideVolcanic);
void emberAsh(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void scatterBonePile(DecoWorld& w, JavaRandom& r, int x, int y, int z);
void scatterBoulder(DecoWorld& w, int x, int y, int z, bool sulfuric);
void soulCatcherPatch(DecoWorld& w, JavaRandom& r, int x, int y, int z);
}  // namespace feat
}  // namespace bta
