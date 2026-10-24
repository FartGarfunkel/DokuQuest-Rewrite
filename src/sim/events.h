// sim/events.h — the game's event bus. Gameplay code emits named events; C++ systems, native
// plugins and JS mods (DQ.on) subscribe. Names follow the original DQ mod API:
//   worldOpen(worldName) tick(dt) blockBreak(player,x,y,z,key) blockPlace(player,x,y,z,key)
//   mobDeath(info,killer) chat(name,text) join(name) leave(name) serverStart serverStop log(text)
// plus C++ additions: playerRespawn, itemUse, entitySpawn, dimensionChange, portalEnter, craft, screenOpen.
// A handler returning false cancels a cancellable event (chat, blockBreak, blockPlace, itemUse, craft).
#pragma once
#include "common/fs.h"
#include <map>

namespace dq {

class EventBus {
public:
  using Handler = std::function<bool(const json& args)>;   // args is a JSON array
  int on(const std::string& name, Handler h, int priority = 0);
  void off(int id);
  bool emit(const std::string& name, const json& args = json::array());   // false if cancelled
  bool has(const std::string& name) const { auto it = handlers.find(name); return it != handlers.end() && !it->second.empty(); }
private:
  struct H { int id, pri; Handler fn; };
  std::map<std::string, std::vector<H>> handlers;
  int nextId = 1;
};

} // namespace dq
