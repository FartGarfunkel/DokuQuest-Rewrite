// gen/carvers.cpp — Minecraft Beta's carvers (MapGenCaves / MapGenCavesHell / MapGenRavine), carving straight into
// one band chunk's GenBuf. Every source chunk within RANGE chunks seeds its systems from the world seed the way
// Minecraft's MapGenBase does, so a tunnel is the same whichever chunk it is carved from.
#include "gen/carvers.h"
#include <cmath>

namespace dq {

namespace {
constexpr int RANGE = 8;
constexpr double PI_D = 3.141592653589793;

struct Carver {
  GenBuf& b; const CarveRules& r; int cx, cz;
  // MapGenCaves.generateCaveNode / MapGenRavine.generateRavine share this walk; ravine = taller, rougher sides
  void tunnel(i64 seed, double x, double y, double z, float width, float yaw, float pitch, int step, int end, double vScale,
              bool ravine) {
    const double mx = cx * 16 + 8, mz = cz * 16 + 8;
    float yawV = 0, pitchV = 0;
    JRandom rnd(seed);
    float wmul[DIM_H];
    if (ravine) {
      float f = 1;
      for (int i = 0; i < DIM_H; i++) {
        if (i == 0 || rnd.nextInt(3) == 0) { const float a = rnd.nextFloat(); f = 1.f + a * rnd.nextFloat(); }
        wmul[i] = f * f;
      }
    }
    if (end <= 0) { const int span = RANGE * 16 - 16; end = span - rnd.nextInt(span / 4); }
    bool room = false;
    if (step == -1) { step = end / 2; room = true; }
    const int fork = rnd.nextInt(end / 2) + end / 4;
    const bool steep = rnd.nextInt(6) == 0;
    for (; step < end; step++) {
      double hr = 1.5 + std::sin(step * PI_D / end) * width;
      double vr = hr * vScale;
      if (ravine) { hr *= rnd.nextFloat() * 0.25 + 0.75; vr *= rnd.nextFloat() * 0.25 + 0.75; }
      const float cp = std::cos(pitch), sp = std::sin(pitch);
      x += std::cos(yaw) * cp; y += sp; z += std::sin(yaw) * cp;
      if (ravine) { pitch *= 0.7f; pitch += pitchV * 0.05f; yaw += yawV * 0.05f; pitchV *= 0.8f; yawV *= 0.5f; }
      else { pitch *= steep ? 0.92f : 0.7f; pitch += pitchV * 0.1f; yaw += yawV * 0.1f; pitchV *= 0.9f; yawV *= 0.75f; }
      // (every draw in its own statement: C++ leaves the order of operands open, Java doesn't)
      { const float a = rnd.nextFloat(), b2 = rnd.nextFloat(), c = rnd.nextFloat(); pitchV += (a - b2) * c * 2.f; }
      { const float a = rnd.nextFloat(), b2 = rnd.nextFloat(), c = rnd.nextFloat(); yawV += (a - b2) * c * 4.f; }
      if (!ravine && !room && step == fork && width > 1.f) {
        for (int side : {-1, 1}) {
          const i64 sd = rnd.nextLong(); const float w = rnd.nextFloat() * 0.5f + 0.5f;
          tunnel(sd, x, y, z, w, yaw + side * (float)(PI_D / 2), pitch / 3.f, step, end, 1.0, false);
        }
        return;
      }
      if (!room && rnd.nextInt(4) == 0) continue;
      const double dx = x - mx, dz = z - mz, left = end - step, reach = width + 2.0 + 16.0;
      if (dx * dx + dz * dz - left * left > reach * reach) return;
      if (x < mx - 16 - hr * 2 || z < mz - 16 - hr * 2 || x > mx + 16 + hr * 2 || z > mz + 16 + hr * 2) continue;
      carveBlob(x, y, z, hr, vr, ravine ? wmul : nullptr);
      if (room) break;
    }
  }

