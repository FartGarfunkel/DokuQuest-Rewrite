// assets/holiday_tiles.cpp — December: chests wrapped as presents (red paper, a gold ribbon round them, a bow on the
// lid), painted over whatever picture the pack gave them, keeping its light and shade. Done when the tiles load.
#include "assets/tileset.h"
#include "common/holiday.h"
#include <cmath>

namespace dq {

void holidayTiles(TileSet& ts) {
  if (!christmas()) return;
  const int N = ts.size;
  for (const char* name : {"chest_front", "chest_side", "chest_top"}) {
    auto it = ts.index.find(name);
    if (it == ts.index.end() || it->second < 0 || it->second >= ts.count) continue;
    const bool top = std::string(name) == "chest_top";
    u8* px = ts.tile(it->second);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
      u8* p = px + (y * N + x) * 4;
      if (p[3] < 128) continue;
      const float lum = (0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]) / 160.f, k = std::clamp(0.55f + 0.55f * lum, 0.4f, 1.25f);
      const float cx = std::fabs(x + 0.5f - N / 2.f) / N, cy = std::fabs(y + 0.5f - N / 2.f) / N;
      const bool ribbon = cx < 0.09f || (top && cy < 0.09f);
      const bool edge = ribbon && (std::fabs(cx - 0.09f) < 0.035f || (top && std::fabs(cy - 0.09f) < 0.035f));
      const bool bow = top && ((std::pow((x + 0.5f - N * 0.38f) / (N * 0.12f), 2) + std::pow((y + 0.5f - N * 0.5f) / (N * 0.08f), 2) < 1) ||
                               (std::pow((x + 0.5f - N * 0.62f) / (N * 0.12f), 2) + std::pow((y + 0.5f - N * 0.5f) / (N * 0.08f), 2) < 1));
      float r, g, b;
      if (ribbon || bow) { r = 236; g = 186; b = 52; if (edge) { r = 200; g = 140; b = 30; } }
      else { r = 178; g = 26; b = 34; if (((x / 4 + y / 4) & 3) == 0) { r = 198; g = 40; b = 46; } }   // the paper's little pattern
      p[0] = (u8)std::min(255.f, r * k); p[1] = (u8)std::min(255.f, g * k); p[2] = (u8)std::min(255.f, b * k);
    }
  }
}

} // namespace dq
