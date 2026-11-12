// render/book_model.cpp — see book_model.h
#include "render/book_model.h"
#include <cmath>
#include <functional>
#include <map>
#include <memory>

namespace dq {
namespace {
// unit boxes, stretched to each part when drawn: a leather cover (in the book's colour) with a gilt rule, the pages
struct BookMeshes { VoxelMeshGL cover, pages; };
bool g_dark = true;
BookMeshes& meshes(u32 rgb) {
  static auto& cache = *new std::map<u64, std::unique_ptr<BookMeshes>>();
  auto& m = cache[(u64)rgb | (g_dark ? 1ull << 32 : 0)];
  if (m) return *m;
  m = std::make_unique<BookMeshes>();
  auto unit = [](VoxelMeshGL& gl, int nx, int ny, int nz, const std::function<u32(int, int, int)>& col) {
    VoxelGrid g(ivec3(nx, ny, nz));
    for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) g.set(x, y, z, col(x, y, z));
    VoxelMeshData d; buildVoxelMesh(g, d); gl.upload(d);
  };
  const int r = rgb >> 16 & 255, gg = rgb >> 8 & 255, b = rgb & 255;
  unit(m->cover, 12, 1, 20, [&](int x, int, int z) {
    const bool rim = x == 11 || z == 0 || z == 19, gilt = (x == 9 || z == 2 || z == 17) && x >= 2 && z >= 2 && z <= 17;
    return rim ? voxRGBA(r * 6 / 10, gg * 6 / 10, b * 6 / 10) : gilt ? voxRGBA(214, 172, 70) : voxRGBA(r, gg, b);
  });
  if (g_dark) unit(m->pages, 11, 4, 18, [](int x, int y, int z) { return ((x + y + z) & 1) && y > 0 && y < 3 ? voxRGBA(44, 40, 48) : voxRGBA(30, 27, 33); });
  else unit(m->pages, 11, 4, 18, [](int x, int y, int z) { return ((x + y + z) & 1) && y > 0 && y < 3 ? voxRGBA(214, 204, 178) : voxRGBA(238, 230, 204); });
  return *m;
}
}  // namespace
void setBookPagesDark(bool dark) { g_dark = dark; }
void bookPageArea(float scale, float& hw, float& hl, float& top) {
  hw = (6 / 16.f - 0.015f) * scale; hl = (10 / 16.f / 2 - 0.02f) * scale; top = 1.5f / 32 * scale;
}
namespace {
mat4 boxM(const mat4& frame, vec3 lo, vec3 hi, vec3 n) { return glm::scale(glm::translate(frame, lo), (hi - lo) / n); }
}  // namespace

void drawBook(VoxelRenderer& VR, const mat4& frame, VoxelLight L, float open, float leaf, int dir, float scale, u32 coverRgb) {
  BookMeshes& M = meshes(coverRgb);
  if (!M.cover.ready()) return;
  const float HALF_W = 6 / 16.f * scale, LEN = 10 / 16.f * scale, COVER_T = 1 / 64.f * scale, PAGE_T = 1.5f / 32 * scale;
  const bool flat = open > 1;
  open = std::clamp(open, 0.f, 1.f);
  const float a = flat ? 0.f : 3.14159265f / 2 * (1 - open) + 0.18f * open;   // each half's angle up from flat
  // one half: its direction away from the spine (th: 0 flat to the right, pi flat to the left), its pages on the side the
  // other half is (toward +y when open)
  auto half = [&](float th, int sd, bool leafOnly) {
    const vec3 u(std::cos(th), std::sin(th), 0);
    const vec3 in = sd > 0 ? vec3(-std::sin(th), std::cos(th), 0) : vec3(std::sin(th), -std::cos(th), 0);
    // (its own side of the spine's thickness: the page blocks' width apart shut, together at the spine open)
    mat4 F = glm::translate(frame, vec3(sd * PAGE_T * std::sin(a), 0, 0));
    mat4 H(1); H[0] = vec4(u, 0); H[1] = vec4(in, 0); H[2] = vec4(0, 0, 1, 0);
    F = F * H;
    if (!leafOnly) VR.draw(M.cover, boxM(F, vec3(0, -COVER_T, -LEN / 2), vec3(HALF_W, 0, LEN / 2), vec3(12, 1, 20)), L);
    const float t = leafOnly ? PAGE_T * 0.15f : PAGE_T;
    VR.draw(M.pages, boxM(F, vec3(0, 0, -LEN / 2 + 0.02f * scale), vec3(HALF_W - 0.015f * scale, t, LEN / 2 - 0.02f * scale), vec3(11, 4, 18)), L);
  };
  {   // the spine: one piece of leather joining the two covers (they were two halves with a gap down the middle)
    const float w = PAGE_T * std::sin(a) + COVER_T;
    VR.draw(M.cover, boxM(frame, vec3(-w, -COVER_T * 1.5f, -LEN / 2), vec3(w, COVER_T * 0.5f, LEN / 2), vec3(12, 1, 20)), L);
  }
  half(a, 1, false);                       // the right half
  half(3.14159265f - a, -1, false);        // the left
  if (leaf >= 0 && leaf <= 1 && open > 0.5f) {   // a page lifting off one side and laid on the other
    const float u = leaf * leaf * (3 - 2 * leaf);
    const float th = dir >= 0 ? a + (3.14159265f - 2 * a) * u : 3.14159265f - a - (3.14159265f - 2 * a) * u;
    half(th, th < 3.14159265f / 2 ? 1 : -1, true);
  }
}

} // namespace dq
