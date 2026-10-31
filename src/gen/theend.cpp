// gen/theend.cpp — The End (game/theend.js): the main island of end stone with its ring of obsidian pillars
// and the exit portal's bowl, the platform the stronghold portal lets you down onto, and past the gulf the
// outer islands with their chorus groves. Shapes are the original's, lifted END_DY (64). End cities are
// structures (gen/structures.h, DIM_END).
#include "gen/dimgens.h"

namespace dq {
using namespace vmap;
static const double JS_TAU = 6.283185307179586;   // Math.PI * 2 (common.h's TAU is a float)


EndGen::EndGen(const WorldGenOptions& o)
  : structures(*this), seed_(o.seed), nA(o.seed ^ 0xE4D01), nB(o.seed ^ 0xE4D02), nC(o.seed ^ 0xE4D03) {
  B.resolve();
  // the ten obsidian pillars (WorldGen.endPillars): where, how high, how thick; the two tallest caged
  Mulberry32 rnd((u32)(seed_ ^ 0x5b1e5));
  double off = rnd() * JS_TAU;
  int hs[10];
  for (int i = 0; i < 10; i++) hs[i] = 76 + i * 3;
  for (int i = 9; i > 0; i--) { int j = jsFloor(rnd() * (i + 1)); std::swap(hs[i], hs[j]); }
  for (int i = 0; i < 10; i++) {
    double a = off + i / 10.0 * JS_TAU; int h = hs[i];
    pillars_.push_back({jsRound(std::cos(a) * PILLAR_R), jsRound(std::sin(a) * PILLAR_R), h + END_DY, 2 + (h - 76) / 8, h >= 97});
  }
}

bool EndGen::island(int x, int z, Isle& o) const {
  double r = std::hypot((double)x, (double)z);
  if (r < 112) {
    double R = 80 + nA.fbm2(x / 60.0, z / 60.0, 2) * 9;
    if (r >= R) return false;
    double k = 1 - (r / R) * (r / R);
    int top = 61 + jsFloor(k * 3 + nB.noise2(x / 24.0, z / 24.0) * 1.5);
    int bot = top - jsFloor(3 + k * 38 + nC.noise2(x / 18.0, z / 18.0) * 4);
    o = {top + END_DY, bot + END_DY, true};
    return true;
  }
  if (r < OUTER) return false;
  double th = 0.28 + std::max(0.0, (OUTER + 70 - r) / 70) * 0.45 - std::min(0.1, std::max(0.0, r - OUTER - 70) / 4000);
  double m = nA.fbm2(x / 130.0, z / 130.0, 3) + nB.noise2(x / 40.0, z / 40.0) * 0.12;
  if (m < th) return false;
  double k = std::min(1.0, (m - th) * 5);
  int top = 58 + jsFloor(nC.fbm2(x / 90.0, z / 90.0, 2) * 9 + k * 3);
  int bot = top - jsFloor(2 + k * 22 + nB.noise2(x / 15.0, z / 15.0) * 3);
  double cd = craterDepth(x, z);   // crater bowls dent the barrens (their rims stand a little proud)
  if (cd != 0) { top -= (int)std::lround(cd); if (top <= bot + 1) return false; }
  o = {top + END_DY, bot + END_DY, false};
  return true;
}

// ---- the outer End (our own Nullscape-style lands, past the gulf) ----
int EndGen::endBiome(int x, int z) const {
  double b = nC.noise2(x / 320.0 + 51.3, z / 320.0 - 17.9), v = nA.noise2(x / 210.0 - 88.1, z / 210.0 + 41.7);
  if (b < -0.28) return 1;   // spire fields
  if (b > 0.3) return 2;     // crater barrens
  if (v > 0.32) return 3;    // violet moors
  return 0;                  // chorus highlands
}
double EndGen::craterDepth(int x, int z) const {
  if (std::hypot((double)x, (double)z) < OUTER + 40 || nC.noise2(x / 320.0 + 51.3, z / 320.0 - 17.9) < 0.2) return 0;   // only in and round the barrens
  const int C = 48;
  int cx = jsFloor(x / (double)C), cz = jsFloor(z / (double)C);
  double best = 0;
  for (int gz = cz - 1; gz <= cz + 1; gz++) for (int gx = cx - 1; gx <= cx + 1; gx++) {
    u32 h = hash32(gx, gz, seed_, 0xC4A7);
    if ((h & 255) > 150) continue;
    double ox = gx * C + 8 + ((h >> 8) & 31), oz = gz * C + 8 + ((h >> 13) & 31);
    if (endBiome((int)ox, (int)oz) != 2) continue;
    double R = 8 + ((h >> 18) & 15) * 0.7, d = std::hypot(x - ox, z - oz) / R;
    if (d < 1) best = std::max(best, (1 - d * d) * R * 0.45);
    else if (d < 1.25 && best <= 0) best = std::min(best, -(1.25 - d) * 8);   // the rim
  }
  return best;
}

void EndGen::outerFeatures(int bx, int bz, ChunkWriter& P) const {
  auto stone = [&](int x, int y, int z) { return ((y * 3 + (x ^ z)) & 7) == 0 ? B.null_stone : B.end_stone; };   // banded
  // spires: tapering needles of end stone, null-stone banded, an obsidian tip
  const int SC = 14;
  for (int gz = jsFloor((bz - 6) / (double)SC); gz <= jsFloor((bz + 21) / (double)SC); gz++)
    for (int gx = jsFloor((bx - 6) / (double)SC); gx <= jsFloor((bx + 21) / (double)SC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x5917E);
      int sx = gx * SC + 2 + (int)(h % 10), sz = gz * SC + 2 + (int)((h >> 4) % 10);
      if (endBiome(sx, sz) != 1 || ((h >> 9) & 7) > 4) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 18 + (int)((h >> 12) % 40); float rb = 1.6f + ((h >> 18) & 7) * 0.35f;
      for (int dy = -2; dy <= H; dy++) {
        float r = rb * (1 - (float)std::max(0, dy) / (H + 2)) + 0.45f; int y = c.top + dy;
        if (y >= DIM_H - 2) break;
        for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
          if (dx * dx + dz * dz > r * r) continue;
          int x = sx + dx, z = sz + dz;
          if (P.in(x, z)) P.put(x, y, z, dy >= H - 1 ? B.obsidian : stone(x, y, z));
        }
      }
    }
  // arches: a stone bow between two islands' tops
  const int AC = 72;
  for (int gz = jsFloor((bz - 30) / (double)AC); gz <= jsFloor((bz + 45) / (double)AC); gz++)
    for (int gx = jsFloor((bx - 30) / (double)AC); gx <= jsFloor((bx + 45) / (double)AC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0xA3C4);
      if ((h & 7) > 2) continue;
      int ax = gx * AC + 20 + (int)((h >> 3) % 32), az = gz * AC + 20 + (int)((h >> 8) % 32);
      if (std::hypot((double)ax, (double)az) < OUTER + 40) continue;
      bool alongX = (h >> 13) & 1; int R = 10 + (int)((h >> 14) % 12);
      int fx0 = alongX ? ax - R : ax, fz0 = alongX ? az : az - R, fx1 = alongX ? ax + R : ax, fz1 = alongX ? az : az + R;
      Isle a, b2; if (!island(fx0, fz0, a) || !island(fx1, fz1, b2)) continue;
      int base = std::min(a.top, b2.top);
      const float tube = 2.2f;
      for (int t = -R - 3; t <= R + 3; t++) for (int w = -3; w <= 3; w++) {
        int x = alongX ? ax + t : ax + w, z = alongX ? az + w : az + t;
        if (!P.in(x, z)) continue;
        for (int y = base - 2; y <= base + R + 4 && y < DIM_H - 2; y++) {
          double dy = y - base, dd = std::hypot((double)t, dy);   // distance from the arch's centre line (a half circle of radius R)
          double off = std::hypot(dd - R, (double)w);
          if (dy >= -2 && off <= tube) P.put(x, y, z, stone(x, y, z));
        }
      }
    }
  // floating shards: small upturned islands drifting above
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    int x = bx + lx, z = bz + lz;
    double m = nB.fbm2(x / 46.0 + 300.5, z / 46.0 - 200.5, 2);
    if (m < 0.42) continue;
    int top = 96 + END_DY + jsFloor(nA.noise2(x / 70.0, z / 70.0) * 14), bot = top - jsFloor((m - 0.42) * 40);
    for (int y = bot; y <= top; y++) P.put(x, y, z, y == top && endBiome(x, z) == 3 ? B.end_moss : B.end_stone);
  }
}

