// sim/guidance.cpp — the journal's "What next" and the first-time hints (sim/guidance.h).
#include "sim/guidance.h"
#include "sim/rpg.h"
#include "sim/skills.h"
#include "sim/skilltree.h"
#include "sim/villagers.h"
#include "sim/magic_data.h"
#include "sim/creature.h"
#include "sim/content.h"
#include "sim/events.h"

namespace dq {

namespace {
const json& sub(const json& j, const char* k) { static const json none = json::object(); return j.is_object() && j.contains(k) && j[k].is_object() ? j[k] : none; }
bool onAnyPath(const Player& p) { return p.stats.is_object() && p.stats.contains("path") && p.stats["path"].is_object() && p.stats["path"].contains("k"); }
int treeLeft(const Player& p) { return skillTrees().empty() ? 0 : std::max(0, treePointsEarned(p) - treePointsSpent(p)); }
int pathLeft(const Player& p) { return onAnyPath(p) ? std::max(0, pathPoints(p) - pathSpent(p)) : 0; }
bool pathReady(const Player& p) { return !onAnyPath(p) && pathPoints(p) > 0; }
bool anyRep(const Player& p) { for (auto& [k, v] : sub(p.stats, "vrep").items()) if (v.is_number() && v.get<double>() != 0) return true; return false; }
bool anyPerk(const Player& p) { for (auto& s : magicData().skills) if (skillLv(p, s.key) >= 30) return true; return false; }
std::string plural(int n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
}

// ---- the first-time hints ----
const char* hintText(const std::string& k) {
  if (k == "stat") return "Tip: a stat point to spend. Open your skills (K; on a controller, the menu tabs with LB / RB) and put it into a stat on the right-hand page (RT crosses over, A spends it).";
  if (k == "talent") return "Tip: a talent point. Your skill tree (K) takes it: pick a node joined to one you have (the d-pad on a controller) and learn it (click, or A).";
  if (k == "path") return "Tip: you're ready to walk a path. Ask a village guard (Knight), fletcher (Ranger) or librarian (Mage) to teach you; each step on it brings talent points to spend with them.";
  if (k == "rep") return "Tip: villages remember you. Jobs, trade and defending them raise your standing; Friends, the Trusted and the Honoured see more in the shops, and the best of them pay less. Harm a village and it falls.";
  if (k == "board") return "Tip: the quest board holds the day's bounties and supply runs. Take one (click, or A), do it, then come back here or to the villager for coins, experience and standing. New notes go up each day.";
  if (k == "perk") return "Tip: your first perk! Every skill earns one at levels 30, 60 and 90. Your skills book (K, or LB / RB in the menus) lists them all.";
  if (k == "tame") return "Tip: a companion! It follows you and joins your stable (H, or a whistle). With an empty hand, sneak and use it to carry it; a War Horn calls all of yours to you.";
  return "";
}
bool hintSeen(const Player& p, const std::string& k) { return sub(p.stats, "hints").contains(k); }
bool hintOnce(Level& lv, Player& p, const std::string& k) {
  if (hintSeen(p, k) || !*hintText(k)) return false;
  if (!p.stats.is_object()) p.stats = json::object();
  if (!p.stats.contains("hints") || !p.stats["hints"].is_object()) p.stats["hints"] = json::object();
  p.stats["hints"][k] = 1;
  toast(lv, p, hintText(k), "hint");
  return true;
}
void hintsCheck(Level& lv, Player& p) {
  if (p.mode == "spectator") return;
  if (p.statPoints > 0) hintOnce(lv, p, "stat");
  if (treeLeft(p) > 0) hintOnce(lv, p, "talent");
  if (pathReady(p)) hintOnce(lv, p, "path");
  if (anyRep(p)) hintOnce(lv, p, "rep");
  if (anyPerk(p)) hintOnce(lv, p, "perk");
  if (!hintSeen(p, "tame"))
    for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->tamed && c->owner == p.name && !isVillager(*c)) { hintOnce(lv, p, "tame"); break; }
}

// ---- What next ----
std::vector<NextGoal> whatNext(Level& lv, Player& p) {
  std::vector<NextGoal> g;
  // the story's next step: the journal's clue
  Story s = storyOf(lv, p);
  if (s.cur >= 0 && s.cur < (int)s.ch.size() && !s.ch[s.cur].done) {
    const StoryChapter& c = s.ch[s.cur];
    g.push_back({"story", "book", "The story: " + c.t, "\"" + c.clue + "\" (Your story, a page back, reads it closer.)"});
  }
  // points waiting to be spent
  if (p.statPoints > 0) g.push_back({"stat", "experience_bottle", "Spend " + plural(p.statPoints, "stat point", "stat points"), "Your skills book (K): the stats on its right-hand page. Constitution for health, Strength for blows, Agility for speed..."});
  if (int t = treeLeft(p)) g.push_back({"talent", "enchanted_book", "Learn " + plural(t, "talent", "talents") + " on your skill tree", "Your skills book (K): pick a node joined to one you have."});
  if (pathReady(p)) g.push_back({"path", "iron_sword", "Choose a path", "A village guard teaches the Knight's ways, a fletcher the Ranger's, a librarian the Mage's."});
  else if (int t = pathLeft(p)) g.push_back({"path", "iron_sword", "Learn " + plural(t, "path talent", "path talents"), "Whoever taught you your path teaches the next step: talk to them."});
  // a job: one to hand in, one under way, or the board
  {
    json& Q = npcStats(p)["quests"];
    const json* ready = nullptr; const json* going = nullptr;
    for (auto& q : Q) if (q.is_object()) { if (questReady(lv, p, q)) { if (!ready) ready = &q; } else if (!going) going = &q; }
    if (ready) g.push_back({"quest", "emerald", "Hand in: " + ready->value("title", std::string("a job")), "It's done: go back to the village for the reward."});
    else if (going) g.push_back({"quest", "paper", "Under way: " + going->value("title", std::string("a job")), going->value("text", std::string())});
    else if (!sub(p.stats, "vseen").empty()) g.push_back({"quest", "paper", "Take a job from a quest board", "Every village's board has bounties and supply runs, new each day: coins, experience and standing."});
    else g.push_back({"quest", "bell", "Find a village", "Villagers trade, give out jobs and teach the paths. Their quest board has work every day."});
  }
  // a skill close to a perk
  {
    std::string best; int gap = 99, at = 0; std::string perk, desc;
    const auto& M = magicData();
    for (auto& sk : M.skills) {
      const int lvn = skillLv(p, sk.key);
      auto it = M.perks.find(sk.key); if (it == M.perks.end()) continue;
      for (auto& pk : it->second) if (pk.lvl > lvn) { if (pk.lvl - lvn < gap) { gap = pk.lvl - lvn; best = sk.name; at = pk.lvl; perk = pk.name; desc = pk.desc; } break; }
    }
    if (!best.empty() && gap <= 10)
      g.push_back({"perk", "iron_pickaxe", best + ": " + plural(gap, "level", "levels") + " to " + perk, perk + " (level " + std::to_string(at) + "): " + desc + "."});
  }
  // a world not yet seen
  {
    const json& dims = sub(sub(p.stats, "explore"), "dims");
    static const std::vector<std::tuple<const char*, const char*, const char*, const char*>> W = {
      {"1", "netherrack", "Go down to the Ember Depths", "An obsidian frame (2 x 3 or more) lit with flint and steel opens the way."},
      {"2", "feather", "Go up to the Skylands", "A frame of glowstone filled from a water bucket opens the way up."},
      {"5", "end_stone", "Find the End", "A stronghold lies deep in the Overworld: twelve Eyes of Ender in its frames open the pool."}};
    for (auto& [k, icon, t, why] : W) if (!dims.contains(k)) { g.push_back({"world", icon, t, why}); break; }
  }
  // enough to choose from: the Compendium, new lands
  if (g.size() < 3) {
    int met = (int)sub(p.stats, "dex").size(), all = 0;
    if (reg().mobs.is_object()) for (auto& [k, m] : reg().mobs.items()) if (m.is_object() && !m.value("npc", false)) all++;
    g.push_back({"dex", "spawn_egg", "Meet new creatures", std::to_string(met) + " of " + std::to_string(all) + " met so far: the Compendium (in this journal) fills in as you get a good look at each."});
  }
  if (g.size() < 3) g.push_back({"lands", "empty_map", "Find a land you haven't seen", "Every new land is Exploring experience, and new creatures, plants and stones."});
  if (g.size() < 3) g.push_back({"gear", "anvil", "Better your gear", "Tools and armour gain levels as you use them; the anvil mends them and the smithing table upgrades them."});
  if (g.size() > 5) g.resize(5);
  return g;
}

namespace {
class GuidanceSystem : public LevelSystem {
public:
  explicit GuidanceSystem(Level& l) : lv(l) {
    if (!lv.events) return;
    // villagerScreen [player, "board", ...]: the quest board opened
    h = lv.events->on("villagerScreen", [this](const json& a) {
      if (lv.isRemote() || !a.is_array() || a.size() < 2 || !a[1].is_string() || a[1] != "board") return true;
      if (Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "")) hintOnce(lv, *p, "board");
      return true;
    });
  }
  ~GuidanceSystem() override { if (lv.events && h) lv.events->off(h); }
  const char* name() const override { return "guidance"; }
  void tick(Level& l, double dt) override {
    if (l.isRemote() || (acc += dt) < 1.5) return;
    acc = 0;
    for (auto& e : l.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && !p->ghost && !p->dead) hintsCheck(l, *p);
  }
private:
  Level& lv; int h = 0; double acc = 0;
};
}

void registerGuidance() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level& l) -> std::shared_ptr<LevelSystem> { return std::make_shared<GuidanceSystem>(l); });
}

} // namespace dq
