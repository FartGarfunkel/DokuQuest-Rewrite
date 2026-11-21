// sim/skilltree.cpp — see skilltree.h
#include "sim/spell_items.h"
#include "sim/skilltree.h"
#include "sim/player.h"
#include "sim/skills.h"
#include "sim/deeds.h"
#include "common/fs.h"
#include <algorithm>
#include <unordered_map>

namespace dq {

int SkillTree::find(const std::string& id) const {
  for (int i = 0; i < (int)nodes.size(); i++) if (nodes[i].id == id) return i;
  return -1;
}

const std::vector<SkillTree>& skillTrees() {
  static const std::vector<SkillTree> T = [] {
    std::vector<SkillTree> out;
    auto j = readJson(dataDir() / "skilltree.json");
    if (!j || !j->contains("trees")) return out;
    for (const char* k : {"warrior", "ranger", "rogue", "mage"}) {
      if (!(*j)["trees"].contains(k)) continue;
      const json& tj = (*j)["trees"][k];
      SkillTree t; t.key = k; t.name = tj.value("name", std::string(k)); t.limits = tj.value("limits", json::object());
      for (auto& nj : tj["nodes"]) {
        TreeNode n; n.id = nj.value("id", ""); n.title = nj.value("title", ""); n.desc = nj.value("desc", ""); n.col = nj.value("col", "");
        n.x = nj.value("x", 0.f); n.y = nj.value("y", 0.f); n.size = nj.value("size", 0); n.start = nj.value("start", false);
        for (auto& l : nj.value("links", json::array())) n.links.push_back(l);
        for (auto& l : nj.value("one", json::array())) n.oneWay.push_back(l);
        for (auto& l : nj.value("tags", json::array())) n.tags.push_back(l);
        for (auto& b : nj.value("bonus", json::array())) n.bonus.push_back({b.value("attr", ""), b.value("v", 0.f), b.value("op", 0) != 0});
        t.nodes.push_back(std::move(n));
      }
      out.push_back(std::move(t));
    }
    return out;
  }();
  return T;
}

static const json& learnt(const Player& p) {
  static const json none = json::array();
  return p.stats.contains("tree") && p.stats["tree"].is_object() && p.stats["tree"].contains("nodes") && p.stats["tree"]["nodes"].is_array() ? p.stats["tree"]["nodes"] : none;
}
// a point every 3 levels (at most 200), and the deeds' (sim/deeds.h); the start of your Path's tree costs none
int treePointsEarned(const Player& p) { return std::min(200, totalLevel(p) / 3) + deedTalentPoints(p); }
int treePointsSpent(const Player& p) { return std::max(0, (int)learnt(p).size() - treeFreeNodes(p)); }
bool treeHas(const Player& p, const std::string& id) { for (auto& x : learnt(p)) if (x == id) return true; return false; }

std::string treeWhyNot(const Player& p, const SkillTree& t, const TreeNode& n) {
  if (treeHas(p, n.id)) return "Learnt";
  if (treePointsSpent(p) >= treePointsEarned(p) && !treeStartFree(p, t, n)) return "No points left (a point every 3 levels of your skills, and your deeds)";
  // joined to one you have: its own links, or a node you have that links (or leads one-way) to it
  bool joined = n.start;
  for (const std::string& l : n.links) if (treeHas(p, l)) joined = true;
  if (!joined) for (const TreeNode& o : t.nodes) if (treeHas(p, o.id)) {
    for (const std::string& l : o.links) if (l == n.id) joined = true;
    for (const std::string& l : o.oneWay) if (l == n.id) joined = true;
    if (joined) break;
  }
  if (!joined) return n.start ? "" : "Not joined to anything you've learnt";
  // only one tree's start: a class
  if (n.start) for (const SkillTree& o : skillTrees()) if (o.key != t.key) for (const TreeNode& s : o.nodes) if (s.start && treeHas(p, s.id)) return "You follow the " + o.name + "'s way (reset to change)";
  // the tree's limits: so many of a tag
  for (const std::string& tag : n.tags) {
    if (!t.limits.contains(tag) || !t.limits[tag].is_number()) continue;
    int have = 0;
    for (const TreeNode& o : t.nodes) if (treeHas(p, o.id) && std::find(o.tags.begin(), o.tags.end(), tag) != o.tags.end()) have++;
    if (have >= t.limits[tag].get<int>()) return "You have all the " + tag + " you can";
  }
  return "";
}

bool treeLearn(Player& p, const std::string& tree, const std::string& id) {
  for (const SkillTree& t : skillTrees()) {
    if (t.key != tree) continue;
    const int i = t.find(id);
    if (i < 0 || !treeWhyNot(p, t, t.nodes[i]).empty()) return false;
    if (!p.stats.contains("tree") || !p.stats["tree"].is_object()) p.stats["tree"] = json::object();
    p.stats["tree"]["nodes"].push_back(id);
    p.recalcMax();
    return true;
  }
  return false;
}
void treeReset(Player& p) { p.stats["tree"] = json{{"nodes", json::array()}}; p.recalcMax(); }

static float sumAttr(const Player& p, const std::string& attr, bool pct) {
  float s = gemAttr(p, attr, pct);   // (gems set in the gear worn and held: sim/spell_items.h)
  // bonuses granted outside the trees (mods, commands, the balance test): stats "attrBonus" {attr: [flat, pct]}
  if (auto it = p.stats.find("attrBonus"); it != p.stats.end() && it->is_object())
    if (auto a = it->find(attr); a != it->end() && a->is_array() && a->size() == 2 && (*a)[pct ? 1 : 0].is_number()) s += (*a)[pct ? 1 : 0].get<float>();
  if (skillTrees().empty() || learnt(p).empty()) return s;
  for (const SkillTree& t : skillTrees()) for (const TreeNode& n : t.nodes) {
    bool any = false; for (auto& b : n.bonus) if (b.attr == attr && b.pct == pct) any = true;
    if (!any || !treeHas(p, n.id)) continue;
    for (auto& b : n.bonus) if (b.attr == attr && b.pct == pct) s += b.v;
  }
  return s;
}
float treeAttr(const Player& p, const std::string& attr) { return sumAttr(p, attr, false); }
std::vector<std::string> treeGrants(const Player& p, const std::string& prefix) {
  std::vector<std::string> out;
  auto add = [&](const std::string& attr) { if (attr.size() > prefix.size() && attr.compare(0, prefix.size(), prefix) == 0) { std::string k = attr.substr(prefix.size()); if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k); } };
  if (auto it = p.stats.find("attrBonus"); it != p.stats.end() && it->is_object()) for (auto& [k, v] : it->items()) add(k);
  if (learnt(p).empty()) return out;
  for (const SkillTree& t : skillTrees()) for (const TreeNode& n : t.nodes) {
    bool any = false; for (auto& b : n.bonus) if (b.attr.compare(0, prefix.size(), prefix) == 0) any = true;
    if (any && treeHas(p, n.id)) for (auto& b : n.bonus) if (b.v > 0) add(b.attr);
  }
  return out;
}
float treeAttrPct(const Player& p, const std::string& attr) { return sumAttr(p, attr, true); }

} // namespace dq
