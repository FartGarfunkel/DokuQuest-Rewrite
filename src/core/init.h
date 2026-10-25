// core/init.h — start-up shared by the game, the launcher's server and the headless server.
// Every subsystem with global registration (world types, world systems, commands, mod API bindings)
// exposes an explicit register function that is called from initCore() — never rely on static
// constructors in other files (the static library linker drops unreferenced objects).
#pragma once
#include "common/settings.h"

namespace dq {

struct CoreOptions {
  bool server = false;          // headless/dedicated server (no client-only content)
  std::string userDirOverride;  // --userdir
};
bool initCore(const CoreOptions& o);   // logging, registry (+ data packs from mods), world types
void shutdownCore();

// Registration hooks (implemented by their subsystems; declared here so initCore can call them)
void registerWorldTypes();       // gen/worldgen*.cpp
void registerCommands();         // sim/commands*.cpp
void registerModApi();           // mod/*.cpp
void registerPortals();          // sim/portals.cpp (+ the stacked worlds, sim/seamless.cpp)
void registerMagic();            // sim/magic.cpp (magic, enchanting, brewing, skills, rpg)

} // namespace dq
