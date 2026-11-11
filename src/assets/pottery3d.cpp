// assets/pottery3d.cpp — the immersive pottery and early tools as real 3D voxel models (after Vintage Story's and
// TerraFirmaCraft's: round thrown pots with rims and lips, a jug with its handle, molds as clay blocks with the tool's
// cavity cut in their top (the cavity is the TFC knapping pattern itself, sim/knapping.h), a filled mold shows its metal),
// and the firestarter (a drill stick on its hearth board).
// Grids are 32 voxels to the block, the model standing on its foot at y = -0.5 like the bucket (assets/item_parts.cpp).
#include "assets/pottery3d.h"
#include "sim/knapping.h"
#include "core/registry.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace dq {
namespace {

constexpr int N = 32;   // grid edge (one block)
u32 hashN(int x, int y, int z) { u32 h = (u32)x * 73856093u ^ (u32)y * 19349663u ^ (u32)z * 83492791u; h ^= h >> 13; h *= 0x5bd1e995u; return h ^ (h >> 15); }
// a colour with a little grain, darker toward the foot
u32 tone(vec3 rgb, int x, int y, int z, float k = 1.f) {
  const float n = 0.94f + (hashN(x, y, z) & 15) / 15.f * 0.1f;
  const float foot = 0.86f + 0.14f * std::min(1.f, y / 10.f);
  const float f = n * foot * k;
  return voxRGBA((int)(rgb.x * f), (int)(rgb.y * f), (int)(rgb.z * f));
}

struct Lathe {
  VoxelGrid& g; vec3 rgb;
  // a ring of radius r (voxels) around the grid's middle at height y, wall thickness t (t <= 0: solid disc)
  // inner < 0: solid disc; else the wall runs from inner out to r
  void ring(int y, float r, float inner, float k = 1.f) {
    const float c = (N - 1) / 2.f;
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
      const float d = std::hypot(x - c, z - c);
      if (d <= r + 0.35f && (inner < 0 || d >= inner + 0.35f)) g.set(x, y, z, tone(rgb, x, y, z, k));
    }
  }
};

// ---- thrown pottery: a profile of radii from the foot up; `open` rows from the top are hollow (the mouth)
void thrown(VoxelGrid& g, vec3 rgb, const std::vector<float>& prof, int floorRows, float wall, bool lipShade = true) {
  Lathe L{g, rgb};
  const int H = (int)prof.size();
  for (int y = 0; y < H; y++) {
    const bool solid = y < floorRows;
    const bool lip = lipShade && y == H - 1;
    // the wall reaches in to its neighbours' insides too, so a belly or a neck that steps out or in leaves no gap
    float inner = prof[y] - wall;
    if (y > 0) inner = std::min(inner, prof[y - 1] - wall);
    if (y + 1 < H) inner = std::min(inner, prof[y + 1] - wall);
    L.ring(y, prof[y], solid ? -1.f : std::max(0.f, inner), lip ? 1.12f : 1.f);
  }
}

void addJugHandle(VoxelGrid& g, vec3 rgb, int y0, int y1, int x0, int reach) {
  const int c = N / 2;
  for (int y = y0; y <= y1; y++) {
    const float t = (float)(y - y0) / std::max(1, y1 - y0);
    const int out = (int)std::round(std::sin(t * 3.14159f) * reach);
    for (int dz = -1; dz <= 0; dz++) { g.set(x0 + out, y, c + dz, tone(rgb, x0 + out, y, c + dz, 0.95f)); g.set(x0 + out + 1, y, c + dz, tone(rgb, x0 + out + 1, y, c + dz, 0.9f)); }
  }
}

