// sim/villager_social.cpp — the villagers' free time together. In their free hours, two to four of them stop where they
// are, turn to face each other and talk — one at a time, each line a bubble over the speaker's head (render/
// villager_layer.cpp), the next waiting for the last to finish — threads written in data/villager_talk.json about the
// raids, the golem, bad trades, village life, gossip; or they joke, share a drink in the evening, or argue. What they
// say to each other moves how they feel about each other (villageRec(...)["rel"]: -100 .. 100 a pair): friends and
// rivals, enemies; and friends who are all friends together become a clan, named for the village's old families.
#include "sim/villagers_internal.h"
#include "sim/creature.h"
#include "common/fs.h"
#include <cstdlib>
#include <map>
#include <set>

namespace dq {
namespace {
float rnd() { return frand(); }
const json& TD() {
  static const json j = [] { auto r = readJson(dataDir() / "villager_talk.json"); if (!r) DQ_WARN("villagers: data/villager_talk.json missing"); return r ? *r : json::object(); }();
  return j;
}
struct VTalk {
  std::string vid, kind; int thread = 0;
  std::vector<u32> who; std::vector<int> idx;   // the people (entity ids) and their places in the village
  vec3 centre{0};
  int line = -1; float lineT = 0, gatherT = 8, life = 0;
  bool seated = false;   // on the benches: nobody walks, they turn their heads
  bool done = false;
};
std::map<const Level*, std::vector<VTalk>>& talks() { static auto* m = new std::map<const Level*, std::vector<VTalk>>(); return *m; }
std::map<u32, size_t>& inTalk() { static auto* m = new std::map<u32, size_t>(); return *m; }   // entity id -> its talk (index)

std::string pairKey(int a, int b) { if (a > b) std::swap(a, b); return std::to_string(a) + ":" + std::to_string(b); }
}  // namespace

// ---- how they feel about each other
int npcRelation(Level& lv, const std::string& vid, int a, int b) {
  if (a == b) return 100;
  const json& r = villageRec(lv, vid)["rel"];
  const std::string k = pairKey(a, b);
  return r.is_object() && r.contains(k) && r[k].is_number() ? r[k].get<int>() : 0;
}
void npcRelationAdd(Level& lv, const std::string& vid, int a, int b, int d) {
  if (a == b) return;
  json& r = villageRec(lv, vid)["rel"];
  if (!r.is_object()) r = json::object();
  const std::string k = pairKey(a, b);
  r[k] = std::clamp((r.contains(k) && r[k].is_number() ? r[k].get<int>() : 0) + d, -100, 100);
}
const char* npcRelationWord(int r) { return r >= 70 ? "close friends" : r >= 35 ? "friends" : r > -20 ? "acquaintances" : r > -55 ? "rivals" : "enemies"; }

// ---- clans: friends who are all friends together (each two in it at 35 or better), three or more, named for an old family
std::vector<NpcClan> villageClans(Level& lv, const Village& v, const std::vector<int>& people) {
  std::vector<NpcClan> out;
  std::set<int> used;
  const json& names = jarr(TD(), "clans");
  for (int a : people) {
    if (used.count(a)) continue;
    std::vector<int> g = {a};
    for (int b : people) {
      if (b == a || used.count(b)) continue;
      bool all = true; for (int c : g) if (npcRelation(lv, v.id, b, c) < 35) { all = false; break; }
      if (all) g.push_back(b);
    }
    if (g.size() < 3) continue;
    for (int c : g) used.insert(c);
    NpcClan cl; cl.members = g;
    u32 h = v.seed * 2654435761u; for (int c : g) h = h * 31 + (u32)c;
    cl.name = names.empty() ? "the clan" : "the " + names[h % names.size()].get<std::string>() + " clan";
    out.push_back(cl);
  }
  return out;
}

// ---- the talks
static std::string fill(const std::string& t, Level& lv, const VTalk& T, const Village& v) {
  std::string s = t;
  for (size_t k = 0; k < T.who.size(); k++) {
    const std::string tok = "{" + std::to_string(k) + "}";
    Entity* e = lv.entity(T.who[k]); const json* N = e ? npcOf(*e) : nullptr;
    const std::string nm = N ? N->value("n", std::string("you")) : "you";
    for (size_t p; (p = s.find(tok)) != std::string::npos;) s.replace(p, tok.size(), nm);
  }
  std::string golem = "the golem";
  for (auto& up : lv.entities()) if (const json* N = npcOf(*up); N && N->value("v", std::string()) == v.id && N->value("i", 0) >= 200 && N->value("i", 0) < 300) { golem = N->value("n", golem); break; }
  for (size_t p; (p = s.find("{golem}")) != std::string::npos;) s.replace(p, 7, golem);
  for (size_t p; (p = s.find("{village}")) != std::string::npos;) s.replace(p, 9, v.name);
  return s;
}

static void say(Level& lv, Creature& e, const std::string& text, float dur) {
  json& N = e.data["npc"];
  json& b = N["say"];
  static int serial = 0;   // (every line its own number: a talk's first line never matches the last talk's, which hid it)
  b = json{{"t", text}, {"k", ++serial}, {"d", dur}};
  npcSay(lv, e, text, 0.45f);
}
static void unsay(Creature& e) { if (e.data.contains("npc") && e.data["npc"].contains("say")) e.data["npc"].erase("say"); }

static void endTalk(Level& lv, VTalk& T) {
  if (T.done) return;
  T.done = true;
  if (T.line >= 0) {   // (finished, or near enough: it counts)
    const int d = T.kind == "joke" ? 4 : T.kind == "drink" ? 6 : T.kind == "love" ? 8 : T.kind == "argue" ? -9 : T.kind == "gossip" ? 2 : 1;
    for (size_t a = 0; a < T.idx.size(); a++) for (size_t b = a + 1; b < T.idx.size(); b++) npcRelationAdd(lv, T.vid, T.idx[a], T.idx[b], d);
  }
  for (u32 id : T.who) { inTalk().erase(id); if (auto* c = dynamic_cast<Creature*>(lv.entity(id))) { unsay(*c); if (c->heldItem == "glass_bottle" || c->heldItem == "honey_bottle") c->heldItem.clear(); } }
}

// someone free starts a talk with whoever is near and free too
static void startTalk(Level& lv, Creature& e, const Village& v, int idx, bool seated) {
  VillagerSystem* S = vsys(lv);
  if (!S) return;
  std::vector<std::pair<float, Creature*>> near;
  for (auto& up : lv.entities()) {
    auto* o = dynamic_cast<Creature*>(up.get()); const json* ON = o ? npcOf(*o) : nullptr;
    if (!ON || o == &e || o->removed || !o->alive() || o->type != "villager" || ON->value("v", "") != v.id || inTalk().count(o->id)) continue;
    const int oi = ON->value("i", 0);
    if (oi >= 100 && oi < 500) continue;   // (guards, golems, the lost, travellers)
    if (ON->value("p", 0) & (NP_SLEEP | NP_WORK)) continue;
    const NpcMind& OM = S->mind(o->id);
    if (OM.holdT > 0) continue;
    if (!seated && S->now - OM.aboutAt > 1.5) continue;   // (on their way somewhere: not stopped by a talk)
    const bool oSeated = (ON->value("p", 0) & NP_SIT) != 0;
    if (oSeated != seated) continue;   // (those sitting talk with who's sat by them; those about, with who's about)
    const float d = std::hypot(o->pos.x - e.pos.x, o->pos.z - e.pos.z);
    if (d < (seated ? 3.6f : 16) && std::fabs(o->pos.y - e.pos.y) < 2) near.push_back({d, o});
  }
  if (near.empty()) return;
  std::sort(near.begin(), near.end(), [](auto& a, auto& b) { return a.first < b.first; });
  const int want = 1 + (int)(rnd() * 3);   // two to four in all
  VTalk T; T.vid = v.id;
  T.who.push_back(e.id); T.idx.push_back(idx);
  for (int k = 0; k < (int)near.size() && (int)T.who.size() < want + 1; k++) { T.who.push_back(near[k].second->id); T.idx.push_back(npcOf(*near[k].second)->value("i", 0)); }
  // what about: how they feel about each other first
  float sum = 0; int n = 0;
  for (size_t a = 0; a < T.idx.size(); a++) for (size_t b = a + 1; b < T.idx.size(); b++) { sum += npcRelation(lv, v.id, T.idx[a], T.idx[b]); n++; }
  const float avg = n ? sum / n : 0;
  const double ph = npcPhase(lv);
  std::string kind;
  const json& rec = villageRec(lv, v.id);
  const bool lovers = T.idx.size() == 2 && npcCouple(lv, v.id, T.idx[0]) == T.idx[1];
  if (lovers && rnd() < 0.6f) kind = "love";
  else if (T.idx.size() == 2 && npcRelation(lv, v.id, T.idx[0], T.idx[1]) >= 60 && npcCouple(lv, v.id, T.idx[0]) < 0 && npcCouple(lv, v.id, T.idx[1]) < 0 && rnd() < 0.35f) kind = "love";   // (fond of each other: a little courting)
  else if (avg <= -25 || (avg < 10 && rnd() < 0.12f)) kind = "argue";
  else {
    const float r = rnd();
    const bool raided = rec.contains("lastRaidDay") && npcDay(lv) - rec.value("lastRaidDay", (i64)-99) < 4;
    if (r < 0.16f && ph >= 0.4) kind = "drink";
    else if (r < 0.34f) kind = "joke";
    else if (r < 0.48f) kind = "gossip";
    else if (r < 0.60f && (raided || rnd() < 0.4f)) kind = "raid";
    else if (r < 0.72f && v.golems > 0) kind = "golem";
    else if (r < 0.84f) kind = "trades";
    else kind = "life";
  }
  // a thread of that kind for this many (fewer people than its speakers won't do)
  std::vector<int> pick;
  const json& th = jarr(TD(), "threads");
  for (int i = 0; i < (int)th.size(); i++) if (th[i].value("kind", "") == kind && th[i].value("n", 2) <= (int)T.who.size()) pick.push_back(i);
  if (pick.empty()) { for (int i = 0; i < (int)th.size(); i++) if (th[i].value("kind", "") == "life" && th[i].value("n", 2) <= (int)T.who.size()) pick.push_back(i); kind = "life"; }
  if (pick.empty()) return;
  T.kind = kind; T.thread = pick[(size_t)(rnd() * pick.size()) % pick.size()];
  // as many as it has speakers
  const int sp = th[T.thread].value("n", 2);
  T.who.resize(sp); T.idx.resize(sp);
  vec3 c(0); for (u32 id : T.who) c += lv.entity(id)->pos; T.centre = c / (float)T.who.size();
  T.seated = seated;
  auto& L = talks()[&lv];
  L.push_back(T);
  for (u32 id : T.who) inTalk()[id] = L.size() - 1;
  if (std::getenv("DQ_DEV_TALKLOG")) DQ_LOG("village %s: a talk (%s, %d people)", v.name.c_str(), kind.c_str(), (int)T.who.size());
  if (kind == "drink") for (u32 id : T.who) if (auto* o = dynamic_cast<Creature*>(lv.entity(id))) o->heldItem = "honey_bottle";   // (a mug each)
}

// the talk's turns go on (every villager's brain calls it; the first of them each tick moves it on)
OptMove npcSocial(Level& lv, Creature& e, NpcMind& M, const Village& v, int idx, float dt, bool seated) {
  auto it = inTalk().find(e.id);
  if (it == inTalk().end()) {
    if (rnd() < dt * (seated ? 0.18f : 0.12f)) startTalk(lv, e, v, idx, seated);
    it = inTalk().find(e.id);
    if (it == inTalk().end()) return std::nullopt;
  }
  auto& L = talks()[&lv];
  if (it->second >= L.size()) { inTalk().erase(it); return std::nullopt; }
  VTalk& T = L[it->second];
  if (T.done) { inTalk().erase(e.id); return std::nullopt; }
  const json& th = jarr(TD(), "threads")[T.thread];
  const json& lines = jarr(th, "lines");
  const size_t me = (size_t)(std::find(T.who.begin(), T.who.end(), e.id) - T.who.begin());
  // anyone gone (hurt, asleep, taken away): it ends here
  for (u32 id : T.who) { auto* o = dynamic_cast<Creature*>(lv.entity(id)); if (!o || o->removed || !o->alive() || o->panicT > 0) { endTalk(lv, T); return std::nullopt; } }
  // its place in the ring, facing the middle
  const float ang = (float)me / T.who.size() * TAU + (float)(T.thread % 7);
  const float rad = T.who.size() == 2 ? 0.75f : 1.05f;
  const vec3 spot = T.centre + vec3(std::cos(ang) * rad, 0, std::sin(ang) * rad);
  const float d = std::hypot(spot.x - e.pos.x, spot.z - e.pos.z);
  if (me == 0) {   // (the first moves it on, once a tick)
    T.life += dt;
    bool all = true; for (u32 id : T.who) { Entity* o = lv.entity(id); if (glm::distance(vec2(o->pos.x, o->pos.z), vec2(T.centre.x, T.centre.z)) > rad + 0.9f) all = false; }
    if (T.line < 0) { if (all || T.seated || (T.gatherT -= dt) <= 0) { T.line = 0; T.lineT = 0; } }
    if (T.line >= 0 && (T.lineT -= dt) <= 0) {
      if (T.line >= (int)lines.size()) { endTalk(lv, T); return std::nullopt; }
      const int who = lines[T.line][0].get<int>();
      const std::string text = fill(lines[T.line][1].get<std::string>(), lv, T, v);
      const float dur = 1.6f + text.size() * 0.055f;
      for (u32 id : T.who) if (auto* o = dynamic_cast<Creature*>(lv.entity(id))) unsay(*o);   // (one bubble at a time)
      if (auto* sp = dynamic_cast<Creature*>(lv.entity(T.who[std::min<size_t>(who, T.who.size() - 1)]))) say(lv, *sp, text, dur);
      T.lineT = dur + 0.35f; T.line++;
    }
    if (T.life > 90) { endTalk(lv, T); return std::nullopt; }
  }
  if (T.seated) {   // sat: a turn toward who's speaking, no more (they stay on the seat)
    vec3 look = T.centre;
    if (T.line > 0) { const int who = lines[std::min<int>(T.line - 1, (int)lines.size() - 1)][0].get<int>(); if (who != (int)me) if (Entity* sp = lv.entity(T.who[std::min<size_t>(who, T.who.size() - 1)])) look = sp->pos; }
    float want = std::atan2(-(look.x - e.pos.x), -(look.z - e.pos.z)), dyw = want - e.yaw;
    while (dyw > PI) dyw -= TAU; while (dyw < -PI) dyw += TAU;
    e.yaw += std::clamp(dyw, -0.7f, 0.7f);
    M.pose |= NP_TALK;
    return Move{0, e.yaw};
  }
  if (d > 0.35f && T.line < 0) {   // gathering
    if (OptMove m = npcGo(lv, e, M, vec3(spot.x, e.pos.y, spot.z), dt, e.T->speed * 0.4f)) return m;
  }
  // face whoever is speaking (or the middle)
  vec3 look = T.centre;
  if (T.line > 0) { const int who = lines[std::min<int>(T.line - 1, (int)lines.size() - 1)][0].get<int>(); if (who != (int)me) if (Entity* sp = lv.entity(T.who[std::min<size_t>(who, T.who.size() - 1)])) look = sp->pos; }
  e.yaw = turnTo(e.yaw, std::atan2(-(look.x - e.pos.x), -(look.z - e.pos.z)), dt * 6);
  M.pose |= NP_TALK;
  if (T.kind == "argue" && T.line > 0) M.waveT = 0.3f;   // (arms about)
  return Move{0, e.yaw};
}

bool npcInTalk(u32 id) { return inTalk().count(id) > 0; }
void npcSocialForget(const Level& lv) { talks().erase(&lv); inTalk().clear(); }

} // namespace dq
