// assets/tile_relief.cpp — bakes the relief of every tile once per tile set (after the pack and the animations
// are in): the way "Voxel displacement" (Daniel Schroeder) splits it.
//  * ts.relief  — GEOMETRY: whole micro-voxel steps (0..RELIEF_STEPS), only where the height changes a lot
//                 (mortar grooves, ore gems, brick seams, a chest's latch, tools on a crafting table). The
//                 smoothed heights are quantised, gentle areas stay flat and blobs smaller than a few
//                 micro-voxels are dropped, so a pixel of noise never becomes a bump.
//  * ts.normal  — everything finer goes into a SMOOTH normal map computed from the exact (blurred) heights,
//                 used only for lighting: r,g = tangent-space normal x,y (0.5 = flat), b = crevice
//                 darkening (255 = open, lower = a groove or a corner), a = the blurred height.
// The shader (chunk.glsl) marches the geometry and shades the column tops with the smooth normals.
#include "assets/tileset.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace dq {

namespace {
constexpr int STEPS = TileSet::RELIEF_STEPS;

// separable [1 4 6 4 1]/16 binomial blur (sigma 1), wrapping at the tile's borders (tiles repeat)
void blur5(const std::vector<float>& in, int S, std::vector<float>& out) {
  static const float K[5] = {1 / 16.f, 4 / 16.f, 6 / 16.f, 4 / 16.f, 1 / 16.f};
  std::vector<float> tmp((size_t)S * S);
  out.resize((size_t)S * S);
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    float a = 0; for (int k = -2; k <= 2; k++) a += K[k + 2] * in[y * S + (x + k + S) % S];
    tmp[y * S + x] = a;
  }
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    float a = 0; for (int k = -2; k <= 2; k++) a += K[k + 2] * tmp[((y + k + S) % S) * S + x];
    out[y * S + x] = a;
  }
}
void blur3(const std::vector<float>& in, int S, std::vector<float>& out) {   // [1 2 1]/4, sigma 0.7
  std::vector<float> tmp((size_t)S * S);
  out.resize((size_t)S * S);
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++)
    tmp[y * S + x] = 0.25f * in[y * S + (x + S - 1) % S] + 0.5f * in[y * S + x] + 0.25f * in[y * S + (x + 1) % S];
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++)
    out[y * S + x] = 0.25f * tmp[((y + S - 1) % S) * S + x] + 0.5f * tmp[y * S + x] + 0.25f * tmp[((y + 1) % S) * S + x];
}

// groups of 8-connected micro-voxels with the same step that cover fewer than `minSize` become part of their
// surroundings (the most common step around them)
void dropSpecks(std::vector<u8>& g, int S, int minSize) {
  std::vector<int> label((size_t)S * S, -1), stack, comp;
  for (int s = 0; s < S * S; s++) {
    if (label[s] >= 0) continue;
    comp.clear(); stack.assign(1, s); label[s] = s;
    while (!stack.empty()) {
      int i = stack.back(); stack.pop_back(); comp.push_back(i);
      int x = i % S, y = i / S;
      for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int j = ((y + dy + S) % S) * S + (x + dx + S) % S;
        if (label[j] < 0 && g[j] == g[s]) { label[j] = s; stack.push_back(j); }
      }
    }
    if ((int)comp.size() >= minSize) continue;
    int votes[STEPS + 1] = {0};
    for (int i : comp) {
      int x = i % S, y = i / S;
      for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int j = ((y + dy + S) % S) * S + (x + dx + S) % S;
        if (label[j] != s) votes[g[j]]++;
      }
    }
    int best = g[s], bv = 0;
    for (int v = 0; v <= STEPS; v++) if (votes[v] > bv || (votes[v] == bv && bv > 0 && std::abs(v - g[s]) < std::abs(best - g[s]))) { bv = votes[v]; best = v; }
    for (int i : comp) g[i] = (u8)best;
  }
}
} // namespace