// ---- a mold: a block of clay, the knapping pattern's chipped cells cut 3 deep into its top (5 x 5 cells of 5 voxels)
bool buildMold(VoxelGrid& g, vec3 clay, const std::string& unfiredKey, const vec3* metal) {
  const KnapPattern* pat = nullptr;
  for (const KnapPattern& p : knapPatterns("clay")) if (p.out == unfiredKey) { pat = &p; break; }
  bool cut[5][5] = {};
  if (pat) {
    const int h = (int)pat->rows.size(), w = h ? (int)pat->rows[0].size() : 0;
    const int oy = (5 - h) / 2, ox = (5 - w) / 2;
    for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) {
      const int px = x - ox, py = y - oy;
      const bool in = px >= 0 && px < w && py >= 0 && py < h;
      const bool kept = in ? pat->rows[py][px] == '#' : pat->defOn;
      cut[y][x] = !kept;
    }
  }
  const int C = 5, X0 = (N - 5 * C) / 2, H = 8, DEPTH = 3;
  for (int z = 0; z < 5 * C; z++) for (int x = 0; x < 5 * C; x++) {
    const bool hole = cut[z / C][x / C];
    const bool edge = x == 0 || z == 0 || x == 5 * C - 1 || z == 5 * C - 1;
    for (int y = 0; y < H; y++) {
      const int gx = X0 + x, gz = X0 + z;
      if (hole && y >= H - DEPTH) {
        if (metal && y < H - 1) g.set(gx, y, gz, tone(*metal, gx, y, gz, y == H - 2 ? 1.1f : 0.95f));   // the poured metal, nearly to the brim
        continue;
      }
      float k = edge ? 0.9f : 1.f;
      if (y == H - 1) k *= 1.08f;                       // the top face, lit
      if (hole && y == H - DEPTH - 1) k *= 0.8f;        // the cavity's floor, in shadow
      g.set(gx, y, gz, tone(clay, gx, y, gz, k));
    }
  }
  return true;
}

void stick(VoxelGrid& g, int x0, int z0, int y0, int y1, vec3 wood) {
  for (int y = y0; y <= y1; y++) for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++) g.set(x0 + dx, y, z0 + dz, tone(wood, x0 + dx, y, z0 + dz, (dx + dz) ? 0.9f : 1.f));
}

const vec3 UNFIRED(150, 146, 140), FIRED(150, 80, 54), FIRECLAY(214, 204, 186);   // (fired: a deep terracotta, so poured copper shows against it)

}  // namespace

