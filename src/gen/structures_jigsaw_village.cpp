// gen/structures_jigsaw_village.cpp — our villages from terrain version 9: our village pieces (gen/village_pieces.h; built by
// tools/port/village_builder.py) put together the way
// Minecraft's jigsaw does it: a town centre, streets growing out of its street joints, houses on the streets' building
// joints, each piece joined face to face and turned to fit, none overlapping, up to a depth and a distance from the
// centre. Streets follow the land; everything else stands level at the height of the street joint it hangs off, the
// ground levelled under it (villageflat's blend, piece by piece). Our own hall (the elder, the inn, the quest board's
// keepers) and windmill (the miller's millstone, sails on a real axle) join the streets like any other house, so the
// living village (sim/villager*.cpp) finds what it needs: the plan's buildings, beds, jobs and workplaces come from the
// pieces themselves (their beds, job blocks, doors and pens).
#include "gen/town_mine.h"
#include <cstdlib>
#include <cstring>
#include "gen/structures_town.h"
#include "gen/village_pieces.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>

namespace dq {

static const int* DXJ = DIRX;
static const int* DZJ = DIRZ;

struct JPlaced {
  const VPPiece* p = nullptr; int r = 0, x0 = 0, y0 = 0, z0 = 0, w = 0, d = 0, depth = 0, bld = -1;
  int parent = -1, jx = 0, jz = 0, dy = 0;   // what it hangs off: the parent's joint column, and its y0 from the joint's height
  int x1() const { return x0 + w - 1; }
  int z1() const { return z0 + d - 1; }
};
struct JigsawTown {
  std::vector<JPlaced> pcs;
  std::vector<std::array<int, 5>> flat;       // x0, z0, x1, z1, y: level ground under the rigid pieces
  std::array<int, 4> fb{};                    // the levelling's reach
  std::vector<std::array<int, 3>> ends;       // street ends, for the roads between villages: x, z, direction
  bool board = false; int bx = 0, bz = 0, by = 0, bf = 0;   // the quest board (built here when the centre has none)
  std::vector<VPPiece> ours;                  // our hall and windmill as pieces (kept alive with the plan)
  std::map<std::string, int> trades;          // the kinds of building placed so far ("house/baker", "farm/farmer"...) and how many
  std::vector<std::array<int, 2>> paths;      // a walled town's ways out: from a street through its gate and on
  bool walled = false; std::array<int, 4> wall{};   // a town's or city's wall (trees are kept off all inside it)
  std::unordered_set<long long> street;          // every street block's column (and the ways out): its climbs get stairs
  std::unordered_map<long long, int> streetH;    // each street column's level: smoothed, never a block more than its neighbour's
  std::vector<std::array<int, 5>> boxes;         // the rigid pieces' own boxes and levels (the streets give way only to these)
  bool hasMine = false; std::array<int, 4> mineBox{};   // the town mine's workings under the town (gen/town_mine.h): built wherever they reach
};
static constexpr int JFLAT_R = 8;
static long long colKey(int x, int z) { return (long long)(u32)x | ((long long)(u32)z << 32); }

// our buildings as pieces: a W x (D + 1) box (the row in front is the porch), its entrance joint in the porch row's
// middle facing north, one up from the floor like a pack house's
static VPPiece ourPiece(const std::string& type) {
  VPPiece p; int W = 7, D = 7, H = 10; townTemplateSize(type, W, D, H);
  p.name = "dq_" + type; p.kind = "houses"; p.sx = W; p.sz = D + 1; p.sy = H; p.top = H;
  VPJoint j; j.x = (W - 1) / 2; j.y = 1; j.z = 0; j.front = 0; j.name = "minecraft:building_entrance"; j.target = "minecraft:building_entrance"; j.pool = "minecraft:empty";
  p.joints.push_back(j); p.animals = type;   // (animals carries the building type for our own pieces)
  return p;
}
static bool isOurs(const VPPiece& p) { return p.name.rfind("dq_", 0) == 0; }

// a block as a place of work, for the trades' rounds (data/villagers.json jobs.*.tasks say what's done at each kind)
static std::string stationKind(const BlockDef& d) {
  const std::string& k = d.key;
  auto ends = [&](const char* s) { const size_t n = std::strlen(s); return k.size() >= n && k.compare(k.size() - n, n, s) == 0; };
  if (k == "anvil" || k == "chipped_anvil" || k == "damaged_anvil") return "anvil";
  if (k == "furnace" || k == "blast_furnace") return "forge";
  if (k == "smoker") return "smoker";
  if (k == "cauldron" || k == "water_cauldron") return "tub";
  if (k == "armor_stand") return "stand";
  if (k.find("weapon_rack") != std::string::npos) return "rack";
  if (k == "grindstone") return "grindstone";
  if (k == "smithing_table") return "smithing";
  if (k == "lectern") return "lectern";
  if (k == "bookshelf" || ends("_bookcase")) return "books";
  if (k == "brewing_stand") return "brew";
  if (k == "stonecutter") return "cutter";
  if (k == "loom") return "loom";
  if (k == "fletching_table") return "fletching";
  if (k == "cartography_table") return "maps";
  if (k == "barrel" || k == "chest" || ends("_cabinet")) return "stores";
  if (ends("_shelf")) return "shelf";
  if (d.has("furniture") && ends("_table")) return "table";
  return "";
}
static const std::vector<std::string>* tradeRound(const std::string& job) {
  static const std::map<std::string, std::vector<std::string>> R = {
    {"armorer", {"forge", "anvil", "tub", "stand"}}, {"weaponsmith", {"forge", "anvil", "grindstone", "tub", "rack"}},
    {"toolsmith", {"forge", "anvil", "smithing", "grindstone"}}, {"butcher", {"smoker", "table", "stores"}},
    {"baker", {"smoker", "forge", "table", "stores"}}, {"fletcher", {"fletching", "table", "stores"}},
    {"librarian", {"lectern", "books", "table"}}, {"cartographer", {"maps", "books", "lectern"}},
    {"cleric", {"brew", "tub", "books", "lectern"}}, {"mason", {"cutter", "forge", "table"}}, {"shepherd", {"loom", "stores"}},
    {"jeweller", {"grindstone", "smithing", "table"}}, {"merchant", {"stores", "shelf", "table"}},
    {"fisherman", {"stores", "tub"}}, {"innkeeper", {"stores", "table", "shelf"}}};
  auto it = R.find(job); return it == R.end() ? nullptr : &it->second;
}

// what a pack piece is to the village: building type and job, from its name
static void pieceRole(const VPPiece& p, std::string& type, std::string& job) {
  const std::string& n = p.name;
  auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
  type = "house"; job.clear();
  if (p.kind == "town_centers" || has("meeting_point")) { type = "square"; return; }
  if (has("farm")) { type = "farm"; job = "farmer"; return; }
  if (has("animal_pen") || !p.animals.empty()) { type = "pen"; return; }
  if (has("stable")) { type = "stable"; return; }
  if (has("temple")) { type = "chapel"; job = "cleric"; return; }
  if (has("market_stall")) {   // a trader's stall: its wares are in its name (market_stall_<job>_N)
    type = "stall"; job = "merchant";
    for (const char* j : {"baker", "butcher", "fisherman", "farmer", "jeweller", "merchant", "shepherd"}) if (has(j)) { job = j; break; }
    return;
  }
  if (has("windmill")) { type = "house"; return; }   // (the millstone in it makes it the windmill)
  if (has("barracks") || has("watchtower")) { type = "barracks"; job = "guard"; return; }
  static const std::vector<std::pair<const char*, const char*>> J = {
    {"armorer", "armorer"}, {"weaponsmith", "weaponsmith"}, {"weapon_smith", "weaponsmith"}, {"tool_smith", "toolsmith"}, {"toolsmith", "toolsmith"}, {"butcher", "butcher"},
    {"fletcher", "fletcher"}, {"shepherd", "shepherd"}, {"mason", "mason"}, {"cartographer", "cartographer"}, {"library", "librarian"},
    {"fisher", "fisherman"}, {"tannery", "merchant"}, {"jeweller", "jeweller"}, {"bakery", "baker"}, {"tavern", "innkeeper"},
    {"sawmill", "lumberjack"}, {"builders_yard", "builder"}, {"alchemist", "cleric"}, {"general_store", "merchant"}};
  for (auto& [k, j] : J) if (has(k)) { job = j; return; }
}

// the quarter of the town a building belongs in: round the square the market and the civic buildings (stalls, the baker,
// the tavern, the jeweller, the alchemist, the general store, the butcher; the temple, the library); on one side the
// craft quarter (the smiths, the mason, the fletcher, the tannery, the cartographer, the builder); out at the edge the
// farms, pens, stable, windmill, sawmill, the shepherd's fold, the fisher's and the guards' barracks; homes between
enum { Q_HOME, Q_MARKET, Q_CIVIC, Q_CRAFT, Q_EDGE };
static int quarterOf(const VPPiece& p) {
  std::string type, job; pieceRole(p, type, job);
  const std::string& n = p.name;
  auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
  if (type == "stall" || has("bakery") || has("tavern") || has("jeweller") || has("alchemist") || has("general_store") || has("butcher")) return Q_MARKET;
  if (type == "chapel" || has("library")) return Q_CIVIC;
  if (type == "farm" || type == "pen" || type == "stable" || has("windmill") || has("sawmill") || has("shepherd") || has("fisher") || type == "barracks") return Q_EDGE;
  if (!job.empty()) return Q_CRAFT;
  return Q_HOME;
}

bool jigsawTownPlan(const TerrainQuery& t, const VillagePieces& pack, Town& v, Mulberry32& rnd, std::shared_ptr<JigsawTown>& out) {
  const VStyle& st = vstyle(v.style);
  const std::string sk = st.key;
  const std::vector<VPPiece>* centers = pack.pool(sk + "/town_centers");
  if (!centers) return false;
  auto J = std::make_shared<JigsawTown>();
  J->ours.reserve(3);
  J->ours.push_back(ourPiece("hall"));
  // (no windmill of ours here: each style's pieces have their own, a tower with sails, shaft and millstone)
  J->ours.push_back(ourPiece("mine"));   // the miner's: a drift down to a working face (structures_townbld.cpp bMine)
  std::vector<const VPPiece*> oursLeft; for (auto& p : J->ours) oursLeft.push_back(&p);
  const int maxDepth = v.tier == TIER_CITY ? 8 : v.tier == TIER_TOWN ? 7 : 6, R = v.tier == TIER_CITY ? 112 : v.tier == TIER_TOWN ? 96 : 80;
  auto top = [&](int x, int z) { return t.terrainColumn(x, z).y; };
  // water over it: the land below the sea (plan 11; the original's height at sea level was a test too coarse for the taller
  // band: dry shore at the sea's level and a block above counted, the streets there had no level of their own, and a
  // house's levelled ground beside one raised a ridge across it with the street sunk in a trench)
  auto wet = [&](int x, int z) { const TerrainColumn c = t.terrainColumn(x, z); return v.planV >= 11 ? c.water >= 0 : c.jsY <= JS_SEA; };
  auto overlaps = [&](int x0, int z0, int x1, int z1) {
    for (auto& q : J->pcs) if (x0 <= q.x1() && x1 >= q.x0 && z0 <= q.z1() && z1 >= q.z0) return true;
    return false;
  };
  auto inside = [&](int x, int z) { return overlaps(x, z, x, z); };
  auto pickWeighted = [&](const std::vector<VPPiece>& list) {
    std::vector<const VPPiece*> order; std::vector<double> keys;
    for (auto& p : list) { order.push_back(&p); keys.push_back(std::pow(rnd(), 1.0 / std::max(1, p.weight))); }   // weighted shuffle
    std::vector<int> idx(order.size()); for (int i = 0; i < (int)idx.size(); i++) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return keys[a] > keys[b]; });
    std::vector<const VPPiece*> o; for (int i : idx) o.push_back(order[i]);
    return o;
  };
  // the centre
  {
    const VPPiece* c = pickWeighted(*centers)[0];
    JPlaced q; q.p = c; q.r = jsFloor(rnd() * 4); q.w = q.r & 1 ? c->sz : c->sx; q.d = q.r & 1 ? c->sx : c->sz;
    q.x0 = v.x - q.w / 2; q.z0 = v.z - q.d / 2; q.y0 = v.y;
    J->pcs.push_back(q);
  }
  for (size_t qi = 0; qi < J->pcs.size(); qi++) {
    const JPlaced Q = J->pcs[qi];
    std::vector<int> js; for (int i = 0; i < (int)Q.p->joints.size(); i++) js.push_back(i);
    for (int i = (int)js.size() - 1; i > 0; i--) std::swap(js[i], js[jsFloor(rnd() * (i + 1))]);
    for (int ji : js) {
      const VPJoint& A = Q.p->joints[ji];
      if (A.front > 3 || A.pool.empty() || A.pool == "minecraft:empty") continue;
      auto [tx, tz] = vpTurn(*Q.p, Q.r, A.x, A.z);
      const int ax = Q.x0 + tx, az = Q.z0 + tz, af = (A.front + Q.r) & 3;
      const int ay = Q.p->terrain ? top(ax, az) + A.y : Q.y0 + A.y;
      const int Tx = ax + DXJ[af], Tz = az + DZJ[af];
      if (inside(Tx, Tz)) continue;
      const bool streets = A.pool.find("/streets") != std::string::npos, houses = A.pool.find("/houses") != std::string::npos;
      std::vector<const VPPiece*> cands;
      if (Q.depth + 1 <= maxDepth) {
        if (const auto* pl = pack.pool(A.pool)) cands = pickWeighted(*pl);
        if (houses) {
          // a trade's building once in a village (twice in a town or a city; farms and pens as the land allows), a stall
          // of each kind once, and the stalls only near the square (a market street, not stalls up every lane)
          auto tradeOf = [](const VPPiece* p) { std::string t, j; pieceRole(*p, t, j); return t == "house" && j.empty() ? std::string() : t + "/" + j; };
          const int cap = v.tier == TIER_VILLAGE ? 1 : 2;
          cands.erase(std::remove_if(cands.begin(), cands.end(), [&](const VPPiece* p) {
            const std::string t = tradeOf(p);
            if (t.empty() || t.rfind("farm", 0) == 0 || t.rfind("pen", 0) == 0) return false;
            const bool stall = t.rfind("stall", 0) == 0;
            if (stall && Q.depth > 2) return true;
            auto it = J->trades.find(t); return it != J->trades.end() && it->second >= (stall ? 1 : cap);
          }), cands.end());
          // the town's quarters: by how far out the spot is and which way (the craft quarter one side of the square)
          {
            const double rr = std::max(std::abs(Tx - v.x), std::abs(Tz - v.z)) / (double)R;
            const double ang = std::atan2((double)(Tz - v.z), (double)(Tx - v.x)), craftAng = (hash32((i32)v.seed, 7, 7, 7) % 360) * 3.14159265 / 180;
            double da = std::fabs(ang - craftAng); if (da > 3.14159265) da = 2 * 3.14159265 - da;
            const int zone = rr < 0.28 ? 0 : rr > 0.66 ? 2 : da < 1.4 ? 1 : 3;   // 0 the centre, 1 the craft side (a wide one), 2 the edge, 3 the homes
            auto fits = [&](const VPPiece* p) {
              const int q = quarterOf(*p);
              switch (zone) {
                case 0: return q == Q_MARKET || q == Q_CIVIC;
                case 1: return q == Q_CRAFT || q == Q_HOME;
                case 2: return q == Q_EDGE || q == Q_HOME;
                default: return q == Q_HOME || q == Q_CIVIC;
              }
            };
            std::vector<const VPPiece*> keep; for (const VPPiece* p : cands) if (fits(p)) keep.push_back(p);
            if (keep.empty()) for (const VPPiece* p : cands) if (quarterOf(*p) == Q_HOME) keep.push_back(p);   // (that quarter's full: homes)
            cands.swap(keep);
          }
          // the trades it hasn't got come first (most of the time: a plain house now and then between them)
          if (rnd() < 0.7) std::stable_partition(cands.begin(), cands.end(), [&](const VPPiece* p) { const std::string t = tradeOf(p); return !t.empty() && !J->trades.count(t) && t.rfind("stall", 0) != 0; });
        }
        if (houses) {
          if (!oursLeft.empty() && Q.depth >= 1) cands.insert(cands.begin(), oursLeft.front());   // our hall first, then the windmill
          else if (rnd() < (v.tier == TIER_CITY ? 0.0 : v.tier == TIER_TOWN ? 0.05 : 0.12)) continue;   // the pools' "empty": a gap between the houses now and then (towns and cities fill up)
        }
      }
      if (streets) if (const auto* tl = pack.pool(sk + "/terminators")) for (auto* p : pickWeighted(*tl)) cands.push_back(p);
      bool placed = false;
      for (const VPPiece* C : cands) {
        int rs[4] = {0, 1, 2, 3}; for (int i = 3; i > 0; i--) std::swap(rs[i], rs[jsFloor(rnd() * (i + 1))]);
        for (int r : rs) {
          for (const VPJoint& B : C->joints) {
            if (B.front > 3 || B.name != A.target || ((B.front + r) & 3) != ((af + 2) & 3)) continue;
            auto [cx2, cz2] = vpTurn(*C, r, B.x, B.z);
            JPlaced q; q.p = C; q.r = r; q.w = r & 1 ? C->sz : C->sx; q.d = r & 1 ? C->sx : C->sz;
            q.x0 = Tx - cx2; q.z0 = Tz - cz2; q.depth = Q.depth + 1;
            if (std::max({std::abs(q.x0 - v.x), std::abs(q.x1() - v.x), std::abs(q.z0 - v.z), std::abs(q.z1() - v.z)}) > R) continue;
            if (overlaps(q.x0, q.z0, q.x1(), q.z1())) continue;
            const int climb = v.tier == TIER_VILLAGE ? 8 : v.tier == TIER_TOWN ? 10 : 12;
            if (C->terrain) {   // a street may bridge a stream, not run out across a lake
              // and not steep along itself: a street climbs or falls a block in four at most (sampled every few blocks along
              // it; it had run down a ravine to a stream in a stair of steps, eight above the square to seven below)
              bool high = false; int lo = 1 << 30, hi = -(1 << 30);
              for (int sx = 0; sx <= 4 && !high; sx++) for (int sz = 0; sz <= 4; sz++) {
                const int px = q.x0 + (q.w - 1) * sx / 4, pz = q.z0 + (q.d - 1) * sz / 4, h = top(px, pz);
                if (std::abs(h - v.y) > climb) { high = true; break; }
                if (!wet(px, pz)) { lo = std::min(lo, h); hi = std::max(hi, h); }
              }
              if (!high && hi > lo && hi - lo > std::max(2, std::max(q.w, q.d) / 4)) high = true;
              if (!high && std::abs(top(Tx, Tz) - (Q.p->terrain ? top(ax, az) : Q.y0 + A.y - 1)) > 2) high = true;   // (and meets its neighbour near its level)
              if (high) continue;
              int wetN = 0;
              for (int sx = 0; sx <= 2; sx++) for (int sz = 0; sz <= 2; sz++) if (wet(q.x0 + (q.w - 1) * sx / 2, q.z0 + (q.d - 1) * sz / 2)) wetN++;
              if (wetN > 2) continue;
            } else {
              q.y0 = ay - B.y;
              if (std::abs(q.y0 - v.y) > climb) continue;
              // not out over water, nor hung off a slope it would have to stand far above or below
              // (the whole footprint, every few blocks: a lake under one side of it got through when only the middle and
              // the corners were looked at, and the farm stood half in it)
              bool steep = q.y0 < SEA_LEVEL;   // (below the sea: its levelled ground would be a pond)
              for (int pz = q.z0; pz <= q.z1() + 2 && !steep; pz += 3) for (int px = q.x0; px <= q.x1() + 2 && !steep; px += 3) {
                const int sx = std::min(px, q.x1()), sz = std::min(pz, q.z1());
                if (wet(sx, sz)) steep = true;
              }
              for (auto [px, pz] : {std::pair{q.x0, q.z0}, {q.x1(), q.z0}, {q.x0, q.z1()}, {q.x1(), q.z1()}}) if (!steep && std::abs(top(px, pz) - q.y0) > 7) steep = true;
              if (steep) continue;
            }
            q.parent = (int)qi; q.jx = ax; q.jz = az; q.dy = A.y - B.y;
            J->pcs.push_back(q);
            { std::string t, j; pieceRole(*C, t, j); if (!(t == "house" && j.empty())) J->trades[t + "/" + j]++; }
            if (isOurs(*C)) oursLeft.erase(std::find(oursLeft.begin(), oursLeft.end(), C));
            placed = true; break;
          }
          if (placed) break;
        }
        if (placed) break;
      }
      if (!placed && streets) J->ends.push_back({Tx, Tz, af});
    }
  }
  std::sort(J->ends.begin(), J->ends.end(), [&](const std::array<int, 3>& a, const std::array<int, 3>& b) {
    return std::hypot(a[0] - v.x, a[1] - v.z) > std::hypot(b[0] - v.x, b[1] - v.z); });   // the farthest first: where roads leave
  int nb = 0; for (auto& q : J->pcs) if (!q.p->terrain && q.p->kind != "town_centers") nb++;
  if (nb < 3) return false;
  // ---- the streets' own level ----
  // Each street column at the land's height, smoothed, then eased until no column is more than a block off a
  // neighbour's (each such step gets stairs when built), the columns round the town centre held at its level. The
  // street was the land as it lay before: a house's levelled ground beside it, or a steep bit, left walls three blocks
  // high across it. Over water it stays a bridge (no level).
  {
    std::unordered_map<long long, int> nat;
    for (auto& q : J->pcs) {
      if (!q.p->terrain) continue;
      for (const VPBlock& b : q.p->blocks) {
        if (b.y != 0) continue;
        auto [tx, tz] = vpTurn(*q.p, q.r, b.x, b.z);
        const int x = q.x0 + tx, z = q.z0 + tz;
        if (!wet(x, z)) nat[colKey(x, z)] = top(x, z);
      }
    }
    const JPlaced& C0 = J->pcs[0];
    std::unordered_set<long long> pinned;
    for (auto& [k, h] : nat) {
      const int x = (int)(u32)(k & 0xFFFFFFFF), z = (int)(u32)(k >> 32);
      if (x >= C0.x0 - 1 && x <= C0.x1() + 1 && z >= C0.z0 - 1 && z <= C0.z1() + 1) pinned.insert(k);
    }
    std::unordered_map<long long, int>& T = J->streetH;
    for (auto& [k, h] : nat) {   // smoothed over the street within 2
      const int x = (int)(u32)(k & 0xFFFFFFFF), z = (int)(u32)(k >> 32);
      if (pinned.count(k)) { T[k] = v.y; continue; }
      int sum = 0, n = 0;
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) { auto it = nat.find(colKey(x + dx, z + dz)); if (it != nat.end()) { sum += it->second; n++; } }
      T[k] = (int)std::lround((double)sum / n);
    }
    // no further from the square's level than the walk to it (so the streets can always meet it a block a step)
    std::unordered_map<long long, int> dist;
    {
      std::vector<long long> q(pinned.begin(), pinned.end());
      for (long long k : q) dist[k] = 0;
      for (size_t h = 0; h < q.size(); h++) {
        const long long k = q[h]; const int x = (int)(u32)(k & 0xFFFFFFFF), z = (int)(u32)(k >> 32);
        for (int d = 0; d < 4; d++) { const long long n = colKey(x + DXJ[d], z + DZJ[d]); if (T.count(n) && !dist.count(n)) { dist[n] = dist[k] + 1; q.push_back(n); } }
      }
    }
    auto bound = [&](long long k, int t) { auto d = dist.find(k); return d == dist.end() ? t : std::clamp(t, v.y - d->second, v.y + d->second); };
    for (auto& [k, t] : T) t = bound(k, t);
    for (int it = 0; it < 400; it++) {   // never more than a block from a neighbour (squeezed both ways: up, which settles)
      bool changed = false;
      for (auto& [k, t] : T) {
        if (pinned.count(k)) continue;
        const int x = (int)(u32)(k & 0xFFFFFFFF), z = (int)(u32)(k >> 32);
        int lo = -(1 << 30), hi = 1 << 30;
        for (int d = 0; d < 4; d++) { auto j = T.find(colKey(x + DXJ[d], z + DZJ[d])); if (j != T.end()) { lo = std::max(lo, j->second - 1); hi = std::min(hi, j->second + 1); } }
        int nt = lo > hi ? lo : std::clamp(t, lo, hi);
        nt = bound(k, nt);
        if (nt != t) { t = nt; changed = true; }
      }
      if (!changed) break;
    }
    // the houses stand at their street's level where they join it (a house on a house: its parent's, as before)
    for (size_t i = 1; i < J->pcs.size(); i++) {
      JPlaced& q = J->pcs[i];
      if (q.p->terrain || q.parent < 0) continue;
      const JPlaced& par = J->pcs[q.parent];
      if (par.p->terrain) { auto t = T.find(colKey(q.jx, q.jz)); if (t != T.end()) q.y0 = t->second + q.dy; }   // (the joint stands on the street: y0 = its level + A.y - B.y)
      else q.y0 = par.y0 + q.dy;
    }
  }
  if (std::getenv("DQ_VDEBUG"))   // (every piece of the plan, logged: where a street or a house went)
    for (auto& q : J->pcs) DQ_LOG("vplan %s %d,%d..%d,%d y%d r%d%s", q.p->name.c_str(), q.x0, q.z0, q.x1(), q.z1(), q.y0, q.r, q.p->terrain ? " street" : "");
  // ---- the plan for the villagers ----
  int fx0 = 1 << 30, fz0 = 1 << 30, fx1 = -(1 << 30), fz1 = -(1 << 30);
  // the plain houses' ovens, counters and benches: where a baker, a merchant or a miller with no house of their own works
  struct Station { int bld, kind; VSpot spot; bool indoor; };   // kind 0 an oven (furnace, smoker), 1 a counter (barrel, chest), 2 a bench
  std::vector<Station> stations;
  for (auto& q : J->pcs) {
    const VPPiece& p = *q.p;
    fx0 = std::min(fx0, q.x0); fz0 = std::min(fz0, q.z0); fx1 = std::max(fx1, q.x1()); fz1 = std::max(fz1, q.z1());
    if (!p.terrain) { J->flat.push_back({q.x0 - 1, q.z0 - 1, q.x1() + 1, q.z1() + 1, q.y0}); J->boxes.push_back({q.x0, q.z0, q.x1(), q.z1(), q.y0}); }
    if (p.terrain) continue;
    Bld b; b.seed = hash32((i32)v.seed, q.x0, q.z0, 4243); b.tier = v.tier; b.y = b.yRaw = q.y0; b.r = q.r;
    b.x0 = q.x0; b.z0 = q.z0; b.x1 = q.x1(); b.z1 = q.z1();
    if (isOurs(p)) {
      b.type = p.animals; b.planV = v.planV; townTemplateSize(b.type, b.W, b.D, b.H);
      auto [ox, oz] = vpTurn(p, q.r, 0, 1); b.ox = q.x0 + ox; b.oz = q.z0 + oz;
      // the box without the porch row
      auto c0 = vWorld(b, 0, 0, 0), c1 = vWorld(b, b.W - 1, 0, b.D - 1);
      b.x0 = std::min(c0[0], c1[0]); b.x1 = std::max(c0[0], c1[0]); b.z0 = std::min(c0[2], c1[2]); b.z1 = std::max(c0[2], c1[2]);
      b.roofI = (int)(hash32((i32)b.seed, 3, 1, 7) % st.roofs.size());
      if (b.type == "mine" && v.planV >= 11) {   // its galleries run out under the town, well past its plot
        auto m0 = vWorld(b, townmine::REACH_X0, 0, townmine::REACH_Z0), m1 = vWorld(b, townmine::REACH_X1, 0, townmine::REACH_Z1);
        J->hasMine = true; J->mineBox = {std::min(m0[0], m1[0]), std::min(m0[2], m1[2]), std::max(m0[0], m1[0]), std::max(m0[2], m1[2])};
      }
      Mulberry32 br(b.seed); townBuildingPlan(b, br, st);
      q.bld = (int)v.blds.size();
      for (const VSeat& s : b.seats) v.seats.push_back(s);
      v.blds.push_back(std::move(b));
      continue;
    }
    std::string type, job; pieceRole(p, type, job);
    if (type == "square" && p.kind == "town_centers") {   // the centre: its bell, and the quest board beside it
      for (const VPBlock& k : p.blocks) if (p.states[k.s].kind == VPK_BELL) { auto [x, z] = vpTurn(p, q.r, k.x, k.z); v.bell = {q.x0 + x, q.y0 + k.y, q.z0 + z}; }
      continue;
    }
    b.type = type; b.job = job; b.W = q.w; b.D = q.d; b.H = p.top + 1; b.ox = q.x0; b.oz = q.z0; b.r = 0;
    // its beds (the foot, facing to the head), its door (the one nearest its entrance), where its trade is worked
    auto world = [&](const VPBlock& k) { auto [x, z] = vpTurn(p, q.r, k.x, k.z); return std::array<int, 3>{q.x0 + x, q.y0 + k.y, q.z0 + z}; };
    std::set<long long> solid;
    auto key = [](int x, int y, int z) { return ((long long)x & 0xFFFFF) | (((long long)y & 0xFFF) << 20) | (((long long)z & 0xFFFFF) << 32); };
    std::set<long long> tableAt;   // (furniture: a station on one is worked standing beside it)
    for (const VPBlock& k : p.blocks) {
      const VPState& s = p.states[k.s]; auto w = world(k);
      if (reg().tSolid[s.id]) solid.insert(key(w[0], w[1], w[2]));
      if (reg().block(s.id).has("furniture")) tableAt.insert(key(w[0], w[1], w[2]));
    }
    const VPJoint* ent = nullptr; for (auto& j : p.joints) if (j.front < 4 && j.name.find("building_entrance") != std::string::npos) { ent = &j; break; }
    std::array<int, 3> entW{(q.x0 + q.x1()) / 2, q.y0 + 1, (q.z0 + q.z1()) / 2};
    if (ent) { auto [x, z] = vpTurn(p, q.r, ent->x, ent->z); entW = {q.x0 + x, q.y0 + ent->y, q.z0 + z}; }
    std::array<int, 3> door = entW; int dd = 1 << 30; bool work = false, mill = false;
    // where the trade is worked: its own station first (Minecraft's job block for it, or the smith's anvil), indoors
    // before out in the yard (the weaponsmith stood at the yard's sharpening wheel, Minecraft's grindstone, not his anvil)
    static const std::map<std::string, std::vector<std::string>> PREF = {
      {"armorer", {"blast_furnace", "anvil", "smithing_table"}}, {"weaponsmith", {"anvil", "smithing_table", "grindstone"}},
      {"toolsmith", {"smithing_table", "anvil"}}, {"butcher", {"smoker"}}, {"fletcher", {"fletching_table"}}, {"librarian", {"lectern"}},
      {"cartographer", {"cartography_table"}}, {"cleric", {"brewing_stand"}}, {"mason", {"stonecutter"}}, {"farmer", {"composter"}},
      {"fisherman", {"barrel"}}, {"shepherd", {"loom"}}, {"merchant", {"cauldron", "barrel"}}, {"baker", {"smoker", "furnace"}},
      {"jeweller", {"grindstone", "smithing_table"}}, {"lumberjack", {"stonecutter"}}, {"builder", {"crafting_table", "stonecutter"}}};
    int bestScore = 1 << 30;
    for (const VPBlock& k : p.blocks) {
      const VPState& s = p.states[k.s];
      const auto w = world(k);
      if (s.kind == VPK_BED_FOOT) b.beds.push_back({w[0], w[1], w[2], s.meta[q.r] & 3});
      const BlockDef& d = reg().block(s.id);
      if (d.key == "millstone") mill = true;
      if (d.has("door") && !(s.meta[q.r] & 16)) {   // a way in: open floor both sides of it (not a cupboard's or a closet's)
        const int f = s.meta[q.r] & 3;
        bool through = true;
        for (int sd : {1, -1}) {
          const int cx = w[0] + DXJ[f] * sd, cz = w[2] + DZJ[f] * sd;
          if (solid.count(key(cx, w[1], cz)) || solid.count(key(cx, w[1] + 1, cz))) through = false;
        }
        const int dist = std::abs(w[0] - entW[0]) + std::abs(w[2] - entW[2]) + (through ? 0 : 1000);
        if (dist < dd) { dd = dist; door = w; }
      }
      if (s.kind != VPK_JOB || job.empty()) continue;
      int pref = 9;
      if (auto it = PREF.find(job); it != PREF.end()) for (int i = 0; i < (int)it->second.size(); i++) if (it->second[i] == d.key) { pref = i; break; }
      bool roof = false; for (int up = 2; up <= 6 && !roof; up++) if (solid.count(key(w[0], w[1] + up, w[2]))) roof = true;
      const int score = pref * 2 + (roof ? 0 : 1);
      if (score >= bestScore) continue;
      const int wy = tableAt.count(key(w[0], w[1] - 1, w[2])) ? w[1] - 1 : w[1];   // on a table (the brewing stand): stand beside the table
      for (int f = 0; f < 4; f++) {
        const int sx = w[0] + DXJ[f], sz = w[2] + DZJ[f];
        if (solid.count(key(sx, wy, sz)) || solid.count(key(sx, wy + 1, sz)) || !solid.count(key(sx, wy - 1, sz))) continue;
        b.work = {sx, wy, sz, (f + 2) & 3}; b.hasWork = true; work = true; bestScore = score;
        break;
      }
    }
    // the trade's round (sim/villager_ai.cpp villagerRound): each kind of station it works, in the order the work goes
    // (the armourer heats at the forge, beats it out on the anvil, dyes or quenches in the tub, fits it on a stand), the
    // nearest of each to the main workplace, and where to stand at it
    if (const std::vector<std::string>* round = tradeRound(job); round && b.hasWork) {
      for (const std::string& kind : *round) {
        int bestD = 1 << 30; VSpot at{};
        for (const VPBlock& k : p.blocks) {
          if (stationKind(reg().block(p.states[k.s].id)) != kind) continue;
          const auto w = world(k);
          const int wy = tableAt.count(key(w[0], w[1] - 1, w[2])) ? w[1] - 1 : w[1];
          for (int f = 0; f < 4; f++) for (int dy = 0; dy <= 2; dy++) {   // (a rack or a shelf up on a wall: the floor in front of it)
            const int sx = w[0] + DXJ[f], sy = wy - dy, sz = w[2] + DZJ[f];
            if (solid.count(key(sx, sy, sz)) || solid.count(key(sx, sy + 1, sz)) || !solid.count(key(sx, sy - 1, sz))) continue;
            const int d = std::abs(sx - b.work.x) + std::abs(sz - b.work.z) + 2 * std::abs(sy - b.work.y);
            if (d < bestD) { bestD = d; at = {sx, sy, sz, (f + 2) & 3}; }
            break;
          }
        }
        if (bestD < (1 << 30)) b.tasks.push_back({at, kind});
      }
      if (b.tasks.size() < 2) b.tasks.clear();   // (one station: the workplace is all there is)
    }
    if (type == "house" && job.empty()) for (const VPBlock& k : p.blocks) {
      const std::string& kk = reg().block(p.states[k.s].id).key;
      const int kind = kk == "furnace" || kk == "smoker" ? 0 : kk == "barrel" || kk == "chest" ? 1 : kk == "crafting_table" ? 2 : -1;
      if (kind < 0) continue;
      const auto w = world(k);
      bool roof = false; for (int up = 2; up <= 6 && !roof; up++) if (solid.count(key(w[0], w[1] + up, w[2]))) roof = true;
      for (int f = 0; f < 4; f++) {
        const int sx = w[0] + DXJ[f], sz = w[2] + DZJ[f];
        if (solid.count(key(sx, w[1], sz)) || solid.count(key(sx, w[1] + 1, sz)) || !solid.count(key(sx, w[1] - 1, sz))) continue;
        stations.push_back({(int)v.blds.size(), kind, {sx, w[1], sz, (f + 2) & 3}, roof});
        break;
      }
    }
    if (!job.empty() && !work) { b.work = {door[0], door[1], door[2], 0}; b.hasWork = true; }
    if (type == "farm") { b.work = {entW[0], q.y0 + 1, entW[2], 0}; b.hasWork = true; }
    if (type == "pen") {
      static const char* A[3] = {"cow", "pig", "chicken"};
      b.animals = p.animals == "sheep" || p.name.find("shepherd") != std::string::npos ? std::string("sheep") : std::string(A[hash32((i32)b.seed, 5, 5, 5) % 3]);
      b.mid = {(q.x0 + q.x1()) / 2, q.y0 + 1, (q.z0 + q.z1()) / 2, 0}; b.hasMid = true;
    }
    // the plan's "door" is the block in front of the door: from the entrance side
    b.ox = door[0]; b.oz = door[2]; b.W = 1; b.D = 1;   // (vWorld(b, 0, 1, 0) is then the door)
    b.y = door[1] - 1; b.yRaw = q.y0;
    q.bld = (int)v.blds.size();
    if (mill) {   // a piece with a millstone is a windmill: the miller keeps its stones, the box its millstone's in
      Bld m = b; m.type = "windmill"; m.job = "miller"; m.beds.clear(); m.y = q.y0; m.hasWork = true;
      m.work = {door[0], door[1], door[2], 0};
      v.blds.push_back(std::move(b)); v.blds.push_back(std::move(m));
    } else v.blds.push_back(std::move(b));
  }
  // the bell's square: the quest board on a post beside the centre, facing it
  {
    const JPlaced& C = J->pcs[0];
    const int cy = C.y0;
    if (v.bell == std::array<int, 3>{0, 0, 0}) v.bell = {v.x + 2, cy + 1, C.z0 - 2};
    for (int side = 0; side < 4 && !J->board; side++) for (int k = 2; k < 12 && !J->board; k++) {
      // just outside the centre's box, along one side; free of every piece and two blocks of clear ground
      const int f = (side + 2) & 3;   // the board faces back toward the centre
      int x = 0, z = 0;
      if (side == 0) { x = C.x0 + k; z = C.z0 - 2; } else if (side == 1) { x = C.x1() + 2; z = C.z0 + k; }
      else if (side == 2) { x = C.x1() - k; z = C.z1() + 2; } else { x = C.x0 - 2; z = C.z1() - k; }
      const int lx = -DZJ[f], lz = DXJ[f];
      bool ok = true;
      for (int a = -2; a <= 3 && ok; a++) for (int b2 = -1; b2 <= 1 && ok; b2++) if (inside(x + lx * a + DXJ[f] * b2, z + lz * a + DZJ[f] * b2)) ok = false;
      if (!ok || wet(x, z)) continue;
      J->board = true; J->bx = x; J->bz = z; J->by = cy; J->bf = f;
      J->flat.push_back({std::min(x - 3 * std::abs(lx), x + 3 * std::abs(lx)) - 1, std::min(z - 3 * std::abs(lz), z + 3 * std::abs(lz)) - 1,
                         std::max(x - 3 * std::abs(lx), x + 3 * std::abs(lx)) + 1, std::max(z - 3 * std::abs(lz), z + 3 * std::abs(lz)) + 1, cy});
    }
    if (J->board) v.board = {J->bx, J->by + 2, J->bz};
    else v.board = {v.x, cy + 2, v.z};
  }
  // the baker, the merchant and (with no windmill) the miller: Better Village has no house for them, so they take a
  // plain house with an oven, a counter or a bench (indoors first, nearest the centre); else a place by the hall door.
  // (They stood out in the street at the hall's porch: the miller grinding at its wall.)
  {
    const Bld* hall = nullptr; for (const Bld& b : v.blds) if (b.type == "hall") { hall = &b; break; }
    Bld hallC; if (hall) hallC = *hall;   // (a copy: the posts going in may move the list)
    auto post = [&](const char* job) {
      Bld b; b.type = "post"; b.job = job; b.hasWork = true;
      const std::string j = job; const int off = j == "baker" ? -2 : j == "miller" ? 4 : 2;
      if (hall) { auto w = vWorld(hallC, (hallC.W - 1) / 2 + off, 1, -1); b.work = {w[0], w[1], w[2], (hallC.r + 2) & 3}; }
      else b.work = {v.x + 3, v.y + 1, v.z + 3, 0};
      b.x0 = b.x1 = b.ox = b.work.x; b.z0 = b.z1 = b.oz = b.work.z; b.y = b.yRaw = b.work.y - 1; b.W = b.D = 1;
      v.blds.push_back(std::move(b));
    };
    auto has = [&](const std::string& j) { for (const Bld& b : v.blds) if (b.job == j) return true; return false; };
    bool mill = false; for (const Bld& b : v.blds) if (b.type == "windmill") mill = true;
    struct Need { const char* job; int a, b; };
    for (const Need& n : {Need{"baker", 0, 0}, Need{"merchant", 1, 2}, Need{"miller", 2, 1}}) {
      if (has(n.job) || (std::string(n.job) == "miller" && mill)) continue;
      int best = -1; double bd = 1e18;
      for (int i = 0; i < (int)stations.size(); i++) {
        const Station& st = stations[i];
        if ((st.kind != n.a && st.kind != n.b) || !v.blds[st.bld].job.empty()) continue;
        const double d = std::hypot((double)st.spot.x - v.x, (double)st.spot.z - v.z) + (st.indoor ? 0 : 400) + (st.kind == n.a ? 0 : 50);
        if (d < bd) { bd = d; best = i; }
      }
      if (best < 0) { post(n.job); continue; }
      Bld& h = v.blds[stations[best].bld]; h.job = n.job; h.work = stations[best].spot; h.hasWork = true;
    }
  }
  // towns and cities keep their walls (stone for a city, a palisade for most towns), round the whole place, with a gate
  // on each side where a street can run straight out to it past no building; the way out paved through the gate
  const std::string wallKind = v.tier == TIER_CITY ? "stone" : v.tier == TIER_TOWN && rnd() < 0.6 ? "wood" : "";
  if (!wallKind.empty()) {
    int bx0 = fx0, bz0 = fz0, bx1 = fx1, bz1 = fz1;
    if (J->board) { bx0 = std::min(bx0, J->bx - 4); bz0 = std::min(bz0, J->bz - 4); bx1 = std::max(bx1, J->bx + 4); bz1 = std::max(bz1, J->bz + 4); }
    const int mg = 4;
    v.hasWall = true; v.wall = {wallKind, bx0 - mg, bz0 - mg, bx1 + mg, bz1 + mg, wallKind == "stone" ? 6 : 5};
    const TownWall& W = v.wall;
    auto inRigid = [&](int x, int z) {
      for (auto& q : J->pcs) if (!q.p->terrain && x >= q.x0 && x <= q.x1() && z >= q.z0 && z <= q.z1()) return true;
      return J->board && std::abs(x - J->bx) <= 3 && std::abs(z - J->bz) <= 3;
    };
    int gates = 0;
    for (int d = 0; d < 4; d++) {
      const int dx = DXJ[d], dz = DZJ[d]; const bool ns = d == 0 || d == 2;
      const int line = d == 0 ? W.z0 : d == 2 ? W.z1 : d == 1 ? W.x1 : W.x0;
      const int lo = ns ? W.x0 : W.z0, hi = ns ? W.x1 : W.z1, mid = ns ? v.x : v.z;
      double best = 1e9; int gx = 0, gz = 0;
      for (auto& q : J->pcs) {
        if (!q.p->terrain) continue;
        for (const VPBlock& b : q.p->blocks) {
          if (b.y != 0) continue;
          auto [tx, tz] = vpTurn(*q.p, q.r, b.x, b.z);
          const int x = q.x0 + tx, z = q.z0 + tz, lat = ns ? x : z, len = ns ? std::abs(line - z) : std::abs(line - x);
          if (lat < lo + 6 || lat > hi - 6) continue;   // clear of the corner towers
          double score = len + 0.15 * std::abs(lat - mid);
          if (score >= best) continue;
          bool clear = true;
          for (int k = 1; k < len && clear; k++) for (int sd = -2; sd <= 2 && clear; sd++)
            if (inRigid(x + dx * k + (ns ? sd : 0), z + dz * k + (ns ? 0 : sd))) clear = false;
          if (!clear) continue;
          // not up a hillside: the gate about level with the street it leads from
          const int climb = std::abs(top(ns ? x : line, ns ? line : z) - top(x, z));
          if (climb > 6) continue;
          score += 2.0 * climb;
          if (score < best) { best = score; gx = x; gz = z; }
        }
      }
      if (best >= 1e9) continue;
      Street g; g.d = d; g.hw = 1; g.ox = ns ? gx : v.x; g.oz = ns ? v.z : gz;
      const int cxg = ns ? gx : line, czg = ns ? line : gz, len = ns ? std::abs(line - gz) : std::abs(line - gx);
      g.L = ns ? std::abs(line - v.z) : std::abs(line - v.x);
      // the way out keeps to the street's level and eases to the land's, a block a step at most
      int lvl = J->streetH.count(colKey(gx, gz)) ? J->streetH[colKey(gx, gz)] : top(gx, gz);
      for (int k = 1; k <= len + 8; k++) {
        const int cx = gx + dx * k, cz = gz + dz * k;
        lvl = std::clamp(std::max(top(cx, cz), SEA_LEVEL), lvl - 1, lvl + 1);
        if (k == len) g.gy = lvl;
        for (int sd = -1; sd <= 1; sd++) {
          const int x = cx + (ns ? sd : 0), z = cz + (ns ? 0 : sd);
          J->paths.push_back({x, z}); J->streetH[colKey(x, z)] = lvl;
        }
        g.cells.push_back({cx, cz, lvl});
      }
      J->flat.push_back({cxg - 4, czg - 4, cxg + 4, czg + 4, g.gy});   // level ground through the gate
      v.roads.push_back(std::move(g)); gates++;
    }
    if (!gates) { v.hasWall = false; v.roads.clear(); }   // (a wall with no way through it would shut the town in)
    if (v.hasWall) { J->walled = true; J->wall = {v.wall.x0, v.wall.z0, v.wall.x1, v.wall.z1}; }
  }
  // the levelling's reach
  for (auto& f : J->flat) { fx0 = std::min(fx0, f[0]); fz0 = std::min(fz0, f[1]); fx1 = std::max(fx1, f[2]); fz1 = std::max(fz1, f[3]); }
  J->fb = {fx0 - JFLAT_R - 2, fz0 - JFLAT_R - 2, fx1 + JFLAT_R + 2, fz1 + JFLAT_R + 2};
  // the streets, for the villagers' walks and the guards' rounds: every other block of each one's surface
  for (auto& q : J->pcs) {
    if (!q.p->terrain) continue;
    Street sw; sw.gate = false; sw.hw = 1; sw.L = 8; sw.ox = v.x; sw.oz = v.z;
    int k = 0;
    for (const VPBlock& b : q.p->blocks) {
      if (b.y != 0 || (k++ & 1)) continue;
      auto [tx, tz] = vpTurn(*q.p, q.r, b.x, b.z);
      const int x = q.x0 + tx, z = q.z0 + tz;
      auto t = J->streetH.find(colKey(x, z));
      sw.cells.push_back({x, z, t != J->streetH.end() ? t->second : std::max(top(x, z), SEA_LEVEL)});
    }
    if (!sw.cells.empty()) v.roads.push_back(std::move(sw));
  }
  for (auto& q : J->pcs) {
    if (!q.p->terrain) continue;
    for (const VPBlock& b : q.p->blocks) if (b.y == 0) { auto [tx, tz] = vpTurn(*q.p, q.r, b.x, b.z); J->street.insert(colKey(q.x0 + tx, q.z0 + tz)); }
  }
  for (auto& c : J->paths) J->street.insert(colKey(c[0], c[1]));
  v.flatRects.clear(); for (auto& f : J->flat) v.flatRects.push_back({f[0], f[1], f[2], f[3]});
  v.fb = J->fb;
  v.P = std::max(J->pcs[0].w, J->pcs[0].d) / 2; v.hw = 1;
  out = J;
  return true;
}

