// sim/villager_quests.cpp — what the villagers have for you: errands and the quest board's bounties (villagers.js,
// with the board's additions from dungeons.js, rpg.js scout/word, livingworld.js named foes and hearth.js the
// missing; fairplay.js lapses; together.js shared jobs), the shops, the conversation (NpcTalk and everything that
// adds to it: riftarc.js, riftlore.js, hearth.js, paths.js, rpg.js) and paths.js's path choice and talents.
#include "sim/villagers_internal.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "sim/magic_data.h"
#include "sim/mob_combat.h"
#include "gen/overworld.h"
#include "gen/structures.h"
#include "gen/generator.h"
#include "world/dims.h"
#include "core/registry.h"
#include <set>

namespace dq {

static float rnd() { return frand(); }
static Mulberry32 questRand(i32 a, i32 b, i32 c, i32 d) { return Mulberry32(hash32(a, b, c, d)); }
// livingworld.js eliteMix: its own mix of the village seed and the period
static u32 eliteMix(u32 s, i64 p, int salt) {
  double t = std::trunc((double)(i32)p + (double)salt * 1013904242.0);   // (p | 0) + salt * 0x3c6ef372, then ToInt32 in Math.imul
  double m = std::fmod(t, 4294967296.0); if (m < 0) m += 4294967296.0;
  u32 h = (u32)(((u32)s ^ 0x5bd1e995u) * 0x27d4eb2du) ^ (u32)((u32)(u64)m * 0x85ebca6bu);
  h ^= h >> 15; h *= 0xc2b2ae35u; h ^= h >> 13;
  return h;
}
static std::string plural(int n, const char* w) { return std::to_string(n) + " " + w + (n == 1 ? "" : "s"); }
static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string noArticle(const std::string& s) {
  for (const char* a : {"an ", "a ", "the ", "An ", "A ", "The "}) if (s.rfind(a, 0) == 0) return s.substr(strlen(a));
  return s;
}

bool hasShop(const std::string& job) { const json& S = VD()["shops"]; return S.contains(job); }
static const json& famOf(const std::string& fam) { static const json E = json::array(); const json& H = VD()["huntFamilies"]; return H.contains(fam) ? H[fam] : E; }

// ------------------------------------------------------------------ quests
json npcQuestFor(Level& lv, const Village& v, int i) {
  if (i < 0 || i >= (int)v.folk.size()) return nullptr;
  const VFolk& f = v.folk[i];
  const i64 day = npcDay(lv);
  Mulberry32 r = questRand((i32)v.seed, i, (i32)day, 31);
  if (r() > 0.55) return nullptr;
  const json& E = VD()["errands"];
  json list = json::array();
  for (auto& q : (E.contains(f.job) ? E[f.job] : E["none"])) if (reg().item(q[0].get<std::string>())) list.push_back(q);
  if (list.empty()) return nullptr;
  const json& q = list[(size_t)std::floor(r() * list.size())];
  std::string item = q[0]; int a = q[1][0], b = q[1][1]; std::string why = q[2];
  int need = a + (int)std::floor(r() * (b - a + 1));
  std::string name = npcName(npcSeedOf(v, i));
  int em = 2 + (int)std::floor(r() * 3) + (need > 20 ? 1 : 0);
  return {{"id", v.id + ":" + std::to_string(i) + ":" + std::to_string(day)}, {"vid", v.id}, {"giver", i}, {"from", name}, {"kind", "fetch"}, {"item", item}, {"need", need},
          {"title", name + "'s errand"}, {"text", why + " Bring " + std::to_string(need) + " " + itemName(item) + "."}, {"reward", {{"em", em}, {"xp", 15 + need}, {"rep", 5}}}};
}

static int structIndexByKey(const char* key) { const auto& T = structureTypes(); for (int i = 0; i < (int)T.size(); i++) if (T[i].key == key) return i; return -1; }
static const StructureIndex* owIdx(Level& lv) { auto* o = lv.gen ? dynamic_cast<const OverworldGen*>(lv.gen->forDim(DIM_OVERWORLD)) : nullptr; return o ? &o->structures : nullptr; }

json boardQuests(Level& lv, const Village& v) {
  const i64 day = npcDay(lv);
  json out = json::array();
  const json& HN = VD()["huntNames"];
  for (int k = 0; k < 3; k++) {
    Mulberry32 r = questRand((i32)v.seed, k, (i32)day, 32);
    std::string id = v.id + ":b" + std::to_string(k) + ":" + std::to_string(day);
    if (k < 2) {
      std::vector<std::string> fams;
      for (auto& [f, l] : VD()["huntFamilies"].items()) if (f != "raider" || r() < 0.3) fams.push_back(f);
      std::string fam = fams[(size_t)std::floor(r() * fams.size())];
      int need = fam == "fuseling" ? 3 + (int)std::floor(r() * 3) : fam == "raider" ? 3 + (int)std::floor(r() * 3) : 5 + (int)std::floor(r() * 8);
      std::string hn = HN.value(fam, fam);
      out.push_back({{"id", id}, {"vid", v.id}, {"giver", -1}, {"from", "Quest Board"}, {"kind", "hunt"}, {"fam", fam}, {"need", need}, {"title", "Bounty: " + hn},
                     {"text", "Monsters have been seen near " + v.name + ". Slay " + std::to_string(need) + " " + hn + "."},
                     {"reward", {{"em", 2 + (int)std::ceil(need / 2.0)}, {"xp", 20 + need * 6}, {"rep", 6}, {"skill", json::array({"melee", need * 25})}}}});
    } else {
      static const std::vector<std::tuple<const char*, int, int>> SUP = {{"log", 24, 48}, {"cobblestone", 48, 64}, {"iron_ingot", 6, 12}, {"bread", 8, 16}, {"coal", 16, 32}, {"torch", 24, 48}, {"wool", 8, 16}, {"cooked_beef", 6, 12}};
      std::vector<std::tuple<const char*, int, int>> list; for (auto& t : SUP) if (reg().item(std::get<0>(t))) list.push_back(t);
      if (list.empty()) continue;
      auto [item, a, b] = list[(size_t)std::floor(r() * list.size())];
      int need = a + (int)std::floor(r() * (b - a + 1));
      out.push_back({{"id", id}, {"vid", v.id}, {"giver", -1}, {"from", "Quest Board"}, {"kind", "fetch"}, {"item", item}, {"need", need}, {"title", "Supplies: " + itemName(item)},
                     {"text", v.name + " needs supplies for the stores. Bring " + std::to_string(need) + " " + itemName(item) + "."},
                     {"reward", {{"em", 3 + need / 12}, {"xp", 20 + need}, {"rep", 6}}}});
    }
  }
  const StructureIndex* idx = owIdx(lv);
  // dungeons.js: the nearest dungeon not yet cleared, within a few hundred blocks
  if (int k = structIndexByKey("dungeon"); idx && k >= 0) {
    const int REG = structureTypes()[k].region;
    const StructurePlan* best = nullptr; double bd = 420;
    const int rx = floordiv(v.x, REG), rz = floordiv(v.z, REG);
    const json& D = lv.info.data.value("dungeons", json::object());
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      const StructurePlan* d = idx->at(k, rx + dx, rz + dz);
      if (!d || !d->data.contains("ent")) continue;
      double dist = std::hypot(d->data["ent"][0].get<double>() - v.x, d->data["ent"][1].get<double>() - v.z);
      bool cleared = D.is_object() && D.contains(d->id) && D[d->id].value("cleared", false);
      if (dist < bd && !cleared) { bd = dist; best = d; }
    }
    if (best) {
      int ex = best->data["ent"][0], ez = best->data["ent"][1];
      std::string dir = dirWord(std::atan2(ez - v.z, ex - v.x)), nm = capFirst(noArticle(best->name));
      std::string bt = best->data.contains("boss") ? best->data["boss"].value("type", std::string()) : "";
      const CreatureType* BT = creatureType(bt);
      std::string boss = BT ? BT->label : "Something old";
      out.push_back({{"id", v.id + ":dg:" + best->id}, {"vid", v.id}, {"giver", -1}, {"from", "Quest Board"}, {"kind", "dungeon"}, {"did", best->id}, {"need", 1}, {"at", json::array({ex, ez})},
                     {"title", "Clear the " + nm}, {"text", "An old " + lower(nm) + " lies about " + std::to_string(roundTo(bd, 10)) + " blocks to the " + dir + " (near x " + std::to_string(ex) + ", z " + std::to_string(ez) + "). " + capFirst(boss) + " keeps it. End it."},
                     {"reward", {{"em", 14}, {"xp", 150}, {"rep", 15}, {"skill", json::array({"melee", 600})}}}});
    }
  }
  // rpg.js: a place nobody here has found yet; a neighbouring village to carry word to
  {
    Mulberry32 r = questRand((i32)v.seed, 7, (i32)day, 33);
    std::set<std::string> seen;
    for (Player* p : levelPlayers(lv)) if (p->stats.contains("sseen") && p->stats["sseen"].is_object()) for (auto& [k, x] : p->stats["sseen"].items()) seen.insert(k);
    const StructurePlan* best = nullptr; double bd = 480;
    if (idx) {
      static const std::set<std::string> NOT = {"village", "dungeon", "dragon_lair", "stronghold", "ancient_city"};
      const auto& T = structureTypes();
      for (int k = 0; k < (int)T.size(); k++) {
        if (!(T[k].dims & (1u << DIM_OVERWORLD)) || NOT.count(T[k].key) || T[k].region <= 0 || T[k].region > 4096) continue;
        const int REG = T[k].region, rx = floordiv(v.x, REG), rz = floordiv(v.z, REG), span = std::max(1, (int)std::ceil(480.0 / REG));
        for (int dz = -span; dz <= span; dz++) for (int dx = -span; dx <= span; dx++) {
          const StructurePlan* S = idx->at(k, rx + dx, rz + dz);
          if (!S || seen.count(S->id)) continue;
          double d = std::hypot((S->x0 + S->x1) / 2.0 - v.x, (S->z0 + S->z1) / 2.0 - v.z);
          if (d < bd && d > 40) { bd = d; best = S; }
        }
      }
    }
    if (best && r() < 0.8) {
      int cx = (int)std::floor((best->x0 + best->x1) / 2.0 + 0.5), cz = (int)std::floor((best->z0 + best->z1) / 2.0 + 0.5);
      std::string nm = best->name, dir = dirWord(std::atan2(cz - v.z, cx - v.x));
      std::string spoken = nm.rfind("The ", 0) == 0 ? "the" + nm.substr(3) : (nm.rfind("a ", 0) == 0 || nm.rfind("an ", 0) == 0 || nm.rfind("A ", 0) == 0 || nm.rfind("An ", 0) == 0) ? nm : "a " + nm;
      out.push_back({{"id", v.id + ":sc:" + best->id}, {"vid", v.id}, {"giver", -1}, {"from", "Quest Board"}, {"kind", "scout"}, {"sid", best->id}, {"need", 1}, {"at", json::array({cx, cz})},
                     {"title", "Scout: " + capFirst(noArticle(nm))},
                     {"text", "Travellers speak of " + spoken + " some " + std::to_string(roundTo(bd, 10)) + " blocks to the " + dir + " (near x " + std::to_string(cx) + ", z " + std::to_string(cz) + "). Go and see it with your own eyes."},
                     {"reward", {{"em", 5 + (int)std::floor(bd / 80 + 0.5)}, {"xp", 60}, {"rep", 8}, {"skill", json::array({"exploring", 300})}}}});
    }
    float nd = 1e9f; VillageRef nb = villageNeighbour(lv, v, &nd);
    if (nb && nd < 1100 && r() < 0.7) {
      std::string dir = dirWord(std::atan2(nb->z - v.z, nb->x - v.x));
      out.push_back({{"id", v.id + ":wd:" + nb->id + ":" + std::to_string(day / 3)}, {"vid", nb->id}, {"from", "Quest Board"}, {"fromV", v.id}, {"giver", -1}, {"kind", "word"}, {"need", 1}, {"at", json::array({nb->x, nb->z})},
                     {"title", "Word to " + nb->name},
                     {"text", "Carry " + v.name + "'s news to " + nb->name + ", some " + std::to_string(roundTo(nd, 50)) + " blocks to the " + dir + " (near x " + std::to_string(nb->x) + ", z " + std::to_string(nb->z) + "). Speak to their elder or a guard there."},
                     {"reward", {{"em", 4 + (int)std::floor(nd / 120 + 0.5)}, {"xp", 50}, {"rep", 10}, {"skill", json::array({"exploring", 250})}}}});
    }
  }
  // livingworld.js: named foes
  {
    const i64 period = day / 3; Mulberry32 r(eliteMix(v.seed, period, 34));
    if (r() <= 0.75) {
      std::vector<std::string> kinds; for (auto& k : VD()["eliteKind"]) if (creatureType(k[0].get<std::string>())) kinds.push_back(k[0]);
      if (!kinds.empty()) {
        std::string type = kinds[(size_t)std::floor(r() * kinds.size())];
        const json& F = VD()["eliteFirst"]; const json& L = VD()["eliteLast"]; const json& Ep = VD()["eliteEpithet"];
        std::string name = pickStr(F, r()); name += pickStr(L, r()); name += " " + pickStr(Ep, r());
        double a = r() * TAU, d = 120 + r() * 140;
        int x = (int)std::floor(v.x + std::cos(a) * d + 0.5), z = (int)std::floor(v.z + std::sin(a) * d + 0.5);
        int lvl = 1 + (int)std::min<i64>(4, day / 6);
        std::string label = creatureType(type)->label; label = noArticle(label); for (auto& c : label) if (c == '_') c = ' ';
        out.push_back({{"id", v.id + ":el:" + std::to_string(period)}, {"vid", v.id}, {"giver", -1}, {"from", "Quest Board"}, {"kind", "elite"}, {"etype", type}, {"ename", name}, {"elvl", lvl}, {"need", 1},
                       {"at", json::array({x, z})}, {"title", "Wanted: " + name},
                       {"text", name + " has been seen some " + std::to_string(roundTo(d, 10)) + " blocks to the " + dirWord(a) + " (near x " + std::to_string(x) + ", z " + std::to_string(z) + "). Bring it down. It is no ordinary " + label + "."},
                       {"reward", {{"em", 10 + lvl * 4}, {"xp", 120 + lvl * 40}, {"rep", 12}, {"skill", json::array({"melee", 500 + lvl * 200})}}}});
      }
    }
  }
  // hearth.js: the missing
  {
    const i64 period = day / 2; Mulberry32 r(eliteMix(v.seed, period, 41));
    if (r() <= 0.55) {
      int seed = (int)std::floor(r() * 99999); std::string name = npcName((u32)seed), why = pickStr(VD()["lostWhy"], r());
      double a = r() * TAU, d = 90 + r() * 120;
      int x = (int)std::floor(v.x + std::cos(a) * d + 0.5), z = (int)std::floor(v.z + std::sin(a) * d + 0.5);
      out.push_back({{"id", v.id + ":lost:" + std::to_string(period)}, {"vid", v.id}, {"giver", -1}, {"from", "Quest Board"}, {"kind", "rescue"}, {"lname", name}, {"ls", seed}, {"at", json::array({x, z})}, {"need", 1},
                     {"title", "Missing: " + name},
                     {"text", name + " " + why + " and hasn't come back. Last seen some " + std::to_string(roundTo(d, 10)) + " blocks to the " + dirWord(a) + " (near x " + std::to_string(x) + ", z " + std::to_string(z) + "). Find them and walk them home."},
                     {"reward", {{"em", 8}, {"xp", 100}, {"rep", 15}, {"skill", json::array({"exploring", 600})}}}});
    }
  }
  return out;
}

