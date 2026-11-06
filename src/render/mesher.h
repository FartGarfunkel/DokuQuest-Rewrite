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
struct ChunkVertex { u32 a = 0, b = 0, c = 0, d = 0, e = 0; };
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

// A torch the chunk mesh left out, drawn as coloured voxels (render/torch_layer.cpp).
// x,y,z are the base of the stick in section-local blocks. cx,cy,cz is the cell, for its light.
struct TorchSpot {
  float x = 0, y = 0, z = 0;
  u8 cx = 0, cy = 0, cz = 0;
  i8 lx = 0, lz = 0;    // which way a wall torch leans (-1, 0, 1)
  u16 tile = 0;
};
// One small box the chunk mesh left out, in sixteenths: a candle, sea pickle, button, plate, a lever's base,
// a cake or a daylight detector. Sides use `tile`. The top and the bottom use their own pictures (a cake's
// icing, a detector's face); they match `tile` when the box had only one.
struct CandleSpot {
  float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
  float sink = 0;
  u8 cx = 0, cy = 0, cz = 0;
  u16 tile = 0;             // +X, and every side face when the box has one picture
  u16 nx = 0, pz = 0, nz = 0; // -X, +Z, -Z when a box names them (a piston's front)
  u16 top = 0, bot = 0;
  u8 only = 63;       // faces drawn (bit per face). A cactus hides the faces that would sit inside it.
  u16 faceRot = 0;    // two bits a face: the quarter-turn a piston's side picture needs. 0 leaves it as it is.
  i16 flame = -1;     // candle_flame layer when it is lit, else -1. -2 scaffolding, -3 a pot flower, -4 a ladder, -5 a campfire's four walls, -6 a fire block's two crosses, -7 a chain block's two link strips (only bit 0 stretches the top link). -8 redstone dust: only bits 0-3 are the arms (north, east, south, west) and bits 4-7 climb the next block, faceRot is the power, sink is the slab drop in pixels. -9 a minecart rail: only is the shape 0-9, faceRot is 1 when a powered rail is on, sink is the slab drop in pixels. -12 an amethyst cluster, a bud, or a spore blossom: x0 and z0 are the sideways shift, y0 is the turn, and only 3 draws three planes. <= -10 glows, except -12.
};
// A lever handle. x,y,z is its centre in section-local blocks. The three axes are the stick's
// (yy runs from the base toward the tip), the same ones the tilted box used.
struct LeverSpot {
  float x = 0, y = 0, z = 0;
  float xx = 1, xy = 0, xz = 0;
  float yx = 0, yy = 1, yz = 0;
  float zx = 0, zy = 0, zz = 1;
  u8 cx = 0, cy = 0, cz = 0;
  u16 tile = 0;
};
// A static prop (a pot, a chair, an anvil) the chunk mesh left out. model is an index into modelShape.
// facing is the same 0..3 turn rotBox uses. ox,oz and sink are the seat shift, in blocks and pixels.
struct ModelSpot {
  u8 cx = 0, cy = 0, cz = 0;
  u8 facing = 0;
  float sink = 0;
  float ox = 0, oz = 0;
  u16 model = 0;
};
// An end rod. axis is 0 up, 1 along x, 2 along z. Bit 0x80 is a lightning rod: the copper strip, not the end-rod picture.
struct RodSpot {
  u8 cx = 0, cy = 0, cz = 0;
  u8 axis = 0;
  float sink = 0;
  float ox = 0, oz = 0;
  u16 tile = 0;
};

// cave culling: the bit for a pair of faces (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z), 15 pairs
inline int sectionVisPair(int a, int b) { if (a > b) std::swap(a, b); static const int base[6] = {0, 5, 9, 12, 14, 15}; return base[a] + (b - a - 1); }
u16 sectionVisibility(const MeshInput& in);   // flood fill of the open cells (mesher.cpp)
// blocks left out of the meshes while something else draws them (render/swing_layer.cpp: doors swinging); main thread
std::vector<ivec3>& meshHiddenBlocks();
struct MeshOutput {
  std::vector<ChunkVertex> opaque, cutout, trans;   // quads (4 vertices each)
  MeshFx fx;
  std::vector<TorchSpot> torches;   // filled only when torchVox: the cutout quads were left out
  std::vector<CandleSpot> candles;
  std::vector<LeverSpot> levers;
  std::vector<ModelSpot> models;
  std::vector<RodSpot> rods;
  bool hasTorch = false;       // a torch, candle, button, plate, lever or near prop, whether or not it was voxelised
  bool torchVox = false;       // those torches are in `torches` instead of the cutout list
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
  bool torchVoxels = false;    // near torches become coloured voxels instead of cutout quads
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
