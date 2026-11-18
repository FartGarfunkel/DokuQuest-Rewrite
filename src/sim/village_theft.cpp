// sim/village_theft.cpp — see village_theft.h.
// How a theft is found: the village's own containers near a player are watched (what's in each, by kind); when one holds
// less while a player stands at it, that player took the difference (whatever way: the chest's screen, the tray by hand,
// take-all, a hopper of theirs is not counted). Putting things back counts the other way. Blocks the village owns broken
// (its beds, stations, crops, containers) are thefts too. What a player placed in the village is kept as theirs.
#include "sim/village_theft.h"
#include "sim/villagers_internal.h"
#include "sim/creature.h"
#include "sim/events.h"
#include "sim/rpg.h"
#include "core/registry.h"
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>

namespace dq {

namespace {
std::string posKey(ivec3 p) { return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z); }

json& recOf(Player& p, const std::string& vid) {
  json& T = p.stats["theft"];
  if (!T.is_object()) T = json::object();
  json& r = T[vid];
  if (!r.is_object()) r = json{{"owed", json::object()}, {"value", 0.0}, {"wanted", false}, {"count", 0}, {"day", 0}};
  return r;
}
bool hasRec(Player& p, const std::string& vid) { return p.stats.contains("theft") && p.stats["theft"].is_object() && p.stats["theft"].contains(vid); }

// what a thing is worth to the village, in coins: its shops' prices, else a coin for every two
double worth(const std::string& id) {
  static std::map<std::string, double> M;
  if (M.empty()) {
    const json& SH = VD()["shops"];
    if (SH.is_object()) for (auto& [job, s] : SH.items()) for (const char* tab : {"buy", "sell"}) if (s.contains(tab) && s[tab].is_array())
      for (auto& l : s[tab]) if (l.is_array() && l.size() >= 3 && l[0].is_string()) {
        const double each = l[2].get<double>() / std::max(1, l[1].get<int>());
        double& m = M[l[0].get<std::string>()]; m = std::max(m, each);
      }
    M["gold_coin"] = 1;
  }
  auto it = M.find(id);
  return it != M.end() ? it->second : 0.5;
}

bool owned(Level& lv, const std::string& vid, ivec3 at) {
  const json& placed = villageRec(lv, vid)["placed"];
  return !(placed.is_object() && placed.contains(posKey(at)));
}
std::string villageOf(Level& lv, int realm, vec3 p) { VillageRef v = villageAtPos(lv, realm, p, 6); return v ? v->id : std::string(); }

bool isGuard(const Creature& c) { return c.type == "village_guard"; }
std::vector<Creature*> folkOf(Level& lv, const std::string& vid, vec3 near, float r) {
  std::vector<Creature*> out;
  for (auto& up : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(up.get()); const json* N = c ? npcOf(*c) : nullptr;
    if (!N || c->removed || !c->alive() || N->value("v", "") != vid) continue;
    if (glm::distance(c->pos, near) < r) out.push_back(c);
  }
  return out;
}
// a villager of the village who sees `p` (sneaking in the dark out of their eye, unseen)
Creature* witness(Level& lv, Player& p, const std::string& vid) {
  Creature* best = nullptr; float bd = 1e9f;
  for (Creature* c : folkOf(lv, vid, p.pos, 16)) {
    const float d = glm::distance(c->pos, p.pos);
    if (d < bd && c->lineOfSight(lv, p) && c->notices(lv, p, d)) { bd = d; best = c; }
  }
  return best;
}
void sendGuard(Level& lv, Player& p, const std::string& vid, Creature* fallback) {
  VillagerSystem* S = vsys(lv); if (!S) return;
  Creature* g = nullptr; float bd = 1e9f;
  for (Creature* c : folkOf(lv, vid, p.pos, 48)) if (isGuard(*c)) { const float d = glm::distance(c->pos, p.pos); if (d < bd) { bd = d; g = c; } }
  if (!g) g = fallback;
  if (!g) return;
  NpcMind& M = S->mind(g->id);
  M.confront = p.id; M.confrontT = 30; M.confrontSaid = false; M.confrontVictim = "theft"; M.foe = 0;
}
void attackers(Level& lv, Player& p, const std::string& vid) {
  VillagerSystem* S = vsys(lv); if (!S) return;
  for (Creature* c : folkOf(lv, vid, p.pos, 40)) if (isGuard(*c) || c->T->j.value("golem", false)) { NpcMind& M = S->mind(c->id); M.foe = p.id; M.foeT = 30; M.confront = 0; }
}

// a theft by `p` from the village: what went, and whether anyone saw
void stole(Level& lv, Player& p, const std::string& vid, const std::map<std::string, int>& what, ivec3 from) {
  if (what.empty()) return;
  json& r = recOf(p, vid);
  double val = 0;
  for (auto& [id, n] : what) { r["owed"][id] = r["owed"].value(id, 0) + n; val += worth(id) * n; }
  r["value"] = r.value("value", 0.0) + val;
  r["from"] = json::array({from.x, from.y, from.z});
  Creature* seen = witness(lv, p, vid);
  if (!seen) {   // unseen: missed later, a little standing
    npcRep(lv, p, vid, -1);
    later(lv, 40, p, "Someone in the village is asking who's been at their things.");
    return;
  }
  const i64 day = npcDay(lv);
  const bool recent = day - r.value("day", (i64)0) <= 2;
  r["day"] = day; r["seen"] = true;
  r["count"] = recent ? r.value("count", 0) + 1 : 1;
  npcRep(lv, p, vid, -std::min(10.0, 2 + val / 10), "theft");
  if (r.value("wanted", false) || r.value("count", 0) >= 3) {   // a known thief at it again: no more talking
    r["wanted"] = true;
    toast(lv, p, "Thief! The guards are coming for you!", "warn");
    attackers(lv, p, vid);
    return;
  }
  lv.fx->voice("villager_alarm", "Thief!", seen->pos + vec3(0, 1.8f, 0), 24);
  toast(lv, p, "You were seen stealing.", "warn");
  sendGuard(lv, p, vid, seen);
}

class VillageTheft : public LevelSystem {
public:
  explicit VillageTheft(Level& l) : lv(l) {
    if (!lv.events) return;
    // [player, x, y, z, key]: what a player puts down in a village is theirs
    hPlace = lv.events->on("blockPlace", [this](const json& a) {
      if (!a.is_array() || a.size() < 4 || lv.isRemote()) return true;
      Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p) return true;
      const ivec3 at(a[1].get<int>(), a[2].get<int>(), a[3].get<int>());
      const std::string vid = villageOf(lv, p->realm, vec3(at));
      if (!vid.empty()) { json& P = villageRec(lv, vid)["placed"]; if (!P.is_object()) P = json::object(); P[posKey(at)] = p->name; }
      return true;
    });
    // [player, x, y, z, key] before it goes: the village's beds, stations, crops and stores broken are taken
    hBreak = lv.events->on("blockBreak", [this](const json& a) {
      if (!a.is_array() || a.size() < 5 || lv.isRemote()) return true;
      Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p) return true;
      const ivec3 at(a[1].get<int>(), a[2].get<int>(), a[3].get<int>());
      const std::string vid = villageOf(lv, p->realm, vec3(at));
      if (vid.empty()) return true;
      World& w = lv.world(p->realm);
      const BlockId id = w.getBlock(at.x, at.y, at.z); const BlockDef& b = reg().block(id);
      json& P = villageRec(lv, vid)["placed"];
      if (P.is_object() && P.contains(posKey(at))) { P.erase(posKey(at)); return true; }   // (yours)
      const bool theirs = b.has("container") || b.has("bed") || b.has("station") || b.has("table") || b.has("furnace") || b.has("crop") || b.key.find("_bed") != std::string::npos;
      if (!theirs) return true;
      std::map<std::string, int> what; what[b.drop.empty() ? b.key : b.drop] += 1;
      if (const json* e = w.blockEntity(at.x, at.y, at.z); e && e->contains("items") && (*e)["items"].is_array())
        for (auto& it : (*e)["items"]) { ItemStack q = ItemStack::fromJson(it); if (!q.empty()) what[q.id] += q.n; }
      seen.erase(key(p->realm, at));
      stole(lv, *p, vid, what, at);
      return true;
    });
  }
  ~VillageTheft() override { if (lv.events) { lv.events->off(hPlace); lv.events->off(hBreak); } }
  const char* name() const override { return "villageTheft"; }

  void tick(Level&, double dt) override {
    if (lv.isRemote()) return;
    acc += dt; if (acc < 0.25) return; acc = 0;
    for (auto& up : lv.entities()) {
      auto* p = dynamic_cast<Player*>(up.get());
      if (!p || p->removed || p->dead) continue;
      const std::string vid = villageOf(lv, p->realm, p->pos);
      if (std::getenv("DQ_THEFT_TRACE")) fprintf(stderr, "THEFT %s at %.0f %.0f %.0f village '%s'\n", p->name.c_str(), p->pos.x, p->pos.y, p->pos.z, vid.c_str());
      if (vid.empty()) continue;
      watch(*p, vid);
      // wanted: its guards set on you when they see you; a few days and it's let go
      if (hasRec(*p, vid)) {
        json& r = recOf(*p, vid);
        if (r.value("wanted", false)) {
          if (npcDay(lv) - r.value("day", (i64)0) > 3) theftForgive(lv, *p, vid);
          else if (VillagerSystem* S = vsys(lv)) for (Creature* c : folkOf(lv, vid, p->pos, 18)) if (isGuard(*c) && c->lineOfSight(lv, *p)) { NpcMind& M = S->mind(c->id); if (M.foe != p->id) { M.foe = p->id; M.foeT = 20; } }
        }
      }
    }
  }