json boardJob(Level& lv, const std::string& id, const std::string& kind) {
  // the job's id says the village and the period: worked out again for that day
  size_t c1 = id.find(':'); if (c1 == std::string::npos) return nullptr;
  VillageRef v = villageById(lv, id.substr(0, c1)); if (!v) return nullptr;
  size_t c2 = id.find(':', c1 + 1); if (c2 == std::string::npos) return nullptr;
  std::string tag = id.substr(c1 + 1, c2 - c1 - 1); i64 per = std::atoll(id.c_str() + c2 + 1);
  i64 d0 = lv.info.day; lv.info.day = per * (tag == "el" ? 3 : 2);
  json q = nullptr;
  for (auto& o : boardQuests(lv, *v)) if (o["id"] == id && o["kind"] == kind) q = o;
  lv.info.day = d0;
  return q;
}

json* findQuest(Player& p, const std::string& qid) { for (auto& q : npcStats(p)["quests"]) if (q.value("id", "") == qid) return &q; return nullptr; }
void questHudChanged(Level& lv, Player& p) { if (lv.events) lv.events->emit("questHud", json::array({p.name})); }

int questHave(Level& lv, Player& p, json& q) {
  std::string k = q.value("kind", std::string("fetch"));
  if (k == "dungeon" && !q.value("have", 0)) {   // fairplay.js: a cleared dungeon counts whoever cleared it
    const json& D = lv.info.data.value("dungeons", json::object());
    std::string did = q.value("did", std::string());
    if (D.is_object() && D.contains(did) && D[did].value("cleared", false)) q["have"] = 1;
  }
  if (k == "hunt" || k == "elite" || k == "rescue" || k == "dungeon") return q.value("have", 0);
  if (k == "scout") { const json& s = p.stats.value("sseen", json::object()); return s.is_object() && s.contains(q.value("sid", "")) ? 1 : 0; }
  if (k == "word") {
    if (!q.value("have", 0)) if (VillageRef v = villageById(lv, q.value("vid", ""))) if (p.realm == REALM_MAIN && dimOfY(ifloor(p.pos.y)) == DIM_OVERWORLD && std::hypot(p.pos.x - v->x, p.pos.z - v->z) < 40) q["have"] = 1;
    return q.value("have", 0);
  }
  return countOf(p, q.value("item", std::string()));
}
bool questReady(Level& lv, Player& p, json& q) { return questHave(lv, p, q) >= q.value("need", 1); }

