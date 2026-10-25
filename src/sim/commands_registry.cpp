// sim/commands_registry.cpp — the chat command registry and where chat lines go (see sim/commands.h).
#include "sim/commands.h"
#include "sim/player.h"
#include <mutex>
#include <map>

namespace dq {

static std::mutex& cmdMx() { static std::mutex m; return m; }
static std::map<std::string, ChatCommand>& cmds() { static std::map<std::string, ChatCommand> m; return m; }

static bool validName(const std::string& n) {
  if (n.empty() || n.size() > 24) return false;
  for (char c : n) if (!isalnum((unsigned char)c) && c != '_') return false;
  return true;
}

void registerChatCommand(const std::string& name, const std::string& desc, CommandFn fn, bool cheat, const std::string& usage, const std::string& owner) {
  if (!validName(name) || !fn) return;
  ChatCommand c; c.name = toLower(name); c.desc = desc; c.run = std::move(fn); c.cheat = cheat; c.usage = usage; c.owner = owner;
  std::lock_guard<std::mutex> lk(cmdMx());
  cmds()[c.name] = std::move(c);
}
void unregisterChatCommands(const std::string& owner) {
  std::lock_guard<std::mutex> lk(cmdMx());
  for (auto it = cmds().begin(); it != cmds().end();) { if (it->second.owner == owner) it = cmds().erase(it); else ++it; }
}
const ChatCommand* findChatCommand(const std::string& name) {
  std::lock_guard<std::mutex> lk(cmdMx());
  auto it = cmds().find(toLower(name));
  return it == cmds().end() ? nullptr : &it->second;
}
std::vector<ChatCommand> allChatCommands() {
  std::lock_guard<std::mutex> lk(cmdMx());
  std::vector<ChatCommand> v; for (auto& [k, c] : cmds()) v.push_back(c);
  return v;
}

static std::vector<std::string> splitWords(const std::string& s) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (isspace((unsigned char)c)) { if (!cur.empty()) out.push_back(cur), cur.clear(); } else cur += c; }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string runCommand(Level& lv, CommandSource& src, const std::string& line) {
  std::string s = line;
  while (!s.empty() && s[0] == '/') s.erase(0, 1);
  auto parts = splitWords(s);
  if (parts.empty()) return "";
  std::string name = toLower(parts[0]);
  parts.erase(parts.begin());
  src.level = &lv;
  if (name == "help" && !findChatCommand("help")) {
    if (!parts.empty()) { auto* c = findChatCommand(parts[0]); return c ? "/" + c->name + " " + c->usage + " — " + c->desc : "No such command"; }
    std::string out = "Commands:";
    for (auto& c : allChatCommands()) out += " /" + c.name;
    return out + "\n/help <command> for details.";
  }
  ChatCommand cmd;
  { const ChatCommand* c = findChatCommand(name); if (!c) return "No such command: /" + name + " (try /help)"; cmd = *c; }
  if (cmd.cheat && !src.op)
    return src.player && lv.net ? "Only the host can use commands" : "Commands are off in this world. Turn on \"Allow commands\" in World settings (pause menu).";
  try { return cmd.run(src, parts); }
  catch (const std::exception& e) { return std::string("That went wrong: ") + e.what(); }
}

std::string runCommand(Level& lv, Player* p, const std::string& line) {
  CommandSource src; src.level = &lv; src.player = p;
  if (p) { src.name = p->name; src.op = lv.info.rules.is_object() ? lv.info.rules.value("commands", true) : true; }
  return runCommand(lv, src, line);
}

void submitChat(Level& lv, Player* p, const std::string& text0) {
  std::string text = text0.substr(0, 200);
  while (!text.empty() && isspace((unsigned char)text.back())) text.pop_back();
  if (text.empty()) return;
  if (p && lv.net && lv.net->chat(*p, text)) return;   // a client: the server answers
  std::string who = p ? p->name : "Server";
  if (text[0] == '/') {
    std::string out = runCommand(lv, p, text);
    if (!out.empty()) lv.fx->message(p ? p->id : 0, out, "cmd");
    return;
  }
  if (lv.events && !lv.events->emit("chat", json::array({who, text}))) return;   // a mod/plugin said no
  lv.fx->message(0, "<" + who + "> " + text, "chat");
}

} // namespace dq
