// sim/gamecommands.h — the original game's chat commands (game/commands.js COMMANDS: /give /tp /time /gamemode
// /gamerule /weather /kill /heal /xp /summon /setblock /fill /spawnpoint ...) and WorldEdit (//wand //set
// //replace //copy //paste //undo ...), registered into the chat command registry (sim/commands.h) by
// registerGameCommands(). Cheating commands need op / "Allow commands", like the original's cheat flag.
#pragma once
#include "sim/commands.h"
#include "sim/player.h"

namespace dq {

void registerGameCommands();
std::string completeCommand(const std::string& line, std::string& hint);   // Tab completion (command names, item keys)

// block / item names as the commands read them (keys, Minecraft names, display names)
int resolveBlock(const std::string& name);              // -1 unknown, 0 air
std::string resolveItem(const std::string& name);       // "" unknown

// WorldEdit selection (the wand's corners), for the renderer's outline and the controller
struct WeSelection { bool has1 = false, has2 = false; ivec3 pos1{0}, pos2{0}; };
WeSelection weSelection(Level& lv, const Player& p);
void weSetCorner(Level& lv, const Player& p, int which, ivec3 at);
std::string weSizeText(Level& lv, const Player& p);   // " (N blocks selected)" or ""

} // namespace dq