bool questTake(Level& lv, Player& p, const json& q, bool shared) {
  json& S = npcStats(p);
  if (S["quests"].size() >= 5) { if (!shared) toast(lv, p, "You already have five jobs \xE2\x80\x94 finish one first", "warn"); return false; }
  std::string id = q.value("id", "");
  if (findQuest(p, id) || S["qdone"].contains(id)) return false;
  json o = q; if (!o.contains("have")) o["have"] = 0; o["taken"] = npcDay(lv);
  if (shared) { o["party"] = 1; if (o.value("kind", "") == "fetch") o.erase("have"); else o["have"] = 0; }
  S["quests"].push_back(o);
  if (!shared) { sfxUi(lv, p, "map_paper"); toast(lv, p, "Quest taken: " + q.value("title", std::string()), "good"); }
  else toast(lv, p, "A partner shared a job with you: " + q.value("title", std::string()), "good");
  // together.js: take one and your partners get it too
  if (!shared) {
    bool any = false;
    for (Player* o2 : levelPlayers(lv)) if (o2 != &p && questTake(lv, *o2, q, true)) any = true;
    if (any) if (json* m = findQuest(p, id)) (*m)["party"] = 1;
  }
  questHudChanged(lv, p);
  return true;
}
void questDrop(Level& lv, Player& p, const std::string& qid) {
  json& L = npcStats(p)["quests"]; json keep = json::array();
  for (auto& q : L) if (q.value("id", "") != qid) keep.push_back(q);
  L = keep; questHudChanged(lv, p);
}
bool questFinish(Level& lv, Player& p, const std::string& qid) {
  json* qp = findQuest(p, qid); if (!qp) return false;
  json q = *qp;
  if (!questReady(lv, p, *qp)) return false;
  json& S = npcStats(p);
  if (q.value("kind", "") == "fetch") removeOf(p, q.value("item", std::string()), q.value("need", 1));
  questDrop(lv, p, qid);
  S["qdone"][qid] = 1;
  if (S["qdone"].size() > 120) {   // keep the done list short
    std::vector<std::string> ks; for (auto& [k, x] : S["qdone"].items()) ks.push_back(k);
    for (size_t i = 0; i + 120 < ks.size(); i++) S["qdone"].erase(ks[i]);
  }
  const json R = q.value("reward", json::object());
  if (R.value("em", 0) > 0) vGive(lv, p, ItemStack("emerald", R.value("em", 0)));
  if (R.contains("item") && R["item"].is_array() && reg().item(R["item"][0].get<std::string>())) vGive(lv, p, ItemStack(R["item"][0].get<std::string>(), R["item"][1].get<int>()));
  if (R.value("xp", 0) > 0) grantXp(lv, p, R.value("xp", 0), p.pos + vec3(0, 1, 0));
  if (R.contains("skill") && R["skill"].is_array()) skillGain(lv, p, R["skill"][0].get<std::string>(), R["skill"][1].get<double>());
  npcRep(lv, p, q.value("vid", std::string()), R.value("rep", 5));
  sfxUi(lv, p, "levelup");
  toast(lv, p, "Quest complete: " + q.value("title", std::string()) + " (+" + std::to_string(R.value("em", 0)) + " emeralds)", "epic");
  S["questsDone"] = S.value("questsDone", 0) + 1;
  return true;
}
void questKill(Level& lv, Player& p, const std::string& type, bool party) {
  bool hit = false;
  for (auto& q : npcStats(p)["quests"]) {
    if (q.value("kind", "") != "hunt" || (party && !q.value("party", 0))) continue;
    const json& F = famOf(q.value("fam", ""));
    if (std::find(F.begin(), F.end(), type) == F.end() || q.value("have", 0) >= q.value("need", 1)) continue;
    q["have"] = q.value("have", 0) + 1; hit = true;
    if (q["have"].get<int>() >= q.value("need", 1)) {
      VillageRef v = villageById(lv, q.value("vid", ""));
      toast(lv, p, party ? q.value("title", std::string()) + ": done (with your party) \xE2\x80\x94 back to the village for the reward"
                         : q.value("title", std::string()) + ": done \xE2\x80\x94 back to " + (v ? v->name : std::string("the village")) + " for the reward", "good");
    }
  }
  if (hit) questHudChanged(lv, p);
}
void questLapse(Level& lv, Player& p) {
  const i64 day = npcDay(lv);
  std::vector<std::string> old;
  for (auto& q : npcStats(p)["quests"]) if (q.value("giver", 0) == -1 && q.value("kind", "") != "dungeon" && q.contains("taken") && day - q["taken"].get<i64>() >= 7 && !questReady(lv, p, q)) old.push_back(q.value("id", ""));
  for (auto& id : old) { json* q = findQuest(p, id); std::string t = q ? q->value("title", std::string()) : id; questDrop(lv, p, id); toast(lv, p, "The bounty \"" + t + "\" has lapsed"); }
}

