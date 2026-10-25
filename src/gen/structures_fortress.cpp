// gen/structures_fortress.cpp — Nether fortresses laid out like Minecraft's: a web of nether-brick bridges on arched
// pillars reaching down to the lava sea, crossings, bridge stairs, a castle side of roofed corridors with fence
// windows, blaze (cinder wisp) cages on raised platforms, nether wart growing in a walled soul-sand court, and loot
// chests. The layout is a small grid graph decided once per plan (deterministic) and written chunk by chunk.
// Placement and registration stay in structures_far.cpp (the original's index 20, same region and seeds).
#include "gen/structures_common.h"
#include <bit>

namespace dq {

namespace {
enum FKind : u8 { F_CROSS, F_BLAZE, F_WART, F_ROOM, F_END };
struct FNode { int gx, gz; u8 kind; u8 links; bool castle; };
struct FEdge { int a, b, dir; bool corridor, stairs; };
struct FortExt { std::vector<FNode> nodes; std::vector<FEdge> edges; };
constexpr int G = 22;   // grid spacing (node centre to node centre)
int halfOf(u8 k) { return k == F_WART ? 7 : k == F_END ? 2 : 4; }
}

bool fortressPlanMC(const TerrainQuery&, int x, int z, Mulberry32& rnd, StructurePlan& S) {
  const int y = DepthsGen::LAVA_Y + 46 + jsFloor(rnd() * 30);   // the middle of the band, high over the lava sea
  auto E = std::make_shared<FortExt>();
  auto& N = E->nodes; auto& Ed = E->edges;
  const int castleDir = jsFloor(rnd() * 4);
  auto castleAt = [&](int gx, int gz) { return gx * DIRX[castleDir] + gz * DIRZ[castleDir] > 0; };
  auto find = [&](int gx, int gz) { for (size_t i = 0; i < N.size(); i++) if (N[i].gx == gx && N[i].gz == gz) return (int)i; return -1; };
  N.push_back({0, 0, F_CROSS, 0, false});
  std::vector<std::pair<int, int>> queue{{0, 0}};   // node, depth
  for (size_t q = 0; q < queue.size(); q++) {
    const auto [ni, depth] = queue[q];
    const double p = depth == 0 ? 0.9 : depth == 1 ? 0.62 : depth == 2 ? 0.38 : 0.0;
    const int d0 = jsFloor(rnd() * 4);
    for (int k = 0; k < 4; k++) {
      const int d = (d0 + k) & 3;
      if (rnd() >= p) continue;
      const int gx = N[ni].gx + DIRX[d], gz = N[ni].gz + DIRZ[d];
      if (std::abs(gx) > 3 || std::abs(gz) > 3 || find(gx, gz) >= 0) continue;
      const bool castle = castleAt(gx, gz) && castleAt(N[ni].gx, N[ni].gz) ? true : castleAt(gx, gz);
      N.push_back({gx, gz, F_CROSS, 0, castle});
      const int nj = (int)N.size() - 1;
      N[ni].links |= 1 << d; N[nj].links |= 1 << ((d + 2) & 3);
      Ed.push_back({ni, nj, d, castle, !castle && rnd() < 0.35});
      queue.push_back({nj, depth + 1});
    }
  }
  if (N.size() < 3) return false;   // a lone crossing isn't a fortress: try the next region
  // the ends of the web: a blaze platform and a wart court for sure, then rooms and lookouts
  bool blaze = false, wart = false;
  for (size_t i = 1; i < N.size(); i++) {
    FNode& n = N[i];
    if (std::popcount((unsigned)n.links) != 1) continue;
    if (!wart && n.castle) { n.kind = F_WART; wart = true; }
    else if (!blaze) { n.kind = F_BLAZE; blaze = true; }
    else { const double r = rnd(); n.kind = r < 0.3 ? F_BLAZE : r < 0.65 ? F_ROOM : F_END; }
  }
  if (!wart) for (size_t i = N.size() - 1; i > 0 && !wart; i--) if (N[i].kind == F_CROSS) { N[i].kind = F_WART; wart = true; }
  if (!blaze) for (size_t i = N.size() - 1; i > 0 && !blaze; i--) if (N[i].kind == F_CROSS) { N[i].kind = F_BLAZE; blaze = true; }
  int x0 = x, x1 = x, z0 = z, z1 = z;
  json spawners = json::array();
  for (const FNode& n : N) {
    const int cx = x + n.gx * G, cz = z + n.gz * G, h = halfOf(n.kind) + 2;
    x0 = std::min(x0, cx - h); x1 = std::max(x1, cx + h); z0 = std::min(z0, cz - h); z1 = std::max(z1, cz + h);
    if (n.kind == F_BLAZE) spawners.push_back(mobAt(cx, y + 2, cz, "cinder_wisp"));
    if (n.kind == F_ROOM && ((cx ^ cz) & 1)) spawners.push_back(mobAt(cx, y + 1, cz, "wither_skeleton"));
  }
  S.x0 = x0; S.x1 = x1; S.z0 = z0; S.z1 = z1; S.y = y; S.hashDy = 34 - y; S.cx = x; S.cz = z; S.name = "a Nether Fortress";
  S.data = {{"spawners", spawners}};
  S.ext = E;
  return true;
}

void fortressBuildMC(ChunkWriter& w, const StructurePlan& S) {
  SP P(w);
  const FortExt& E = extOf<FortExt>(S);
  const int y = S.y;
  const BlockId NB = bid(PLK("nether_bricks", "blackstone")), FENCE = bid(PLK("nether_brick_fence", "nether_bricks")),
                STAIR = bid(PLK("nether_brick_stairs", "nether_bricks")), SOUL = bid(PLK("soul_sand", "soul_soil")), WART = bid("nether_wart");
  auto touches = [&](int ax, int az, int bx2, int bz2) { return !(bx2 < P.bx || ax > P.bx + 15 || bz2 < P.bz || az > P.bz + 15); };
  // a support from just under the deck down to the ground (through lava) — Minecraft's fortress pillars
  auto pillar = [&](int x, int z, int top) {
    if (!P.in(x, z)) return;
    for (int yy = top; yy > 5; yy--) {
      const BlockId cur = P.get(x, yy, z);
      if (yy < top && cur != NOBLOCK && tSolid(cur) && !tLiquid(cur)) break;
      P.putId(x, yy, z, NB);
    }
  };
  auto railing = [&](int x, int yy, int z) { P.putId(x, yy + 1, z, NB); P.putId(x, yy + 2, z, FENCE); for (int h = 3; h <= 5; h++) P.put(x, yy + h, z, "air"); };
  auto center = [&](const FNode& n, int& cx, int& cz) { cx = S.cx + n.gx * G; cz = S.cz + n.gz * G; };

  // ---- bridges and corridors between nodes ----
  for (const FEdge& e : E.edges) {
    const FNode &A = E.nodes[e.a], &Bn = E.nodes[e.b];
    int ax, az, bx2, bz2; center(A, ax, az); center(Bn, bx2, bz2);
    if (!touches(std::min(ax, bx2) - 3, std::min(az, bz2) - 3, std::max(ax, bx2) + 3, std::max(az, bz2) + 3)) continue;
    const int dx = DIRX[e.dir], dz = DIRZ[e.dir], px = -dz, pz = dx;
    const int s0 = halfOf(A.kind) + 1, s1 = G - halfOf(Bn.kind) - 1, len = s1 - s0;
    auto hump = [&](int d) { if (!e.stairs || len < 12) return 0; return std::clamp(std::min(d - s0 - 2, s1 - 2 - d), 0, 3); };
    for (int d = s0; d <= s1; d++) {
      const int h = hump(d), deck = y + h;
      for (int k = -2; k <= 2; k++) {
        const int x = ax + dx * d + px * k, z = az + dz * d + pz * k;
        if (!P.in(x, z)) continue;
        for (int yy = y; yy <= deck; yy++) P.putId(x, yy, z, NB);
        P.putId(x, y - 1, z, NB);
        const int ph = (d - s0) % 10;
        if (!e.corridor) {
          if (ph == 1 || ph == 2 || ph == 8 || ph == 9) P.putId(x, y - 2, z, NB);
          if (ph == 1 || ph == 9) P.putId(x, y - 3, z, NB);
          if (ph == 0 && d > s0 && d < s1) pillar(x, z, y - 2);
          if (std::abs(k) == 2) railing(x, deck, z);
          else {
            for (int hh = 1; hh <= 5; hh++) P.put(x, deck + hh, z, "air");
            // bridge stairs: a step up where the deck rises, down where it falls
            if (h < hump(d + 1)) P.putId(x, deck + 1, z, STAIR, (u8)e.dir);
            else if (h < hump(d - 1)) P.putId(x, deck + 1, z, STAIR, (u8)((e.dir + 2) & 3));
          }
        } else {
          // the castle: a roofed corridor with fence windows, legs down to the ground
          if (std::abs(k) == 2) {
            for (int hh = 1; hh <= 4; hh++) P.putId(x, y + hh, z, (hh == 2 || hh == 3) && d % 3 == 1 ? FENCE : NB);
            if (ph == 0 && d > s0 && d < s1) pillar(x, z, y - 2);
          } else for (int hh = 1; hh <= 4; hh++) P.put(x, y + hh, z, "air");
          P.putId(x, y + 5, z, NB);
        }
      }
    }
  }

  // ---- the nodes ----
  for (const FNode& n : E.nodes) {
    int cx, cz; center(n, cx, cz);
    const int H = halfOf(n.kind);
    if (!touches(cx - H - 1, cz - H - 1, cx + H + 1, cz + H + 1)) continue;
    auto open = [&](int x, int z) {   // in a doorway toward a linked neighbour?
      for (int d = 0; d < 4; d++) if (n.links & (1 << d)) {
        const int along = (x - cx) * DIRX[d] + (z - cz) * DIRZ[d], across = (x - cx) * -DIRZ[d] + (z - cz) * DIRX[d];
        if (along == H && std::abs(across) <= 1) return true;
      }
      return false;
    };
    const bool enclosed = n.castle && n.kind != F_BLAZE && n.kind != F_END;
    const int wallH = n.kind == F_WART ? 6 : 5;
    for (int z = cz - H; z <= cz + H; z++) for (int x = cx - H; x <= cx + H; x++) {
      if (!P.in(x, z)) continue;
      const bool edge = std::abs(x - cx) == H || std::abs(z - cz) == H;
      P.putId(x, y, z, NB); P.putId(x, y - 1, z, NB);
      if (!edge) { for (int h = 1; h <= wallH; h++) P.put(x, y + h, z, "air"); if (enclosed && n.kind != F_WART) P.putId(x, y + wallH + 1, z, NB); continue; }
      if (open(x, z)) { for (int h = 1; h <= 4; h++) P.put(x, y + h, z, "air"); if (enclosed) { P.putId(x, y + 5, z, NB); if (n.kind != F_WART) P.putId(x, y + wallH + 1, z, NB); } continue; }
      if (enclosed) {
        for (int h = 1; h <= wallH; h++) P.putId(x, y + h, z, (h == 2 || h == 3) && ((x + z) % 3 == 0) ? FENCE : NB);
        if (n.kind != F_WART) P.putId(x, y + wallH + 1, z, NB);
      } else railing(x, y, z);
    }
    // legs at the corners, and a great one under a crossing's middle
    for (int sx : {-1, 1}) for (int sz : {-1, 1}) for (int a = 0; a <= 1; a++) for (int c = 0; c <= 1; c++)
      pillar(cx + sx * (H - a), cz + sz * (H - c), y - 2);
    if (n.kind == F_CROSS && !n.castle) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) pillar(cx + dx, cz + dz, y - 2);
    switch (n.kind) {
      case F_BLAZE: {
        // a raised dais: stairs all round, the cage on top
        for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
          const int x = cx + dx, z = cz + dz;
          if (!P.in(x, z)) continue;
          if (std::abs(dx) <= 1 && std::abs(dz) <= 1) { P.putId(x, y + 1, z, NB); continue; }
          int f = std::abs(dx) > std::abs(dz) ? (dx > 0 ? 3 : 1) : (dz > 0 ? 0 : 2);   // rises toward the middle
          P.putId(x, y + 1, z, STAIR, (u8)f);
        }
        if (P.in(cx, cz)) P.spawner(cx, y + 2, cz, "cinder_wisp");
        break;
      }
      case F_WART: {
        // soul sand beds along two walls with wart growing, a chest at the far end
        for (int bed : {-1, 1}) for (int r = 4; r <= 5; r++) for (int a = -5; a <= 5; a++) {
          const bool alongX = (n.links & 5) != 0;   // linked north/south: beds run east-west
          const int x = alongX ? cx + a : cx + bed * r, z = alongX ? cz + bed * r : cz + a;
          if (!P.in(x, z)) continue;
          P.putId(x, y, z, SOUL);
          if (WART != NOBLOCK) P.putId(x, y + 1, z, WART, (u8)std::min(3, jsFloor(P.h(x, 0, z, 9) * 5)));
        }
        int back = 0; for (int d = 0; d < 4; d++) if (n.links & (1 << d)) back = (d + 2) & 3;
        const int chx = cx + DIRX[back] * (H - 1), chz = cz + DIRZ[back] * (H - 1);
        if (P.in(chx, chz)) P.chest(chx, y + 1, chz, (back + 2) & 3, "nether_fortress");
        break;
      }
      case F_ROOM: {
        int back = 0; for (int d = 0; d < 4; d++) if (n.links & (1 << d)) back = (d + 2) & 3;
        const int bxx = cx + DIRX[back] * (H - 1), bzz = cz + DIRZ[back] * (H - 1);
        const int sx = -DIRZ[back], sz = DIRX[back];
        if (P.in(bxx + sx, bzz + sz)) P.chest(bxx + sx, y + 1, bzz + sz, (back + 2) & 3, "nether_fortress");
        if (P.in(bxx - sx, bzz - sz)) P.chest(bxx - sx, y + 1, bzz - sz, (back + 2) & 3, "nether_fortress");
        if (((cx ^ cz) & 1) && P.in(cx, cz)) P.spawner(cx, y + 1, cz, "wither_skeleton");
        break;
      }
      case F_END: {
        // a lookout where the bridge stops: a chest now and then
        if (P.h(cx, 1, cz, 4) < 0.5 && P.in(cx, cz)) P.chest(cx, y + 1, cz, 0, "nether_fortress");
        break;
      }
      default: break;
    }
  }
}

} // namespace dq
