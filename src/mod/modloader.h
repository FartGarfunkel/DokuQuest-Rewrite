// mod/modloader.h — mods, built in (like Fabric + Forge + OptiFine together):
//   * JS mods (QuickJS) using the original DQ API (game/mods.js) — the same .js files the HTML game's Mods panel
//     took: the file is the body of a function given the API as `DQ`. A plugin (DQ.plugin = true) runs only
//     while this game hosts a world (a server, or a world opened to LAN).
//   * data packs: blocks/items/recipes/mobs JSON + textures/*.png, no code
//   * native plugins (.so/.dll) with the C ABI in mod/dq_plugin.h
//   * shader packs (userDir()/shaderpacks) and resource packs are chosen in the settings
// Mods live in userDir()/mods/ as single .js files or folders (mod.json + main.js / data / textures / *.so);
// which are switched on is kept in mods/mods.json. Changes take effect the next time a world opens.
#pragma once
#include "sim/events.h"

namespace dq {

class Level;

struct ModInfo {
  std::string id, name, version, author, description, kind;   // kind: js | data | native
  fs::path path;
  bool enabled = true, plugin = false;   // plugin = host/server plugin (DQ.plugin = true, or "plugin": true in mod.json)
  bool loaded = false;                    // ran / registered this session
  std::string error;                      // the last problem it had (shown in the Mods list)
};

class ModLoader {
public:
  ModLoader();
  ~ModLoader();
  void scan();                                // find mods in userDir()/mods
  std::vector<ModInfo>& list() { return mods; }
  ModInfo* find(const std::string& id);
  void setEnabled(const std::string& id, bool on);
  void saveState();
  bool addMod(const fs::path& file, bool asPlugin, std::string* err = nullptr);   // copy a .js (syntax-checked) or folder into mods/
  bool removeMod(const std::string& id);
  static bool checkSyntax(const std::string& code, std::string* err);           // the original's `new Function('DQ', code)` check
  static std::string exampleMod();                                                // MOD_EXAMPLE

  void loadData();                            // data packs, then JS mods and native plugins run (items, recipes, commands)
  void onWorldOpen(Level& lv, EventBus& events, bool serving);   // their event handlers start hearing this world
  void onWorldClose();
  void setServing(bool on);                   // a world opened to LAN: plugins start (or stop) running
  bool serving() const { return servingNow; }

  // what the DQ API acts on (the open world)
  Level* level = nullptr;
  EventBus* bus = nullptr;
  struct Impl; std::unique_ptr<Impl> impl;
private:
  std::vector<ModInfo> mods;
  bool servingNow = false;
  std::string loadedSet;                      // which mods were run, to know when a reload is due
};

// Textures that mods bring for their blocks: (tile name, png) from each enabled mod's textures/ folder.
// The assets code appends them to the TileSet (setting reg().tileIndex[name] and the faces of blocks whose
// `tex` is that name). A mod texture with a built-in tile's name replaces it, like a resource pack.
std::vector<std::pair<std::string, fs::path>> modTextures();

} // namespace dq