// ------------------------------------------------------------------ shops
static std::string shopKey(const Village& v, int npcIndex) { return v.id + ":" + std::to_string(npcIndex); }
std::vector<ShopLine> shopLines(Level& lv, Player& p, const Village& v, int npcIndex, const std::string& job, const std::string& tab) {
  std::vector<ShopLine> out;
  const json& SH = VD()["shops"]; if (!SH.contains(job)) return out;
  const json& list = SH[job].contains(tab) ? SH[job][tab] : json::array();
  Standing st = standingOf(repOf(p, v.id));
  double disc = st.k >= 6 ? 0.8 : st.k >= 5 ? 0.9 : 1;
  json& S = npcStats(p); std::string k = shopKey(v, npcIndex); const i64 day = npcDay(lv);
  json rec = S["vstock"].contains(k) && S["vstock"][k].value("d", (i64)-1) == day ? S["vstock"][k] : json{{"d", day}, {"u", json::object()}};
  int em = countOf(p, "emerald");
  int i = 0;
  for (auto& l : list) {
    if (!reg().item(l[0].get<std::string>())) continue;
    ShopLine o; o.i = i; o.id = l[0]; o.n = l[1]; o.tier = l[3]; o.key = tab + std::to_string(i);
    double base = l[2].get<double>();
    o.price = std::max(1, (int)std::floor(base * (tab == "buy" ? disc : 1) + 0.5));
    o.locked = o.tier > st.tier; o.left = 12 - rec["u"].value(o.key, 0);
    o.can = !o.locked && o.left > 0 && (tab == "buy" ? em >= o.price : countOf(p, o.id) >= o.n);
    out.push_back(o); i++;
  }
  return out;
}
bool shopTrade(Level& lv, Player& p, const Village& v, int npcIndex, const std::string& job, const std::string& tab, int line, std::string& say) {
  auto L = shopLines(lv, p, v, npcIndex, job, tab);
  if (line < 0 || line >= (int)L.size()) return false;
  const ShopLine& l = L[line];
  const json& TN = VD()["tierName"];
  if (l.locked) { say = "I keep that for my " + lower(TN[l.tier].get<std::string>()) + "s. Help the village and we'll see."; return false; }
  if (l.left <= 0) { say = "All gone for today. Come back tomorrow!"; return false; }
  if (tab == "buy") {
    if (countOf(p, "emerald") < l.price) { say = "That's " + plural(l.price, "emerald") + ". You haven't enough."; sfxUi(lv, p, "click_off"); return false; }
    removeOf(p, "emerald", l.price);
    vGive(lv, p, ItemStack(l.id, l.n));
  } else {
    if (countOf(p, l.id) < l.n) { say = "I need " + std::to_string(l.n) + " of those."; sfxUi(lv, p, "click_off"); return false; }
    removeOf(p, l.id, l.n);
    vGive(lv, p, ItemStack("emerald", l.price));
  }
  json& S = npcStats(p); std::string k = shopKey(v, npcIndex); const i64 day = npcDay(lv);
  if (!S["vstock"].contains(k) || S["vstock"][k].value("d", (i64)-1) != day) S["vstock"][k] = {{"d", day}, {"u", json::object()}};
  S["vstock"][k]["u"][l.key] = S["vstock"][k]["u"].value(l.key, 0) + 1;
  npcRep(lv, p, v.id, 1, "trade");
  sfxUi(lv, p, "pickup");
  say.clear();
  return true;
}