void jigsawTownBounds(const JigsawTown& J, int& x0, int& z0, int& x1, int& z1) {
  x0 = z0 = 1 << 30; x1 = z1 = -(1 << 30);
  for (auto& q : J.pcs) { x0 = std::min(x0, q.x0); z0 = std::min(z0, q.z0); x1 = std::max(x1, q.x1()); z1 = std::max(z1, q.z1()); }
  if (J.board) { x0 = std::min(x0, J.bx - 4); x1 = std::max(x1, J.bx + 4); z0 = std::min(z0, J.bz - 4); z1 = std::max(z1, J.bz + 4); }
  if (J.hasMine) { x0 = std::min(x0, J.mineBox[0]); z0 = std::min(z0, J.mineBox[1]); x1 = std::max(x1, J.mineBox[2]); z1 = std::max(z1, J.mineBox[3]); }
}
const std::vector<std::array<int, 3>>& jigsawTownEnds(const JigsawTown& J) { return J.ends; }

bool jigsawTownFlatten(const JigsawTown& J, int x, int z, double& weight, int& targetY) {
  if (x < J.fb[0] || x > J.fb[2] || z < J.fb[1] || z > J.fb[3]) return false;
  for (const auto& r : J.boxes) if (x >= r[0] && x <= r[2] && z >= r[1] && z <= r[3]) { weight = 1; targetY = r[4]; return true; }   // under a building
  if (auto t = J.streetH.find(colKey(x, z)); t != J.streetH.end()) { weight = 1; targetY = t->second; return true; }   // a street at its own level
  double best = 0; int by = 0;
  for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {   // beside a street: the ground eases to it
    if (!dx && !dz) continue;
    auto t = J.streetH.find(colKey(x + dx, z + dz));
    if (t == J.streetH.end()) continue;
    const double w = 1 - std::hypot((double)dx, (double)dz) / 4.2;
    if (w > best) { best = w; by = t->second; }
  }
  for (const auto& r : J.flat) {
    const int dx = std::max({r[0] - x, 0, x - r[2]}), dz = std::max({r[1] - z, 0, z - r[3]});
    if (dx == 0 && dz == 0) { weight = 1; targetY = r[4]; return true; }
    const double d = std::hypot((double)dx, (double)dz);
    if (d >= JFLAT_R) continue;
    const double w = 1 - jsSmooth(0, JFLAT_R, d);
    if (w > best) { best = w; by = r[4]; }
  }
  if (best <= 0) return false;
  weight = best; targetY = by;
  return true;
}

