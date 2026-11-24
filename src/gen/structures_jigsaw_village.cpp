// gen/structures_jigsaw_village.cpp — our villages from terrain version 9: our village pieces (gen/village_pieces.h; built by
// tools/port/village_builder.py) put together
// jigsaw-fashion: a town centre, streets growing out of its street joints, houses on the streets' building
// joints, each piece joined face to face and turned to fit, none overlapping, up to a depth and a distance from the
// centre. Streets follow the land; everything else stands level at the height of the street joint it hangs off, the
// ground levelled under it (villageflat's blend, piece by piece). Our own hall (the elder, the inn, the quest board's
// keepers) and windmill (the miller's millstone, sails on a real axle) join the streets like any other house, so the
// living village (sim/villager*.cpp) finds what it needs: the plan's buildings, beds, jobs and workplaces come from the
// pieces themselves (their beds, job blocks, doors and pens).
#include "gen/town_mine.h"
#include <cstdlib>
#include <cstring>
#include "core/tool_parts.h"
#include "gen/structures_town.h"
#include "gen/village_pieces.h"
#include "gen/village_mill_lift.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <deque>
#include <unordered_map>

namespace dq {
bool isTntStyle(const std::string& st);

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
  bool blend = false;                         // terrain version 17: the levels round the plots blended, not the nearest
  bool millOut = false;                       // terrain version 22: a windmill's big sails a block further out (clear of its gable)
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
  VPJoint j; j.x = (W - 1) / 2; j.y = 1; j.z = 0; j.front = 0; j.name = "main:building_entrance"; j.target = "main:building_entrance"; j.pool = "main:empty";
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
    {"fisherman", {"stores", "tub"}}, {"innkeeper", {"stores", "table", "shelf"}},
    {"leatherworker", {"tub", "table", "stores", "stand"}}};
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
    if (has("alchemist")) job = "cleric";   // (the potion stall: the cleric brews)
    return;
  }
  if (has("windmill")) { type = "house"; return; }   // (the millstone in it makes it the windmill)
  if (has("barracks") || has("watchtower")) { type = "barracks"; job = "guard"; return; }
  static const std::vector<std::pair<const char*, const char*>> J = {
    {"armorer", "armorer"}, {"weaponsmith", "weaponsmith"}, {"weapon_smith", "weaponsmith"}, {"tool_smith", "toolsmith"}, {"toolsmith", "toolsmith"}, {"butcher", "butcher"},
    {"fletcher", "fletcher"}, {"shepherd", "shepherd"}, {"mason", "mason"}, {"cartographer", "cartographer"}, {"library", "librarian"},
    {"fisher", "fisherman"}, {"tannery", "leatherworker"}, {"jeweller", "jeweller"}, {"bakery", "baker"}, {"tavern", "innkeeper"},
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
  J->blend = t.genVersion() >= 17;
  J->millOut = t.genVersion() >= 22;
  J->ours.reserve(4);   // (room for all of them: oursLeft points into it)
  J->ours.push_back(ourPiece("hall"));
  // a windmill: most styles' pieces have their own, a tower with sails, shaft and millstone; (terrain 22) a style without one
  // (the town styles, the Romanian, Japanese, Swiss...) has ours, so every village has its miller
  if (t.genVersion() >= 22) {
    bool own = false;
    for (const auto& [name, list] : pack.pools) if (name.rfind(sk + "/", 0) == 0) for (const VPPiece& q : list) if (q.name.find("windmill") != std::string::npos) { own = true; break; }
    if (!own) J->ours.push_back(ourPiece("windmill"));
  }
  J->ours.push_back(ourPiece("mine"));   // the miner's: a drift down to a working face (structures_townbld.cpp bMine)
  std::vector<const VPPiece*> oursLeft; for (auto& p : J->ours) oursLeft.push_back(&p);
  // (terrain 21: the town-style villages a street further out, a market round their square: they were small and plain)
  const bool tnt21 = isTntStyle(sk) && t.genVersion() >= 21;
  const int maxDepth = (v.tier == TIER_CITY ? 8 : v.tier == TIER_TOWN ? 7 : 6) + (tnt21 ? 1 : 0), R = (v.tier == TIER_CITY ? 112 : v.tier == TIER_TOWN ? 96 : 80) + (tnt21 ? 16 : 0);
  auto top = [&](int x, int z) { return t.terrainColumn(x, z).y; };
  // water over it: the land below the sea (plan 11; the original's height at sea level was a test too coarse for the taller
  // band: dry shore at the sea's level and a block above counted, the streets there had no level of their own, and a
  // house's levelled ground beside one raised a ridge across it with the street sunk in a trench)
  auto wet = [&](int x, int z) { const TerrainColumn c = t.terrainColumn(x, z); return v.planV >= 11 ? c.water >= 0 : c.jsY <= JS_SEA; };
  auto overlaps = [&](int x0, int z0, int x1, int z1, int skip = -1) {
    for (int i = 0; i < (int)J->pcs.size(); i++) { if (i == skip) continue; const auto& q = J->pcs[i]; if (x0 <= q.x1() && x1 >= q.x0 && z0 <= q.z1() && z1 >= q.z0) return true; }
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
      if (A.front > 3 || A.pool.empty() || A.pool == "main:empty") continue;
      auto [tx, tz] = vpTurn(*Q.p, Q.r, A.x, A.z);
      const int ax = Q.x0 + tx, az = Q.z0 + tz, af = (A.front + Q.r) & 3;
      const int ay = Q.p->terrain ? top(ax, az) + A.y : Q.y0 + A.y;
      const int Tx = ax + DXJ[af], Tz = az + DZJ[af];
      // the jigsaw: a joint whose spot is inside its own piece's box puts the next piece inside that box (the town
      // styles' streets are plots with the houses' lots in them); anywhere else, the spot must be free
      const bool inParent = isTntStyle(Q.p->style) && Tx >= Q.x0 && Tx <= Q.x1() && Tz >= Q.z0 && Tz <= Q.z1();
      if (inParent ? overlaps(Tx, Tz, Tx, Tz, (int)qi) : inside(Tx, Tz)) continue;
      const bool streets = A.pool.find("/streets") != std::string::npos, houses = A.pool.find("/houses") != std::string::npos;
      std::vector<const VPPiece*> cands;
      if (Q.depth + 1 <= maxDepth) {
        if (const auto* pl = pack.pool(A.pool)) cands = pickWeighted(*pl);
        if (houses && tnt21 && Q.depth <= 2) if (const auto* mk = pack.pool(sk + "/market")) { auto st = pickWeighted(*mk); cands.insert(cands.begin(), st.begin(), st.end()); }
        if (houses) {
          // a trade's building once in a village (twice in a town or a city; farms and pens as the land allows), a stall
          // of each kind once, and the stalls only near the square (a market street, not stalls up every lane)
          auto tradeOf = [&](const VPPiece* p) {
            if (t.genVersion() >= 22 && p->name.find("windmill") != std::string::npos) return std::string("mill/miller");   // (terrain 22: a trade, wanted once)
            std::string tt, j; pieceRole(*p, tt, j); return tt == "house" && j.empty() ? std::string() : tt + "/" + j; };
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
            for (const VPPiece* p : cands) if (quarterOf(*p) == Q_HOME && !fits(p)) keep.push_back(p);   // (homes after them: where none of those fits, a home does, not an empty joint)
            if (!keep.empty()) cands.swap(keep);   // (a style whose every house has its trade (the Vikings): any of them)
          }
          // the trades it hasn't got come first (most of the time: a plain house now and then between them)
          if (rnd() < 0.7) std::stable_partition(cands.begin(), cands.end(), [&](const VPPiece* p) { const std::string t = tradeOf(p); return !t.empty() && !J->trades.count(t) && t.rfind("stall", 0) != 0; });
        }
        // (terrain 22) no mill yet: the style's own windmill tried first at any plot two streets or more out (it was kept to the
        // village's edge, where its big plot seldom found room, and most villages went without)
        if (houses && t.genVersion() >= 22 && Q.depth >= 2 && !J->trades.count("mill/miller"))
          if (const auto* pl = pack.pool(A.pool))
            for (const VPPiece& pc : *pl) if (pc.name.find("windmill") != std::string::npos) {
              cands.erase(std::remove(cands.begin(), cands.end(), &pc), cands.end()); cands.insert(cands.begin(), &pc);
            }
        if (houses) {
          if (!oursLeft.empty() && Q.depth >= 1) {   // our hall first, then the windmill
            // (terrain 22: each of ours still to place, in turn; only the first had been tried, and a hall too big for any of a
            // style's lots (the Tudor) had kept the windmill and the mine behind it out of the village)
            if (t.genVersion() >= 22) cands.insert(cands.begin(), oursLeft.begin(), oursLeft.end());
            else cands.insert(cands.begin(), oursLeft.front());
          }
          else if (rnd() < (v.tier == TIER_CITY ? 0.0 : v.tier == TIER_TOWN ? 0.05 : 0.12)) continue;   // the pools' "empty": a gap between the houses now and then (towns and cities fill up)
        }
      }
      if (streets) if (const auto* tl = pack.pool(sk + "/terminators")) for (auto* p : pickWeighted(*tl)) cands.push_back(p);
      bool placed = false;
      static const bool dbgJ = std::getenv("DQ_VILLAGE_LOG2") != nullptr;
      int rej[6] = {0, 0, 0, 0, 0, 0};
      for (const VPPiece* C : cands) {
        int rs[4] = {0, 1, 2, 3}; for (int i = 3; i > 0; i--) std::swap(rs[i], rs[jsFloor(rnd() * (i + 1))]);
        for (int r : rs) {
          for (const VPJoint& B : C->joints) {
            if (B.front > 3 || B.name != A.target || ((B.front + r) & 3) != ((af + 2) & 3)) { rej[0]++; continue; }
            auto [cx2, cz2] = vpTurn(*C, r, B.x, B.z);
            JPlaced q; q.p = C; q.r = r; q.w = r & 1 ? C->sz : C->sx; q.d = r & 1 ? C->sx : C->sz;
            q.x0 = Tx - cx2; q.z0 = Tz - cz2; q.depth = Q.depth + 1;
            if (std::max({std::abs(q.x0 - v.x), std::abs(q.x1() - v.x), std::abs(q.z0 - v.z), std::abs(q.z1() - v.z)}) > R) { rej[1]++; continue; }
            if (inParent && (q.x0 < Q.x0 || q.x1() > Q.x1() || q.z0 < Q.z0 || q.z1() > Q.z1())) { rej[5]++; continue; }
            if (inParent ? overlaps(q.x0, q.z0, q.x1(), q.z1(), (int)qi) : overlaps(q.x0, q.z0, q.x1(), q.z1())) {
              rej[2]++;
              if (dbgJ && houses) for (int i = 0; i < (int)J->pcs.size(); i++) { const auto& o = J->pcs[i]; if (i != (int)qi && q.x0 <= o.x1() && q.x1() >= o.x0 && q.z0 <= o.z1() && q.z1() >= o.z0) { DQ_LOG("  %s (in %s, inParent %d) hits %s (%s)", C->name.c_str(), Q.p->name.c_str(), (int)inParent, o.p->name.c_str(), i == o.parent ? "?" : (o.parent == (int)qi ? "a sibling" : "other")); break; } }
              continue;
            }
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
              if (std::abs(q.y0 - v.y) > climb) { rej[3]++; continue; }
              // not out over water, nor hung off a slope it would have to stand far above or below
              // (the whole footprint, every few blocks: a lake under one side of it got through when only the middle and
              // the corners were looked at, and the farm stood half in it)
              bool steep = q.y0 < SEA_LEVEL;   // (below the sea: its levelled ground would be a pond)
              for (int pz = q.z0; pz <= q.z1() + 2 && !steep; pz += 3) for (int px = q.x0; px <= q.x1() + 2 && !steep; px += 3) {
                const int sx = std::min(px, q.x1()), sz = std::min(pz, q.z1());
                if (wet(sx, sz)) steep = true;
              }
              for (auto [px, pz] : {std::pair{q.x0, q.z0}, {q.x1(), q.z0}, {q.x0, q.z1()}, {q.x1(), q.z1()}}) if (!steep && std::abs(top(px, pz) - q.y0) > 7) steep = true;
              // nor sunk into a rise: the land over most of it well above its floor (a pit dug round it, only its roof showing)
              if (!steep) {
                int above = 0, n = 0;
                for (int pz = q.z0; pz <= q.z1(); pz += 2) for (int px = q.x0; px <= q.x1(); px += 2) { n++; if (top(px, pz) - q.y0 > 3) above++; }
                if (above * 3 > n) steep = true;
              }
              if (steep) { rej[4]++; continue; }
            }
            q.parent = (int)qi; q.jx = ax; q.jz = az; q.dy = A.y - B.y;
            J->pcs.push_back(q);
            if (std::getenv("DQ_VILLAGE_LOG") && C->name.find("garden_bridge") != std::string::npos) DQ_LOG("garden bridge in %s at %d %d", v.name.c_str(), q.x0, q.z0);
            { std::string tt, j; pieceRole(*C, tt, j);
              if (t.genVersion() >= 22 && C->name.find("windmill") != std::string::npos) J->trades["mill/miller"]++;
              else if (!(tt == "house" && j.empty())) J->trades[tt + "/" + j]++; }
            if (isOurs(*C)) oursLeft.erase(std::find(oursLeft.begin(), oursLeft.end(), C));
            placed = true; break;
          }
          if (placed) break;
        }
        if (placed) break;
      }
      if (dbgJ && houses && !placed) DQ_LOG("house joint %s: %zu cands, rejected joint %d range %d overlap %d climb %d steep %d outOfLot %d", A.pool.c_str(), cands.size(), rej[0], rej[1], rej[2], rej[3], rej[4], rej[5]);
      if (!placed && streets) J->ends.push_back({Tx, Tz, af});
    }
  }
  std::sort(J->ends.begin(), J->ends.end(), [&](const std::array<int, 3>& a, const std::array<int, 3>& b) {
    return std::hypot(a[0] - v.x, a[1] - v.z) > std::hypot(b[0] - v.x, b[1] - v.z); });   // the farthest first: where roads leave
  // (the pack's own houses, counted: our hall and mine had made a well, a street and one house pass for a village)
  int nb = 0; for (auto& q : J->pcs) if (!q.p->terrain && q.p->kind != "town_centers" && q.p->name.rfind("dq_", 0) != 0) nb++;
  if (std::getenv("DQ_VILLAGE_LOG")) {
    std::map<std::string, int> k; for (auto& q : J->pcs) k[q.p->kind]++;
    std::string s; for (auto& [a2, n] : k) s += " " + a2 + " " + std::to_string(n);
    DQ_LOG("jigsaw %s (%s):%s; ends %zu", v.name.c_str(), sk.c_str(), s.c_str(), J->ends.size());
  }
  if (nb < (v.tier == TIER_CITY || v.tier == TIER_TOWN ? 6 : 4)) return false;   // (too few: the classic town instead)
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
  if (std::getenv("DQ_VSINK")) for (auto& q : J->pcs) if (!q.p->terrain) {   // (a building well below the land it stands on)
    const int c = top(q.x0 + q.w / 2, q.z0 + q.d / 2);
    if (c - q.y0 > 4) DQ_LOG("vsink %s (%s) y%d land %d: %d below; parent %s", q.p->name.c_str(), q.p->style.c_str(), q.y0, c, c - q.y0, q.parent >= 0 ? J->pcs[q.parent].p->name.c_str() : "-");
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
    std::set<long long> passAt, climbAt;   // (doors walked through; ladders and vines climbed)
    std::map<long long, int> lowAt;   // (each column's lowest block of the piece's: under it, and where it has none, the land)
    for (const VPBlock& k : p.blocks) {
      const VPState& s = p.states[k.s]; auto w = world(k);
      if (auto [it, fresh] = lowAt.try_emplace(key(w[0], 0, w[2]), w[1]); !fresh) it->second = std::min(it->second, w[1]);
      if (reg().tSolid[s.id]) solid.insert(key(w[0], w[1], w[2]));
      if (reg().block(s.id).has("furniture")) tableAt.insert(key(w[0], w[1], w[2]));
      if (reg().block(s.id).has("door")) passAt.insert(key(w[0], w[1], w[2]));
      if (reg().block(s.id).has("climb")) { passAt.insert(key(w[0], w[1], w[2])); climbAt.insert(key(w[0], w[1], w[2])); }
    }
    const VPJoint* ent = nullptr; for (auto& j : p.joints) if (j.front < 4 && j.name.find("building_entrance") != std::string::npos) { ent = &j; break; }
    std::array<int, 3> entW{(q.x0 + q.x1()) / 2, q.y0 + 1, (q.z0 + q.z1()) / 2};
    if (ent) { auto [x, z] = vpTurn(p, q.r, ent->x, ent->z); entW = {q.x0 + x, q.y0 + ent->y, q.z0 + z}; }
    // the cells a body walks to from outside the piece, by its own blocks: a station's side shut in behind a stall's
    // counter, past the keeper's chair across the aisle, isn't where its trade is worked (the jeweller stood there for
    // good, with no way out on foot). Stairs and slabs are a step up, doors walked through, ladders climbed.
    std::set<long long> reach;
    // (nothing of the piece's at or under y in that column, no higher than the ground at its entrance: the land it stands on)
    auto bare = [&](int x, int y, int z) { auto it = lowAt.find(key(x, 0, z)); return y < q.y0 || (y <= std::max(q.y0, entW[1] - 1) && (it == lowAt.end() || it->second > y)); };
    {
      auto inside = [&](int x, int z) { return x >= q.x0 && x <= q.x1() && z >= q.z0 && z <= q.z1(); };
      auto pass = [&](int x, int y, int z) { const long long k = key(x, y, z); return !solid.count(k) || passAt.count(k); };
      auto floorAt = [&](int x, int y, int z) { const long long k = key(x, y, z); return (solid.count(k) && !passAt.count(k)) || climbAt.count(k) || bare(x, y, z) || !inside(x, z); };
      auto stand = [&](int x, int y, int z) { return pass(x, y, z) && pass(x, y + 1, z) && (floorAt(x, y - 1, z) || climbAt.count(key(x, y, z))); };
      const int Y0 = q.y0 - 1, Y1 = q.y0 + p.top + 1;
      std::deque<std::array<int, 3>> open;
      auto add = [&](int x, int y, int z) { if (y < Y0 || y > Y1 || x < q.x0 - 1 || x > q.x1() + 1 || z < q.z0 - 1 || z > q.z1() + 1) return; if (reach.insert(key(x, y, z)).second) open.push_back({x, y, z}); };
      for (int x = q.x0 - 1; x <= q.x1() + 1; x++) for (int z = q.z0 - 1; z <= q.z1() + 1; z++)   // (the ground round it, about the entrance's height)
        if (!inside(x, z)) for (int y : {entW[1] - 1, entW[1], entW[1] + 1, q.y0 + 1}) add(x, y, z);
      while (!open.empty()) {
        const auto [x, y, z] = open.front(); open.pop_front();
        if (climbAt.count(key(x, y, z)) && stand(x, y + 1, z)) add(x, y + 1, z);
        if (climbAt.count(key(x, y - 1, z)) && stand(x, y - 1, z)) add(x, y - 1, z);
        for (int f = 0; f < 4; f++) {
          const int nx = x + DXJ[f], nz = z + DZJ[f];
          if (!inside(nx, nz)) { for (int ny = y - 1; ny <= y + 1; ny++) add(nx, ny, nz); continue; }
          if (stand(nx, y, nz)) add(nx, y, nz);
          else if (stand(nx, y + 1, nz) && pass(x, y + 2, z)) add(nx, y + 1, nz);   // (a step up)
          else if (stand(nx, y - 1, nz) && pass(nx, y + 1, nz)) add(nx, y - 1, nz);   // (a step down)
        }
      }
    }
    // a place to stand at a station: 1 one they walk to (on the piece's floor, or on the ground it stands on, just outside
    // it or where it has nothing under them: a stall's wheel worked from its front when the aisle behind it is shut in); 2 one
    // on its floor they can't walk to (taken only when there's nothing better); 0 none
    auto standSpot = [&](int x, int y, int z) {
      if (solid.count(key(x, y, z)) || solid.count(key(x, y + 1, z))) return 0;
      const bool floor = solid.count(key(x, y - 1, z)) != 0;
      const bool out = x < q.x0 || x > q.x1() || z < q.z0 || z > q.z1(), ground = bare(x, y - 1, z);
      if (reach.count(key(x, y, z)) && (floor || out || ground)) return 1;
      return floor ? 2 : 0;
    };
    std::array<int, 3> door = entW; int dd = 1 << 30; bool work = false, mill = false;
    // where the trade is worked: its own station first (the job block for it, or the smith's anvil), indoors
    // before out in the yard (the weaponsmith stood at the yard's sharpening wheel, the grindstone, not his anvil)
    static const std::map<std::string, std::vector<std::string>> PREF = {
      {"armorer", {"blast_furnace", "anvil", "smithing_table"}}, {"weaponsmith", {"anvil", "smithing_table", "grindstone"}},
      {"toolsmith", {"smithing_table", "anvil"}}, {"butcher", {"smoker"}}, {"fletcher", {"fletching_table"}}, {"librarian", {"lectern"}},
      {"cartographer", {"cartography_table"}}, {"cleric", {"brewing_stand"}}, {"mason", {"stonecutter"}}, {"farmer", {"composter"}},
      {"fisherman", {"barrel"}}, {"shepherd", {"loom"}}, {"leatherworker", {"cauldron", "water_cauldron", "crafting_table"}},
      {"merchant", {"cauldron", "barrel"}}, {"baker", {"smoker", "furnace"}},
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
      const int score0 = pref * 2 + (roof ? 0 : 1);
      if (score0 >= bestScore) continue;
      const int wy = tableAt.count(key(w[0], w[1] - 1, w[2])) ? w[1] - 1 : w[1];   // on a table (the brewing stand): stand beside the table
      // (a side they can walk to first; one shut in only when none is)
      for (int pass = 0; pass < 2; pass++) {
        const int score = score0 + pass * 100;
        if (score >= bestScore) break;
        bool found = false;
        for (int f = 0; f < 4; f++) {
          const int sx = w[0] + DXJ[f], sz = w[2] + DZJ[f];
          if (standSpot(sx, wy, sz) != pass + 1) continue;
          b.work = {sx, wy, sz, (f + 2) & 3}; b.hasWork = true; work = true; bestScore = score; found = true;
          break;
        }
        if (found) break;
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
            const int spot = standSpot(sx, sy, sz);
            if (!spot) continue;
            const int d = std::abs(sx - b.work.x) + std::abs(sz - b.work.z) + 2 * std::abs(sy - b.work.y) + (spot == 1 ? 0 : 1000);
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
      for (int pass = 0, found = 0; pass < 2 && !found; pass++) for (int f = 0; f < 4; f++) {   // (a side they can walk to first)
        const int sx = w[0] + DXJ[f], sz = w[2] + DZJ[f];
        if (standSpot(sx, w[1], sz) != pass + 1) continue;
        stations.push_back({(int)v.blds.size(), kind, {sx, w[1], sz, (f + 2) & 3}, roof}); found = 1;
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
    if (J->board && v.genV >= 25) {   // (terrain 25) the Shrine of Renewal past the board's far lantern, on its levelled ground
      const int lx = -DZJ[J->bf], lz = DXJ[J->bf];
      v.hasShrine = true; v.shrine = {J->bx + lx * 3, J->by + 1, J->bz + lz * 3};
    }
  }
  // the baker, the merchant and (with no windmill) the miller: the imported pieces have no house for them, so they take a
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
  // Not round a town-style village: theirs are open (their streets run off in long lines; a box round them shut a bare
  // hillside in a palisade with a few houses in it). (The draw is still made, so the rest of the plan stays as it was.)
  const bool wallRoll = v.tier == TIER_TOWN && rnd() < 0.6;
  const bool tntStyle = !J->pcs.empty() && isTntStyle(J->pcs[0].p->style);
  const std::string wallKind = tntStyle ? "" : v.tier == TIER_CITY ? "stone" : wallRoll ? "wood" : "";
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
  if (std::getenv("DQ_STALL_LOG")) { int st = 0; for (auto& q : J->pcs) if (q.p->name.find("market_stall") != std::string::npos) st++; DQ_LOG("village %s (%s): %d pieces, %d market stalls", v.name.c_str(), sk.c_str(), (int)J->pcs.size(), st); }
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
void jigsawTownBoxes(const JigsawTown& J, const std::array<int, 4>& n, std::vector<std::array<int, 4>>& out) {
  auto add = [&](int x0, int z0, int x1, int z1) { if (x1 >= n[0] && x0 <= n[2] && z1 >= n[1] && z0 <= n[3]) out.push_back({x0, z0, x1, z1}); };
  for (auto& q : J.pcs) add(q.x0, q.z0, q.x1(), q.z1());
  if (J.board) add(J.bx - 2, J.bz - 2, J.bx + 2, J.bz + 2);
}

// the town styles (tools/local/town_import.py): their streets are plots with the houses' lots in them, and
// their villages have no wall
bool isTntStyle(const std::string& st) {
  static const std::set<std::string> TNT = {"meadow_swiss", "birch_forest_romanian", "flower_forest_japanese", "forest_ruins", "mushroom_fields_fantasy",
    "swamp_boat", "jungle_tribal", "sparse_jungle_polynesian", "badlands_pueblo", "old_growth_taiga_polish", "snowy_taiga_viking", "tudor", "classic",
    "rustic", "swedish", "iberian", "mediterranean", "nilotic"};
  return TNT.count(st) > 0;
}

bool jigsawTownFlatten(const JigsawTown& J, int x, int z, double& weight, int& targetY) {
  if (x < J.fb[0] || x > J.fb[2] || z < J.fb[1] || z > J.fb[3]) return false;
  for (const auto& r : J.boxes) if (x >= r[0] && x <= r[2] && z >= r[1] && z <= r[3]) { weight = 1; targetY = r[4]; return true; }   // under a building
  if (auto t = J.streetH.find(colKey(x, z)); t != J.streetH.end()) { weight = 1; targetY = t->second; return true; }   // a street at its own level
  // beside a street or a plot: the ground eases to them, every level near blended by nearness (it took only the nearest:
  // where a street met a plot, or two plots, the land stepped between their levels in sheer earth walls)
  double best = 0, sw = 0, swy = 0;
  int by = 0;
  auto add = [&](double w, int y) { if (w <= 0) return; if (w > best) { best = w; by = y; } const double k = w * w * w; sw += k; swy += k * y; };
  for (int dz = -JFLAT_R; dz <= JFLAT_R; dz++) for (int dx = -JFLAT_R; dx <= JFLAT_R; dx++) {
    if (!dx && !dz) continue;
    const double d = std::hypot((double)dx, (double)dz);
    if (d >= JFLAT_R) continue;
    auto t = J.streetH.find(colKey(x + dx, z + dz));
    if (t == J.streetH.end()) continue;
    add(1 - jsSmooth(0, JFLAT_R, d), t->second);
  }
  for (const auto& r : J.flat) {
    const int dx = std::max({r[0] - x, 0, x - r[2]}), dz = std::max({r[1] - z, 0, z - r[3]});
    if (dx == 0 && dz == 0) { weight = 1; targetY = r[4]; return true; }
    const double d = std::hypot((double)dx, (double)dz);
    if (d >= JFLAT_R) continue;
    add(1 - jsSmooth(0, JFLAT_R, d), r[4]);
  }
  if (best <= 0 || sw <= 0) return false;
  weight = best; targetY = J.blend ? (int)std::lround(swy / sw) : by;
  return true;
}

bool jigsawTownTreeOff(const JigsawTown& J, int x, int z) {
  if (J.walled && x >= J.wall[0] - 3 && x <= J.wall[2] + 3 && z >= J.wall[1] - 3 && z <= J.wall[3] + 3) return true;
  for (auto& q : J.pcs) if (x >= q.x0 - 2 && x <= q.x1() + 2 && z >= q.z0 - 2 && z <= q.z1() + 2) return true;
  for (auto& p : J.paths) if (std::abs(x - p[0]) <= 2 && std::abs(z - p[1]) <= 2) return true;
  return J.board && std::abs(x - J.bx) <= 4 && std::abs(z - J.bz) <= 4;
}

// an armour stand the piece brought with it: not in a doorway, and facing into the room (the front is the other way from meta)
static bool genDoorBeside(const SP& P, int x, int y, int z) {
  for (int dy = 0; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    if (!dx && !dz) continue;
    const BlockId id = P.get(x + dx, y + dy, z + dz);
    if (id != NOBLOCK && id != AIR && reg().block(id).has("door")) return true;
  }
  return false;
}
static bool genFree(const SP& P, int x, int y, int z) {
  if (!P.in(x, z) || y < 2 || y >= DIM_H - 1) return false;
  const Registry& R = reg();
  const BlockId a = P.get(x, y, z), b = P.get(x, y + 1, z), g = P.get(x, y - 1, z);
  if (a == NOBLOCK || b == NOBLOCK || g == NOBLOCK) return false;
  return (a == AIR || R.tReplaceable[a]) && (b == AIR || R.tReplaceable[b]) && R.tSolid[g] && R.tOpaque[g];
}
static int genFaceRoom(const SP& P, const std::vector<ivec3>& room, int x, int y, int z) {
  long sx = 0, sz = 0, n = 0;
  for (const ivec3& c : room) if (c.y == y) { sx += c.x; sz += c.z; n++; }
  if (!n) return 2;
  const long dx = sx - (long)x * n, dz = sz - (long)z * n;
  long ddx = 0, ddz = 0; bool gotDoor = false;
  for (const ivec3& c : room) if (c.y == y) for (int k = 0; k < 4 && !gotDoor; k++) {
    const int dx0 = c.x + DIRX[k], dz0 = c.z + DIRZ[k];
    const BlockId a = P.get(dx0, c.y, dz0), b = P.get(dx0, c.y + 1, dz0);
    const bool door = (a != NOBLOCK && a != AIR && reg().block(a).has("door")) || (b != NOBLOCK && b != AIR && reg().block(b).has("door"));
    if (!door) continue;
    ddx = dx0 - x; ddz = dz0 - z; gotDoor = true;
  }
  int best = 2; long bestScore = LONG_MIN, bestDoor = LONG_MIN;
  for (int k = 0; k < 4; k++) {
    const long score = dx * DIRX[k] + dz * DIRZ[k];
    const long ds = gotDoor ? ddx * DIRX[k] + ddz * DIRZ[k] : 0;
    if (score > bestScore || (score == bestScore && ds > bestDoor)) { bestScore = score; bestDoor = ds; best = k; }
  }
  return best;
}
static void fixGeneratedStand(SP& P, int x, int y, int z) {
  const BlockId STAND = bid("armor_stand");
  if (STAND == NOBLOCK || STAND == AIR || P.get(x, y, z) != STAND) return;
  const Registry& R = reg();
  auto doorAt = [&](int cx, int cy, int cz) {
    const BlockId a = P.get(cx, cy, cz), b = P.get(cx, cy + 1, cz);
    return (a != NOBLOCK && a != AIR && R.block(a).has("door")) || (b != NOBLOCK && b != AIR && R.block(b).has("door"));
  };
  auto step = [&](int cx, int cy, int cz) {
    if (!P.in(cx, cz) || doorAt(cx, cy, cz)) return false;
    const BlockId a = P.get(cx, cy, cz), b = P.get(cx, cy + 1, cz), g = P.get(cx, cy - 1, cz);
    if (a == NOBLOCK || b == NOBLOCK || g == NOBLOCK) return false;
    const bool feet = a == AIR || R.tReplaceable[a] || a == STAND;
    return feet && (b == AIR || R.tReplaceable[b]) && R.tSolid[g] && R.tOpaque[g];
  };
  std::vector<ivec3> room, q;
  auto add = [&](ivec3 c) {
    if (std::max(std::abs(c.x - x), std::max(std::abs(c.y - y), std::abs(c.z - z))) > 4) return;
    for (const ivec3& p : room) if (p.x == c.x && p.y == c.y && p.z == c.z) return;
    if (!step(c.x, c.y, c.z)) return;
    room.push_back(c); q.push_back(c);
  };
  add(ivec3(x, y, z));
  for (size_t i = 0; i < q.size(); i++)
    for (int k = 0; k < 4; k++) for (int dy : {0, -1, 1}) {
      const ivec3 n = q[i] + ivec3(DIRX[k], dy, DIRZ[k]);
      if (doorAt(n.x, n.y, n.z)) continue;
      add(n);
    }
  int nx = x, ny = y, nz = z;
  if (genDoorBeside(P, x, y, z)) {
    bool found = false;
    for (int pass = 0; pass < 2 && !found; pass++)
      for (const ivec3& c : room) {
        if (c.x == x && c.y == y && c.z == z) continue;
        if (!genFree(P, c.x, c.y, c.z) || genDoorBeside(P, c.x, c.y, c.z)) continue;
        bool wall = false;
        for (int k = 0; k < 4; k++) {
          const BlockId wl = P.get(c.x + DIRX[k], c.y, c.z + DIRZ[k]);
          if (wl != NOBLOCK && R.tSolid[wl] && R.tOpaque[wl] && !R.block(wl).has("door")) wall = true;
        }
        if (pass == 0 && !wall) continue;
        nx = c.x; ny = c.y; nz = c.z; found = true; break;
      }
  }
  const int face = (genFaceRoom(P, room, nx, ny, nz) + 2) & 3;
  const int oldIdx = colIdx(x - P.bx, y, z - P.bz);
  json ent = nullptr;
  if (json* e = P.entity(oldIdx)) ent = *e;
  if (nx != x || ny != y || nz != z) P.put(x, y, z, "air");
  const int ni = P.putId(nx, ny, nz, STAND, (u8)face);
  if (ent.is_object() && ni >= 0) P.setEntity(ni, ent);
}

// a big windmill piece's sails, kept clear (terrain 22): its hub where it is or a block further out along its shaft,
// whichever has less of the piece in the sails' circle (the pack's gables overhang the hub, and some hang trapdoors, snow
// or a cap's edge there: the sails turned through them, or the mill counted itself walled in and stood still, sim/
// machines.cpp's 4-block rule); then whatever of the piece is still in the sails' sweep left out (an eave's notch, not sails
// through the roof)
struct MillFix { bool on = false, shift = false; int hub[3] = {0, 0, 0}, out[3] = {0, 0, 0}; std::set<long> skip; };
static MillFix millFix(const VPPiece& p, BlockId WINDMILL, BlockId AXLE) {
  MillFix f;
  if (WINDMILL == NOBLOCK || WINDMILL == AIR) return f;
  const VPBlock* hub = nullptr;
  for (const VPBlock& k : p.blocks) if (p.states[k.s].id == WINDMILL && (p.states[k.s].meta[0] & 4)) { hub = &k; break; }
  if (!hub) return f;
  const int ax = p.states[hub->s].meta[0] & 3;
  if (ax != 1 && ax != 2) return f;
  std::map<std::array<int, 3>, long> at;
  for (const VPBlock& k : p.blocks) {
    const VPState& s = p.states[k.s];
    if (s.kind == VPK_AIR || s.id == AIR || s.id == NOBLOCK) continue;
    at[{k.x, k.y, k.z}] = &k - p.blocks.data();
  }
  int d = 0;   // outward: away from the axle behind the hub
  for (int sgn : {1, -1}) {
    auto it = at.find({hub->x + (ax == 1 ? sgn : 0), hub->y, hub->z + (ax == 2 ? sgn : 0)});
    if (it != at.end() && p.states[p.blocks[it->second].s].id == AXLE) { d = -sgn; break; }
  }
  if (!d) return f;
  auto inPlane = [&](int off, int r2max, std::vector<long>* list) {   // the piece's blocks in the sails' circle (r2max), hub `off` out
    const int px = hub->x + (ax == 1 ? d * off : 0), pz = hub->z + (ax == 2 ? d * off : 0);
    int n = 0;
    for (int a = -7; a <= 7; a++) for (int b = -7; b <= 7; b++) {
      if ((!a && !b) || a * a + b * b > r2max) continue;
      const int x = ax == 1 ? px : px + a, z = ax == 2 ? pz : pz + a, y = hub->y + b;
      auto it = at.find({x, y, z});
      if (it == at.end()) continue;
      const BlockId id = p.states[p.blocks[it->second].s].id;
      if (id == WINDMILL || id == AXLE) continue;
      n++; if (list) list->push_back(it->second);
    }
    return n;
  };
  auto score = [&](int off) { return inPlane(off, 17, nullptr) * 1000 + inPlane(off, 56, nullptr); };   // (56: the big sails sweep 7.5 blocks out, render/vehicle_layer.cpp)
  const int ox = hub->x + (ax == 1 ? d : 0), oz = hub->z + (ax == 2 ? d : 0);
  const bool roomOut = ox >= 0 && ox < p.sx && oz >= 0 && oz < p.sz && !at.count({ox, hub->y, oz});
  f.on = true; f.hub[0] = hub->x; f.hub[1] = hub->y; f.hub[2] = hub->z;
  f.shift = roomOut && score(1) < score(0);
  if (f.shift) { f.out[0] = ax == 1 ? d : 0; f.out[2] = ax == 2 ? d : 0; }
  std::vector<long> in; inPlane(f.shift ? 1 : 0, 56, &in);
  for (long i : in) f.skip.insert(i);
  return f;
}

void jigsawTownBuild(SP& P, const Town& v, const JigsawTown& J) {
  const VStyle& st = vstyle(v.style);
  const int bx = P.bx, bz = P.bz;
  const BlockId DIRT = bid("dirt"), PLANKS = bid(st.planks == "smooth_sandstone" ? std::string("jungle_planks") : st.planks), PATH = bid("dirt_path");
  const BlockId WINDMILL = bid("windmill"), AXLE = bid("axle");
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
    const bool follow = pieceFollowsLand(p);
    // a lamp, a well, a fountain: one floor (the high ground under it). Each column of the
    // slope used to carry its own height, and the beam left the post.
    int rigidY = 0;
    if (p.terrain && !follow)
      rigidY = rigidFloorY(q.x0, q.z0, q.x1(), q.z1(), [&](int x, int z) { return P.ground(x, z); });
    if (!follow) {   // a rigid piece: the plants and snow and any tree out of its box first, and no lake or river in it
      const int y0 = p.terrain ? rigidY : q.y0;
      const BlockId TOPG = bid(vbase(v.style) == VS_DESERT ? "sand" : "grass");
      for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) {
        if (p.terrain) {
          const int g = P.ground(x, z);
          for (int y = std::max(1, g); y < y0; y++) P.putId(x, y, z, DIRT);   // the low side, filled up to the floor
        } else {
          for (int y = std::max(1, y0 - 6); y <= y0; y++) if (tLiquid(P.get(x, y, z))) P.putId(x, y, z, y == y0 ? TOPG : DIRT);
        }
        for (int y = y0 + 1; y <= std::min(DIM_H - 1, y0 + p.top + 2); y++) { const BlockId g = P.get(x, y, z); if (g != AIR && g != NOBLOCK && softBlock(g)) P.put(x, y, z, "air"); }
      }
    }
    const MillFix mf = J.millOut && !p.terrain ? millFix(p, WINDMILL, AXLE) : MillFix{};
    for (const VPBlock& k : p.blocks) {
      auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
      const int x = q.x0 + tx, z = q.z0 + tz;
      // (terrain 22) a big windmill's sails clear of the building (millFix): the hub a block further out along its shaft
      // when that's clearer (an axle where it was), and nothing of the piece left in the sails' way
      if (mf.on) {
        if (k.x == mf.hub[0] && k.y == mf.hub[1] && k.z == mf.hub[2]) {
          const int y = q.y0 + k.y;
          auto [hx0, hz0] = vpTurn(p, q.r, k.x + mf.out[0], k.z + mf.out[2]);
          const int hx = q.x0 + hx0, hz = q.z0 + hz0;
          if (y >= 1 && y < DIM_H) {
            if (mf.shift && P.in(x, z)) P.putId(x, y, z, AXLE, (u8)(p.states[k.s].meta[q.r] & 3));
            if (P.in(hx, hz)) P.putId(hx, y, hz, WINDMILL, p.states[k.s].meta[q.r]);
          }
          continue;
        }
        if (mf.skip.count(&k - p.blocks.data())) continue;
      }
      if (!P.in(x, z)) continue;
      const VPState& s = p.states[k.s];
      BlockId id = s.id; int y; int stairMeta = -1;
      if (follow) {
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
        y = (p.terrain ? rigidY : q.y0) + k.y;
        if (!p.terrain && k.y == 0) for (int yy = y - 1; yy > y - 8 && yy > 1 && softBlock(P.get(x, yy, z)) && P.get(x, yy, z) != NOBLOCK; yy--) P.putId(x, yy, z, DIRT);
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
      else if (s.kind == VPK_SPAWNER) P.setEntity(idx, json{{"type", "spawner"}, {"mob", s.items}});
      else if (s.kind == VPK_WARES) {   // the goods on show: "id" or "id*n", comma-separated, empty = a gap
        json items = json::array(); size_t a0 = 0;
        while (a0 <= s.items.size()) {
          size_t b0 = s.items.find(',', a0); if (b0 == std::string::npos) b0 = s.items.size();
          std::string id = s.items.substr(a0, b0 - a0); a0 = b0 + 1; int n = 1;
          if (const size_t st2 = id.find('*'); st2 != std::string::npos) { n = std::max(1, std::atoi(id.c_str() + st2 + 1)); id = id.substr(0, st2); }
          items.push_back(!id.empty() && reg().item(id) ? proceduralOf(id, n).toJson() : json(nullptr));
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
    {   // armour stands the piece brought: out of the doorway, facing into the room
      const BlockId STAND = bid("armor_stand");
      if (STAND != NOBLOCK && STAND != AIR) for (const VPBlock& k : p.blocks) {
        if (p.states[k.s].id != STAND) continue;
        auto [tx, tz] = vpTurn(p, q.r, k.x, k.z);
        const int x = q.x0 + tx, z = q.z0 + tz;
        if (!P.in(x, z)) continue;
        int y;
        if (follow) {
          int g = P.ground(x, z);
          if (g < SEA_LEVEL && tLiquid(P.get(x, g + 1, z))) g = SEA_LEVEL;
          y = g + k.y;
        } else y = (p.terrain ? rigidY : q.y0) + k.y;
        if (y < 1 || y >= DIM_H - 1 || P.get(x, y, z) != STAND) continue;
        fixGeneratedStand(P, x, y, z);
      }
    }
    // (terrain 24) a style's windmill with its millstone up a floor or more: a lift up to it, as ours has
    // (gen/village_mill_lift.cpp; the miller rides it, sim/villager_chores.cpp)
    if (v.genV >= 24 && !p.terrain) if (const auto ML = millLiftOf(p)) {
      auto at = [&](int lx, int lz) { auto [tx, tz] = vpTurn(p, q.r, lx, lz); return std::array<int, 2>{q.x0 + tx, q.z0 + tz}; };
      const auto S = at(ML->shaft[0], ML->shaft[2]), R = at(ML->rail[0], ML->rail[1]), T = at(ML->stop[0], ML->stop[1]);
      const int sy = q.y0 + ML->shaft[1], top = q.y0 + ML->top;
      if (P.in(S[0], S[1])) {
        const int i = P.put(S[0], sy, S[1], "slider", 1); if (i >= 0) P.setEntity(i, json{{"type", "machine_state"}, {"glue", 1 << 2}});
        const int j = P.put(S[0], sy + 1, S[1], hasBlock("spruce_slab") ? "spruce_slab" : st.slab, 0); if (j >= 0) P.setEntity(j, json{{"glue", 1 << 3}});
        for (int y = sy + 2; y <= top; y++) P.put(S[0], y, S[1], "air");   // (the shaft open through the floors between)
      }
      if (P.in(R[0], R[1])) for (int y = sy; y <= top; y++) P.put(R[0], y, R[1], "lift_rail");
      if (P.in(T[0], T[1])) { P.put(T[0], sy + 1, T[1], "lift_stop"); P.put(T[0], top, T[1], "lift_stop"); }
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
    if (v.hasShrine && hasBlock("respec_shrine") && P.in(v.shrine[0], v.shrine[2])) {   // (terrain 25)
      for (int y = v.shrine[1] + 1; y <= v.shrine[1] + 3; y++) P.put(v.shrine[0], y, v.shrine[2], "air");
      P.put(v.shrine[0], v.shrine[1], v.shrine[2], "respec_shrine");
    }
  }
  // the garden bridge's brook carried out of its piece into the land: from each end a stream (3 wide, at the brook's own
  // level) runs on outward, meandering a little, till rising ground or a drop ends it in a pond, or it's gone ~20 blocks;
  // never across a street or into a building, and banked wherever the land beside it is lower, so none of it spills.
  // Worked out the same in every chunk (from the piece and the terrain), carved where it falls in this one
  {
    const BlockId WATER = bid("water"), BANK = bid("dirt"), BED = bid("gravel"), STONE = bid("mossy_cobblestone");
    auto blocked = [&](int x, int z) {
      if (J.street.count(colKey(x, z))) return true;
      for (auto& b : J.boxes) if (x >= b[0] && x <= b[2] && z >= b[1] && z <= b[3]) return true;
      return false;
    };
    for (const JPlaced& q : J.pcs) {
      const VPPiece& p = *q.p;
      if (p.name.find("garden_bridge") == std::string::npos) continue;
      const int wl = q.y0;
      auto inPiece = [&](int x, int z) { return x >= q.x0 && x <= q.x1() && z >= q.z0 && z <= q.z1(); };   // (the bridge's own: left as built)
      for (int end : {0, 1}) {
        // the brook's end row (local z 0 or sz-1, x 5..7) and the way out from it
        const int lz = end ? p.sz - 1 : 0;
        auto [cx0, cz0] = vpTurn(p, q.r, 6, lz);
        auto [ix, iz] = vpTurn(p, q.r, 6, end ? lz - 1 : lz + 1);
        int dx = cx0 - ix, dz = cz0 - iz;   // (outward)
        const int sx = -dz, sz = dx;          // (across it)
        int x = q.x0 + cx0, z = q.z0 + cz0;
        Mulberry32 mr(hash32((i32)v.seed, x, z, 91));
        auto carve = [&](int cx, int cz, bool pond) {
          for (int k = pond ? -2 : -1; k <= (pond ? 2 : 1); k++) for (int k2 = pond ? -2 : 0; k2 <= (pond ? 2 : 0); k2++) {
            const int wx = cx + sx * k + dx * k2, wz = cz + sz * k + dz * k2;
            if (!P.in(wx, wz) || blocked(wx, wz) || inPiece(wx, wz)) continue;
            P.putId(wx, wl - 1, wz, BED); P.putId(wx, wl, wz, WATER);
            for (int y = wl + 1; y <= wl + 4; y++) { const BlockId u = P.get(wx, y, wz); if (u != AIR && u != NOBLOCK && !tLiquid(u)) P.put(wx, y, wz, "air"); }
          }
          for (int k : {pond ? -3 : -2, pond ? 3 : 2}) {   // its banks: built up where the land beside is lower
            for (int k2 = pond ? -3 : 0; k2 <= (pond ? 3 : 0); k2++) {
              const int wx = cx + sx * k + dx * k2, wz = cz + sz * k + dz * k2;
              if (!P.in(wx, wz) || blocked(wx, wz) || inPiece(wx, wz)) continue;
              for (int y = P.ground(wx, wz) + 1; y <= wl; y++) P.putId(wx, y, wz, y == wl ? BANK : STONE);
            }
          }
        };
        // the end row itself opened to the stream
        int steps = 0;
        { const int g0 = P.ground(x + dx, z + dz); if (blocked(x + dx, z + dz) || g0 < wl - 1 || g0 > wl + 2) continue; }   // (nowhere to run: its end stays closed)
        for (int k = -1; k <= 1; k++) { const int wx = x + sx * k, wz = z + sz * k; if (P.in(wx, wz)) { P.putId(wx, wl, wz, WATER); P.put(wx, wl + 1, wz, "air"); } }
        for (; steps < 22; steps++) {
          const int nx = x + dx, nz = z + dz;
          const int g = P.ground(nx, nz);
          if (blocked(nx, nz) || g < wl - 1 || g > wl + 2) break;   // (a street, a building, a drop or a rise: the pond here)
          x = nx; z = nz;
          carve(x, z, false);
          if (steps > 3 && mr() < 0.18) {   // (a bend now and then: it wanders, but on)
            const int t = mr() < 0.5 ? 1 : -1;
            const int ndx = dx == 0 ? t : dx, ndz = dz == 0 ? t : dz;
            if (!blocked(x + (dx == 0 ? t : 0), z + (dz == 0 ? t : 0))) { const int bxx = x + (dx == 0 ? t : 0), bzz = z + (dz == 0 ? t : 0); x = bxx; z = bzz; carve(x, z, false); }
            (void)ndx; (void)ndz;
          }
        }
        if (steps > 0) carve(x, z, true);   // where it ends: a pond (reeds and lily pads about it); hemmed in at once, none
        for (auto [ox, oz] : {std::pair<int, int>{2, 0}, {-2, 1}, {1, -2}}) {
          const int wx = x + sx * ox + dx * oz, wz = z + sz * ox + dz * oz;
          if (steps > 0 && P.in(wx, wz) && !blocked(wx, wz) && !inPiece(wx, wz) && P.get(wx, wl, wz) == WATER && mr() < 0.7) P.put(wx, wl + 1, wz, "lily_pad");
        }
      }
    }
  }
}

} // namespace dq