// ------------------------------------------------------------------ paths.js
int combatLevel(const Player& p) {
  int me = skillLv(p, "melee"), de = skillLv(p, "defence"), ar = skillLv(p, "archery"), ma = skillLv(p, "magic");
  return (int)std::floor((me + de + std::max(ar, ma)) / 3.0 + (me + de) / 12.0);
}
static const json* pathOf(const Player& p) {
  if (!p.stats.is_object() || !p.stats.contains("path") || !p.stats["path"].is_object()) return nullptr;
  const json& P = p.stats["path"];
  return magicData().paths.contains(P.value("k", "")) ? &P : nullptr;
}
int pathPoints(const Player& p) { int cl = combatLevel(p); int at = VD().value("pathAt", 20); return cl < at ? 0 : std::min(6, 1 + (cl - at) / 6); }
int pathSpent(const Player& p) { const json* P = pathOf(p); return P && P->contains("t") && (*P)["t"].is_object() ? (int)(*P)["t"].size() : 0; }
int pathMaxHealthBonus(const Player& p) { return onPath(p, "knight") ? 4 : 0; }
bool pathPick(Level& lv, Player& p, const std::string& k) {
  const json& PD = magicData().paths;
  if (!PD.contains(k) || pathOf(p) || combatLevel(p) < VD().value("pathAt", 20)) return false;
  p.stats["path"] = {{"k", k}, {"t", json::object()}};
  toast(lv, p, "You walk the " + PD[k].value("name", k) + "'s path. " + PD[k].value("base", std::string()), "epic");
  sfxUi(lv, p, "levelup");
  return true;
}
bool pathLearn(Level& lv, Player& p, const std::string& id) {
  const json* P = pathOf(p); if (!P) return false;
  const json& D = magicData().paths[(*P)["k"].get<std::string>()];
  int tier = -1; std::string nm;
  for (int i = 0; i < (int)D["tiers"].size(); i++) for (auto& t : D["tiers"][i]) if (t[0] == id) { tier = i; nm = t[1]; }
  if (tier < 0 || pathSpent(p) < tier * 2 || pathPoints(p) - pathSpent(p) <= 0 || ((*P).contains("t") && (*P)["t"].contains(id))) return false;
  p.stats["path"]["t"][id] = 1;
  sfxUi(lv, p, "enchant");
  toast(lv, p, "Learned: " + nm, "good");
  return true;
}
bool pathForget(Level& lv, Player& p) {
  if (!pathOf(p)) return false;
  if (countOf(p, "emerald") < 20) { toast(lv, p, "That takes 20 emeralds", "warn"); return false; }
  removeOf(p, "emerald", 20); p.stats["path"] = nullptr;
  toast(lv, p, "You set your path aside. Choose again when you're ready.");
  return true;
}
float pathMeleeFactor(const Player& p) { return hasTal(p, "heavy") ? 1.1f : 1.f; }
float pathArrowFactor(const Player& p, float distance) {
  float k = 1;
  if (onPath(p, "ranger")) { k *= 1.1f; if (hasTal(p, "eagle")) k *= 1.15f; if (hasTal(p, "deadeye") && distance > 20) k *= 1.5f; }
  return k;
}
bool pathArrowPoisons(const Player& p) { return onPath(p, "ranger") && hasTal(p, "poison"); }
bool pathQuiverSaves(const Player& p) { return hasTal(p, "quiver") && frand() < 0.3f; }
float pathSpeedFactor(const Player& p) { return !p.flying && hasTal(p, "fleet") ? 1.08f : 1.f; }
bool pathCheatDeath(Level& lv, Player& p) {
  VillagerSystem* S = vsys(lv);
  double now = S ? S->now : 0;
  if (!hasTal(p, "unbreak") || p.data.value("ubAt", -1e9) > now - 120) return false;
  p.data["ubAt"] = now; p.hp = 1; p.dead = false;
  toast(lv, p, "Unbreakable! You stand, on half a heart", "epic"); sfxUi(lv, p, "levelup");
  return true;
}
void pathCleave(Level& lv, Player& p, Creature& hit, float dealt) {
  if (!hasTal(p, "arc") || dealt <= 0) return;
  std::vector<Creature*> near;
  for (auto& up : lv.entities()) {
    auto* e = dynamic_cast<Creature*>(up.get());
    if (!e || e == &hit || e->removed || !e->alive() || !e->T->hostile || !e->owner.empty() || e->realm != hit.realm) continue;
    if (std::hypot(e->pos.x - hit.pos.x, e->pos.z - hit.pos.z) > 2.2f || std::fabs(e->pos.y - hit.pos.y) > 2) continue;
    near.push_back(e);
  }
  for (Creature* e : near) { DamageSrc s; s.type = "cleave"; s.attacker = p.id; s.knock = 1.5f; e->hurt(lv, dealt * 0.25f, s); }
}
float pathPetFactor(Level& lv, const Creature& victim, const Entity* attacker) {
  float k = 1;
  if (!victim.owner.empty()) if (Player* o = findPlayer(lv, victim.owner)) if (hasTal(*o, "beast")) k *= 0.7f;
  if (auto* ac = dynamic_cast<const Creature*>(attacker)) if (!ac->owner.empty()) if (Player* o = findPlayer(lv, ac->owner)) if (hasTal(*o, "beast")) k *= 1.3f;
  return k;
}