private:
  Level& lv; int hPlace = -1, hBreak = -1; double acc = 0;
  std::map<std::string, std::map<std::string, int>> seen;   // what each watched container held (realm|x,y,z)
  static std::string key(int realm, ivec3 at) { return std::to_string(realm) + "|" + posKey(at); }

  void watch(Player& p, const std::string& vid) {
    World& w = lv.world(p.realm);
    const ivec3 c = ivec3(glm::floor(p.pos));
    for (int dx = -5; dx <= 5; dx++) for (int dy = -3; dy <= 4; dy++) for (int dz = -5; dz <= 5; dz++) {
      const ivec3 at = c + ivec3(dx, dy, dz);
      const BlockId id = w.getBlock(at.x, at.y, at.z);
      if (!id || !reg().block(id).has("container")) continue;
      const json* e = w.blockEntity(at.x, at.y, at.z);
      if (!e || !e->contains("items") || !(*e)["items"].is_array()) continue;
      std::map<std::string, int> now;
      for (auto& it : (*e)["items"]) { ItemStack q = ItemStack::fromJson(it); if (!q.empty()) now[q.id] += q.n; }
      const std::string k = key(p.realm, at);
      auto it = seen.find(k);
      if (it == seen.end()) { seen[k] = now; continue; }   // (first sight: what it holds now)
      const std::map<std::string, int> before = it->second;
      it->second = now;
      if (!owned(lv, vid, at)) continue;
      // nearest player at it is the one who took it
      Player* by = &p; float bd = glm::distance(p.pos, vec3(at) + vec3(0.5f));
      for (auto& up : lv.entities()) if (auto* q = dynamic_cast<Player*>(up.get()); q && q != &p && !q->dead && q->realm == p.realm) { const float d = glm::distance(q->pos, vec3(at) + vec3(0.5f)); if (d < bd) { bd = d; by = q; } }
      if (by != &p || bd > 6) continue;
      std::map<std::string, int> took;
      for (auto& [id2, n] : before) { const int left = now.count(id2) ? now[id2] : 0; if (left < n) took[id2] = n - left; }
      for (auto& [id2, n] : now) {   // put back: off what's owed
        const int had = before.count(id2) ? before.at(id2) : 0;
        if (n > had && hasRec(p, vid)) { json& o = recOf(p, vid)["owed"]; if (o.contains(id2)) { const int left = o.value(id2, 0) - (n - had); if (left > 0) o[id2] = left; else o.erase(id2); } }
      }
      stole(lv, p, vid, took, at);
    }
  }
};
}

