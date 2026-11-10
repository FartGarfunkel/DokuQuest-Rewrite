# BTA 8.0.1 Nether generator port (`src/gen/bta/`)

An exact C++20 port of Better than Adventure 8.0.1's Nether world generation
(`minecraft:nether.default`): same seed, same blocks. It is standalone: no engine types, no globals with
static constructors, no external libraries. `namespace bta`.

| File | What |
|---|---|
| `jrandom.h` | `java.util.Random` (bit exact, incl. `nextGaussian` via a port of fdlibm `log`), Java cast/overflow helpers |
| `bta_noise.h/.cpp` | `ImprovedPerlinNoise` (all three legacy branches and the y-cache quirk), `SimplexNoise`, `WorleyNoise`, `FractalNoise2D/3D`, `Normalize2D`, `MathHelper` sin table/floor |
| `bta_nether.h` | public API |
| `bta_nether.cpp` | biome provider, `DensityGeneratorNether` + `TerrainGeneratorNether`, `SurfaceGeneratorNether`, `CavesLargeFeature(64,256)`, `LavaFloe`/`RubyglassFloe`/`Shelf` large features, terrain cache |
| `bta_nether_world.h/.cpp` | the decoration "World": BTA `World`/`Chunk` set/notify/schedule semantics and the block logic decoration triggers (fluid flow + hardening, fire, magma/brimthaw melting, vents, growth/flower/ash support checks) |
| `bta_nether_features.h/.cpp` | every `WorldFeature*` used by `ChunkDecoratorNether` |
| `bta_nether_deco.cpp` | `ChunkDecoratorNether` (47 decorations, selectors, placement methods, per-decoration seeds, `postDecorate` ash), neighbourhood models, decorated-region cache |
| `bta_nether_internal.h`, `bta_nether_stats.h` | internal |

Compile without `-ffast-math`, and without floating-point contraction (the `.cpp` files carry
`#pragma GCC optimize("fp-contract=off")` / `#pragma clang fp contract(off)`; on MSVC use `/fp:precise`
with `/fp:contract-` or `/fp:strict`). Plain x86-64 SSE2 code is exact; FMA contraction is not.

## API

```cpp
#include "gen/bta/bta_nether.h"

bta::NetherGen gen(seed);                       // optional: bta::NetherGenOptions (see below)
int    b = gen.biomeAt(x, y, z);                // bta::NetherBiome, = BTA world.getBlockBiome
double t = gen.temperatureAt(x, z);             // BiomeProviderNether.getTemperature, in [0,1]
auto* out = new bta::NetherGen::Out;            // 128 KB, keep it off the stack
gen.generateChunk(cx, cz, *out);                // final (decorated) chunk, thread-safe
uint8_t k = out->kind[(y << 8) | (z << 4) | x]; // bta::NB, y = BTA y 0..255
uint8_t m = out->meta[(y << 8) | (z << 4) | x];
const char* name = bta::nbName((bta::NB)k);     // "minecraft:block/cobble_netherrack"
```

Exact signatures:

