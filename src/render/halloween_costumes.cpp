// render/halloween_costumes.cpp — see halloween_costumes.h.
#include "render/halloween_costumes.h"
#include <functional>

namespace dq {

Costume villagerCostume(u32 hh, bool pumpkinHead) {
  if (pumpkinHead || (hh >> 16) % 100 >= 50) return Costume::None;   // 70% have no pumpkin, half of those dress up: ~35%
  return (Costume)(1 + (hh >> 24) % 4);
}

namespace {
// a grid of half-pixel voxels over a box of part space; fill() colours each voxel by its centre (0 = empty)
struct Builder {
  vec3 lo; VoxelGrid g;
  Builder(vec3 a, vec3 b) : lo(a) { g.resize(ivec3(glm::ceil((b - a) * (float)CHAR_VPP))); }
  void fill(const std::function<u32(vec3)>& f) {
    for (int k = 0; k < g.size.z; k++) for (int j = 0; j < g.size.y; j++) for (int i = 0; i < g.size.x; i++)
      if (u32 c = f(lo + (vec3(i, j, k) + 0.5f) / (float)CHAR_VPP)) g.set(i, j, k, c);
  }
  void finish(CharMesh& out) { VoxelMeshData md; buildVoxelMesh(g, md); out.gl.upload(md); out.origin = lo; out.voxel = 1.f / CHAR_VPP; }
};
u32 rgb(int r, int g, int b) { return voxRGBA(r, g, b); }
float smooth(float x) { x = clampv(x, 0.f, 1.f); return x * x * (3 - 2 * x); }

void witchHat(CharMesh& m) {
  Builder b(vec3(-7.5f, 7.6f, -7.5f), vec3(7.5f, 19.4f, 7.5f));
  b.fill([](vec3 p) -> u32 {
    float r = std::hypot(p.x, p.z);
    if (p.y >= 8.9f && p.y < 10.3f && std::fabs(p.x) < 1.25f && p.z < -3.7f && p.z > -4.8f && r < 4.8f) return rgb(226, 178, 34);   // the buckle
    if (p.y >= 7.7f && p.y < 8.7f && r <= 7.2f) return r > 6.5f ? rgb(30, 22, 46) : rgb(40, 30, 62);                              // the brim
    if (p.y >= 8.7f && p.y < 19.2f) {
      float t = (p.y - 8.7f) / 10.5f, cz = t * t * 3.4f, rr = 4.4f * (1 - t) + 0.55f;   // the cone, its tip bent back
      if (std::hypot(p.x, p.z - cz) <= rr) return p.y < 10.4f ? rgb(112, 52, 168) : rgb(44, 33, 68 + (int)(t * 14));
    }
    return 0u;
  });
  b.finish(m);
}

void skullFace(CharMesh& m) {
  Builder b(vec3(-4.f, 0.f, -5.f), vec3(4.f, 8.f, -4.f));   // 16 x 16 voxels over the face, two deep
  b.fill([&](vec3 p) -> u32 {
    int u = (int)std::floor((p.x + 4.f) * CHAR_VPP), v = (int)std::floor(p.y * CHAR_VPP);   // 0..15, v up
    bool outer = p.z < -4.5f;
    u32 bone = rgb(232, 228, 212), edge = rgb(198, 194, 178), dark = rgb(14, 12, 16);
    if (outer) {
      auto socket = [&](int u0) { return u >= u0 && u <= u0 + 3 && v >= 6 && v <= 9 && !((u == u0 || u == u0 + 3) && (v == 6 || v == 9)); };
      if (socket(2) || socket(10)) return dark;
      if (u >= 7 && u <= 8 && v >= 3 && v <= 5) return dark;                                    // the nose hole
      if (v >= 1 && v <= 3 && u >= 3 && u <= 12) return u % 2 == 0 ? rgb(244, 241, 226) : dark;   // teeth
    }
    return (u == 0 || u == 15 || v == 15 || v == 0) ? edge : bone;
  });
  b.finish(m);
}

u32 sheetShade(float shade) { return rgb((int)(242 * shade), (int)(242 * shade), (int)(248 * shade)); }

void sheetHead(CharMesh& m) {
  Builder b(vec3(-5.f, -0.6f, -5.f), vec3(5.f, 9.4f, 5.f));
  b.fill([](vec3 p) -> u32 {
    float yy = p.y > 7.4f ? (p.y - 7.4f) * 1.7f : 0.f, lim = 4.95f - yy;
    if (p.y < -0.5f || p.y > 8.9f || lim < 0.8f) return 0u;
    if (std::pow(std::fabs(p.x) / lim, 4.f) + std::pow(std::fabs(p.z) / lim, 4.f) > 1.f) return 0u;
    if (p.z < -4.1f) {   // the eye and mouth holes
      bool eye = p.y > 3.6f && p.y < 5.5f && std::fabs(p.x) > 0.9f && std::fabs(p.x) < 2.7f;
      bool mouth = p.y > 1.2f && p.y < 3.0f && std::fabs(p.x) < 0.9f + (p.y > 1.8f ? 0.3f : 0.f);
      if (eye || mouth) return rgb(12, 10, 18);
    }
    float fold = 0.94f + 0.06f * std::sin(p.x * 2.1f + p.z * 1.3f);
    return sheetShade(p.y < 0.6f ? 0.88f : fold);
  });
  b.finish(m);
}

void sheetBody(CharMesh& m) {
  Builder b(vec3(-9.8f, -10.4f, -5.8f), vec3(9.8f, 13.2f, 5.8f));
  b.fill([](vec3 p) -> u32 {
    if (p.y > 12.9f) return 0u;
    float t = smooth((12.9f - p.y) / 10.f);
    float hx = 5.3f + 3.7f * t + 0.5f * smooth((12.9f - p.y) / 24.f), hz = 3.5f + 1.2f * t;
    if (std::pow(std::fabs(p.x) / hx, 3.f) + std::pow(std::fabs(p.z) / hz, 3.f) > 1.f) return 0u;
    float th = std::atan2(p.z, p.x);
    if (p.y < -9.4f + 1.1f * std::sin(th * 7.f + 0.6f) * (0.6f + 0.4f * std::cos(th * 3.f))) return 0u;   // a wavy hem
    return sheetShade(0.93f + 0.07f * std::sin(th * 6.f));
  });
  b.finish(m);
}

void collar(CharMesh& m) {
  Builder b(vec3(-6.f, -1.4f, -1.4f), vec3(6.f, 6.2f, 5.8f));
  b.fill([](vec3 p) -> u32 {
    u32 black = rgb(26, 22, 32), red = rgb(142, 16, 26);
    float ax = std::fabs(p.x);
    // the stand-up back, and the two wings sloping down toward the front (the neck is open)
    if (p.z >= 4.2f && p.z < 5.4f && ax <= 5.6f && p.y >= -1.2f && p.y < 5.2f - ax * 0.12f)
      return (p.z < 4.8f || p.y > 4.5f - ax * 0.12f) ? red : black;
    float top = 1.0f + (p.z + 0.8f) / 6.2f * 3.8f;
    if (ax >= 4.4f && ax < 5.6f && p.z >= -0.8f && p.z < 5.4f && p.y >= -1.2f && p.y < top)
      return (ax < 5.0f || p.y > top - 0.55f) ? red : black;
    return 0u;
  });
  b.finish(m);
}
} // namespace

const CharMesh& hwPiece(int piece) {
  static CharMesh meshes[HW_PIECE_COUNT];
  static bool built[HW_PIECE_COUNT] = {};
  if (piece < 0 || piece >= HW_PIECE_COUNT) piece = 0;
  if (!built[piece]) {
    built[piece] = true;
    switch (piece) {
      case HW_WITCH_HAT: witchHat(meshes[piece]); break;
      case HW_SKULL_FACE: skullFace(meshes[piece]); break;
      case HW_SHEET_HEAD: sheetHead(meshes[piece]); break;
      case HW_SHEET_BODY: sheetBody(meshes[piece]); break;
      default: collar(meshes[piece]); break;
    }
  }
  return meshes[piece];
}

} // namespace dq
