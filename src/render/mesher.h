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

// 20 bytes per vertex.
#pragma pack(push, 1)
struct ChunkVertex {
  i16 x, y, z;      // section-relative, 1/256 block
  u16 tile;         // bits 0-11 tile layer, bits 12-14 face normal (0..5 = +x -x +y -y +z -z; 6 = none: plants, tilted parts)
  u16 u, v;         // tile units * 256, offset by +64 tiles (the shader wraps; a merged face spans several tiles)
  u8 sway;          // wind/wave code (the original's SWAY codes): 0 none, 1 plant tip, 2 leaves, 3 water, 4..15 hanging (swing grows),
                    // 16..31 partway up a solid plant, 32..47 water surface + how open the water is, 48..63 / 64..79 cloth flapping across z / x
  u8 sky, blk;      // smooth light, level * 17 (0..255)
  u8 shade;         // face shade * ambient occlusion (0..255); liquids: shore foam (0..255)
  u8 r, g, b;       // climate tint (grass/foliage/water/dust); 255,255,255 = none
  u8 mat;           // Mat flags
};
#pragma pack(pop)
static_assert(sizeof(ChunkVertex) == 20, "vertex must be 20 bytes");

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
  u8 light[N * N * NY];
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

struct MeshOutput {
  std::vector<ChunkVertex> opaque, cutout, trans;   // quads (4 vertices each)
  MeshFx fx;
  bool detail = true;          // built with 3D plants
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
