// ui/villager_screens.h — talking with villagers (villagers.js NpcTalk + NpcShop), the quest board (QuestBoard) and
// the paths window (paths.js PathUI), styled like styles.css (.npc-talk, .npc-shop, .quest-board, .path-view).
// Controllers: up/down (and left/right on the board and paths) move the selection, A presses it, left/right switch
// the shop's Buy/Sell pages, B backs out (from the shop to the conversation), Y/E closes. Keys 1–9 pick an option.
#pragma once
#include "ui/screen.h"
#include "common/common.h"

namespace dq {

std::unique_ptr<Screen> makeVillagerTalkScreen(u32 npcId, bool confront = false, const std::string& victim = "", bool startInShop = false);
std::unique_ptr<Screen> makeQuestBoardScreen(ivec3 boardAt);
std::unique_ptr<Screen> makePathScreen(const std::string& hint);

} // namespace dq