```cpp
namespace bta {
enum NB : uint8_t { NB_AIR, ..., NB_OTHER, NB_COUNT };
const char* nbName(NB k);
uint16_t nbToBtaId(NB k);
NB nbFromBtaId(uint16_t btaId);
enum NetherBiome : uint8_t { NBIO_OLD_WORLD, NBIO_CRYSTAL_PLAINS, NBIO_CRYSTAL_FOREST, NBIO_SHELF, NBIO_CRAG,
                             NBIO_SULFUR_POOLS, NBIO_VOLCANIC_ISLANDS, NBIO_OLD_WORLD_DESERT, NBIO_COUNT };
const char* netherBiomeName(int biome);
enum class DecoMode : uint8_t { Region, FourNeighbours };
struct NetherGenOptions { DecoMode mode = DecoMode::Region; int regionSize = 4; int margin = 4;
                          int farMargin = 0; size_t terrainCache = 512; size_t regionCache = 16; };
class NetherGen {
 public:
  struct Out { uint8_t kind[16 * 256 * 16]; uint8_t meta[16 * 256 * 16]; };  // (y<<8)|(z<<4)|x
  explicit NetherGen(int64_t seed, const NetherGenOptions& opt = NetherGenOptions());
  int64_t seed() const;
  int biomeAt(int x, int y, int z) const;
  double temperatureAt(int x, int z) const;
  void generateChunk(int cx, int cz, Out& out);   // decorated
  void generateTerrain(int cx, int cz, Out& out); // undecorated (doBlockGeneration output)
  // validation helpers (raw BTA ids)
  void terrainIds(int cx, int cz, uint16_t* ids, int stages = 63) const;
  void chunkIds(int cx, int cz, uint16_t* ids, uint8_t* meta, DecoStats* stats = nullptr);
  void densityMap(int cx, int cz, double* out) const;
  void setDecorationMaskForTesting(uint64_t mask);
};
}
```

All public methods are thread-safe. Shared mutable state is only the two mutex-protected LRU caches
(undecorated chunks, and decorated regions with in-flight de-duplication); everything else is const.

## Biomes

`BiomeProviderNether`: temperature = 4-octave simplex (lacunarity 0.25, scale 1/60), downfall = 4-octave
simplex (lacunarity 1/3, scale 1/30), fuzz = 4-octave simplex (lacunarity 1/1.7, scale 1/6), then
`BiomeRangeMap`'s 50-step lookup table (last matching range wins). BTA stores biomes per chunk section at
16x2x16 with altitude `(16*section + half)/128`, so every block **below y = 64 is `old_world.desert`**
(soul sand surface), and y >= 64 is the climate biome. `biomeAt` reproduces exactly that.

## Decoration neighbourhood (`DecoMode`)

BTA decorates a chunk once its 8 neighbours exist; the decoration of chunk D starts at D*16+8 and spills
into D+1 (and further: rubyglass crystals lean up to ~3 chunks, nether pillars are centred on a Worley
cell centre up to ~3 chunks away). The final content of a chunk therefore depends on many chunks'
decorations *and their order*, which in BTA follows the player's loading order.

* `DecoMode::Region` (default): defines the world as decorated in row-major order (z outer, x inner) -
  the order BTA uses when chunks stream in row by row. A region of `regionSize`^2 chunks is produced by
  decorating the region plus `margin` chunks around it (and optionally, from `margin` to `farMargin`,
  only the long-reach crystal/pillar decorations) on a composite with one more ring of terrain, and is
  cached. Region borders are seamless (all writers into a region are included and run in the same global
  order); the only approximation is truncated history at the window edge.
* `DecoMode::FourNeighbours`: terrain of cx-2..cx+1 x cz-2..cz+1, then the decorations of (cx-1,cz-1),
  (cx,cz-1), (cx-1,cz), (cx,cz) in that order. Cheapest, but features that reach more than one chunk out
  are cut at chunk borders or missing (crystals, pillars).

