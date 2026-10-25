#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include "assets/tileset.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>

namespace dq {

bool loadPngMem(const u8* data, size_t n, int& w, int& h, std::vector<u8>& rgba) {
  int ch; u8* px = stbi_load_from_memory(data, (int)n, &w, &h, &ch, 4);
  if (!px) return false;
  rgba.assign(px, px + (size_t)w * h * 4); stbi_image_free(px);
  return true;
}
bool loadPng(const fs::path& p, int& w, int& h, std::vector<u8>& rgba) {
  auto b = readBinary(p);
  return b && loadPngMem(b->data(), b->size(), w, h, rgba);
}
bool savePng(const fs::path& p, int w, int h, const u8* rgba) {
  std::error_code ec; if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  return stbi_write_png(p.string().c_str(), w, h, 4, rgba, w * 4) != 0;
}

u32 TileSet::avgColor(int i) const {
  if (i < 0 || i >= count) return 0xFF808080;
  const u8* t = tile(i); u64 r = 0, g = 0, b = 0, n = 0;
  for (int k = 0; k < size * size; k++) if (t[k * 4 + 3] > 127) { r += t[k * 4]; g += t[k * 4 + 1]; b += t[k * 4 + 2]; n++; }
  if (!n) return 0;
  return 0xFF000000u | (u32)(b / n) << 16 | (u32)(g / n) << 8 | (u32)(r / n);
}

bool loadBuiltinTiles(TileSet& ts) {
  auto tj = readJson(dataDir() / "tiles.json");
  int w, h; std::vector<u8> img;
  if (!tj || !loadPng(dataDir() / "tiles.png", w, h, img)) { DQ_ERR("data/tiles.png missing"); return false; }
  ts.size = (*tj)["tile"]; int per = (*tj)["perRow"]; ts.count = (*tj)["count"];
  for (auto& [k, v] : (*tj)["names"].items()) ts.index[k] = v;
  int S = ts.size;
  ts.rgba.assign((size_t)ts.count * S * S * 4, 0);
  for (int t = 0; t < ts.count; t++) {
    int tx = (t % per) * S, ty = (t / per) * S;
    for (int y = 0; y < S; y++) std::memcpy(ts.tile(t) + (size_t)y * S * 4, img.data() + ((size_t)(ty + y) * w + tx) * 4, (size_t)S * 4);
  }
  ts.emissive.assign((size_t)ts.count * S * S, 0);
  ts.res.assign(ts.count, (u8)S);
  if (tj->contains("res") && (*tj)["res"].is_array()) for (int t = 0; t < ts.count && t < (int)(*tj)["res"].size(); t++) ts.res[t] = (u8)(*tj)["res"][t].get<int>();
  deriveMicroVoxelHeights(ts);
  return true;
}

int appendTile(TileSet& ts, const u8* px) {
  size_t S2 = (size_t)ts.size * ts.size;
  ts.rgba.insert(ts.rgba.end(), px, px + S2 * 4);
  ts.height.resize(ts.height.size() + S2, 0);
  ts.emissive.resize(ts.emissive.size() + S2, 0);
  ts.res.push_back((u8)ts.size);
  return ts.count++;
}

// Relief from colour, for the built-in tiles and any resource pack alike (packs ship no height maps):
//   * ores: the gems stand proud of the rock round them (anything far from the rock's own colours);
//   * grooved pictures (cobble's dark outlines, brick mortar, plank seams, stone specks): the thin lines
//     found by splitting the picture into its two main tones and taking the tone that runs in thin lines;
//     they sink deep, and what's between them bulges, sloping down into the grooves (round stones,
//     raised bricks, rounded boards);
//   * anything else: darker-than-local-average micro-voxels sink a little, as before.
// chunk.glsl turns the height field into micro-voxel relief and per-voxel normals (lit by the sun and a
// key light). DQ_RELIEF=old keeps the plain luminance relief (for comparison shots).
namespace {
inline float lumOf(const u8* p) { return (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.f; }
inline float satOf(const u8* p) { int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]}); return mx ? (mx - mn) / (float)mx : 0.f; }

