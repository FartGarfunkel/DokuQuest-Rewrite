// sim/tinyllm.h — spoken lines from TinyLLM's 124M-0.0 model (weiser/124M-0.0, a GPT-2 trained on
// Fineweb). Villagers use it when the player types a line. The canned replies stay the fallback.
#pragma once
#include "common/common.h"
#include <string>

namespace dq {

// What the villager knows, written on the main thread. The model never touches the world.
struct NpcMindBrief {
  std::string name, player, line, world;
  bool where = false;   // they were asked the way somewhere: a reply with no direction is dropped
};

bool tinyllmReady();   // the weights are on disk (userDir()/models/tinyllm/124M-0.0)
// Blocking. Empty when the model is missing or the line is no good.
std::string tinyllmComplete(const NpcMindBrief& b);

// A reply worked out off the game thread. `world` lines are said in the square; the others fill the talk screen.
void tinyllmAsk(u32 who, bool world, NpcMindBrief brief, std::string fallback, std::string speaker);
bool tinyllmTakeTalk(u32 who, std::string& out);
void tinyllmDropTalk(u32 who);
struct TinyWorldLine { u32 who = 0; std::string speaker, line; };
std::vector<TinyWorldLine> tinyllmTakeWorld();

} // namespace dq
