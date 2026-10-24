// sim/rules.h — per-world game rules, day/night length and graves (port of game/rules.js).
// Rules live in LevelInfo::rules (saved in level.json) with the original's keys; anything an older
// world lacks reads as its default. Difference from the original: naturalRegen defaults ON (the
// user's rule: no hunger, health comes back 1 HP / 4 s once you've been unhurt a while).
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"
#include <array>

namespace dq {

class Player;

const json& ruleDefaults();
json& rules(Level& lv);                                    // fills in defaults (the original's rules())
bool rule(Level& lv, const std::string& key);              // boolean rule
double ruleNum(Level& lv, const std::string& key);         // numeric rule (dayMinutes, nightMinutes)
const std::vector<std::array<std::string, 3>>& ruleToggles();   // [key, label, help] for the world settings panel

// The sun moves at a speed that gives day and night their chosen lengths (time stays 0..DAY_LEN,
// the first half is day). Called from Level::tick.
void advanceDayTime(Level& lv, double dt);
float daylight(const Level& lv);                           // the original's env.day for the Overworld (0.2..1)
inline bool isNightForSleep(const Level& lv) { return daylight(lv) < 0.45f; }

// Graves: everything carried goes into a grave where you fell (or the last safe ground).
json makeGrave(Level& lv, Player& p, std::vector<ItemStack> stacks);   // {realm,x,y,z} or null
void openGrave(Level& lv, Player& p, int x, int y, int z);

} // namespace dq