int theftFine(Player& p, const std::string& vid) {
  if (!hasRec(p, vid)) return 0;
  json& r = recOf(p, vid);
  if (r["owed"].empty() && !r.value("wanted", false)) return 0;
  const int fine = std::max(5, (int)std::ceil(r.value("value", 0.0) * 2));
  return hasTal(p, "lightfingers") ? std::max(3, fine / 2) : fine;   // the Rogue's Light Fingers
}
bool theftWanted(Player& p, const std::string& vid) { return hasRec(p, vid) && recOf(p, vid).value("wanted", false); }
std::string theftWhat(Player& p, const std::string& vid) {
  if (!hasRec(p, vid)) return "";
  std::string s; const json& o = recOf(p, vid)["owed"]; size_t i = 0;
  for (auto& [id, n] : o.items()) {
    const ItemDef* d = reg().item(id);
    s += (i ? (i + 1 == o.size() ? " and " : ", ") : "") + std::to_string(n.get<int>()) + " " + (d ? d->name : id);
    i++;
  }
  return s.empty() ? "what you took" : s;
}
bool theftGiveBack(Level& lv, Player& p, const std::string& vid) {
  if (!hasRec(p, vid)) return false;
  json& r = recOf(p, vid);
  json owed = r["owed"];
  if (owed.empty()) return false;
  World& w = lv.world(p.realm);
  json* box = nullptr;
  if (r.contains("from") && r["from"].is_array()) { const json& f = r["from"]; json* e = w.blockEntity(f[0].get<int>(), f[1].get<int>(), f[2].get<int>()); if (e && e->contains("items")) box = e; }
  double missing = 0;
  for (auto& [id, nj] : owed.items()) {
    const int n = nj.get<int>(), have = std::min(n, countOf(p, id));
    if (have > 0) {
      removeOf(p, id, have);
      if (box) {   // (back in the chest it came out of, as much as fits)
        int left = have;
        for (auto& it : (*box)["items"]) { if (left <= 0) break; ItemStack q = ItemStack::fromJson(it); if (q.id == id && q.n < q.maxStack()) { const int mv = std::min(left, q.maxStack() - q.n); q.n += mv; left -= mv; it = q.toJson(); } }
        for (auto& it : (*box)["items"]) { if (left <= 0) break; if (it.is_null() || ItemStack::fromJson(it).empty()) { ItemStack q(id, std::min(left, ItemStack(id, 1).maxStack())); left -= q.n; it = q.toJson(); } }
      }
    }
    missing += worth(id) * (n - have);
  }
  if (box) { const json& f = r["from"]; w.blockEntityChanged(f[0].get<int>(), f[1].get<int>(), f[2].get<int>()); }
  const int rest = missing > 0 ? std::max(1, (int)std::ceil(missing * 2)) : 0;
  if (rest > 0 && !payCoins(lv, p, rest)) { npcRep(lv, p, vid, -2, "theft"); toast(lv, p, "You couldn't give it all back.", "warn"); }
  npcRep(lv, p, vid, 1 + std::min(5.0, r.value("value", 0.0) / 20), "theft");
  theftForgive(lv, p, vid);
  return true;
}
bool theftPayFine(Level& lv, Player& p, const std::string& vid) {
  const int fine = theftFine(p, vid);
  if (fine <= 0) return true;
  if (!payCoins(lv, p, fine)) return false;
  theftForgive(lv, p, vid);
  return true;
}
void theftRefuse(Level& lv, Player& p, const std::string& vid) {
  json& r = recOf(p, vid);
  r["wanted"] = true; r["day"] = npcDay(lv);
  npcRep(lv, p, vid, -5, "theft");
  attackers(lv, p, vid);
}
void theftForgive(Level& lv, Player& p, const std::string& vid) {
  (void)lv;
  if (p.stats.contains("theft") && p.stats["theft"].is_object()) p.stats["theft"].erase(vid);
}
double shopPriceFactor(Player& p, const std::string& vid) { return standingOf(repOf(p, vid)).k == 0 ? 1.5 : 1.0; }

void registerVillageTheft() { registerLevelSystem([](Level& lv) { return std::make_shared<VillageTheft>(lv); }); }

} // namespace dq
