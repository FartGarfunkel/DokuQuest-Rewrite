// sim/skilltree.h — the four path trees in data/skilltree.json (Knight, Ranger, Rogue, Mage). Points come from your total level
// (a point every 3) and your deeds (sim/deeds.h); the start of your Path's tree is free. You learn a tree's starting
// node first, then any node joined to one you have. A tree's limits (its tags) cap how many of a kind (one subclass, two
// keystones of a school...). What you've learnt adds to your attributes:
// treeAttr(p, "health") the flat bonus, treeAttrPct(p, "damage") the percentage (0.1 = +10%).
// Saved in the player's stats "tree": {"nodes": [ids]}.
#pragma once
#include "common/common.h"
#include "common/fs.h"
#include <string>
#include <vector>

namespace dq {
class Player;

struct TreeNode {
  std::string id, title, desc, col;
  float x = 0, y = 0; int size = 0; bool start = false;
  std::vector<std::string> links, oneWay, tags;
  struct Bonus { std::string attr; float v = 0; bool pct = false; };
  std::vector<Bonus> bonus;
};
struct SkillTree { std::string key, name; std::vector<TreeNode> nodes; json limits; int find(const std::string& id) const; };

const std::vector<SkillTree>& skillTrees();          // empty without the import
int treePointsEarned(const Player& p);
int treePointsSpent(const Player& p);
bool treeHas(const Player& p, const std::string& id);
// can it be learnt now (a point free, joined to one you have or a start, within its tree's limits): "" if so, else why not
std::string treeWhyNot(const Player& p, const SkillTree& t, const TreeNode& n);
bool treeLearn(Player& p, const std::string& tree, const std::string& id);
void treeReset(Player& p);
float treeAttr(const Player& p, const std::string& attr);      // flat
float treeAttrPct(const Player& p, const std::string& attr);   // fraction
// the keys of what you've learnt (and stats "attrBonus") that start with `prefix`, the prefix cut off: "grant_spell." -> the
// spells the trees grant (sim/magic.cpp knownSpells)
std::vector<std::string> treeGrants(const Player& p, const std::string& prefix);
} // namespace dq