// ------------------------------------------------------------------ the conversation
static Creature* talkNpc(Level& lv, const Talk& t) { return dynamic_cast<Creature*>(lv.entity(t.npc)); }
static std::string fill(std::string s, Level& lv, const Talk& t, const Player& p) {
  VillageRef v = villageById(lv, t.vid);
  auto rep = [&](const std::string& k, const std::string& r) { size_t i; while ((i = s.find(k)) != std::string::npos) s.replace(i, k.size(), r); };
  rep("{v}", v ? v->name : "the village"); rep("{p}", p.name.empty() ? "friend" : p.name);
  return s;
}
static void say(Level& lv, Player& p, Talk& t, std::string text) {
  // riftarc.js: when the boundary is paper-thin, sometimes something else speaks
  if (riftStability(lv) < 15 && rnd() < 0.18f && t.job != "elder") {
    static const char* L[4] = {"He sees you through our eyes.", "IT BREAKS. IT BREAKS. IT BREAKS.", "Why did you open it?", "There is no village. There never was."};
    text = L[(int)std::floor(rnd() * 4)]; sfxUi(lv, p, "rift_static");
  }
  // rpg.js: the elder tells of every kind of keeper now
  const std::string A = "Find the dungeons, defeat their keepers, bring their Realm Fragments to the rift. Four of them.";
  if (text.rfind("It opened when you came.", 0) == 0) if (size_t i = text.find(A); i != std::string::npos)
    text.replace(i, A.size(), "Find the dungeons and defeat their keepers. The great beasts of the world carry them too: the golem of cinders, the archmage in his tower, the beast of the maze, the dragons. Bring four Realm Fragments to the rift.");
  t.text = fill(text, lv, t, p);
  t.voiceLines++;
  t.spoken = t.text; t.spokenVol = 0.5f; t.spokenN++;   // babble(sfx, text, voice, { rate: 26 }): the talk screen plays it as the words type out
}
static Standing standOf(Level& lv, Player& p, const Talk& t) { return standingOf(t.vid.empty() ? 0 : repOf(p, t.vid)); }
static std::string greeting(Level& lv, Player& p, Talk& t) {
  double ph = npcPhase(lv);
  const char* k = ph < 0.12 ? "morning" : ph < 0.42 ? "day" : ph < 0.53 ? "evening" : "night";
  Standing s = standOf(lv, p, t);
  if (s.k == 0) return "Hmph. We remember what you did. What do you want?";
  std::string line = pickStr(VD()["greet"][k], rnd());
  if (s.k >= 5) { static const char* H[3] = {"Our hero! ", "Ah, my friend! ", "Welcome back, friend! "}; line = H[(int)std::floor(rnd() * 3)] + line; }
  return line;
}
static bool riftClueFits(const json& c, int tier) { int t = c.value("t", 0); int upto = c.contains("upto") ? c["upto"].get<int>() : t == 5 ? 5 : 4; return t <= tier && tier <= upto; }
static bool hasJob(const json& c, const std::string& j) { for (auto& x : c["jobs"]) if (x == j) return true; return false; }
bool riftAskAvailable(Level& lv) { return riftTier(lv) >= 0; }

static void home(Level& lv, Player& p, Talk& t, const std::string& text0 = "") {
  VillageRef v = villageById(lv, t.vid);
  t.mode = "home";
  std::string text = text0;
  const int tier = riftTier(lv);
  // riftlore.js: their greeting carries it
  t.greetLine = false;
  if (text.empty() && tier >= 1 && rnd() < 0.45f && standOf(lv, p, t).k > 0) { const json& L = VD()["riftGreet"].value(std::to_string(tier), json::array()); if (!L.empty()) { text = pickStr(L, rnd()); t.greetLine = true; } }
  say(lv, p, t, text.empty() ? greeting(lv, p, t) : text);
  auto& O = t.opts; O.clear();
  if (hasShop(t.job)) O.push_back({"Let me see your wares", "", "shop", nullptr});
  json& S = npcStats(p);
  json* mine = nullptr;
  if (v) for (auto& q : S["quests"]) if (q.value("vid", "") == v->id && q.value("giver", -9) == t.idx) { mine = &q; break; }
  json offer = v && t.idx < 100 ? npcQuestFor(lv, *v, t.idx) : json(nullptr);
  int boardDone = 0;
  if (v) for (auto& q : S["quests"]) if (q.value("vid", "") == v->id && q.value("giver", 0) == -1 && questReady(lv, p, q)) boardDone++;
  if (mine) O.push_back({questReady(lv, p, *mine) ? "Here's what you asked for (" + mine->value("title", std::string()) + ")" : "About your errand\xE2\x80\xA6", "", "errand", (*mine)["id"]});
  else if (offer.is_object() && !S["qdone"].contains(offer.value("id", ""))) O.push_back({"Any work for me?", "", "offer", offer});
  if ((t.job == "guard" || t.job == "elder") && boardDone) O.push_back({"I've done a bounty (" + std::to_string(boardDone) + ")", "", "bounties", nullptr});
  O.push_back({"What news?", "", "news", nullptr});
  if (t.job == "elder") O.push_back({"Tell me about this place", "", "about", nullptr});
  // riftarc.js: the elder knows the tale
  const json R = lv.info.data.value("rift", json::object());
  if (t.job == "elder" && R.is_object() && R.value("stage", 0) && (text0.empty() || t.greetLine))
    O.push_back({R.value("sealed", 0) ? "About the rift\xE2\x80\xA6" : R.value("stage", 0) >= 3 ? "The gateway\xE2\x80\xA6" : "The rift in the square\xE2\x80\xA6", "\xE2\x98\x85", "riftElder", nullptr});
  // riftlore.js: seen anything strange?
  if (tier >= 0) O.push_back({tier == 0 ? "Seen anything strange lately?" : tier >= 5 ? "About what happened\xE2\x80\xA6" : "What do you make of the rift?", "?", "rift", nullptr});
  // hearth.js: the elder and a Hero's house
  if (t.job == "elder" && v && (homeOf(p, v->id) || isHero(p, v->id))) O.push_back({homeOf(p, v->id) ? "About my house\xE2\x80\xA6" : "You wanted a word?", "\xE2\x8C\x82", "house", nullptr});
  // paths.js: training
  const json& TR = VD()["trains"];
  if (TR.contains(t.job)) {
    const json* P = pathOf(p);
    std::string want = TR[t.job].is_string() ? TR[t.job].get<std::string>() : "";
    int left = P ? pathPoints(p) - pathSpent(p) : 0;
    const json& PD = magicData().paths;
    std::string lbl = P ? "My path" + (left > 0 ? " (" + std::to_string(left) + " to spend)" : std::string()) + "\xE2\x80\xA6"
                        : !want.empty() ? "Teach me the " + PD[want].value("name", want) + "'s ways\xE2\x80\xA6" : "About the paths\xE2\x80\xA6";
    O.push_back({lbl, "\xE2\x9A\x94", "path", want});
  }
  O.push_back({"Goodbye", "", "close", nullptr});
}