bool buildPotteryGrid(const std::string& keyIn, VoxelGrid& g, VoxelFrame& fr, float& depth, bool& stand, const vec3* metal) {
  std::string key = keyIn;
  g.resize(ivec3(N)); fr.voxel = 1.f / N; fr.origin = vec3(-0.5f); stand = true; depth = 0.3f;
  const bool unfired = key.rfind("unfired_", 0) == 0, ceramic = key.rfind("ceramic_", 0) == 0;
  std::string shape = unfired ? key.substr(8) : ceramic ? key.substr(8) : key;
  vec3 col = unfired ? UNFIRED : FIRED;
  if (key == "crucible" || key == "unfired_crucible") { shape = "crucible"; col = unfired ? FIRECLAY * 0.85f : FIRECLAY; }

  if (shape.size() > 5 && shape.compare(shape.size() - 5, 5, "_mold") == 0) {
    buildMold(g, col, "unfired_" + shape, metal); depth = 0.12f; return true;
  }
  if (!unfired && !ceramic && shape != "crucible") {
    if (key == "candle" || (key.size() > 7 && key.compare(key.size() - 7, 7, "_candle") == 0)) {   // a wax candle and its wick
      static const std::pair<const char*, vec3> DYE[] = {{"white", {233, 236, 236}}, {"orange", {240, 118, 19}}, {"magenta", {189, 68, 179}}, {"light_blue", {58, 175, 217}},
        {"yellow", {248, 197, 39}}, {"lime", {112, 185, 25}}, {"pink", {237, 141, 172}}, {"gray", {72, 78, 82}}, {"light_gray", {150, 150, 142}}, {"cyan", {21, 137, 145}},
        {"purple", {121, 42, 172}}, {"blue", {53, 57, 157}}, {"brown", {114, 71, 40}}, {"green", {84, 109, 27}}, {"red", {160, 39, 34}}, {"black", {32, 32, 38}}, {"soul_wax", {168, 214, 226}}};
      vec3 wax(232, 212, 162);
      for (auto& [n, c] : DYE) if (key == std::string(n) + "_candle") wax = c;
      Lathe L{g, wax};
      for (int y = 0; y < 13; y++) L.ring(y, 2.6f, -1.f, y == 12 ? 1.1f : 1.f);
      for (int y = 13; y < 15; y++) g.set(N / 2, y, N / 2, voxRGBA(40, 34, 30));
      depth = 0.1f; return true;
    }
    if (key == "firestarter") {   // a hearth board with the drill stick standing in its notch
      const vec3 wood(150, 112, 70), dark(96, 70, 44);
      for (int z = 12; z < 20; z++) for (int x = 6; x < 26; x++) for (int y = 0; y < 3; y++) g.set(x, y, z, tone(wood, x, y, z, y == 2 ? 1.06f : 0.92f));
      for (int z = 15; z < 17; z++) for (int x = 18; x < 21; x++) g.set(x, 2, z, voxRGBA(40, 30, 24));   // the charred notch
      stick(g, 19, 15, 3, 24, dark);
      depth = 0.15f; return true;
    }
    return false;
  }
  if (shape == "vessel") {   // TFC's small vessel: a round-bellied pot, a short neck and a rolled lip
    thrown(g, col, {4, 6, 7.5f, 8.5f, 9, 9.5f, 9.5f, 9.5f, 9, 8.5f, 7.5f, 6.5f, 5.5f, 5, 5, 5.5f, 6}, 2, 1.6f);
    depth = 0.3f; return true;
  }
  if (shape == "jug") {   // a tall jug: belly, a narrow neck, the lip, a handle on its side
    thrown(g, col, {4.5f, 6, 7, 7.5f, 8, 8, 8, 7.5f, 7, 6, 5, 4, 3.5f, 3.5f, 3.5f, 3.5f, 3.5f, 4, 4.5f}, 2, 1.5f);
    addJugHandle(g, col, 6, 17, N / 2 + 7, 3);
    depth = 0.28f; return true;
  }
  if (shape == "pot") {   // a cooking pot: wide and straight-sided, two lugs
    thrown(g, col, {7, 8.5f, 9.5f, 10, 10, 10, 10, 10, 10, 10, 10.5f}, 2, 1.4f);
    for (int y = 7; y <= 8; y++) for (int dz = -1; dz <= 0; dz++) { g.set(N / 2 + 11, y, N / 2 + dz, tone(col, 0, y, dz, 0.9f)); g.set(N / 2 - 12, y, N / 2 + dz, tone(col, 1, y, dz, 0.9f)); }
    depth = 0.33f; return true;
  }
  if (shape == "bowl") { thrown(g, col, {4, 6, 7.5f, 8.5f, 9.5f, 10}, 1, 1.3f); depth = 0.32f; return true; }
  if (shape == "pan") { thrown(g, col, {7, 10, 11, 11.5f}, 1, 1.2f); depth = 0.36f; return true; }   // a wide shallow gold pan
  if (shape == "spindle_head") {   // a whorl: a disc with a hole through it
    Lathe L{g, col};
    for (int y = 0; y < 3; y++) L.ring(y, 6.5f, -1.f);
    const float c = (N - 1) / 2.f;
    for (int y = 0; y < 3; y++) for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) if (std::hypot(x - c, z - c) < 1.4f) g.set(x, y, z, 0);
    depth = 0.2f; return true;
  }
  if (shape == "crucible") {   // fire clay, thick-walled and tapering, a pouring lip
    thrown(g, col, {6, 6.5f, 7, 7.2f, 7.4f, 7.6f, 7.8f, 8, 8.2f, 8.4f, 8.6f, 8.8f, 9, 9.2f}, 3, 2.2f);
    for (int y = 12; y < 14; y++) for (int x = N / 2 + 8; x < N / 2 + 11; x++) g.set(x, y, N / 2, tone(col, x, y, 0, 1.05f));
    depth = 0.3f; return true;
  }
  return false;
}

}  // namespace dq
