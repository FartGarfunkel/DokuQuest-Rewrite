// gen/realms.cpp — the side realms, in their own Worlds (stored in the Overworld band, original y kept):
// the pocket dimension's walled rooms (worldgen.js generatePocket) and his realm (riftarc.js generateRealm).
// World types "pocket" and "realm"; registerDefaultWorldGen() also registers "default".
#include "gen/dimgens.h"
#include "gen/overworld.h"
#include "core/init.h"

namespace dq {

// ---- the pocket dimension: one room per player every 256 blocks along x; unbreakable glowing walls ----
void PocketGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * 16, bz = ch.cz * 16;
  const int rx = jsFloor((bx + 8) / 256.0 + 0.5) * 256;
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = bx + x - rx, wz = bz + z;
    if (std::fabs(wx + 0.5) > HALF + 1 || std::fabs(wz + 0.5) > HALF + 1) continue;
    const bool wall = std::fabs(wx + 0.5) > HALF || std::fabs(wz + 0.5) > HALF;
    for (int y = FLOOR; y <= CEIL; y++) if (wall || y == FLOOR || y == CEIL) b.blocks[colIdx(x, y, z)] = B.pocket_wall;
  }
  b.store(ch);
}
void PocketGen::sample(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample();
  const int rx = jsFloor((wx + 0.5) / 256.0 + 0.5) * 256, dx = wx - rx;
  if (std::fabs(dx + 0.5) > HALF + 1 || std::fabs(wz + 0.5) > HALF + 1) { s.height = -1; return; }
  s.height = CEIL; s.top = s.under = B.pocket_wall; s.floatBottom = FLOOR;
}