static void news(Level& lv, Player& p, Talk& t) {
  const int tier = riftTier(lv);
  const json& RN = VD()["riftNews"];
  if (RN.contains(std::to_string(tier)) && rnd() < 0.55f) { home(lv, p, t, pickStr(RN[std::to_string(tier)], rnd())); return; }
  json list = json::array();
  const json& TK = VD()["talk"];
  if (TK.contains(t.job)) for (auto& x : TK[t.job]) list.push_back(x);
  for (auto& x : TK["any"]) list.push_back(x);
  std::string line = pickStr(list, rnd());
  VillageRef v = villageById(lv, t.vid);
  if (rnd() < 0.3f && v) {   // sometimes a pointer to another village
    float bd; VillageRef best = villageNeighbour(lv, *v, &bd);
    if (best) line = best->name + " lies some " + std::to_string(roundTo(bd, 50)) + " blocks to the " + dirWord(std::atan2(best->z - v->z, best->x - v->x)) + ". Good folk there.";
  }
  home(lv, p, t, line);
}
static void riftAsk(Level& lv, Player& p, Talk& t) {
  const int tier = riftTier(lv);
  json& heard = p.stats["riftClues"]; if (!heard.is_object()) heard = json::object();
  const json& LORE = VD()["riftLore"];
  std::vector<const json*> mine;
  for (auto& c : LORE) if (riftClueFits(c, tier) && !heard.contains(c.value("id", "")) && (hasJob(c, t.job) || hasJob(c, "any"))) mine.push_back(&c);
  std::stable_sort(mine.begin(), mine.end(), [&](const json* a, const json* b) {
    int d = (*b)["t"].get<int>() - (*a)["t"].get<int>();
    if (d) return d < 0;
    return (hasJob(*b, t.job) ? 1 : 0) - (hasJob(*a, t.job) ? 1 : 0) < 0;
  });
  const json* c = nullptr;
  for (auto* x : mine) if ((*x)["t"].get<int>() == tier) { c = x; break; }
  if (!c && !mine.empty()) c = mine[0];
  if (c) {
    heard[c->value("id", "")] = 1; p.stats["riftCluesN"] = heard.size();
    home(lv, p, t, c->value("text", std::string()));
    toast(lv, p, tier >= 1 ? "You learn something about the rift\xE2\x80\xA6" : "You hear something strange\xE2\x80\xA6", "rare");
    sfxUi(lv, p, tier >= 2 ? "rift_whisper" : "click");
    return;
  }
  // nothing new: a feeling, and perhaps someone else to ask
  std::set<std::string> here = {"guard"};
  VillageRef v = villageById(lv, t.vid);
  if (v) for (auto& f : v->folk) here.insert(f.job);
  std::vector<const json*> others;
  for (auto& x : LORE) {
    if (!riftClueFits(x, tier) || heard.contains(x.value("id", "")) || hasJob(x, "any")) continue;
    bool ok = false; for (auto& j : x["jobs"]) if (j != t.job && here.count(j.get<std::string>())) ok = true;
    if (ok) others.push_back(&x);
  }
  const json& RR = VD()["riftReact"];
  std::string key = std::to_string(std::max(0, tier));
  std::string line = pickStr(RR.contains(key) ? RR[key] : RR["1"], rnd());
  if (!others.empty() && rnd() < 0.7f) {
    const json& o = *others[(size_t)std::floor(rnd() * others.size())];
    std::string j; for (auto& x : o["jobs"]) if (x != t.job && here.count(x.get<std::string>())) { j = x; break; }
    static const char* A[3] = {"You could ask the", "Talk to the", "The"};
    static const char* B[3] = {" \xE2\x80\x94 they've been muttering about it.", ". They saw more than I did.", " won't stop talking about it."};
    line += std::string(" ") + A[(int)std::floor(rnd() * 3)] + " " + lower(jobTitle(j)) + B[(int)std::floor(rnd() * 3)];
  }
  home(lv, p, t, line);
}
static void confront(Level& lv, Player& p, Talk& t, const std::string& victim0) {
  Creature* e = talkNpc(lv, t);
  NpcMind* M = e && vsys(lv) ? &vsys(lv)->mind(e->id) : nullptr;
  std::string victim = !victim0.empty() ? victim0 : M && !M->confrontVictim.empty() ? M->confrontVictim : "one of ours";
  bool mine = victim == "me" || victim == t.name;
  t.mode = "confront";
  say(lv, p, t, mine ? "Whoa! Watch where you swing that! Do that again and we'll have a problem." : "Hey! I saw that. What do you think you're doing, hitting " + victim + "?");
  t.opts = {{"Sorry! It was an accident.", "", "sorry", "Alright. Accidents happen. Just be more careful around here."},
            {"I didn't mean to, I'll be careful.", "", "sorry", "Hmph. Fine. I'll be keeping an eye on you."},
            {"Mind your own business.", "", "rude", nullptr}};
}

bool talkOpen(Level& lv, Player& p, Creature& npc, Talk& t, bool conf, const std::string& victim) {
  const json* N = npcOf(npc);
  if (!N || !npc.alive()) return false;
  t = Talk{};
  t.npc = npc.id; t.vid = N->value("v", std::string()); t.job = N->value("j", std::string("none")); t.name = N->value("n", std::string("Villager"));
  t.idx = N->value("i", 0); t.seed = N->value("s", 0u);
  VillageRef v = villageById(lv, t.vid);
  t.title = jobTitle(t.job) + (v ? " of " + v->name : std::string());
  talkHold(lv, p, t);
  if (conf) confront(lv, p, t, victim); else home(lv, p, t);
  return true;
}
void talkHold(Level& lv, Player& p, Talk& t) {
  if (VillagerSystem* S = vsys(lv)) { NpcMind& M = S->mind(t.npc); M.holdT = 3; M.holdBy = p.id; }
}
bool talkStillOk(Level& lv, Player& p, const Talk& t) {
  Creature* e = talkNpc(lv, t);
  return e && !e->removed && e->alive() && !p.dead && std::hypot(p.pos.x - e->pos.x, p.pos.z - e->pos.z) <= 7;
}
void talkClosed(Level& lv, Player& p, Talk& t) { if (VillagerSystem* S = vsys(lv)) { NpcMind& M = S->mind(t.npc); if (M.holdBy == p.id) M.holdT = 0.5f; } }