bool jigsawTownTreeOff(const JigsawTown& J, int x, int z) {
  if (J.walled && x >= J.wall[0] - 3 && x <= J.wall[2] + 3 && z >= J.wall[1] - 3 && z <= J.wall[3] + 3) return true;
  for (auto& q : J.pcs) if (x >= q.x0 - 2 && x <= q.x1() + 2 && z >= q.z0 - 2 && z <= q.z1() + 2) return true;
  for (auto& p : J.paths) if (std::abs(x - p[0]) <= 2 && std::abs(z - p[1]) <= 2) return true;
  return J.board && std::abs(x - J.bx) <= 4 && std::abs(z - J.bz) <= 4;
}

void jigsawTownBuild(SP& P, const Town& v, const JigsawTown& J) {
  const VStyle& st = vstyle(v.style);
  const int bx = P.bx, bz = P.bz;
  const BlockId DIRT = bid("dirt"), PLANKS = bid(st.planks == "smooth_sandstone" ? std::string("jungle_planks") : st.planks), PATH = bid("dirt_path");
  Mulberry32 lr(hash32((i32)v.seed, bx, bz, 77));
  // a street climbing a block from here: a half slab on this one makes two half steps of it (you and the villagers walk
  // up half a block, not a whole one)
  // A street climbing a block from one cell to the next: the higher cell's block becomes stairs rising away from the
  // lower one, so a steady slope is a staircase you walk up (two half steps a block; step height is 0.6). Half slabs on
  // the lower cells didn't do it: on a steady slope every cell got one and each step was still a whole block.
  const BlockId DPST = bid("dirt_path_stairs");
  auto stairFace = [&](int x, int z, int g) {   // the way it rises (from a street cell one lower); -1 none, or a ridge
    int f = -1, n = 0; bool opp = false;
    for (int d = 0; d < 4; d++) {
      const int nx = x - DXJ[d], nz = z - DZJ[d];
      if (!J.street.count(colKey(nx, nz)) || P.ground(nx, nz) != g - 1) continue;
      if (f >= 0 && ((f + 2) & 3) == d) opp = true;
      if (f < 0) f = d; n++;
    }
    return opp ? -1 : f;
  };
  auto stairOf = [&](BlockId on) {
    if (on != PATH) { const BlockId t = bid(blockKey(on) + "_stairs"); if (t != NOBLOCK && t != AIR) return t; }
    return DPST != NOBLOCK && DPST != AIR ? DPST : bid(st.stairs);
  };
  for (const JPlaced& q : J.pcs) {
    const bool mine = J.hasMine && isOurs(*q.p) && q.p->animals == "mine";   // (its galleries reach chunks its plot doesn't)
    if (mine ? (J.mineBox[2] < bx || J.mineBox[0] > bx + 15 || J.mineBox[3] < bz || J.mineBox[1] > bz + 15)
             : (q.x1() + 1 < bx || q.x0 - 1 > bx + 15 || q.z1() + 1 < bz || q.z0 - 1 > bz + 15)) continue;
    const VPPiece& p = *q.p;
    const int ax = std::max(q.x0, bx), ex = std::min(q.x1(), bx + 15), az = std::max(q.z0, bz), ez = std::min(q.z1(), bz + 15);
    if (isOurs(p)) {   // our hall or windmill: the classic way (ground cleared, a foundation, the building)
      const Bld& b = v.blds[q.bld];
      const std::string found = !st.base2.empty() ? st.base2 : st.stone;
      for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) {
        const bool in = x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1;
        for (int y = b.y + 1; y <= std::min(DIM_H - 1, b.y + b.H + 1); y++) if (in || softBlock(P.get(x, y, z))) P.put(x, y, z, "air");
        if (!in) continue;
        P.put(x, b.y, z, found);
        for (int y = b.y - 1; y > 1 && softBlock(P.get(x, y, z)); y--) P.put(x, y, z, found);
      }
      townBuildingBuild(P, v, b);
      continue;
    }
    if (!p.terrain) {   // a rigid piece: the plants and snow and any tree out of its box first, and no lake or river in it:
      // its ground made good under it (its own water, the farms' channels, goes in after with its blocks)
      const BlockId TOPG = bid(vbase(v.style) == VS_DESERT ? "sand" : "grass");
      for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) {
        for (int y = std::max(1, q.y0 - 6); y <= q.y0; y++) if (tLiquid(P.get(x, y, z))) P.putId(x, y, z, y == q.y0 ? TOPG : DIRT);
        for (int y = q.y0 + 1; y <= std::min(DIM_H - 1, q.y0 + p.top + 2); y++) { const BlockId g = P.get(x, y, z); if (g != AIR && g != NOBLOCK && softBlock(g)) P.put(x, y, z, "air"); }
      }
    }
    for (const VPBlock& k : p.blocks) {
      auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
      const int x = q.x0 + tx, z = q.z0 + tz;
      if (!P.in(x, z)) continue;
      const VPState& s = p.states[k.s];
      BlockId id = s.id; int y; int stairMeta = -1;
      if (p.terrain) {
        int g = P.ground(x, z);
        if (g < SEA_LEVEL && tLiquid(P.get(x, g + 1, z))) { g = SEA_LEVEL; if (k.y == 0 && !reg().tLiquid[id]) id = PLANKS; }   // bridged over water
        y = g + k.y;
        if (k.y == 0) {
          for (int yy = y + 1; yy <= y + 4; yy++) { const BlockId u = P.get(x, yy, z); if (u != AIR && u != NOBLOCK && softBlock(u) && !tLiquid(u)) P.put(x, yy, z, "air"); }
          const BlockId under = P.get(x, y - 1, z); if (under == AIR || tLiquid(under)) P.putId(x, y - 1, z, DIRT);
          if (id == PATH && lr() < 0.04 && !st.path.empty()) { const BlockId w2 = bid(st.path[jsFloor(lr() * st.path.size())]); if (w2 != NOBLOCK && w2 != AIR) id = w2; }   // a worn stone now and then
        }
        if (k.y == 0 && id != PLANKS && (id == PATH || (reg().tSolid[id] && !reg().tShape[id]))) {
          const int f = stairFace(x, z, g);
          if (f >= 0) { const BlockId sb = stairOf(id); if (sb != NOBLOCK && sb != AIR) { id = sb; stairMeta = f; } }
        }
      } else {
        y = q.y0 + k.y;
        if (k.y == 0) for (int yy = y - 1; yy > y - 8 && yy > 1 && softBlock(P.get(x, yy, z)) && P.get(x, yy, z) != NOBLOCK; yy--) P.putId(x, yy, z, DIRT);
      }
      if (y < 1 || y >= DIM_H) continue;
      const int idx = P.putId(x, y, z, id, stairMeta >= 0 ? (u8)stairMeta : s.meta[q.r]);
      if (idx < 0) continue;
      if (s.rug) P.rug(x, y, z, s.rug);
      if (s.kind == VPK_CHEST) {
        Mulberry32 cr(hash32(x, y, z, (i32)v.seed));
        const std::string& n = p.name;
        const char* kind = n.find("smith") != std::string::npos || n.find("armorer") != std::string::npos ? "smithy" : n.find("library") != std::string::npos || n.find("cartographer") != std::string::npos ? "library" : "house";
        json loot = vLoot(kind, cr);
        if (const int n = (int)reg().block(id).num("container", 27); n < (int)loot.size()) {   // (a cupboard holds 9: its share of the loot, packed in)
          json packed = json::array(); for (auto& it : loot) if (!it.is_null() && (int)packed.size() < n) packed.push_back(it);
          while ((int)packed.size() < n) packed.push_back(nullptr);
          loot = packed;
        }
        P.setEntity(idx, json{{"type", "chest"}, {"items", loot}});
      } else if (s.kind == VPK_POT) P.setEntity(idx, json{{"type", "pot"}, {"plant", s.plant.empty() ? json(nullptr) : json(s.plant)}});
      else if (s.kind == VPK_WARES) {   // the goods on show: "id" or "id*n", comma-separated, empty = a gap
        json items = json::array(); size_t a0 = 0;
        while (a0 <= s.items.size()) {
          size_t b0 = s.items.find(',', a0); if (b0 == std::string::npos) b0 = s.items.size();
          std::string id = s.items.substr(a0, b0 - a0); a0 = b0 + 1; int n = 1;
          if (const size_t st2 = id.find('*'); st2 != std::string::npos) { n = std::max(1, std::atoi(id.c_str() + st2 + 1)); id = id.substr(0, st2); }
          items.push_back(!id.empty() && reg().item(id) ? json{{"id", id}, {"n", n}} : json(nullptr));
        }
        const std::string& et = s.etype;
        if (et == "pile") {
          json rot = json::array(); for (int k2 = 0; k2 < (int)items.size(); k2++) rot.push_back(((int)(hash32(x, z, k2, 911) & 15) - 8) * 3.14159265 / 16);
          P.setEntity(idx, json{{"type", "pile"}, {"items", items}, {"rot", rot}});
        } else if (et == "frame") { if (!items.empty() && !items[0].is_null()) P.setEntity(idx, json{{"type", "frame"}, {"item", items[0]}, {"rot", 0}}); }
        else if (et == "rack") { while (items.size() < 3) items.push_back(nullptr); P.setEntity(idx, json{{"type", "rack"}, {"rack", items}}); }
        else if (et == "stand") { while (items.size() < 5) items.push_back(nullptr); while (items.size() > 5) items.erase(items.size() - 1); P.setEntity(idx, json{{"type", "stand"}, {"items", items}, {"pose", 0}}); }   // (sim/build.cpp's stand)
        else if (et == "cabinet") {
          const int n = (int)reg().block(id).num("container", 9); while ((int)items.size() < n) items.push_back(nullptr);
          P.setEntity(idx, json{{"type", "chest"}, {"items", items}});
        } else P.setEntity(idx, json{{"type", et}, {"items", items}});   // shelf, bookcase
      }
    }
  }
  // a walled town's ways out through its gates
  for (const auto& [x, z] : J.paths) {
    if (!P.in(x, z)) continue;
    int g = P.ground(x, z); BlockId pv = PATH;
    if (g < SEA_LEVEL && tLiquid(P.get(x, g + 1, z))) { g = SEA_LEVEL; pv = PLANKS; }
    if (pv == PATH && vbase(v.style) == VS_DESERT && !st.path.empty()) { const BlockId sp = bid(st.path[0]); if (sp != NOBLOCK && sp != AIR) pv = sp; }
    const int f = pv != PLANKS ? stairFace(x, z, g) : -1;
    if (f >= 0) P.putId(x, g, z, stairOf(pv), (u8)f); else P.putId(x, g, z, pv);
    for (int yy = g + 1; yy <= g + 4; yy++) { const BlockId u = P.get(x, yy, z); if (u != AIR && u != NOBLOCK && softBlock(u) && !tLiquid(u)) P.put(x, yy, z, "air"); }
  }
  // the quest board under its little roof, facing the centre
  if (J.board && std::abs(J.bx - (bx + 8)) <= 12 && std::abs(J.bz - (bz + 8)) <= 12) {
    const int f = J.bf, lx = -DZJ[f], lz = DXJ[f], y0 = J.by;
    for (int a : {0, 1}) {
      const int x = J.bx + lx * a, z = J.bz + lz * a;
      if (!P.in(x, z)) continue;
      for (int y = y0 + 1; y <= y0 + 4; y++) P.put(x, y, z, "air");
      P.put(x, y0 + 1, z, st.log); P.put(x, y0 + 2, z, "quest_board", (u8)((2 - f) & 3)); P.put(x, y0 + 3, z, st.slab);
    }
    for (int a : {-1, 2}) {
      const int x = J.bx + lx * a, z = J.bz + lz * a;
      if (!P.in(x, z)) continue;
      P.put(x, y0 + 1, z, st.fence); P.put(x, y0 + 2, z, "lantern");
    }
  }
}

} // namespace dq
