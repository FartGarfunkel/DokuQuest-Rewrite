// sim/village_theft.h — taking what isn't yours in a village. Its chests, barrels and cupboards (and its beds, work
// stations and crops) belong to it; what a player put down there is theirs. Seen taking (a villager or guard that can see
// you: sneaking in the dark helps), it costs you standing with that village and a guard comes over: give it back, pay a
// fine in coins, or refuse and fight. Wanted (refused, or caught again and again), its guards attack on sight until the
// fine is paid or a few days pass. Each player's standing and record are their own (Player::stats["theft"]).
#pragma once
#include "sim/level.h"
#include "sim/player.h"

namespace dq {

int theftFine(Player& p, const std::string& vid);            // coins owed (0: nothing)
bool theftWanted(Player& p, const std::string& vid);
std::string theftWhat(Player& p, const std::string& vid);    // "6 Bread and 3 Iron Ingots"
bool theftGiveBack(Level& lv, Player& p, const std::string& vid);   // what's on you goes back; false: nothing owed
bool theftPayFine(Level& lv, Player& p, const std::string& vid);    // false: you can't pay it
void theftRefuse(Level& lv, Player& p, const std::string& vid);     // wanted: the guards attack
void theftForgive(Level& lv, Player& p, const std::string& vid);    // the record wiped (time, or a test)
double shopPriceFactor(Player& p, const std::string& vid);           // a known thief pays more
void registerVillageTheft();

} // namespace dq
