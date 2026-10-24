// assets/voxel.h — voxel grids and their meshes, shared by items, dropped items, the first-person arm
// and (later) creatures and characters. A grid holds one RGBA colour per voxel; the mesher turns it
// into greedy-merged quads with per-voxel colours and baked ambient occlusion (no textures), and the
// voxel shader (assets/shaders/voxel.glsl) lights them like the world.
//
//   VoxelGrid g({16, 32, 8});  g.set(x, y, z, rgba(200, 40, 40));
//   VoxelMeshData md; buildVoxelMesh(g, md);          // any thread
//   VoxelMeshGL gm; gm.upload(md);                     // GL thread
//   voxelRenderer().begin(&frame, frame.cam.viewProj); voxelRenderer().draw(gm, modelMatrix, light);
//
// The model matrix maps GRID units (one unit = one voxel, origin at the grid's corner) to render
// space; VoxelFrame::matrix() gives the grid -> model-space part.
#pragma once
#include "common/common.h"
#include <memory>

namespace dq {

class Mesh;
class Shader;
struct FrameCtx;

constexpr u8 VOXEL_SOLID = 255;     // alpha of an ordinary voxel
constexpr u8 VOXEL_GLOW = 254;      // alpha of a voxel that glows (unlit: lava, flames, gems' light)
inline u32 voxRGBA(int r, int g, int b, u8 a = VOXEL_SOLID) { return (u32)clampv(r, 0, 255) | (u32)clampv(g, 0, 255) << 8 | (u32)clampv(b, 0, 255) << 16 | (u32)a << 24; }
inline u32 voxShade(u32 c, float k) {   // brighten/darken keeping alpha
  if (!c) return 0;
  return voxRGBA((int)((c & 255) * k + 0.5f), (int)(((c >> 8) & 255) * k + 0.5f), (int)(((c >> 16) & 255) * k + 0.5f), (u8)(c >> 24));
}

struct VoxelGrid {
  ivec3 size{0};
  std::vector<u32> c;      // x fastest, then y, then z; 0 = empty
  VoxelGrid() = default;
  explicit VoxelGrid(ivec3 s) { resize(s); }
  void resize(ivec3 s) { size = s; c.assign((size_t)s.x * s.y * s.z, 0); }
  bool in(int x, int y, int z) const { return (unsigned)x < (unsigned)size.x && (unsigned)y < (unsigned)size.y && (unsigned)z < (unsigned)size.z; }
  size_t idx(int x, int y, int z) const { return ((size_t)z * size.y + y) * size.x + x; }
  u32 at(int x, int y, int z) const { return in(x, y, z) ? c[idx(x, y, z)] : 0; }
  bool solid(int x, int y, int z) const { return at(x, y, z) != 0; }
  void set(int x, int y, int z, u32 col) { if (in(x, y, z)) c[idx(x, y, z)] = col; }
  bool bounds(ivec3& lo, ivec3& hi) const;   // inclusive-exclusive box of the solid voxels; false if empty
  size_t count() const;
};

// where a grid sits in model space: voxel (i, j, k) covers origin + [i, i+1) * voxel (per axis)
struct VoxelFrame {
  vec3 origin{0};
  float voxel = 1.f / 32.f;
  mat4 matrix() const;       // grid units -> model space
  vec3 toModel(vec3 g) const { return origin + g * voxel; }
};

// one vertex of a voxel quad: 12 bytes. Positions in grid units (0..65535).
struct VoxelVertex { u16 x, y, z; u8 nao; u8 pad; u32 rgba; };   // nao = face (0..5: +x -x +y -y +z -z) | ao << 3 (0 dark .. 3 open)
struct VoxelMeshData {
  std::vector<VoxelVertex> verts;     // 4 per quad, counter-clockwise from outside
  size_t quads() const { return verts.size() / 4; }
};
// greedy-merge the exposed faces of the solid voxels (thread-safe, no GL). By default a face takes its
// voxel's colour; faceColor (optional) picks the colour per face instead — then voxel values may be
// material ids (e.g. "box n of a block model"). A faceColor of 0 leaves that face out.
using VoxelFaceColor = std::function<u32(int x, int y, int z, int face, u32 voxel)>;
void buildVoxelMesh(const VoxelGrid& g, VoxelMeshData& out, const VoxelFaceColor* faceColor = nullptr);

// a mesh on the GPU
struct VoxelMeshGL {
  VoxelMeshGL();
  ~VoxelMeshGL();
  VoxelMeshGL(const VoxelMeshGL&) = delete; VoxelMeshGL& operator=(const VoxelMeshGL&) = delete;
  void upload(const VoxelMeshData& d);
  void release();
  bool ready() const { return quads > 0; }
  size_t quads = 0;
  std::unique_ptr<Mesh> mesh;
};

// the shared voxel/entity shader
struct VoxelLight { float sky = 1, block = 0; };
class VoxelRenderer {
public:
  bool init();               // lazily called by begin()
  // world pass: lighting, fog and time from the frame; viewProj maps render space (camera-relative)
  void begin(const FrameCtx& f, const mat4& viewProj, bool fog = true);
  // neutral pass (icons, previews): fixed bright light, no fog
  void beginNeutral(const mat4& viewProj, vec3 lightDir = vec3(0.35f, 0.9f, 0.55f));
  // model: grid units -> render space. tint.a > 0 mixes the colour toward tint.rgb (hurt flash).
  void draw(const VoxelMeshGL& m, const mat4& model, VoxelLight light = {}, vec4 tint = vec4(0), float glint = 0);
  void end();
  Shader* shader() { return sh.get(); }
private:
  std::unique_ptr<Shader> sh;
  bool tried = false;
  double time = 0;
};
VoxelRenderer& voxelRenderer();

} // namespace dq
