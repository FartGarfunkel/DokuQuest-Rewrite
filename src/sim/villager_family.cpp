// sim/villager_family.cpp — love and families in the villages. Two who have grown fond of each other (sim/
// villager_social.cpp: 75 or more, both grown and both free) start courting and become a couple; the one moves into the
// other's home. A couple may have a child (now and then, a few to a home); children live with their parents, go to
// lessons at the square in the morning, play about the village after, and after some days are grown — they take up a
// trade of the village's, or none, and can fall in love in their turn. All of it kept with the village
// (villageRec(...): "couples" {i: partner}, "kids" [{i, n, p: [a, b], s: look seed, born, grown, j}]) — the family tree,
// shown when you talk to them (ui/villager_screens.cpp).
#include "sim/villagers_internal.h"
#include "sim/creature.h"
#include <set>

namespace dq {
namespace {
float rnd() { return frand(); }
constexpr int KID0 = 500, GROW_DAYS = 6;
json& couples(Level& lv, const std::string& vid) { json& c = villageRec(lv, vid)["couples"]; if (!c.is_object()) c = json::object(); return c; }
}  // namespace

json& villageKids(Level& lv, const std::string& vid) { json& k = villageRec(lv, vid)["kids"]; if (!k.is_array()) k = json::array(); return k; }
int npcCouple(Level& lv, const std::string& vid, int i) {
  const json& c = couples(lv, vid); const std::string k = std::to_string(i);
  return c.contains(k) && c[k].is_number() ? c[k].get<int>() : -1;
}
const json* npcKid(Level& lv, const std::string& vid, int i) {
  for (auto& k : villageKids(lv, vid)) if (k.value("i", -1) == i) return &k;
  return nullptr;
}
bool npcIsChild(Level& lv, const std::string& vid, int i) { const json* k = i >= KID0 ? npcKid(lv, vid, i) : nullptr; return k && !k->value("grown", false); }
std::vector<int> npcParents(Level& lv, const std::string& vid, int i) {
  std::vector<int> out;
  if (const json* k = i >= KID0 ? npcKid(lv, vid, i) : nullptr) for (auto& p : jarr(*k, "p")) out.push_back(p.get<int>());
  return out;
}
std::vector<int> npcChildren(Level& lv, const std::string& vid, int i) {
  std::vector<int> out;
  for (auto& k : villageKids(lv, vid)) for (auto& p : jarr(k, "p")) if (p.get<int>() == i) { out.push_back(k.value("i", -1)); break; }
  return out;
}
// everyone who lives in the village (the folk, and those born there)
std::vector<int> villagePeople(Level& lv, const Village& v) {
  std::vector<int> out;
  for (int i = 0; i < (int)v.folk.size(); i++) out.push_back(i);
  for (auto& k : villageKids(lv, v.id)) out.push_back(k.value("i", -1));
  return out;
}
// where someone lives (their own home; moved in with their partner; a child with their parents)
bool npcHome(Level& lv, const Village& v, int i, ivec3& at, int& f, bool& second) {
  second = false;
  int who = i;
  if (i >= KID0) { const std::vector<int> ps = npcParents(lv, v.id, i); if (ps.empty()) return false; who = ps[0]; second = true; }
  const json& mv = villageRec(lv, v.id)["movedIn"];
  if (mv.is_object() && mv.contains(std::to_string(who)) && mv[std::to_string(who)].is_number()) { who = mv[std::to_string(who)].get<int>(); second = true; }
  if (who >= KID0) { const std::vector<int> ps = npcParents(lv, v.id, who); if (ps.empty()) return false; who = ps[0]; second = true; }
  if (who < 0 || who >= (int)v.folk.size()) return false;
  at = v.folk[who].home; f = v.folk[who].homeF;
  return true;
}
std::string npcPersonName(Level& lv, const Village& v, int i) {
  if (i >= KID0) { const json* k = npcKid(lv, v.id, i); return k ? k->value("n", std::string("a child")) : "someone"; }
  if (i >= 0 && i < (int)v.folk.size()) { for (auto& up : lv.entities()) if (const json* N = npcOf(*up); N && N->value("v", std::string()) == v.id && N->value("i", -1) == i) return N->value("n", std::string()); return npcName(npcSeedOf(v, i)); }
  return "someone";
}

// once a day for each village: courting turns to a couple, couples have children, children grow up
void familyDay(Level& lv, const Village& v) {
  json& rec = villageRec(lv, v.id);
  const i64 day = npcDay(lv);
  if (rec.value("famDay", (i64)-1) == day) return;
  rec["famDay"] = day;
  json& C = couples(lv, v.id);
  const std::vector<int> people = villagePeople(lv, v);
  // the grown, single ones fond enough of each other: a couple (the fondest pairs first)
  std::vector<std::tuple<int, int, int>> fond;
  for (size_t a = 0; a < people.size(); a++) for (size_t b = a + 1; b < people.size(); b++) {
    const int x = people[a], y = people[b];
    if (npcIsChild(lv, v.id, x) || npcIsChild(lv, v.id, y) || npcCouple(lv, v.id, x) >= 0 || npcCouple(lv, v.id, y) >= 0) continue;
    // (not family)
    const std::vector<int> px = npcParents(lv, v.id, x), py = npcParents(lv, v.id, y);
    bool kin = std::find(px.begin(), px.end(), y) != px.end() || std::find(py.begin(), py.end(), x) != py.end();
    for (int q : px) if (std::find(py.begin(), py.end(), q) != py.end()) kin = true;
    if (kin) continue;
    const int r = npcRelation(lv, v.id, x, y);
    if (r >= 75) fond.push_back({r, x, y});
  }
  std::sort(fond.begin(), fond.end(), [](auto& a, auto& b) { return std::get<0>(a) > std::get<0>(b); });
  std::set<int> taken;
  for (auto& [r, x, y] : fond) {
    if (taken.count(x) || taken.count(y) || rnd() > 0.5f) continue;
    taken.insert(x); taken.insert(y);
    C[std::to_string(x)] = y; C[std::to_string(y)] = x;
    // one moves in with the other (the one with a home of their own keeps it)
    json& mv = rec["movedIn"]; if (!mv.is_object()) mv = json::object();
    const int stays = x < (int)v.folk.size() ? x : y, moves = stays == x ? y : x;
    if (moves < (int)v.folk.size() || moves >= KID0) mv[std::to_string(moves)] = stays;
    DQ_LOG("village %s: %s and %s are a couple", v.name.c_str(), npcPersonName(lv, v, x).c_str(), npcPersonName(lv, v, y).c_str());
  }
  // a child, now and then (a few to a home, and not too many young ones in the village at once)
  json& K = villageKids(lv, v.id);
  int young = 0; for (auto& k : K) if (!k.value("grown", false)) young++;
  std::set<int> seen;
  for (auto& [k, val] : C.items()) {
    const int a = std::stoi(k), b = val.get<int>();
    if (seen.count(a) || seen.count(b)) continue;
    seen.insert(a); seen.insert(b);
    if (young >= std::max(2, (int)v.folk.size() / 3) || npcChildren(lv, v.id, a).size() >= 3 || rnd() > 0.22f) continue;
    const int n = (int)K.size();
    const u32 seed = (v.seed ^ 0x9e3779b9u) * 2654435761u + (u32)n * 7919u + (u32)day;
    const std::string name = npcName(seed);
    K.push_back(json{{"i", KID0 + n}, {"n", name}, {"p", {a, b}}, {"s", seed}, {"born", day}, {"grown", false}, {"j", "child"}});
    young++;
    DQ_LOG("village %s: %s and %s have a child, %s", v.name.c_str(), npcPersonName(lv, v, a).c_str(), npcPersonName(lv, v, b).c_str(), name.c_str());
    // the family are fond of the child, and it of them
    npcRelationAdd(lv, v.id, a, KID0 + n, 80); npcRelationAdd(lv, v.id, b, KID0 + n, 80);
  }
  // grown up: a trade of the village's (or none)
  for (auto& k : K) if (!k.value("grown", false) && day - k.value("born", day) >= GROW_DAYS) {
    k["grown"] = true;
    static const char* TRADES[] = {"farmer", "lumberjack", "fisherman", "miner", "shepherd", "none", "none"};
    k["j"] = TRADES[(u32)k.value("s", 0u) % 7];
    for (auto& up : lv.entities()) if (const json* N = npcOf(*up); N && N->value("v", std::string()) == v.id && N->value("i", -1) == k.value("i", -1)) up->removed = true;   // (they come back grown)
  }
}

// a child's day (sim/villager_ai.cpp brainInner, for those born here): lessons at the square in the morning, play after
OptMove childDay(Level& lv, Creature& e, NpcMind& M, const Village& v, int idx, double ph, float dt) {
  if (!npcIsChild(lv, v.id, idx)) return std::nullopt;
  if (npcNight(lv)) return std::nullopt;   // (home to bed as anyone: brainInner)
  const vec3 board = vec3(v.board) + vec3(0.5f, 0, 0.5f);
  if (ph < 0.3) {   // lessons: in a row before the board, facing it
    std::vector<int> kids; for (auto& k : villageKids(lv, v.id)) if (!k.value("grown", false)) kids.push_back(k.value("i", -1));
    const int seat = (int)(std::find(kids.begin(), kids.end(), idx) - kids.begin());
    const float side = (seat - (kids.size() - 1) * 0.5f) * 1.1f;
    const vec3 toSquare = glm::length(vec3(v.x, 0, v.z) - vec3(board.x, 0, board.z)) > 0.5f ? glm::normalize(vec3(v.x + 0.5f - board.x, 0, v.z + 0.5f - board.z)) : vec3(0, 0, 1);
    const vec3 across(toSquare.z, 0, -toSquare.x);
    const vec3 spot = board + toSquare * 2.6f + across * side;
    if (std::hypot(spot.x - e.pos.x, spot.z - e.pos.z) > 0.4f) if (OptMove m = npcGo(lv, e, M, vec3(spot.x, e.pos.y, spot.z), dt, e.T->speed * 0.5f)) return m;
    e.yaw = turnTo(e.yaw, std::atan2(-(board.x - e.pos.x), -(board.z - e.pos.z)), dt * 6);
    M.pose |= NP_READ;
    return Move{0, e.yaw};
  }
  // play: run about the square after each other (tag), now and then a wave
  if (!M.hasGoal || M.goalT <= 0 || std::hypot(M.goal.x - e.pos.x, M.goal.z - e.pos.z) < 0.7f) {
    Creature* other = nullptr; float best = 14;
    for (auto& up : lv.entities()) if (auto* o = dynamic_cast<Creature*>(up.get()); o && o != &e && !o->removed) if (const json* N = npcOf(*o); N && N->value("v", std::string()) == v.id && npcIsChild(lv, v.id, N->value("i", -1))) {
      const float d = glm::distance(o->pos, e.pos); if (d < best) { best = d; other = o; }
    }
    const bool chase = other && ((idx + (int)(npcPhase(lv) * 200)) % 2 == 0);
    M.goal = chase ? other->pos : vec3(v.x + 0.5f + (rnd() - 0.5f) * 14, e.pos.y, v.z + 0.5f + (rnd() - 0.5f) * 14);
    M.hasGoal = true; M.goalT = 4 + rnd() * 4;
    if (rnd() < 0.2f) M.waveT = 0.8f;
  }
  M.goalT -= dt;
  if (OptMove m = npcGo(lv, e, M, M.goal, dt, e.T->speed * 0.75f)) return m;   // (children run)
  return Move{0, e.yaw};
}

} // namespace dq