void lumRelief(const u8* p, int S, const std::vector<float>& lum, float avg, std::vector<float>& d) {
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    // local average (5x5, wrapping) so large dark areas don't all sink
    float la = 0; for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) la += lum[((y + dy + S) % S) * S + (x + dx + S) % S];
    la /= 25.f;
    d[y * S + x] = (la - lum[y * S + x]) * 3.5f + (avg - lum[y * S + x]) * 1.0f;   // darker -> deeper
  }
  (void)p;
}

// distance (in micro-voxels, chamfer 3-4, wrapping) from each pixel to the nearest marked one, capped
void distanceTo(const std::vector<u8>& mark, int S, int cap, std::vector<float>& out) {
  const int INF = 1 << 20;
  std::vector<int> d(S * S);
  for (int i = 0; i < S * S; i++) d[i] = mark[i] ? 0 : INF;
  auto W = [&](int x, int y) -> int& { return d[((y + S) % S) * S + (x + S) % S]; };
  for (int pass = 0; pass < 2; pass++) {   // twice round, so wrapping edges settle
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      int& v = W(x, y);
      v = std::min({v, W(x - 1, y) + 3, W(x, y - 1) + 3, W(x - 1, y - 1) + 4, W(x + 1, y - 1) + 4});
    }
    for (int y = S - 1; y >= 0; y--) for (int x = S - 1; x >= 0; x--) {
      int& v = W(x, y);
      v = std::min({v, W(x + 1, y) + 3, W(x, y + 1) + 3, W(x + 1, y + 1) + 4, W(x - 1, y + 1) + 4});
    }
  }
  out.resize(S * S);
  for (int i = 0; i < S * S; i++) out[i] = std::min((float)cap, d[i] / 3.f);
}

bool isOreName(const std::string& n) {
  return n.find("_ore") != std::string::npos || n == "ancient_debris_side" || n == "ancient_debris_top" || n == "gilded_blackstone";
}

void oreRelief(const u8* p, int S, const std::vector<float>& lum, std::vector<float>& d) {
  // the rock: the colour of the greyest 60% of the picture
  std::vector<std::pair<float, int>> bySat;
  for (int i = 0; i < S * S; i++) if (p[i * 4 + 3] > 127) bySat.push_back({satOf(p + i * 4), i});
  std::sort(bySat.begin(), bySat.end());
  size_t nb = std::max<size_t>(1, bySat.size() * 6 / 10);
  float r = 0, g = 0, b = 0, sat = 0;
  for (size_t k = 0; k < nb; k++) { const u8* q = p + bySat[k].second * 4; r += q[0]; g += q[1]; b += q[2]; sat += bySat[k].first; }
  r /= nb * 255.f; g /= nb * 255.f; b /= nb * 255.f; sat /= nb;
  float rl = 0.299f * r + 0.587f * g + 0.114f * b;
  std::vector<u8> gem(S * S, 0);
  for (int i = 0; i < S * S; i++) {
    const u8* q = p + i * 4;
    vec3 c(q[0] / 255.f, q[1] / 255.f, q[2] / 255.f);
    // how far its hue is from the rock's (brightness aside), and how much more colourful it is
    vec3 dc = (c - vec3(lumOf(q))) - (vec3(r, g, b) - vec3(rl));
    float far = glm::length(dc) * 2.2f + std::max(0.f, satOf(q) - sat - 0.1f) * 1.6f;
    gem[i] = far > 0.3f;
  }
  std::vector<float> dist; distanceTo(gem, S, 3, dist);
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    int i = y * S + x;
    if (gem[i]) { d[i] = std::max(0.f, (rl - lum[i]) * 0.6f); continue; }            // flush: the gem stands out
    float la = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) la += lum[((y + dy + S) % S) * S + (x + dx + S) % S];
    la /= 9.f;
    d[i] = 0.42f + 0.14f * (1 - dist[i] / 3.f) + std::clamp((la - lum[i]) * 1.5f, -0.1f, 0.2f);   // the rock sits lower, deepest at the gem's foot
  }
}

