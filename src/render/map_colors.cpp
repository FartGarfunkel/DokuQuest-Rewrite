// render/map_colors.cpp — the colour of a block seen from above (game/maps.js mapColorOf / mapRGB), shared by paper
// maps (render/map_render.cpp) and the launcher's server map and isometric view.
#include "render/map_colors.h"
#include "core/registry.h"

namespace dq {

namespace {
const float TINT_GRASS[3] = {124, 191, 72};
float lerp3(float a, float b, float t) { return a + (b - a) * t; }
void climateGrass(float tb, float hb, float* out) {
  static const float cold[3] = {128, 168, 140}, temperate[3] = {124, 191, 74}, lush[3] = {80, 160, 56}, dry[3] = {190, 178, 92};
  float t = (tb - 128) / 127, h = (hb - 128) / 127;
  float wt = smoothstepf(-0.55f, 0.25f, t), wh = smoothstepf(-0.3f, 0.45f, h), hot = smoothstepf(0.2f, 0.6f, t);
  for (int i = 0; i < 3; i++) {
    float c = lerp3(cold[i], temperate[i], wt);
    c = lerp3(c, lush[i], wh * wt * 0.85f);
    c = lerp3(c, dry[i], hot * (1 - wh));
    out[i] = c;
  }
}
void climateWater(float tb, float hb, float* out) {
  static const float cold[3] = {70, 104, 176}, temperate[3] = {52, 104, 214}, warm[3] = {44, 150, 180};
  float t = (tb - 128) / 127;
  for (int i = 0; i < 3; i++) out[i] = t < 0 ? lerp3(temperate[i], cold[i], smoothstepf(0, -0.6f, t)) : lerp3(temperate[i], warm[i], smoothstepf(0.1f, 0.6f, t));
}
struct MapCol { float p[3] = {0, 0, 0}, t[3] = {0, 0, 0}; int k = 0; bool ok = false; };
std::vector<MapCol> g_cols;
std::string g_colStamp;
const MapCol& mapColorOf(const TileSet& ts, BlockId id) {
  std::string st = mapTileStamp(ts) + "@" + std::to_string((uintptr_t)&ts);
  if (st != g_colStamp) { g_cols.clear(); g_colStamp = st; }
  if (g_cols.size() <= id) g_cols.resize(std::max<size_t>(reg().blocks.size(), id + 1));
  MapCol& c = g_cols[id];
  if (c.ok) return c;
  c.ok = true;
  const BlockDef& b = reg().block(id);
  int tile = b.faces[FACE_PY];
  if (id && tile >= 0 && tile < ts.count) {
    const u8* P = ts.tile(tile);
    int n = 0;
    for (int i = 0; i < ts.size * ts.size; i++) {
      u8 a = P[i * 4 + 3];
      if (a < 26) continue;
      n++;
      float* d = a == 253 ? c.t : c.p;
      d[0] += P[i * 4]; d[1] += P[i * 4 + 1]; d[2] += P[i * 4 + 2];
    }
    if (n) for (int k = 0; k < 3; k++) { c.p[k] /= n; c.t[k] /= n; }
  }
  std::string tint = b.str("tint");
  c.k = b.str("liquid") == "water" ? 3 : tint == "grass" ? 1 : tint == "foliage" ? 2 : 0;
  return c;
}
}

std::string mapTileStamp(const TileSet& ts) { return ts.packName + "#" + std::to_string(ts.count); }
int mapColorKind(const TileSet& ts, BlockId id) { return mapColorOf(ts, id).k; }
void mapRGB(const TileSet& ts, BlockId id, u8 cl, float* out) {
  const MapCol& c = mapColorOf(ts, id);
  float t = (cl >> 4) * 17.f, hu = (cl & 15) * 17.f, tint[3];
  if (c.k == 3) {
    climateWater(t, hu, tint);
    for (int k = 0; k < 3; k++) out[k] = (c.p[k] + c.t[k]) * tint[k] * (1.25f / 255);
    return;
  }
  const float* T = TINT_GRASS;
  if (c.k) {
    climateGrass(t, hu, tint);
    if (c.k == 2) { tint[0] *= 0.82f; tint[1] *= 0.86f; tint[2] *= 0.8f; }
    T = tint;
  }
  for (int k = 0; k < 3; k++) out[k] = c.p[k] + c.t[k] * T[k] * (1.3f / 255);
}

} // namespace dq