  void carveBlob(double x, double y, double z, double hr, double vr, const float* wmul) {
    int x0 = (int)std::floor(x - hr) - cx * 16 - 1, x1 = (int)std::floor(x + hr) - cx * 16 + 1;
    int y0 = (int)std::floor(y - vr) - 1, y1 = (int)std::floor(y + vr) + 1;
    int z0 = (int)std::floor(z - hr) - cz * 16 - 1, z1 = (int)std::floor(z + hr) - cz * 16 + 1;
    x0 = std::max(x0, 0); x1 = std::min(x1, 16); z0 = std::max(z0, 0); z1 = std::min(z1, 16);
    y0 = std::max(y0, r.minY); y1 = std::min(y1, r.maxY);
    if (x0 >= x1 || z0 >= z1 || y0 >= y1) return;
    // water in the shell: leave this piece (no flooded caves under the sea or rivers)
    for (int xx = x0; xx < x1; xx++) for (int zz = z0; zz < z1; zz++) for (int yy = y1 + 1; yy >= y0 - 1; yy--) {
      if (yy < 0 || yy >= DIM_H) continue;
      if (r.stopper[b.blocks[colIdx(xx, yy, zz)]]) return;
      if (yy != y0 - 1 && xx != x0 && xx != x1 - 1 && zz != z0 && zz != z1 - 1) yy = y0;
    }
    for (int xx = x0; xx < x1; xx++) {
      const double ddx = (xx + cx * 16 + 0.5 - x) / hr;
      for (int zz = z0; zz < z1; zz++) {
        const double ddz = (zz + cz * 16 + 0.5 - z) / hr;
        if (ddx * ddx + ddz * ddz >= 1.0) continue;
        const int top = r.protect ? r.protect[zz * 16 + xx] : DIM_H;
        bool grass = false;
        for (int yy = y1 - 1; yy >= y0; yy--) {
          const double ddy = (yy + 0.5 - y) / vr;
          const bool in = wmul ? (ddx * ddx + ddz * ddz) * wmul[yy] + ddy * ddy / 6.0 < 1.0
                               : ddy > -0.7 && ddx * ddx + ddy * ddy + ddz * ddz < 1.0;
          if (!in || yy >= top) continue;
          const int i = colIdx(xx, yy, zz);
          const BlockId id = b.blocks[i];
          if (id == r.grass && r.grass) grass = true;
          if (!r.carvable[id]) continue;
          if (yy < r.lavaY && r.lava) { b.blocks[i] = r.lava; b.meta[i] = 0; }
          else {
            b.blocks[i] = AIR; b.meta[i] = 0;
            if (grass && yy > 0 && b.blocks[i - 256] == r.dirt) b.blocks[i - 256] = r.grass;
          }
        }
      }
    }
  }

