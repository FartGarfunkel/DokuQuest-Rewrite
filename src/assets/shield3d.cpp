// assets/shield3d.cpp — see shield3d.h
#include "assets/shield3d.h"
#include <algorithm>
#include <cmath>

namespace dq {

namespace {
constexpr int N = 32;
constexpr int X0 = 6, X1 = 26, Y0 = 3, Y1 = 29;   // the face: 20 x 26 voxels (0.625 x 0.81 blocks)
constexpr int CUT = 3;                             // its corners cut off
constexpr int SY0 = 11, SY1 = 21, SZ0 = 8;         // the straps' loops: their sides (y) and their far side (z)
const int STRAP_X[2][2] = {{10, 11}, {20, 21}};

u32 rgb(float r, float g, float b) { return voxRGBA((int)std::clamp(r, 0.f, 255.f), (int)std::clamp(g, 0.f, 255.f), (int)std::clamp(b, 0.f, 255.f)); }
float noise(int x, int y, int z) { u32 h = (u32)x * 73856093u ^ (u32)y * 19349663u ^ (u32)z * 83492791u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 1023) / 1023.f; }
// how far in from the outline (the cut corners count): 0 on the edge
int inset(int x, int y) {
  const int dx = std::min(x - X0, X1 - 1 - x), dy = std::min(y - Y0, Y1 - 1 - y);
  return std::min({dx, dy, dx + dy - CUT});
}
// the face bows out: the middle stands proud of the edges (its front z, the boards two voxels thick behind it)
int frontZ(int x) { const float u = (x + 0.5f - 16.f) / 10.f; return 17 + (int)std::lround(2.5f * (1 - u * u)); }
}  // namespace

vec3 shieldArmChannel() {   // the middle of the straps' loops, in model space (blocks; the grid's corner at -0.5)
  return vec3(16.f, (SY0 + 1 + SY1) * 0.5f, (SZ0 + 1 + (frontZ(STRAP_X[0][0]) - 1)) * 0.5f) / (float)N - vec3(0.5f);
}

bool buildShieldGrid(VoxelGrid& g, VoxelFrame& fr, float& depth) {
  g.resize(ivec3(N)); fr.voxel = 1.f / N; fr.origin = vec3(-0.5f); depth = 0.2f;
  const float plank[4][3] = {{132, 80, 48}, {120, 72, 43}, {138, 86, 52}, {114, 68, 40}};
  for (int y = Y0; y < Y1; y++) for (int x = X0; x < X1; x++) {
    const int in = inset(x, y);
    if (in < 0) continue;
    const int zf = frontZ(x), zb = zf - 1;
    if (in < 2) {   // the iron rim, wrapped over the boards' edge front and back
      for (int z = zb - 1; z <= zf + 1; z++) {
        const float lit = (z == zf + 1 ? 1.06f : z == zb - 1 ? 0.78f : 0.92f) * (in == 0 ? 0.9f : 1.f) + (y > 20 ? 0.04f : 0.f) + (noise(x, y, z) - 0.5f) * 0.06f;
        const bool rivet = in == 1 && z == zf + 1 && ((x - X0) % 6 == 2 || x == X0 + 1 || x == X1 - 2) && ((y - Y0) % 6 == 2 || y == Y0 + 1 || y == Y1 - 2);
        g.set(x, y, z, rivet ? rgb(110, 112, 118) : rgb(206 * lit, 210 * lit, 216 * lit));
      }
      continue;
    }
    // the boards: four upright planks, a dark seam between each, the grain running up them
    const int k = std::clamp((x - (X0 + 2)) / 4, 0, 3);
    const bool seam = (x - (X0 + 2)) % 4 == 0 && x > X0 + 2;
    const float grain = 0.9f + 0.18f * noise(x, y / 3, 7) + 0.06f * std::sin(y * 0.9f + k * 2.1f);
    for (int z = zb; z <= zf; z++) {
      const float f = (seam ? 0.62f : grain) * (z == zb ? 0.72f : 1.f);   // (the back of the boards darker)
      g.set(x, y, z, rgb(plank[k][0] * f, plank[k][1] * f, plank[k][2] * f));
    }
  }
  // two leather straps on the back, each a loop the forearm goes through, with an iron buckle
  for (auto& sx : STRAP_X) for (int x = sx[0]; x <= sx[1]; x++) {
    const int zb = frontZ(x) - 1;
    for (int z = SZ0; z < zb; z++) for (int y : {SY0, SY1}) g.set(x, y, z, rgb(150 - (z & 1) * 8, 98, 56));
    for (int y = SY0; y <= SY1; y++) g.set(x, y, SZ0, rgb(142, 92, 52));
    g.set(x, SY1, SZ0 + 2, rgb(170, 172, 176));   // (the buckle)
  }
  return true;
}

}  // namespace dq