Measured against Java BTA worlds (BTA's own classes) whose 24x24 chunks were all decorated in
row-major order; centre 8x8 chunks compared, % of decoration-touched blocks that differ:

| model | seed 1 | seed 12345 | seed -987654321 (crystal heavy) | ms/chunk* |
|---|---|---|---|---|
| Region 4, margin 3 | 0 | 0 | 0.48 % | 11 |
| Region 4, margin 4 (default) | 0 | 0 | 0.07 % | 14 |
| Region 4, margin 4, farMargin 6 | 0 | 0 | 0 | 27 |
| FourNeighbours | 37.6 % | 36.0 % | 35.9 % | 9 |

\* one core, amortised over a 24x24 sweep, terrain generation included (~3.3 ms/undecorated chunk).
A cached region is served in ~0.01 ms. Memory: terrain cache 64 KB/chunk (default 512 = 32 MB),
region cache 2 MB/region (default 16 = 32 MB), plus ~26 MB transient per region being decorated.

## Blocks (`bta::NB`) and metadata

| NB | BTA namespace id | BTA id | `meta` |
|---|---|---|---|
| `NB_AIR` | minecraft:block/air | 0 | 0 |
| `NB_BEDROCK` | minecraft:block/bedrock | 260 | 0 |
| `NB_NETHERRACK` | minecraft:block/netherrack | 803 | 0 |
| `NB_COBBLE_NETHERRACK` | minecraft:block/cobble_netherrack | 800 | 0 (the Nether's filler block) |
| `NB_COBBLE_NETHERRACK_CRYSTALLINE` | minecraft:block/cobble_netherrack_mossy (cobble.netherrack.crystalline) | 802 | 0 |
| `NB_LAVA_STILL` / `NB_LAVA_FLOWING` | minecraft:block/fluid_lava_still / _flowing | 273 / 272 | fluid level* |
| `NB_WATER_STILL` / `NB_WATER_FLOWING` | minecraft:block/fluid_water_still / _flowing | 271 / 270 | fluid level* |
| `NB_ACID_STILL` / `NB_ACID_FLOWING` | minecraft:block/fluid_acid_still / _flowing | 1063 / 1062 | fluid level* |
| `NB_OBSIDIAN` | minecraft:block/obsidian | 180 | 0 |
| `NB_SOULSAND` | minecraft:block/soulsand | 810 | 0 |
| `NB_SULFUR` | minecraft:block/sulfur | 1098 | 0 |
| `NB_BRIMSAND` | minecraft:block/brimsand | 1085 | 0 |
| `NB_BRIMSTONE` | minecraft:block/brimstone | 1086 | 0 |
| `NB_BRIMTHAW` | minecraft:block/brimthaw | 1092 | 0 |
| `NB_GLOOMSTONE` | minecraft:block/gloomstone | 1032 | 0 |
| `NB_COBBLE_GLOOMSTONE` | minecraft:block/cobble_gloomstone | 1033 | 0 |
| `NB_GLOWSTONE` | minecraft:block/glowstone | 820 | 0 |
| `NB_BLOCK_ASH` | minecraft:block/block_ash | 741 | 0 |
| `NB_LAYER_ASH` | minecraft:block/layer_ash | 723 | layer height - 1 (`& 7`; generation places 0 = thinnest) |
| `NB_SLATE` | minecraft:block/slate | 6 | 0 |
| `NB_BASALT` | minecraft:block/basalt | 2 | 0 (thermal vent rims) |
| `NB_COBBLE_BASALT` | minecraft:block/cobble_basalt | 12 | 0 (never generated; ore map key) |
| `NB_PUMICE_WET` / `NB_PUMICE_DRY` | minecraft:block/pumice_wet / _dry | 233 / 232 | 0 (wet: lava floes) |
| `NB_MAGMA` | minecraft:block/magma | 801 | 0 |
| `NB_RUBYGLASS_CRYSTAL` | minecraft:block/rubyglass_crystal | 823 | 0 (crystal-sea surface) |
| `NB_RUBYGLASS_COLUMN` | minecraft:block/rubyglass_column | 821 | 0 |
| `NB_RUBYGLASS_NODE` | minecraft:block/rubyglass_node | 826 | 0 |
| `NB_RUBYGLASS_GROWTH` | minecraft:block/rubyglass_growth | 825 | bits 0-2 growth direction**, bits 3-4 shape: 0 bud, 0x08 tall bottom, 0x10 tall top |
| `NB_ORE_NETHERCOAL_NETHERRACK` | minecraft:block/ore_nethercoal_netherrack | 420 | 0 |
| `NB_ORE_NETHERCOAL_BASALT` | minecraft:block/ore_nethercoal_basalt | 421 | 0 |
| `NB_ORE_NETHERCOAL_GLOOMSTONE` | minecraft:block/ore_nethercoal_gloomstone | 422 | 0 |
| `NB_BLOCK_NETHER_COAL` | minecraft:block/block_nethercoal | 436 | 0 |
| `NB_THERMAL_VENT` | minecraft:block/thermal_vent | 1025 | vent state 0 (dormant) .. 4 (venting)*** |
| `NB_FIRE` | minecraft:block/fire | 630 | fire age 0..15*** |
| `NB_FIRE_SULFURIC` | minecraft:block/fire_sulfuric | 634 | fire age 0..15*** |
| `NB_EMBER` | minecraft:block/ember | 1065 | 0 |
| `NB_BONE_PILE` | minecraft:block/bone_pile | 1030 | 0 |
| `NB_BOULDER_MAGMATIC` | minecraft:block/boulder_magmatic | 1060 | 0 |
| `NB_BOULDER_SULFURIC` | minecraft:block/boulder_sulfuric | 1061 | 0 |
| `NB_SOUL_CATCHER` | minecraft:block/soul_catcher | 1031 | 0 |
| `NB_OVERLAY_PEBBLES` | minecraft:block/overlay_pebbles | 880 | pebble count (`& 3`); never generated in the Nether |
| `NB_OTHER` | (anything else; never produced) | - | - |

\* BTA/b1.7.3 fluid data: 0 = source, 1..7 = flow level (distance from the source, 7 = thinnest),
bit 3 (8) = falling. Flowing blocks are left exactly as BTA leaves them after decoration; BTA ticks
them later (see below), so an engine with fluid simulation should schedule updates for `*_FLOWING`.
\** `Direction` id: 0 down, 1 up, 2 north (-z), 3 south (+z), 4 west (-x), 5 east (+x); the growth points
that way and is supported by the block on the opposite side.
\*** set from BTA's global `World.rand` (see below).

## What is exact

Validated against BTA 8.0.1's own classes run from the jar (Java harness: mock `World`, BTA's
`ChunkGeneratorNether`, `ChunkDecoratorNether`, real block logic):

* terrain stages (density map bit-exact, lerp, surface, caves, lava floe, rubyglass floe, shelf): 66 chunks
  over 3 seeds incl. far/negative coordinates, 0 differing blocks per stage;
* decoration (`FourNeighbours` mode = the harness's order): 213 chunks over 3 seeds (biome-stratified),
  0 differing block ids, 0 differing metas (excluding fire age / stacked vent state, which come from
  `World.rand`; even those match in 508 of 514 blocks);
* biomes (250k points) and `temperatureAt` (bit-exact doubles), MathHelper sin table (65536/65536);
* region mode vs a fully decorated Java world: see the table above.

## Not exact / not ported

* `World.rand`-dependent values: fire age (0/1 after its first tick), the state of a vent stacked on a
  vent, and the ignored outcomes (sounds, particles, item drops) consume BTA's global `World.rand`, whose
  state depends on everything that happened before. The port uses a per-chunk seeded stand-in; block ids
  never depend on it.
* Ordering: BTA's result at chunk borders depends on exploration order; this port fixes row-major order
  (Region) or the four-neighbour order. Writes outside the composite are dropped (BTA would generate and
  load the chunk there).
* Scheduled ticks that BTA queues instead of running immediately (after the first spring or lava pillar of
  a chunk's decoration, BTA's `scheduledUpdatesAreImmediate` is off for the rest of that chunk) are not
  run: BTA runs them later in normal world ticks.
* Not modelled because they cannot happen in generated Nether terrain: nether/drift portal creation by
  fire on obsidian / bedrock on glowstone, flammable-block fire spread (no Nether block is flammable),
  pebbles (they only spawn on overworld soils), the cave "replace surface" path (needs grass).
* Lighting/heightmaps are not produced.
