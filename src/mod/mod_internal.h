// mod/mod_internal.h — what JS mods and native plugins share inside mod/ (not for use elsewhere).
#pragma once
#include "mod/modloader.h"
#include <set>

namespace dq {

// One running mod: a QuickJS context (js_mod.cpp) or a loaded native library (native_mod.cpp).
class ModRuntime {
public:
  virtual ~ModRuntime() = default;
  std::string id, name;
  ModLoader* loader = nullptr;
  bool plugin = false;
  std::set<std::string> listened;                       // events it has handlers for
  virtual bool emit(const std::string& ev, const json& args) = 0;   // false = a handler said no
  virtual void tick(double dt) {}
  ModInfo* info() { return loader ? loader->find(id) : nullptr; }
  void fault(const std::string& what);                  // the original's modFault: remember it, warn once
  bool warned = false;
};

std::unique_ptr<ModRuntime> loadJsMod(ModLoader& L, ModInfo& m, const std::string& code, std::string& err);
std::unique_ptr<ModRuntime> loadNativeMod(ModLoader& L, ModInfo& m, const fs::path& lib, std::string& err);
bool jsCheckSyntax(const std::string& code, std::string* err);

// The DQ API itself, shared by both kinds of mod (game/mods.js modApi).
namespace modapi {
json playerObj(ModLoader& L, const std::string& name);               // {name,x,y,z,dim,realm} (as much as is known)
json players(ModLoader& L);
json getBlock(ModLoader& L, double x, double y, double z, int realm);  // key or null
bool setBlock(ModLoader& L, double x, double y, double z, const std::string& key, int meta, int realm);
bool give(ModLoader& L, const std::string& name, const std::string& item, int n);
u32 spawn(ModLoader& L, const std::string& type, double x, double y, double z);
void say(ModLoader& L, const std::string& from, const std::string& text);
void toast(ModLoader& L, const std::string& text);
double time(ModLoader& L, double setFraction);   // < 0 reads
void log(ModLoader& L, const std::string& from, const std::string& text);
bool registerItem(const std::string& modId, json def);
bool registerBlock(const std::string& modId, json def);
bool addRecipe(const json& r);
bool command(ModRuntime& m, const std::string& name, const std::string& desc, std::function<std::string(const json& player, const json& args)> fn);
void listen(ModRuntime& m, const std::string& ev);   // a mod started listening for `ev` (subscribes when a world is open)
json gameInfo(ModLoader& L);
} // namespace modapi

} // namespace dq
