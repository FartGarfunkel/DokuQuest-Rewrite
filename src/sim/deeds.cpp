// sim/deeds.cpp — see deeds.h
#include "sim/deeds.h"
#include "sim/rpg.h"
#include "sim/skills.h"
#include "sim/skilltree.h"
#include "sim/creature.h"
#include "sim/content.h"
#include "sim/villagers.h"
#include "sim/achievements.h"
#include "sim/magic_data.h"
#include "core/metals.h"
#include "core/registry.h"
#include <cmath>
#include <unordered_map>

namespace dq {

// ---- deeds ----------------------------------------------------------------------------------------------------------------
static const json& deedsOf(const Player& p) {
  static const json none = json::object();
  if (!p.data.is_object()) return none;
  auto it = p.data.find("deeds");
  return it != p.data.end() && it->is_object() ? *it : none;
}
int deedTalentPoints(const Player& p) { const json& D = deedsOf(p); return D.contains("tp") && D["tp"].is_number_integer() ? std::max(0, D["tp"].get<int>()) : 0; }
int deedStatPoints(const Player& p) { const json& D = deedsOf(p); return D.contains("sp") && D["sp"].is_number_integer() ? std::max(0, D["sp"].get<int>()) : 0; }
bool deedDone(const Player& p, const std::string& key) { const json& D = deedsOf(p); return D.contains("got") && D["got"].is_object() && D["got"].contains(key); }

static std::string points(int n, const char* what) { return "+" + std::to_string(n) + " " + what + (n == 1 ? " point" : " points"); }

bool deedGrant(Level& lv, Player& p, const std::string& key, int talent, int stat, const std::string& why, const ItemStack& keepsake) {
  if (deedDone(p, key)) return false;
  if (!p.data.is_object()) p.data = json::object();
  json& D = p.data["deeds"];
  if (!D.is_object()) D = json::object();
  if (!D["got"].is_object()) D["got"] = json::object();
  D["got"][key] = 1;
  if (talent) D["tp"] = deedTalentPoints(p) + talent;
  if (stat) D["sp"] = deedStatPoints(p) + stat;
  std::string what;
  if (talent) what = points(talent, "talent");
  if (stat) what += (what.empty() ? "" : ", ") + points(stat, "stat");
  if (!keepsake.empty()) {
    const std::string nm = keepsake.tag.is_object() && keepsake.tag.contains("name") && keepsake.tag["name"].is_string() ? keepsake.tag["name"].get<std::string>() : keepsake.id;
    what += (what.empty() ? "" : " and ") + std::string("a keepsake: ") + nm;
    static_cast<void (*)(Level&, Player&, const ItemStack&)>(&giveOrDrop)(lv, p, keepsake);   // into the bag, else at their feet (sim/content.h)
  }
  if (lv.fx) lv.fx->message(p.id, why + " \xE2\x80\x94 " + what + (talent || stat ? " (your skill book)" : ""), "epic");
  if (lv.fx) lv.fx->soundUi(p.id, "levelup");
  if (stat) p.statPoints += stat;   // (combat_rpg.cpp works it out again each tick: this is only so it shows at once)
  return true;
}

// a small thing to keep from each chapter of the story: an item of the chapter's own, with its name on it
ItemStack deedKeepsake(const std::string& chapter, const std::string& title) {
  static const std::unordered_map<std::string, std::pair<const char*, const char*>> K = {
    {"village", {"emerald", "Token of the First Village"}},   {"iron", {"iron_nugget", "First Iron"}},
    {"tear", {"amethyst_shard", "Shard of the Tear"}},         {"keepers", {"compass", "The Keepers' Compass"}},
    {"depths", {"cinder_powder", "Ember from Below"}},         {"skylands", {"feather", "Feather from the Clouds"}},
    {"gateway", {"glowstone_dust", "Light of the Gateway"}},   {"realm", {"rift_crystal", "Splinter of His Realm"}},
    {"storm", {"ghast_tear", "The Storm's Last Tear"}},        {"seal", {"nether_star", "Star of the Seal"}},
    {"end", {"ender_pearl", "Pearl of the End"}},              {"deepdark", {"echo_shard", "Echo of the Deep"}},
    {"dragons", {"dragon_scale", "The Dragonslayer's Scale"}},
  };
  auto it = K.find(chapter);
  if (it == K.end() || !reg().item(it->second.first)) return {};
  ItemStack s(it->second.first, 1);
  s.tag = json{{"name", it->second.second}, {"keepsake", chapter}, {"chapter", title}};
  return s;
}

void deedsCheckStory(Level& lv, Player& p) {
  if (p.creative()) return;
  const Story s = storyOf(lv, p);
  for (const StoryChapter& c : s.ch)
    if (c.done && !deedDone(p, "ch:" + c.id)) deedGrant(lv, p, "ch:" + c.id, 1, 0, "Chapter done: " + c.t, deedKeepsake(c.id, c.t));
}

// the achievements that take a real journey or a real fight: a talent point each
bool deedHardAchievement(const std::string& id) {
  static const char* H[] = {"diamond", "enchant", "forge", "nether", "blaze", "sky", "end", "dragon", "glide", "wither", "storm"};
  for (const char* h : H) if (id == h) return true;
  return false;
}
void deedsOnAchievement(Level& lv, Player& p, const std::string& id) {
  if (lv.isRemote() || p.creative() || !deedHardAchievement(id)) return;
  std::string title = id;
  for (auto& d : achievementList()) if (id == d.id) title = d.title;
  deedGrant(lv, p, "ach:" + id, 1, 0, "Achievement: " + title);
}

static void bossBeaten(Level& lv, Player& p, const std::string& type) {
  if (p.creative() || p.dead) return;
  const CreatureType* ct = creatureType(type);
  std::string nm = ct && !ct->label.empty() ? ct->label : type;
  if (!nm.empty()) nm[0] = (char)std::toupper((unsigned char)nm[0]);
  deedGrant(lv, p, "boss:" + type, 0, 1, "First time beating " + nm);
}

// ---- work experience ------------------------------------------------------------------------------------------------------
void workXp(Level& lv, Player& p, const std::string& skill, double xp) {
  if (xp <= 0 || p.dead) return;
  // a guest's copy on the host: their own game grows it (net/server_host.cpp sends them "workXp")
  if (p.ghost) { if (lv.events && !lv.isRemote()) lv.events->emit("workXp", json::array({p.name, skill, xp})); return; }
  skillGain(lv, p, skill, xp);
}
void workXpNear(Level& lv, int realm, vec3 at, float range, const std::string& skill, double xp) {
  Player* best = nullptr; float bd = range * range;
  for (auto& e : lv.entities()) {
    if (e->removed || !e->isPlayer() || e->realm != realm) continue;
    auto* p = static_cast<Player*>(e.get());
    const vec3 d = p->pos - at; const float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if (!p->dead && d2 <= bd) { bd = d2; best = p; }
  }
  if (best) workXp(lv, *best, skill, xp);
}
void onKnapped(Level& lv, Player& p, const std::string& material) {
  workXp(lv, p, "smithing", material == "clay" || material == "fireclay" ? workxp::KNAP_CLAY : workxp::KNAP_STONE);
}
void onAnvilWorked(Level& lv, Player& p, int quality) {
  workXp(lv, p, "smithing", workxp::ANVIL_WORK + (quality == 0 ? workxp::ANVIL_EXACT : 0));
}
void onRidden(Level& lv, Player& p, float metres) {
  if (metres <= 0 || p.creative()) return;
  if (!p.data.is_object()) p.data = json::object();
  double m = p.data.value("rideM", 0.0) + metres;
  while (m >= workxp::RIDE_PER) { m -= workxp::RIDE_PER; workXp(lv, p, "taming", workxp::RIDE_XP); }
  p.data["rideM"] = m;
}

static bool metalPiece(const ItemDef& d, const std::string& id) {
  if (!(d.hasTool || d.has("armor")) || d.has("staff")) return false;
  if (id.rfind("mixed_", 0) == 0 || id.rfind("chain", 0) == 0) return true;
  const size_t u = id.find('_');
  return u != std::string::npos && metalDef(id.substr(0, u)) != nullptr;
}

// ---- the Paths and the trees ----------------------------------------------------------------------------------------------
std::string treeOfPath(const std::string& path) {
  if (path == "knight") return "warrior";
  if (path == "ranger" || path == "rogue" || path == "mage") return path;
  return "";
}
std::string pathOfTree(const std::string& tree) {
  if (tree == "warrior") return "knight";
  if (tree == "ranger" || tree == "rogue" || tree == "mage") return tree;
  return "";
}
std::string trainerJobOf(const std::string& path) {
  const json& TR = VD().contains("trains") ? VD()["trains"] : json();
  if (TR.is_object()) for (auto& [job, k] : TR.items()) if (k.is_string() && k.get<std::string>() == path) return job;
  return "";
}
std::string playerPathKey(const Player& p) {
  if (!p.stats.is_object() || !p.stats.contains("path") || !p.stats["path"].is_object()) return "";
  return p.stats["path"].value("k", std::string());
}
std::string pathTrainerName(const Player& p) {
  if (playerPathKey(p).empty()) return "";
  return p.stats["path"].value("by", std::string());
}
static std::string pathName(const std::string& path) {
  const json& PD = magicData().paths;
  return PD.is_object() && PD.contains(path) ? PD[path].value("name", path) : path;
}
static const SkillTree* treeByKey(const std::string& key) {
  for (const SkillTree& t : skillTrees()) if (t.key == key) return &t;
  return nullptr;
}
bool treeStartFree(const Player& p, const SkillTree& t, const TreeNode& n) {
  if (!n.start) return false;
  const std::string path = playerPathKey(p);
  return !path.empty() && treeOfPath(path) == t.key;
}
int treeFreeNodes(const Player& p) {
  const SkillTree* t = treeByKey(treeOfPath(playerPathKey(p)));
  if (!t) return 0;
  for (const TreeNode& n : t->nodes) if (n.start && treeHas(p, n.id)) return 1;
  return 0;
}
void pathTreeOnPick(Level& lv, Player& p, const std::string& path) {
  const SkillTree* t = treeByKey(treeOfPath(path));
  if (!t) return;
  const TreeNode* start = nullptr;
  for (const TreeNode& n : t->nodes) if (n.start) { start = &n; break; }
  if (!start) return;
  const std::string by = pathTrainerName(p);
  const std::string from = by.empty() ? "" : " (" + by + " showed you how)";
  if (treeHas(p, start->id)) { toast(lv, p, "Your " + t->name + " tree's first node costs no point now: one back to spend" + from, "good"); return; }
  if (treeLearn(p, t->key, start->id)) { toast(lv, p, "The " + t->name + " tree opens to you: its first node is yours, free" + from, "good"); return; }
  toast(lv, p, "Reset your skill tree to take the " + t->name + "'s way: to a " + pathName(path) + " its first node is free", "");
}
void pathTreeOnStart(Level& lv, Player& p, const std::string& tree) {
  if (!playerPathKey(p).empty()) return;
  const std::string path = pathOfTree(tree), job = trainerJobOf(path);
  if (path.empty() || job.empty()) return;
  std::string jt = jobTitle(job); for (auto& c : jt) c = (char)std::tolower((unsigned char)c);
  toast(lv, p, "A village " + jt + " teaches the " + pathName(path) + "'s path (combat level " + std::to_string(VD().value("pathAt", 20)) +
               "). Walk it and this first node gives its point back", "");
}
std::string treeTrainerLine(const Player& p, const std::string& tree) {
  const std::string path = pathOfTree(tree);
  if (path.empty()) return "";
  if (playerPathKey(p) == path) {
    const std::string by = pathTrainerName(p);
    return "Your path: the " + pathName(path) + "'s" + (by.empty() ? std::string() : ", taught by " + by) + " \xC2\xB7 its first node is free";
  }
  const std::string job = trainerJobOf(path);
  if (job.empty()) return "";
  std::string jt = jobTitle(job); for (auto& c : jt) c = (char)std::tolower((unsigned char)c);
  return "The " + pathName(path) + "'s path: a village " + jt + " teaches it";
}

// ---- the checker --------------------------------------------------------------------------------------------------------
void registerDeeds() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      Level* L = nullptr; double t = 0; std::vector<int> hs;
      std::unordered_map<u32, std::pair<vec3, u32>> rode;   // a rider's last place, and on what
      const char* name() const override { return "deeds"; }
      ~S() override { if (L && L->events) for (int h : hs) L->events->off(h); }
      void tick(Level& lv, double dt) override {
        // riding: wherever the rider's own game is (a guest's here too: their own player)
        for (auto& e : lv.entities()) {
          if (e->removed || !e->isPlayer() || e->ghost) continue;
          auto* p = static_cast<Player*>(e.get());
          if (!p->riding || p->dead) { rode.erase(p->id); continue; }
          auto it = rode.find(p->id);
          if (it != rode.end() && it->second.second == p->riding) {
            const float d = std::hypot(p->pos.x - it->second.first.x, p->pos.z - it->second.first.z);
            if (d < 20) onRidden(lv, *p, d);   // (a jump through a portal or a teleport is no ride)
          }
          rode[p->id] = {p->pos, p->riding};
        }
        if (lv.isRemote() || (t += dt) < 2) return;
        t = 0;
        for (auto& e : lv.entities()) if (!e->removed && e->isPlayer()) {
          auto* p = static_cast<Player*>(e.get());
          if (!p->dead) deedsCheckStory(lv, *p);
        }
      }
    };
    auto s = std::make_shared<S>(); s->L = &lv;
    if (lv.events) {
      // a boss falls: a stat point the first time, to whoever struck it down and everyone close by
      s->hs.push_back(lv.events->on("mobDeath", [&lv](const json& a) {
        if (lv.isRemote() || !a.is_array() || a.empty() || !a[0].is_object()) return true;
        const std::string type = a[0].value("type", std::string());
        const CreatureType* ct = creatureType(type);
        if (!ct || !ct->boss) return true;
        const vec3 at(a[0].value("x", 0.f), a[0].value("y", 0.f), a[0].value("z", 0.f));
        Player* killer = nullptr;   // (a guest's copy too: their points are kept here, the host's)
        if (a.size() > 1 && a[1].is_string()) for (Player* p : levelPlayers(lv, true)) if (p->name == a[1].get<std::string>()) killer = p;
        if (killer) bossBeaten(lv, *killer, type);
        for (auto& e : lv.entities()) {
          if (e->removed || !e->isPlayer() || e.get() == killer) continue;
          if (killer && e->realm != killer->realm) continue;
          if (glm::distance(e->pos, at) < 48) bossBeaten(lv, *static_cast<Player*>(e.get()), type);
        }
        return true;
      }));
      // made at a crafting table (the screen or the table in the world): metal gear teaches Smithing, food Cooking
      s->hs.push_back(lv.events->on("craft", [&lv](const json& a) {
        if (!a.is_array() || a.size() < 3 || !a[0].is_string() || !a[1].is_string()) return true;
        Player* p = findPlayer(lv, a[0].get<std::string>());   // (a player whose own state is here: not a guest's copy)
        const std::string id = a[1].get<std::string>();
        const ItemDef* d = reg().item(id);
        if (!p || !d) return true;
        const int n = std::max(1, a[2].is_number_integer() ? a[2].get<int>() : 1);
        if (d->has("food")) workXp(lv, *p, "cooking", workxp::TABLE_FOOD * n);
        else if (metalPiece(*d, id)) workXp(lv, *p, "smithing", workxp::TABLE_METAL * n);
        return true;
      }));
      // the host's work for a guest, told to their game (net/server_host.cpp): grown on their own player
      s->hs.push_back(lv.events->on("workXp", [&lv](const json& a) {
        if (!a.is_array() || a.size() < 3 || !a[0].is_string() || !a[1].is_string() || !a[2].is_number()) return true;
        Player* p = findPlayer(lv, a[0].get<std::string>());
        if (p && !p->ghost && !p->dead) skillGain(lv, *p, a[1].get<std::string>(), a[2].get<double>());
        return true;
      }));
    }
    return s;
  });
}

} // namespace dq
