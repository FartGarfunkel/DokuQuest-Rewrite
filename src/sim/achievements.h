// sim/achievements.h — achievements (BTA's achievement screen; our own list): things done once, kept in the player's
// data["ach"] {id: when}, a toast when earned, shown on their own menu tab (ui/achievements_screen.cpp).
#pragma once
#include "common/common.h"

namespace dq {

struct AchDef { const char* id; const char* title; const char* desc; const char* icon; };
const std::vector<AchDef>& achievementList();
void registerAchievements();   // the checker (a LevelSystem) and its event hooks

} // namespace dq