// grooves: split into two tones (Otsu); the tone that runs in thin lines is the groove. false = no grooves.
bool grooveRelief(const u8* p, int S, const std::vector<float>& lum, std::vector<float>& d) {
  int hist[64] = {0}, n = 0;
  for (int i = 0; i < S * S; i++) if (p[i * 4 + 3] > 127) { hist[std::min(63, (int)(lum[i] * 64))]++; n++; }
  if (n < S * S * 9 / 10) return false;
  double sum = 0; for (int k = 0; k < 64; k++) sum += k * (double)hist[k];
  double sB = 0, wB = 0, best = -1; int thr = 32;
  for (int k = 0; k < 64; k++) {
    wB += hist[k]; if (!wB) continue;
    double wF = n - wB; if (!wF) break;
    sB += k * (double)hist[k];
    double mB = sB / wB, mF = (sum - sB) / wF, v = wB * wF * (mB - mF) * (mB - mF);
    if (v > best) { best = v; thr = k; }
  }
  std::vector<u8> dark(S * S);
  int nd = 0; float md = 0, ml = 0;
  for (int i = 0; i < S * S; i++) { dark[i] = (int)(lum[i] * 64) <= thr; if (dark[i]) { nd++; md += lum[i]; } else ml += lum[i]; }
  if (!nd || nd == S * S) return false;
  md /= nd; ml /= (S * S - nd);
  if (ml - md < 0.07f) return false;   // one tone: a smooth surface
  // how much of each tone touches the other: thin lines touch it everywhere, the stones between only at their rims
  int touchD = 0, touchL = 0;
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    int i = y * S + x; bool t = false;
    for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) if (dark[((y + dy + S) % S) * S + (x + dx + S) % S] != dark[i]) { t = true; break; }
    if (t) (dark[i] ? touchD : touchL)++;
  }
  float fD = touchD / (float)nd, fL = touchL / (float)(S * S - nd);
  bool darkGroove = fD > fL;
  float share = darkGroove ? nd / (float)(S * S) : 1 - nd / (float)(S * S);
  float fG = darkGroove ? fD : fL, fO = darkGroove ? fL : fD;
  if (share > 0.48f || fG < 0.55f || fG - fO < 0.2f) return false;   // speckled all over (grass, sand): no grooves
  std::vector<u8> groove(S * S);
  for (int i = 0; i < S * S; i++) groove[i] = darkGroove ? dark[i] : !dark[i];
  const int R = std::max(2, S / 8);
  std::vector<float> dist; distanceTo(groove, S, R, dist);
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    int i = y * S + x;
    float la = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) la += lum[((y + dy + S) % S) * S + (x + dx + S) % S];
    la /= 9.f;
    float detail = std::clamp((la - lum[i]) * 1.2f, -0.08f, 0.15f);
    if (groove[i]) { d[i] = 0.8f + 0.2f * std::clamp(std::abs(lum[i] - (darkGroove ? ml : md)) * 3.f, 0.f, 1.f); continue; }
    float k = 1 - dist[i] / R;
    d[i] = 0.5f * k * k + std::max(0.f, detail);   // bulging: deepest where it meets the groove, flush in the middle
  }
  return true;
}
} // namespace

void deriveMicroVoxelHeights(TileSet& ts) {
  int S = ts.size;
  ts.height.assign((size_t)ts.count * S * S, 0);
  std::vector<std::string> names(ts.count);
  for (auto& [k, v] : ts.index) if (v >= 0 && v < ts.count) names[v] = k;
  static const bool old = [] { const char* e = std::getenv("DQ_RELIEF"); return e && std::string(e) == "old"; }();
  std::vector<float> lum(S * S), d(S * S);
  int ores = 0, grooved = 0;
  for (int t = 0; t < ts.count; t++) {
    const u8* p = ts.tile(t);
    float avg = 0; int n = 0;
    for (int i = 0; i < S * S; i++) { lum[i] = lumOf(p + i * 4); if (p[i * 4 + 3] > 127) { avg += lum[i]; n++; } }
    if (!n) continue;
    avg /= n;
    std::fill(d.begin(), d.end(), 0.f);
    if (old || n < S * S * 9 / 10) lumRelief(p, S, lum, avg, d);
    else if (isOreName(names[t])) { oreRelief(p, S, lum, d); ores++; }
    else if (grooveRelief(p, S, lum, d)) grooved++;
    else lumRelief(p, S, lum, avg, d);
    u8* hgt = ts.height.data() + (size_t)t * S * S;
    for (int i = 0; i < S * S; i++) hgt[i] = (u8)clampv(d[i] * 255.f, 0.f, 255.f);
  }
  DQ_LOG("relief: %d tiles (%d ores, %d grooved)%s", ts.count, ores, grooved, old ? " [old relief]" : "");
}

} // namespace dq
