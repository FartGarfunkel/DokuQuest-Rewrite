// sim/guidance.h — help that comes when it's wanted: the journal's "What next" page (a few goals read from where you are:
// the story's next step, points to spend, a skill close to a perk, a job to take or hand in, a world not yet seen) and the
// first-time hints (a small note, once per player, the first time you get a stat point or a talent point, your standing
// with a village moves, you open a quest board, earn a perk or tame a creature). Seen hints are kept in the player's
// stats "hints" {key: 1}, so they come once in each world.
#pragma once
#include "sim/level.h"
#include "sim/player.h"

namespace dq {

struct NextGoal { std::string key, icon, title, why; };
// 3 to 5 goals, the most pressing first (the story's step is always among them while it runs)
std::vector<NextGoal> whatNext(Level& lv, Player& p);

// the first-time hints: "stat", "talent", "path", "rep", "board", "perk", "tame"
const char* hintText(const std::string& key);                 // its words ("" unknown)
bool hintSeen(const Player& p, const std::string& key);
bool hintOnce(Level& lv, Player& p, const std::string& key);   // shows it (a toast) if not yet seen; true if shown now
void hintsCheck(Level& lv, Player& p);                        // looks for the moments above in the player's state

void registerGuidance();   // called from initCore(): checks each player now and then, and the quest board opening

} // namespace dq
