// render/ore_overlay.cpp — ores in the colour of their rock (see ore_overlay.h)
#include "render/ore_overlay.h"
#include "assets/tileset.h"
#include "core/registry.h"
#include "sim/cavein.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace dq {
namespace {
std::mutex g_mx;
std::shared_ptr<const OreTables> g_t = std::make_shared<OreTables>();   // (swapped whole: the mesher threads read a snapshot)
}
std::shared_ptr<const OreTables> oreTables() { std::lock_guard<std::mutex> lk(g_mx); return g_t; }

void buildOreOverlays(TileSet& ts) {
  const Registry& R = reg();
  const int n = (int)R.blocks.size(), S = ts.size;
  std::vector<int> overlay(n, -1); std::vector<BlockId> base(n, 0); std::vector<u8> host(n, 0);
  for (int id = 1; id < n; id++) host[id] = isRock((BlockId)id) ? 1 : 0;
  // every natural rock an ore's picture may be painted on (cobble included: zinc and bismuth are)
  std::vector<std::pair<BlockId, int>> stones;
  auto addStone = [&](BlockId b) {
    if (!b || b >= n) return;
    const BlockDef& d = R.block(b);
    if (R.tRender[b] != (u8)Render::Cube || !R.tOpaque[b]) return;
    const int t = d.faces[0]; if (t < 0 || t >= ts.count) return;
    for (int f = 1; f < 6; f++) if (d.faces[f] != t) return;
    for (auto& s : stones) if (s.first == b) return;
    stones.push_back({b, t});
  };
  for (const char* k : {"stone", "netherrack", "end_stone", "slate", "deepslate", "blackstone", "sandstone", "basalt", "tuff", "granite", "diorite", "andesite", "holystone", "skystone", "flavolite", "violecite", "sulphuric_rock", "brimstone", "calcite", "limestone", "marble", "cobblestone", "mossy_cobblestone", "cobbled_slate"})
    addStone(R.blockId(k));
  for (int id = 1; id < n; id++) if (host[id]) addStone((BlockId)id);
  auto dist = [&](const u8* a, const u8* b) { return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]); };
  auto near = [&](const u8* a, const u8* b) { return dist(a, b) < 54; };
  int made = 0;
  for (int id = 1; id < n; id++) {
    const BlockDef& d = R.block((BlockId)id);
    if (d.key.find("_ore") == std::string::npos || R.tRender[id] != (u8)Render::Cube || !R.tOpaque[id]) continue;
    const int t = d.faces[0]; if (t < 0 || t >= ts.count) continue;
    bool same = true; for (int f = 1; f < 6; f++) if (d.faces[f] != t) same = false;
    if (!same) continue;   // (an ore with a top of its own: left as it is)
    const u8* op = ts.tile(t); int best = 0; BlockId bestB = 0; int bestT = -1;
    for (auto [b, st] : stones) {
      const u8* sp = ts.tile(st); int k = 0;
      for (int i = 0; i < S * S; i++) if (near(op + i * 4, sp + i * 4)) k++;
      if (k > best) { best = k; bestB = b; bestT = st; }
    }
    // no stone of ours in it: its own background is the picture's most common colour (and those near it)
    std::vector<u8> bg; const u8* sp = nullptr;
    if (bestT < 0 || best < S * S * 35 / 100) {
      std::unordered_map<int, int> bins; int topBin = -1, topN = 0;
      for (int i = 0; i < S * S; i++) { const u8* q = op + i * 4; if (q[3] < 128) continue; const int k = (q[0] >> 4) << 8 | (q[1] >> 4) << 4 | (q[2] >> 4); if (++bins[k] > topN) { topN = bins[k]; topBin = k; } }
      if (topBin < 0) continue;
      long r = 0, g = 0, bl = 0; int m = 0;
      for (int i = 0; i < S * S; i++) { const u8* q = op + i * 4; if (q[3] >= 128 && ((q[0] >> 4) << 8 | (q[1] >> 4) << 4 | (q[2] >> 4)) == topBin) { r += q[0]; g += q[1]; bl += q[2]; m++; } }
      const u8 avg[4] = {(u8)(r / m), (u8)(g / m), (u8)(bl / m), 255};
      bg.assign((size_t)S * S * 4, 0);
      for (int i = 0; i < S * S; i++) { const u8* q = op + i * 4; const bool isBg = q[3] >= 128 && dist(q, avg) < 90;
        for (int k = 0; k < 4; k++) bg[i * 4 + k] = isBg ? q[k] : (u8)(q[k] ^ 0xFF); }
      sp = bg.data(); bestB = 0;
    } else sp = ts.tile(bestT);
    std::vector<u8> px((size_t)S * S * 4, 0);
    int kept = 0;
    for (int i = 0; i < S * S; i++) if (!near(op + i * 4, sp + i * 4) && op[i * 4 + 3] > 128) { for (int k = 0; k < 4; k++) px[i * 4 + k] = op[i * 4 + k]; px[i * 4 + 3] = 255; kept++; }
    // a unique picture (skulls, sphalerite) is mostly "specks": keep only the half furthest from the rock so the host still shows
    if (kept > S * S / 2) {
      struct Hit { int d, i; };
      std::vector<Hit> hits; hits.reserve((size_t)kept);
      for (int i = 0; i < S * S; i++) if (px[i * 4 + 3]) hits.push_back({dist(op + i * 4, sp + i * 4), i});
      std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.d > b.d; });
      std::fill(px.begin(), px.end(), 0);
      const int want = S * S / 2; kept = 0;
      for (int k = 0; k < want && k < (int)hits.size(); k++) {
        const int i = hits[k].i;
        for (int c = 0; c < 4; c++) px[i * 4 + c] = op[i * 4 + c];
        px[i * 4 + 3] = 255; kept++;
      }
    }
    if (kept < 4) {
      // still nothing: the 8 pixels furthest from the picture's mean, so every ore can take on its rock
      long r = 0, g = 0, bl = 0, m = 0;
      for (int i = 0; i < S * S; i++) { const u8* q = op + i * 4; if (q[3] > 128) { r += q[0]; g += q[1]; bl += q[2]; m++; } }
      if (!m) continue;
      const u8 avg[4] = {(u8)(r / m), (u8)(g / m), (u8)(bl / m), 255};
      struct Hit { int d, i; };
      std::vector<Hit> hits;
      for (int i = 0; i < S * S; i++) if (op[i * 4 + 3] > 128) hits.push_back({dist(op + i * 4, avg), i});
      std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.d > b.d; });
      std::fill(px.begin(), px.end(), 0); kept = 0;
      for (int k = 0; k < 8 && k < (int)hits.size(); k++) {
        const int i = hits[k].i;
        for (int c = 0; c < 4; c++) px[i * 4 + c] = op[i * 4 + c];
        px[i * 4 + 3] = 255; kept++;
      }
    }
    if (kept < 4) continue;
    const int layer = appendTile(ts, px.data());
    if (ts.emissive.size() >= (size_t)ts.count * S * S && ts.emissive.size() >= (size_t)(t + 1) * S * S)
      for (int i = 0; i < S * S; i++) if (px[i * 4 + 3]) ts.emissive[(size_t)layer * S * S + i] = ts.emissive[(size_t)t * S * S + i];
    ts.index[d.key + "__ore_specks"] = layer;
    overlay[id] = layer; base[id] = bestB; made++;
  }
  auto T = std::make_shared<OreTables>(); T->overlay = std::move(overlay); T->base = std::move(base); T->host = std::move(host);
  { std::lock_guard<std::mutex> lk(g_mx); g_t = T; }
  DQ_LOG("ores: %d drawn in the colour of their rock", made);
}

} // namespace dq
