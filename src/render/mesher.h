// render/mesher.h — turns a 16³ section (plus an apron of neighbours) into vertex lists.
// Port of the original world/mesher.js + world/plants3d.js geometry (light now comes from the
// persistent LightEngine instead of a per-mesh flood fill). Runs on worker threads: the main thread
// gathers a MeshInput copy, so no world locking.
//
// Files: mesher.cpp (gather, cubes with greedy merging, liquids, torches, the section loop),
// mesh_shapes.cpp (shaped blocks, models, redstone, rails), mesh_plants.cpp (3D voxel plants).
#pragma once
#include "world/world.h"

namespace dq {

struct TileSet;

// 20 bytes per vertex: four words the pack lightmap still reads, plus Cubyz's 5-bit RGB light.
//   a: x (14 bits) | y (14) << 14 | face normal (3) << 28 | Mat::NoRelief << 31   position: section-relative, 1/256 block, +16 blocks
//   b: z (14)      | tile layer (12) << 14 | tint (5) << 26 | Mat::Lava << 31
//   c: u (14)      | v (14) << 14 | Mat bits 0-3 << 28                         uv: tile units, 1/256, +16 tiles (a merged face spans 16)
//   d: sky light (8) | block light (8) << 8 | shade (8) << 16 | sway (7) << 24 | Mat::Flowing << 31
//   e: sun R,G,B then block R,G,B, 5 bits each (0..31). Shader packs do not read this word; d's two bytes stay
//      the 0..255 lightmap (31 -> 255) so eye brightness keeps a full sun.
// The tint isn't a colour any more but which one: 0 none, 1 grass, 2 foliage, 3 water (the shader reads the climate of
// the column from a texture and looks the colour up: render/world_renderer.cpp climate + tint table), 4..19 redstone
// dust at power 0..15. Water and dust take the whole picture (Mat::TintAll, set by the shader).
#pragma pack(push, 1)
struct ChunkVertex { u32 a = 0, b = 0, c = 0, d = 0, e = 0; };   // e: sun RGB, block RGB (5 bits each), bit 30 animated tile
#pragma pack(pop)
static_assert(sizeof(ChunkVertex) == 20, "vertex must be 20 bytes");
namespace Tint { enum : u8 { None = 0, Grass = 1, Foliage = 2, Water = 3, Dust = 4 }; }

namespace Sway { enum : u8 { None = 0, Plant = 1, Leaves = 2, Water = 3, Hang = 4 }; }
// Emissive: lit at full brightness; Liquid: water/lava/portal surface; Cutout: alpha-tested; Translucent: blended;
// NoRelief: no micro-voxel relief (plants, tilted parts); Lava: glows; Flowing: the flow picture scrolls (rivers,
// waterfalls); TintAll: the whole picture takes the tint (water, dust) instead of only its tint-marked pixels.
namespace Mat { enum : u8 { Emissive = 1, Liquid = 2, Cutout = 4, Translucent = 8, NoRelief = 16, Lava = 32, Flowing = 64, TintAll = 128 }; }
constexpr int NORMAL_NONE = 6;

struct MeshInput {
  static constexpr int N = 18, Y0 = -12, NY = 29;   // x,z -1..16; y -12..16 (deep enough to see how open the water is)
  static int idx(int x, int y, int z) { return ((y - Y0) * N + (z + 1)) * N + (x + 1); }
  int dim = 0, cx = 0, sy = 0, cz = 0;
  BlockId ids[N * N * NY];
  u8 meta[N * N * NY];
  u8 carve[N * N * NY];   // chisel octant mask (parallel to meta)
  u8 coat[N * N * NY];    // snow caps on a carved block (low 4 bits = columns, bit 4 = grown)
  u8 light[N * N * NY];
  LightRgb rgb[N * N * NY];                // sun + block RGB the smooth bias samples (parallel to light)
  u8 climate[N * N * 2];                       // temp, humidity per apron column (x,z)
  std::vector<std::pair<int, BlockId>> potPlants;   // idx -> plant in a flower pot (block entities)
  bool empty = true;                           // nothing to draw
  vec3 origin() const { return vec3(cx * 16, dimBase(dim) + sy * 16, cz * 16); }
};

// Things found while meshing that other systems want (world coordinates). The particles engine adds
// splashes at waterfall bases, drips under wet blocks and leaves, smoke/flames at emitters.
struct MeshFx {
  std::vector<ivec3> waterfalls;     // the lowest falling-water cell of each fall (splash goes on top of what's below)
  std::vector<ivec4> drips;          // x, y, z, kind: 1 water, 2 lava, 3 leaves (rain comes through), 4 honey
  std::vector<std::pair<ivec3, BlockId>> emitters;   // fire, torches, campfires, candles, portals
};

// cave culling: the bit for a pair of faces (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z), 15 pairs
inline int sectionVisPair(int a, int b) { if (a > b) std::swap(a, b); static const int base[6] = {0, 5, 9, 12, 14, 15}; return base[a] + (b - a - 1); }
u16 sectionVisibility(const MeshInput& in);   // flood fill of the open cells (mesher.cpp)
// blocks left out of the meshes while something else draws them (render/swing_layer.cpp: doors swinging); main thread
std::vector<ivec3>& meshHiddenBlocks();
struct MeshOutput {
  std::vector<ChunkVertex> opaque, cutout, trans;   // quads (4 vertices each)
  MeshFx fx;
  bool detail = true;          // built with 3D plants
  u16 vis = 0x7FFF;            // cave culling: which pairs of the section's 6 faces see each other through it (sectionVisPair)
  double ms = 0;               // time spent meshing
  bool empty() const { return opaque.empty() && cutout.empty() && trans.empty(); }
};

struct MesherOptions {
  bool fancyLeaves = true;
  bool wavingPlants = true;
  bool detail = true;          // 3D voxel plants (settings.plantDetail near/all decides per section)
  bool greedy = true;          // merge flat, evenly lit faces
};

bool gatherMeshInput(const World& w, const SectionKey& k, MeshInput& in);   // false if the chunk isn't loaded
void buildSectionMesh(const MeshInput& in, MeshOutput& out, const MesherOptions& opt);

// The tile pixels the plant builder reads (a copy: workers never touch App::tiles). Call after the tiles change.
void setMeshTiles(const TileSet& ts);
u64 meshTilesGeneration();

// climate colours (original climateGrass / climateWater): temp, humidity bytes (value*127+128) -> 0xBBGGRR
u32 climateGrass(u8 temp, u8 humid);
u32 climateWater(u8 temp, u8 humid);
u32 climateTint(const char* kind, u8 temp, u8 humid);   // "grass" | "foliage" | "water" (resource-pack colormap first)

} // namespace dq
