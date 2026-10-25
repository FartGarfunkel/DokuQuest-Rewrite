// sim/commands.h — chat commands (game/commands.js COMMANDS): a registry that the game's own commands, mods
// (DQ.command), native plugins and the server (kick, ban, op...) add to, and the one place a chat line goes
// (submitChat: a /command, or a line said to everyone). Replies are returned as text, like the original.
#pragma once
#include "sim/level.h"

namespace dq {

class Player;

struct CommandSource {
  Level* level = nullptr;
  Player* player = nullptr;     // null = the server console
  std::string name = "Server";
  bool op = true;               // may use cheats and server commands (single player, console, ops)
};
using CommandFn = std::function<std::string(CommandSource& src, const std::vector<std::string>& args)>;

struct ChatCommand {
  std::string name, usage, desc;
  bool cheat = false;           // needs op (or cheats on) — the original's `cheat` flag
  std::string owner;            // mod id that added it ("" = the game)
  bool hidden = false;          // left out of /help, Tab completion and the server's list (the original's `hidden`)
  CommandFn run;
};

// name: /^[a-z0-9_]{1,24}$/i (lower-cased). A later registration with the same name replaces the earlier one.
void registerChatCommand(const std::string& name, const std::string& desc, CommandFn fn, bool cheat = false,
                         const std::string& usage = "", const std::string& owner = "");
void unregisterChatCommands(const std::string& owner);   // a mod unloaded
void hideChatCommand(const std::string& name);           // the original's hidden: true
const ChatCommand* findChatCommand(const std::string& name);
std::vector<ChatCommand> allChatCommands();

// "/name args..." (the slash is optional) -> the reply ("" = nothing to say).
std::string runCommand(Level& lv, CommandSource& src, const std::string& line);
std::string runCommand(Level& lv, Player* p, const std::string& line);

// A line typed in chat by `p` on this device: on a client it goes to the server; otherwise a /command runs
// (its reply goes to that player) or the line is said to everyone (the "chat" event may cancel it).
void submitChat(Level& lv, Player* p, const std::string& text);

} // namespace dq
