// assets/voxel.cpp — greedy voxel mesher with baked AO, GPU meshes and the shared voxel shader.
#include "assets/voxel.h"
#include "render/gl.h"
#include "render/frame.h"
#include "render/pack_geom.h"
#include <glm/gtc/matrix_transform.hpp>

namespace dq {

bool VoxelGrid::bounds(ivec3& lo, ivec3& hi) const {
  lo = size; hi = ivec3(0);
  for (int z = 0; z < size.z; z++) for (int y = 0; y < size.y; y++) {
    const u32* row = &c[idx(0, y, z)];
    for (int x = 0; x < size.x; x++) if (row[x]) { lo = glm::min(lo, ivec3(x, y, z)); hi = glm::max(hi, ivec3(x + 1, y + 1, z + 1)); }
  }
  return hi.x > lo.x;
}
size_t VoxelGrid::count() const { size_t n = 0; for (u32 v : c) n += v != 0; return n; }

mat4 VoxelFrame::matrix() const { return glm::scale(glm::translate(mat4(1), origin), vec3(voxel)); }

// ------------------------------------------------------------------ greedy mesher
namespace {
// unit-cube corners of each face, counter-clockwise seen from outside (same table as the world mesher)
const int FC[6][4][3] = {
  {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
  {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
  {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
  {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
  {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
  {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
};
}

void buildVoxelMesh(const VoxelGrid& g, VoxelMeshData& out, const VoxelFaceColor* faceColor) {
  out.verts.clear();
  ivec3 lo, hi;
  if (!g.bounds(lo, hi)) return;
  for (int f = 0; f < 6; f++) {
    const int a = f / 2, sgn = (f & 1) ? -1 : 1;
    const int ua = (a + 1) % 3, va = (a + 2) % 3;     // the two in-plane axes
    const int nu = hi[ua] - lo[ua], nv = hi[va] - lo[va];
    std::vector<u64> mask((size_t)nu * nv);
    ivec3 n(0); n[a] = sgn;
    ivec3 eu(0), ev(0); eu[ua] = 1; ev[va] = 1;
    for (int s = lo[a]; s < hi[a]; s++) {
      // 1) faces of this slice: key = colour | ao (4 corners x 2 bits) << 32 | 1 << 40
      bool any = false;
      for (int v = 0; v < nv; v++) for (int u = 0; u < nu; u++) {
        ivec3 p(0); p[a] = s; p[ua] = lo[ua] + u; p[va] = lo[va] + v;
        u64 key = 0;
        u32 col = g.at(p.x, p.y, p.z);
        if (col) {
          ivec3 q = p + n;
          // a face shows against air, and (glass hiding nothing) against glass, unless it is glass itself
          const u32 nb = g.at(q.x, q.y, q.z);
          const bool open = !nb || ((nb >> 24) == VOXEL_GLASS && (col >> 24) != VOXEL_GLASS);
          if (faceColor && open) col = (*faceColor)(p.x, p.y, p.z, f, col);
          if (col && open) {
            u32 ao = 0;
            for (int k = 0; k < 4; k++) {
              const int* cc = FC[f][k];
              int du = cc[ua] ? 1 : -1, dv = cc[va] ? 1 : -1;
              ivec3 s1 = q + eu * du, s2 = q + ev * dv, cr = q + eu * du + ev * dv;
              int b1 = g.solid(s1.x, s1.y, s1.z), b2 = g.solid(s2.x, s2.y, s2.z), bc = g.solid(cr.x, cr.y, cr.z);
              u32 o = (b1 && b2) ? 0 : 3 - (b1 + b2 + bc);
              ao |= o << (k * 2);
            }
            key = (u64)col | (u64)ao << 32 | (u64)1 << 40;
            any = true;
          }
        }
        mask[(size_t)v * nu + u] = key;
      }
      if (!any) continue;
      // 2) greedy rectangles of equal keys
      for (int v = 0; v < nv; v++) for (int u = 0; u < nu;) {
        u64 key = mask[(size_t)v * nu + u];
        if (!key) { u++; continue; }
        int w = 1; while (u + w < nu && mask[(size_t)v * nu + u + w] == key) w++;
        int h = 1;
        for (; v + h < nv; h++) {
          bool ok = true;
          for (int k = 0; k < w; k++) if (mask[(size_t)(v + h) * nu + u + k] != key) { ok = false; break; }
          if (!ok) break;
        }
        for (int dy = 0; dy < h; dy++) for (int k = 0; k < w; k++) mask[(size_t)(v + dy) * nu + u + k] = 0;
        // emit: corners of the rectangle
        u32 col = (u32)key, ao = (u32)(key >> 32) & 255;
        VoxelVertex vv[4];
        for (int k = 0; k < 4; k++) {
          const int* cc = FC[f][k];
          int P[3];
          P[a] = s + (sgn > 0 ? 1 : 0);
          P[ua] = lo[ua] + u + (cc[ua] ? w : 0);
          P[va] = lo[va] + v + (cc[va] ? h : 0);
          vv[k].x = (u16)P[0]; vv[k].y = (u16)P[1]; vv[k].z = (u16)P[2];
          vv[k].nao = (u8)(f | ((ao >> (k * 2)) & 3) << 3); vv[k].pad = 0; vv[k].rgba = col;
        }
        // turn the quad so its diagonal follows the AO (no anisotropy streaks)
        int a0 = (ao & 3) + ((ao >> 4) & 3), a1 = ((ao >> 2) & 3) + ((ao >> 6) & 3);
        int r = a0 < a1 ? 1 : 0;
        for (int k = 0; k < 4; k++) out.verts.push_back(vv[(k + r) & 3]);
        u += w;
      }
    }
  }
}

// ------------------------------------------------------------------ GPU
VoxelMeshGL::VoxelMeshGL() = default;
VoxelMeshGL::~VoxelMeshGL() { release(); }
void VoxelMeshGL::release() { if (mesh) mesh->release(); mesh.reset(); quads = 0; }
// What the GPU gets: the position as floats. The grid's 16-bit whole numbers read as floats are a "scaled" vertex
// format on Vulkan, which phone GPUs leave out; and these meshes are drawn by voxel.glsl, creature.glsl, content.glsl
// and shader packs alike, all reading a float position, so the numbers are made float here, once, at upload.
struct VoxelVertexGpu { float x, y, z; u8 nao, pad[3]; u32 rgba; };
static_assert(sizeof(VoxelVertexGpu) == 20, "VoxelVertexGpu layout");
void VoxelMeshGL::upload(const VoxelMeshData& d) {
  if (d.verts.empty()) { release(); return; }
  if (!mesh) {
    mesh = std::make_unique<Mesh>();
    mesh->setup(sizeof(VoxelVertexGpu), {
      {0, 3, GL_FLOAT, GL_FALSE, false, 0},
      {1, 1, GL_UNSIGNED_BYTE, GL_FALSE, true, 12},
      {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, false, 16},
    });
  }
  {
    std::vector<VoxelVertexGpu> gpu(d.verts.size());
    for (size_t i = 0; i < d.verts.size(); i++) { const VoxelVertex& v = d.verts[i]; gpu[i] = {(float)v.x, (float)v.y, (float)v.z, v.nao, {0, 0, 0}, v.rgba}; }
    mesh->uploadQuads(gpu.data(), gpu.size() * sizeof(VoxelVertexGpu), d.quads());
  }
  quads = d.quads();
  glass = false; liquidY = vec2(0);
  vec3 lo(1e9f), hi(-1e9f);
  for (const VoxelVertex& q : d.verts) {
    const u8 a = (u8)(q.rgba >> 24);
    if (a == VOXEL_GLASS) glass = true;
    else if (a == VOXEL_LIQUID) { lo = glm::min(lo, vec3(q.x, q.y, q.z)); hi = glm::max(hi, vec3(q.x, q.y, q.z)); }
  }
  if (hi.x >= lo.x) { liquidXZ = vec4(lo.x, hi.x, lo.z, hi.z); liquidY = vec2(lo.y, hi.y); }
}

// ------------------------------------------------------------------ shader
VoxelRenderer& voxelRenderer() { static VoxelRenderer* r = new VoxelRenderer(); return *r; }   // kept past exit (no GL calls after the context is gone)

bool VoxelRenderer::init() {
  if (sh) return sh->ok();
  if (tried) return false;
  tried = true;
  sh = std::make_unique<Shader>();
  if (!sh->load("voxel.glsl", "voxel.glsl")) { DQ_ERR("voxel.glsl failed to compile"); return false; }
  return true;
}

void VoxelRenderer::begin(const FrameCtx& f, const mat4& viewProj, bool fog) {
  if (!init()) return;
  sh->use();
  f.apply(*sh);
  if (!fog) { sh->set("uFogStart", 1e6f); sh->set("uFogEnd", 2e6f); }
  sh->set("uViewProj", viewProj);
  sh->set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  sh->set("uNeutral", 0.f);
  time = f.time;
  // The hand and the map pass a different projection. Only the world camera matches terrain depth.
  // The hand list is armed by the first-person layer for this begin only.
  noteWorld = packVoxelCaptureOn() && viewProj == f.cam.viewProj;
  noteHand = packHandCaptureOn();
  handSuppress = noteHand && packHandSuppress();
}

void VoxelRenderer::beginNeutral(const mat4& viewProj, vec3 lightDir) {
  if (!init()) return;
  sh->use();
  sh->set("uTime", 0.f); sh->set("uDaylight", 1.f); sh->set("uBrightness", 1.f); sh->set("uAmbient", vec3(1.f));
  sh->set("uFogStart", 1e6f); sh->set("uFogEnd", 2e6f); sh->set("uFogColor", vec3(0)); sh->set("uSkyColor", vec3(1));
  sh->set("uSunDir", glm::normalize(lightDir)); sh->set("uBlockLightColor", vec3(1));
  sh->set("uViewProj", viewProj);
  sh->set("uCamFrac", vec3(0, 0, 10));
  sh->set("uNeutral", 1.f);
  time = 0;
  noteWorld = false;
  noteHand = false;
  handSuppress = false;
}

void VoxelRenderer::draw(const VoxelMeshGL& m, const mat4& model, VoxelLight light, vec4 tint, float glint) {
  if (!sh || !sh->ok() || !m.ready()) return;
  sh->set("uModel", model);
  sh->set("uLight", vec2(light.sky, light.block));
  sh->set("uTint", tint);
  sh->set("uGlint", vec2(glint, (float)std::fmod(time, 1000.0)));
  sh->set("uLiquidXZ", m.liquidXZ); sh->set("uLiquidY", m.liquidY); sh->set("uSlosh", vec3(slosh, (float)std::fmod(time, 1000.0))); sh->set("uClip", clip);
  bool mirrored = glm::determinant(mat3(model)) < 0;
  if (!handSuppress) {
    if (mirrored) glFrontFace(GL_CW);
    if (!m.glass) { sh->set("uPass", 0.f); m.mesh->draw(); }
    else {   // the solid parts and the liquid first, then the glass over them, blended, writing no depth
      sh->set("uPass", 1.f); m.mesh->draw();
      const GLboolean blend = glIsEnabled(GL_BLEND);
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
      sh->set("uPass", 2.f); m.mesh->draw();
      glDepthMask(GL_TRUE); if (!blend) glDisable(GL_BLEND);
      sh->set("uPass", 0.f);
    }
    if (mirrored) glFrontFace(GL_CCW);
  }
  if (noteWorld && m.mesh) packVoxelNote(m.mesh.get(), model, light.sky, light.block, tint);
  if (noteHand && m.mesh) packHandNote(m.mesh.get(), model, light.sky, light.block, tint);
}

void VoxelRenderer::end() { glBindVertexArray(0); }

} // namespace dq