  // MapGenCaves.recursiveGenerate (one source chunk)
  void caves(JRandom& rnd, int sx, int sz) {
    int n = rnd.nextInt(rnd.nextInt(rnd.nextInt(r.hell ? 10 : 40) + 1) + 1);
    if (rnd.nextInt(r.hell ? 5 : 15) != 0) n = 0;
    for (int j = 0; j < n; j++) {
      const double x = sx * 16 + rnd.nextInt(16);
      const double y = r.hell ? rnd.nextInt(r.maxY - 8) : rnd.nextInt(rnd.nextInt(120) + 8);
      const double z = sz * 16 + rnd.nextInt(16);
      int k = 1;
      if (rnd.nextInt(4) == 0) {
        // a room: a short fat blob (MapGenCaves.generateLargeCaveNode)
        const i64 sd = rnd.nextLong(); const float w = 1.f + rnd.nextFloat() * 6.f;
        tunnel(sd, x, y, z, w, 0, 0, -1, -1, 0.5, false);
        k += rnd.nextInt(4);
      }
      for (int l = 0; l < k; l++) {
        const float yaw = rnd.nextFloat() * (float)(PI_D * 2), pitch = (rnd.nextFloat() - 0.5f) * 2.f / 8.f;
        const float w1 = rnd.nextFloat() * 2.f; float w = w1 + rnd.nextFloat();
        if (r.hell) w *= 2.f;
        const i64 sd = rnd.nextLong();
        tunnel(sd, x, y, z, w, yaw, pitch, 0, 0, r.hell ? 0.5 : 1.0, false);
      }
    }
  }
  // MapGenRavine.recursiveGenerate (one source chunk)
  void ravines(JRandom& rnd, int sx, int sz) {
    if (rnd.nextInt(50) != 0) return;
    const double x = sx * 16 + rnd.nextInt(16), y = rnd.nextInt(rnd.nextInt(40) + 8) + 20, z = sz * 16 + rnd.nextInt(16);
    const float yaw = rnd.nextFloat() * (float)(PI_D * 2), pitch = (rnd.nextFloat() - 0.5f) * 2.f / 8.f;
    const float w1 = rnd.nextFloat() * 2.f; const float w = (w1 + rnd.nextFloat()) * 2.f;
    const i64 sd = rnd.nextLong();
    tunnel(sd, x, y, z, w, yaw, pitch, 0, 0, 3.0, true);
  }
};

template <class F> void eachSource(int cx, int cz, i64 seed, F&& f) {
  JRandom rnd(seed);
  const i64 a = rnd.nextLong() / 2 * 2 + 1, c = rnd.nextLong() / 2 * 2 + 1;
  for (int sx = cx - RANGE; sx <= cx + RANGE; sx++) for (int sz = cz - RANGE; sz <= cz + RANGE; sz++) {
    rnd.setSeed((i64)((u64)(i64)sx * (u64)a + (u64)(i64)sz * (u64)c) ^ seed);
    f(rnd, sx, sz);
  }
}
} // namespace

i64 chunkSeed(i64 world, int cx, int cz, i64 salt) {
  JRandom r(world ^ salt);
  const i64 a = r.nextLong() / 2 * 2 + 1, c = r.nextLong() / 2 * 2 + 1;
  return (i64)((u64)(i64)cx * (u64)a + (u64)(i64)cz * (u64)c) ^ (world ^ salt);
}

void oreVein(GenBuf& b, int cx, int cz, JRandom& rnd, int x, int y, int z, int n, BlockId id, const u8* replace) {
  const int bx = cx * 16, bz = cz * 16;
  const float f = rnd.nextFloat() * (float)PI_D;
  const double xa = x + std::sin(f) * n / 8.0, xb = x - std::sin(f) * n / 8.0;
  const double za = z + std::cos(f) * n / 8.0, zb = z - std::cos(f) * n / 8.0;
  const double ya = y + rnd.nextInt(3) - 2, yb = y + rnd.nextInt(3) - 2;
  const double pad = n / 8.0 + 2;
  const bool near = !(std::max(xa, xb) + pad < bx || std::min(xa, xb) - pad > bx + 15 || std::max(za, zb) + pad < bz || std::min(za, zb) - pad > bz + 15);
  for (int l = 0; l <= n; l++) {
    const double px = xa + (xb - xa) * l / n, py = ya + (yb - ya) * l / n, pz = za + (zb - za) * l / n;
    const double s = rnd.nextDouble() * n / 16.0;
    if (!near || !id) continue;
    const double rad = ((std::sin(l * PI_D / n) + 1.0) * s + 1.0) / 2;
    const int x0 = std::max(bx, (int)std::floor(px - rad)), x1 = std::min(bx + 15, (int)std::floor(px + rad));
    const int z0 = std::max(bz, (int)std::floor(pz - rad)), z1 = std::min(bz + 15, (int)std::floor(pz + rad));
    const int y0 = std::max(1, (int)std::floor(py - rad)), y1 = std::min(DIM_H - 2, (int)std::floor(py + rad));
    for (int xx = x0; xx <= x1; xx++) {
      const double dx = (xx + 0.5 - px) / rad;
      for (int yy = y0; yy <= y1; yy++) {
        const double dy = (yy + 0.5 - py) / rad;
        if (dx * dx + dy * dy >= 1) continue;
        for (int zz = z0; zz <= z1; zz++) {
          const double dz = (zz + 0.5 - pz) / rad;
          if (dx * dx + dy * dy + dz * dz >= 1) continue;
          BlockId& cur = b.blocks[colIdx(xx - bx, yy, zz - bz)];
          if (replace[cur]) cur = id;
        }
      }
    }
  }
}

void carveCaves(GenBuf& b, int cx, int cz, const CarveRules& r) {
  Carver k{b, r, cx, cz};
  eachSource(cx, cz, r.seed, [&](JRandom& rnd, int sx, int sz) { k.caves(rnd, sx, sz); });
}

void carveRavines(GenBuf& b, int cx, int cz, const CarveRules& r) {
  Carver k{b, r, cx, cz};
  eachSource(cx, cz, r.seed ^ 0x52A7111EL, [&](JRandom& rnd, int sx, int sz) { k.ravines(rnd, sx, sz); });
}

} // namespace dq