void bakeTileRelief(TileSet& ts) {
  auto t0 = std::chrono::steady_clock::now();
  const int S = ts.size, N = S * S;
  ts.relief.assign((size_t)ts.count * N, 0);
  ts.normal.assign((size_t)ts.count * N * 4, 0);
  if (ts.height.size() < (size_t)ts.count * N) ts.height.resize((size_t)ts.count * N, 0);
  std::vector<float> h0(N), hS(N), hG(N), hM(N);
  std::vector<u8> g(N);
  int withGeometry = 0;
  for (int tl = 0; tl < ts.count; tl++) {
    const u8* hp = ts.height.data() + (size_t)tl * N;
    const u8* px = ts.tile(tl);
    int opaque = 0;
    for (int i = 0; i < N; i++) { h0[i] = hp[i] / 255.f; if (px[i * 4 + 3] > 127) opaque++; }
    const bool full = opaque >= N * 9 / 10;   // a whole block face: gets the slight bevel round its rim

    // --- the smooth surface for lighting: the exact heights, blurred (sigma 1.4) ---
    blur5(h0, S, hS); blur5(hS, S, hS);
    if (full) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      int e = std::min({x, y, S - 1 - x, S - 1 - y});
      if (e < 2) hS[y * S + x] = std::max(hS[y * S + x], e == 0 ? 0.16f : 0.07f);
    }

    // --- geometry: whole micro-voxel steps where the height changes a lot ---
    blur3(h0, S, hG);                                    // a light blur: a groove keeps its depth
    for (int i = 0; i < N; i++) {
      float t = hG[i] * STEPS;
      // only a big change is geometry: shallower than 1.5 micro-voxels stays flat (the normal map shows it),
      // so no one-voxel noise bumps; deeper is rounded to whole steps
      g[i] = t < 1.5f ? 0 : (u8)std::clamp((int)std::floor(t + 0.5f), 2, STEPS);
    }
    dropSpecks(g, S, 6);
    if (full) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      if (std::min({x, y, S - 1 - x, S - 1 - y}) == 0) g[y * S + x] = std::max<u8>(g[y * S + x], 1);   // crisp, slightly bevelled block edge
    }
    bool any = false;
    u8* rl = ts.relief.data() + (size_t)tl * N;
    for (int i = 0; i < N; i++) { rl[i] = g[i]; any |= g[i] > 1; }
    if (any) withGeometry++;

    // --- crevice darkening: how much deeper a micro-voxel sits than its surroundings (radius ~3) ---
    blur5(hS, S, hM); blur5(hM, S, hM);
    // --- normals (Sobel over the blurred heights, in micro-voxel units) ---
    u8* nm = ts.normal.data() + (size_t)tl * N * 4;
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      auto H = [&](int dx, int dy) { return hS[((y + dy + S) % S) * S + (x + dx + S) % S] * STEPS; };
      float gx = ((H(1, -1) + 2 * H(1, 0) + H(1, 1)) - (H(-1, -1) + 2 * H(-1, 0) + H(-1, 1))) * 0.125f;
      float gy = ((H(-1, 1) + 2 * H(0, 1) + H(1, 1)) - (H(-1, -1) + 2 * H(0, -1) + H(1, -1))) * 0.125f;
      float nx = gx * 1.5f, ny = gy * 1.5f, inv = 1.f / std::sqrt(nx * nx + ny * ny + 1.f);
      int i = y * S + x;
      float cav = std::clamp((hS[i] - hM[i]) * STEPS * 0.28f, 0.f, 0.55f);
      nm[i * 4 + 0] = (u8)std::lround((nx * inv * 0.5f + 0.5f) * 255.f);
      nm[i * 4 + 1] = (u8)std::lround((ny * inv * 0.5f + 0.5f) * 255.f);
      nm[i * 4 + 2] = (u8)std::lround((1.f - cav) * 255.f);
      nm[i * 4 + 3] = (u8)std::lround(std::clamp(hS[i], 0.f, 1.f) * 255.f);
    }
  }
  DQ_LOG("relief baked: %d tiles, %d with whole-voxel geometry, smooth normal maps for all (%.0f ms)", ts.count, withGeometry,
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

} // namespace dq