void talkChoose(Level& lv, Player& p, Talk& t, int i) {
  if (i < 0 || i >= (int)t.opts.size()) return;
  TalkOpt o = t.opts[i];
  VillageRef v = villageById(lv, t.vid);
  sfxUi(lv, p, "click");
  const std::string& a = o.act;
  if (a == "close") { t.closeNow = true; return; }
  if (a == "shop") { t.mode = "shop"; t.tab = "buy"; return; }
  if (a == "news") { news(lv, p, t); return; }
  if (a == "about") { home(lv, p, t, "This is " + (v ? v->name : std::string("our village")) + ". We are simple folk: farmers, smiths, traders. Help us and we will help you. The board by the well has work for brave hands."); return; }
  if (a == "offer") {
    const json& q = o.arg; int em = q["reward"].value("em", 0);
    say(lv, p, t, q.value("text", std::string()) + " I can pay " + plural(em, "emerald") + ".");
    t.opts = {{"I'll do it", "", "take", q}, {"Not right now", "", "home", "Maybe another time, then."}};
    return;
  }
  if (a == "take") { if (questTake(lv, p, o.arg)) home(lv, p, t, "Thank you! Come back when you have it."); return; }
  if (a == "home") { home(lv, p, t, o.arg.is_string() ? o.arg.get<std::string>() : ""); return; }
  if (a == "errand") {
    json* q = findQuest(p, o.arg.get<std::string>()); if (!q) { home(lv, p, t); return; }
    if (questReady(lv, p, *q)) { questFinish(lv, p, o.arg.get<std::string>()); home(lv, p, t, "Wonderful! Just what I needed. Here, take this."); return; }
    say(lv, p, t, "I still need " + std::to_string(q->value("need", 1)) + " " + itemName(q->value("item", std::string())) + ". You have " + std::to_string(questHave(lv, p, *q)) + ".");
    t.opts = {{"I'll get them", "", "home", "Thank you!"}, {"I can't do it after all", "", "giveup", o.arg}};
    return;
  }
  if (a == "giveup") { questDrop(lv, p, o.arg.get<std::string>()); home(lv, p, t, "Oh. Never mind, then."); return; }
  if (a == "bounties") {
    std::vector<std::string> ids;
    if (v) for (auto& q : npcStats(p)["quests"]) if (q.value("vid", "") == v->id && q.value("giver", 0) == -1 && questReady(lv, p, q)) ids.push_back(q.value("id", ""));
    for (auto& id : ids) questFinish(lv, p, id);
    home(lv, p, t, "Well done! The village thanks you.");
    return;
  }
  if (a == "sorry") {
    npcForgive(lv, p, t.vid); npcRep(lv, p, t.vid, 1);
    say(lv, p, t, o.arg.get<std::string>());
    t.opts = {{"Thanks. It won't happen again.", "", "close", nullptr}};
    return;
  }
  if (a == "rude") { say(lv, p, t, "Watch yourself, stranger. Next time you'll answer to my sword."); t.opts = {{"\xE2\x80\xA6", "", "close", nullptr}}; return; }
  if (a == "rift") { riftAsk(lv, p, t); return; }
  if (a == "riftElder") {
    const json R = lv.info.data.value("rift", json::object());
    int n = std::min(4, R.value("frags", 0));
    std::string lines = R.value("sealed", 0) ? "You did it. The square is quiet, the nights are only nights again. We will sing of this for a hundred winters."
                      : R.value("stage", 0) >= 3 ? "The gateway stands open. He waits beyond it, at the end of his halls. Go together, go ready, and end this."
                      : "It opened when you came. Something old is pressing on our world from the other side. The old tales say the keepers of the deep places hold the keys of time. Find the dungeons, defeat their keepers, bring their Realm Fragments to the rift. Four of them. " + (n ? "You have found " + std::to_string(n) + " so far." : std::string());
    say(lv, p, t, lines);
    t.opts.erase(t.opts.begin() + i);
    return;
  }
  if (a == "house") {
    std::string s; if (v) homeAsk(lv, p, *v, s);
    if (!s.empty()) say(lv, p, t, s);
    t.opts.erase(t.opts.begin() + i);
    return;
  }
  if (a == "path") {
    std::string want = o.arg.is_string() ? o.arg.get<std::string>() : "";
    int cl = combatLevel(p), at = VD().value("pathAt", 20);
    if (!pathOf(p) && cl < at) {
      const json& PD = magicData().paths;
      say(lv, p, t, "Come back when you've seen more fighting. (Paths open at combat level " + std::to_string(at) + "; you're " + std::to_string(cl) + ".) " +
                    (!want.empty() ? "When you do, I'll show you the " + PD[want].value("name", want) + "'s ways." : std::string("Our guards, fletchers and librarians train Knights, Rangers and Mages.")));
      t.opts.erase(t.opts.begin() + i);
      return;
    }
    t.pathHint = want; t.openScreen = "path"; t.closeNow = true;
    return;
  }
}
void talkShopTrade(Level& lv, Player& p, Talk& t, int line) {
  VillageRef v = villageById(lv, t.vid); if (!v) return;
  std::string s;
  if (shopTrade(lv, p, *v, t.idx, t.job, t.tab, line, s)) {
    static const char* T[5] = {"Pleasure!", "Thank you!", "Deal!", "Ooh, good choice.", "Hm-hm!"};
    t.spoken = T[(int)std::floor(rnd() * 5)]; t.spokenVol = 0.4f; t.spokenN++;   // babble(..., { rate: 26, vol: 0.4 })
  } else if (!s.empty()) say(lv, p, t, s);
}
void talkShopBack(Level& lv, Player& p, Talk& t) { home(lv, p, t, "Anything else?"); }

} // namespace dq