int EndGen::fountainY() const { Isle c; return (island(0, 0, c) ? c.top : 64 + END_DY) + 1; }

// a chorus plant: a stem that branches as it climbs, a flower at each tip
static void chorusTree(Mulberry32& rnd, int x, int y, int z, int depth, std::vector<std::array<int, 4>>& out, BlockId plant, BlockId flower) {
  int h = depth == 0 ? 2 + jsFloor(rnd() * 3) : 1 + jsFloor(rnd() * 3);
  for (int i = 0; i < h; i++) out.push_back({x, y + i, z, plant});
  int top = y + h - 1;
  int n = depth >= 3 ? 0 : depth == 0 ? 1 + jsFloor(rnd() * 3) : jsFloor(rnd() * 2.6);
  for (int b = 0; b < n && out.size() < 70; b++) {
    int d = jsFloor(rnd() * 4), dx = DIRX[d], dz = DIRZ[d];
    out.push_back({x + dx, top, z + dz, plant});
    chorusTree(rnd, x + dx, top + 1, z + dz, depth + 1, out, plant, flower);
  }
  if (!n) out.push_back({x, top + 1, z, flower});
}
bool EndGen::chorusAt(int wx, int wz, std::vector<std::array<int, 4>>& out) const {
  if (std::hypot((double)wx, (double)wz) < OUTER + 20 || hashFloat(wx, wz, seed_, 77) > 0.01) return false;
  { int eb = endBiome(wx, wz); if (eb == 1 || eb == 2 || (eb == 3 && hashFloat(wx, wz, seed_, 79) < 0.6)) return false; }   // groves in the highlands, a few on the moors
  Isle c; if (!island(wx, wz, c)) return false;
  static const int D[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};
  for (auto& d : D) { Isle o; if (!island(wx + d[0], wz + d[1], o) || std::abs(o.top - c.top) > 2) return false; }
  Mulberry32 rnd(hash32(wx, wz, seed_, 78));
  chorusTree(rnd, wx, c.top + 1, wz, 0, out, B.chorus_plant, B.chorus_flower);
  return true;
}

void EndGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * 16, bz = ch.cz * 16;
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    Isle c; if (!island(bx + lx, bz + lz, c)) continue;
    for (int y = std::max(1, c.bot); y <= c.top; y++) b.blocks[colIdx(lx, y, lz)] = B.end_stone;
  }
  StructurePlan S0; S0.seed = (u32)(seed_ ^ 0xE4D);
  ChunkWriter P(b, ch, S0, *this);
  if (std::abs(bx) < 64 && std::abs(bz) < 64) {
    for (const Pillar& pl : pillars_) {
      if (pl.x + pl.rad < bx || pl.x - pl.rad > bx + 15 || pl.z + pl.rad < bz || pl.z - pl.rad > bz + 15) continue;
      Isle c; int base = (island(pl.x, pl.z, c) ? c.top : 60 + END_DY) - 8;
      for (int z = pl.z - pl.rad; z <= pl.z + pl.rad; z++) for (int x = pl.x - pl.rad; x <= pl.x + pl.rad; x++) {
        if ((x - pl.x) * (x - pl.x) + (z - pl.z) * (z - pl.z) > pl.rad * pl.rad + 1) continue;
        for (int y = base; y <= pl.h; y++) P.put(x, y, z, B.obsidian);
      }
      P.put(pl.x, pl.h + 1, pl.z, B.bedrock);
      if (pl.cage) for (int y = pl.h + 1; y <= pl.h + 4; y++) for (int z = pl.z - 2; z <= pl.z + 2; z++) for (int x = pl.x - 2; x <= pl.x + 2; x++) {
        bool edge = std::abs(x - pl.x) == 2 || std::abs(z - pl.z) == 2 || y == pl.h + 4;
        if (edge) P.put(x, y, z, B.iron_bars);
      }
    }
    // the exit portal's bowl, dark until the dragon falls
    const int fy = fountainY();
    if (bx <= 4 && bx + 15 >= -4 && bz <= 4 && bz + 15 >= -4) {
      for (int z = -4; z <= 4; z++) for (int x = -4; x <= 4; x++) {
        double d = std::hypot((double)x, (double)z);
        if (d > 3.6) continue;
        P.put(x, fy - 1, z, B.bedrock);
        if (d > 2.6) P.put(x, fy, z, B.bedrock); else P.put(x, fy, z, AIR);
        for (int y = fy + 1; y < fy + 5; y++) P.put(x, y, z, AIR);
      }
      for (int y = fy; y <= fy + 3; y++) P.put(0, y, 0, B.bedrock);
    }
  }
  // the obsidian platform where the stronghold portal lets you down
  if (PLAT_X + 2 >= bx && PLAT_X - 2 <= bx + 15 && PLAT_Z + 2 >= bz && PLAT_Z - 2 <= bz + 15) {
    P.fill(PLAT_X - 2, PLAT_Y, PLAT_Z - 2, PLAT_X + 2, PLAT_Y, PLAT_Z + 2, B.obsidian);
    P.fill(PLAT_X - 2, PLAT_Y + 1, PLAT_Z - 2, PLAT_X + 2, PLAT_Y + 3, PLAT_Z + 2, AIR);
  }
  // out past the gulf: each land's ground, then its spires, arches and floating shards
  if (std::hypot(bx + 8.0, bz + 8.0) > OUTER - 30) {
    for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
      int x = bx + lx, z = bz + lz; Isle c;
      if (!island(x, z, c) || c.main) continue;
      int eb = endBiome(x, z);
      double cd = craterDepth(x, z);
      if (cd > 0.5) for (int y = c.top; y > c.top - 2 && y > c.bot; y--) P.put(x, y, z, B.null_stone);   // a scorched crater floor
      else if (eb == 3 && hashFloat(x, c.top, z, 41) < 0.85) P.put(x, c.top, z, B.end_moss);
      else if (eb == 1 && hashFloat(x, c.top, z, 42) < 0.12) P.put(x, c.top, z, B.null_stone);
    }
    outerFeatures(bx, bz, P);
  }
  // out on the islands: chorus groves
  if (std::hypot(bx + 8.0, bz + 8.0) > OUTER - 30) {
    std::vector<std::array<int, 4>> tree;
    for (int wz = bz - 6; wz < bz + 22; wz++) for (int wx = bx - 6; wx < bx + 22; wx++) {
      tree.clear(); if (!chorusAt(wx, wz, tree)) continue;
      for (auto& p : tree) if (P.in(p[0], p[2]) && p[1] > 0 && p[1] < DIM_H && !P.get(p[0], p[1], p[2])) P.put(p[0], p[1], p[2], (BlockId)p[3]);
    }
  }
  structures.build(b, ch);
  b.store(ch);
}

TerrainColumn EndGen::terrainColumn(int x, int z) const {
  TerrainColumn t; Isle c;
  if (island(x, z, c)) { t.y = c.top; t.jsY = c.top - END_DY; }
  return t;
}

void EndGen::sample(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample();
  Isle c;
  if (!island(wx, wz, c)) s.height = -1;
  else { s.height = (i16)c.top; s.floatBottom = (i16)std::max(1, c.bot); s.top = s.under = B.end_stone; }
  if (std::abs(wx) < 64 && std::abs(wz) < 64) for (const Pillar& pl : pillars_)
    if ((wx - pl.x) * (wx - pl.x) + (wz - pl.z) * (wz - pl.z) <= pl.rad * pl.rad + 1) {
      s.height = (i16)(pl.h + 1); s.top = B.bedrock; s.under = B.obsidian;
      if (s.floatBottom < 0) { Isle pc; s.floatBottom = (i16)((island(pl.x, pl.z, pc) ? pc.top : 60 + END_DY) - 8); }
    }
}

} // namespace dq