// ---- his realm: hall after hall of rift stone along +x, broken pieces of the overworld hanging in the air ----
void RealmGen::islands(int hi, std::vector<Isle>& l) const {
  if (hi < 1 || hi > 3) return;
  Mulberry32 r(hash32(seed_, hi, 4, 4444));
  for (int k = 0; k < 5; k++) {
    double x = hi * HALL + 14 + r() * 68, y = 52 + r() * 20, z = -34 + r() * 68, rr = 4 + r() * 4; bool tree = r() < 0.5;
    l.push_back({x, y, z, rr, tree});
  }
}
void RealmGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * 16, bz = ch.cz * 16;
  const BlockId W = B.rift_wall, G = B.rift_glass, C = B.rift_crystal;
  std::vector<Isle> isles;
  int isleHall = INT32_MIN;
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    const int wx = bx + lx, wz = bz + lz;
    const int hi = floordiv(wx + 2, HALL), x0 = hi * HALL, x1 = x0 + 95, hx = wx - x0;
    if (hi < 0 || hi >= HALLS) continue;
    auto set = [&](int y, BlockId id) { if (y > 0 && y < DIM_H) b.blocks[colIdx(lx, y, lz)] = id; };
    auto hf = [&](int s) { return hashFloat(wx, wz, seed_, s); };
    const bool inHallX = wx >= x0 && wx <= x1, inHallZ = wz >= -48 && wz <= 47;
    const bool shellX = wx >= x0 - 2 && wx <= x1 + 2, shellZ = wz >= -50 && wz <= 49;
    const bool corr = hi < HALLS - 1 && wx > x1 && wx <= x1 + 17;
    const bool corrIn = corr && wz >= -4 && wz <= 3, corrShell = corr && wz >= -6 && wz <= 5;
    const bool door = (wx >= x1 + 1 || wx <= x0 - 1) && wz >= -4 && wz <= 3;
    if (inHallX && inHallZ) {
      for (int y = FLOOR - 2; y <= FLOOR; y++) set(y, W);
      double q = hf(1);
      if (q < 0.035) set(FLOOR, C); else if (q < 0.1) { set(FLOOR, G); set(FLOOR - 1, C); }
      set(CEIL, W); set(CEIL + 1, W);
      if (hf(2) < 0.025) { int n = 1 + jsFloor(hf(8) * 5); for (int y = CEIL - n; y < CEIL; y++) set(y, C); }
      int px = ((hx + 7 + hi * 5) % 24 + 24) % 24, pz = ((wz + 51 + hi * 9) % 24 + 24) % 24;
      if (px < 3 && pz < 3 && hi < HALLS - 1) for (int y = FLOOR + 1; y < CEIL; y++) set(y, (y % 13 == 0 && px == 1) ? C : W);
      if (hi == HALLS - 1) {
        double d = std::hypot((double)(hx - 48), (double)wz);
        if (d > 20 && d < 21.5) set(FLOOR + 1, C);
        if (d < 3.5) set(FLOOR + 1, W);
        if (d > 30 && d < 31 && hf(3) < 0.4) for (int y = FLOOR + 1; y < FLOOR + 1 + jsFloor(hf(4) * 18); y++) set(y, W);
      }
      if (isleHall != hi) { isles.clear(); islands(hi, isles); isleHall = hi; }
      for (const Isle& is : isles) {
        double dx = wx + 0.5 - is.x, dz = wz + 0.5 - is.z, rr = (dx * dx + dz * dz) / (is.r * is.r);
        if (rr >= 1) continue;
        double k = std::sqrt(1 - rr);
        int top = jsFloor(is.y + k * 1.5), bot = jsFloor(is.y - k * is.r * 1.1 - hf(5) * 2);
        for (int y = bot; y <= top; y++) set(y, y == top ? B.grass : y >= top - 2 ? B.dirt : (hf(6 + y) < 0.06 ? B.coal_ore : B.stone));
        if (is.tree && std::fabs(dx) < 0.6 && std::fabs(dz) < 0.6) for (int y = top + 1; y <= top + 4; y++) set(y, B.log);
        if (is.tree && std::fabs(dx) < 2.6 && std::fabs(dz) < 2.6 && std::hypot(dx, dz) > 0.6) for (int y = top + 3; y <= top + 5; y++) if (hf(30 + y) < 0.8) set(y, B.leaves);
      }
    } else if (shellX && shellZ && !(door && corrIn)) {
      const bool openDoor = door && wz >= -4 && wz <= 3;
      for (int y = FLOOR - 2; y <= CEIL + 1; y++) {
        if (openDoor && y > FLOOR && y <= FLOOR + 10 && ((wx > x1 && hi < HALLS - 1) || (wx < x0 && hi > 0))) continue;
        set(y, hashFloat(wx, y, wz, seed_) < 0.03 ? C : W);
      }
    }
    if (corrShell && !(inHallX && inHallZ)) {
      for (int y = FLOOR - 2; y <= FLOOR + 12; y++) {
        bool inside = corrIn && y > FLOOR && y <= FLOOR + 10;
        set(y, inside ? AIR : (y == FLOOR && corrIn && hf(7) < 0.15 ? G : hashFloat(wx, y, wz, seed_) < 0.04 ? C : W));
      }
    }
  }
  b.store(ch);
}
void RealmGen::sample(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample();
  const int hi = floordiv(wx + 2, HALL);
  if (hi < 0 || hi >= HALLS || wz < -50 || wz > 49) { s.height = -1; return; }
  s.height = CEIL + 1; s.top = s.under = B.rift_wall; s.floatBottom = FLOOR - 2;
}

// ---- registration -----------------------------------------------------------------------------------------
void registerDefaultWorldGen() {
  WorldGen::registerType("default", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    switch (d) {
      case DIM_OVERWORLD: return std::make_unique<OverworldGen>(o);
      case DIM_NETHER: return std::make_unique<DepthsGen>(o);
      case DIM_AETHER: return std::make_unique<SkylandsGen>(o);
      case DIM_END: return std::make_unique<EndGen>(o);
      default: return std::make_unique<VoidGen>();
    }
  });
  WorldGen::registerType("pocket", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    if (d == DIM_OVERWORLD) return std::make_unique<PocketGen>(o);
    return std::make_unique<VoidGen>();
  });
  WorldGen::registerType("realm", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    if (d == DIM_OVERWORLD) return std::make_unique<RealmGen>(o);
    return std::make_unique<VoidGen>();
  });
}

} // namespace dq
