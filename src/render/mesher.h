// render/mesher.h — turns a 16³ section (plus a 1-block apron) into vertex lists.
// Runs on worker threads: the main thread gathers a MeshInput copy, so no world locking.
#pragma once
#include "world/world.h"

namespace dq {

// 16 bytes per vertex.
#pragma pack(push, 1)
struct ChunkVertex {
  i16 x, y, z;      // section-relative, 1/32 block units (the micro-voxel grid)
  u16 layer;        // tile layer in the tile array
  u8 u, v;          // 1/32 tile units (may exceed 32 for merged faces; the shader wraps)
  u8 nf;            // normal index (bits 0-2), ambient occlusion 0..3 (bits 3-4), wave kind (bits 5-7: 0 none, 1 plant base-anchored, 2 leaves, 3 liquid surface, 4 hanging)
  u8 light;         // sky << 4 | block
  u8 r, g, b;       // tint (grass/foliage/water colour), 255 = none
  u8 mat;           // material flags: 1 emissive, 2 liquid, 4 cutout, 8 translucent, 16 no micro-voxel relief, 32 lava
};
#pragma pack(pop)
static_assert(sizeof(ChunkVertex) == 16, "vertex must be 16 bytes");

namespace WaveKind { enum : u8 { None = 0, Plant = 1, Leaves = 2, Liquid = 3, Hanging = 4 }; }
namespace Mat { enum : u8 { Emissive = 1, Liquid = 2, Cutout = 4, Translucent = 8, NoRelief = 16, Lava = 32 }; }

struct MeshInput {
  static constexpr int N = 18;                 // 16 + apron
  static int idx(int x, int y, int z) { return ((y + 1) * N + (z + 1)) * N + (x + 1); }   // x,y,z in -1..16
  int dim = 0, cx = 0, sy = 0, cz = 0;
  BlockId ids[N * N * N];
  u8 meta[N * N * N];
  u8 light[N * N * N];
  u8 climate[N * N * 2];                       // temp, humidity per apron column (x,z)
  bool empty = true;                           // nothing to draw
  vec3 origin() const { return vec3(cx * 16, dimBase(dim) + sy * 16, cz * 16); }
};

struct MeshOutput {
  std::vector<ChunkVertex> opaque, cutout, trans;   // quads (4 vertices each)
  bool empty() const { return opaque.empty() && cutout.empty() && trans.empty(); }
};

struct MesherOptions { bool fancyLeaves = true; bool wavingPlants = true; int plantDetail = 1; };

bool gatherMeshInput(const World& w, const SectionKey& k, MeshInput& in);   // false if the chunk isn't loaded
void buildSectionMesh(const MeshInput& in, MeshOutput& out, const MesherOptions& opt);

// grass/foliage/water tint from climate (temp, humidity 0..255) — 0xBBGGRR
u32 climateTint(const char* kind, u8 temp, u8 humid);

} // namespace dq
