// mod/modloader.h — mods, built in (like Fabric + Forge + OptiFine together):
//   * JS mods (QuickJS) using the original DQ API — the same .js files the HTML game's Mods panel took
//   * data packs: blocks/items/recipes/mobs JSON + textures, no code
//   * native plugins (.so/.dll) with the C++ API (mod/native_api.h) for heavy mods
//   * shader packs (userDir()/shaderpacks) override any engine shader
// Mods live in userDir()/mods/<id>/ (mod.json + files) or as single .js files; enabled list in mods.json.
#pragma once
#include "sim/events.h"

namespace dq {

class Level;

struct ModInfo {
  std::string id, name, version, author, description, kind;   // kind: js | data | native | resource | shader
  fs::path path;
  bool enabled = true, plugin = false;   // plugin = host/server plugin (DQ.plugin = true)
  std::string error;
};

class ModLoader {
public:
  ModLoader();
  ~ModLoader();
  void scan();                                // find mods in userDir()/mods
  std::vector<ModInfo>& list() { return mods; }
  void setEnabled(const std::string& id, bool on);
  void saveState();
  void loadData();                            // data packs + registrations, before any world opens
  void onWorldOpen(Level& lv, EventBus& events, bool server);   // run JS mods for this world
  void onWorldClose();
  struct Impl; std::unique_ptr<Impl> impl;
private:
  std::vector<ModInfo> mods;
};

} // namespace dq
