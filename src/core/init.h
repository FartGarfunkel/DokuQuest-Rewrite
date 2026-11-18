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
void registerWeatherSystem();    // sim/weather.cpp
void registerPortals();          // sim/portals.cpp (+ the stacked worlds, sim/seamless.cpp)
void registerMagic();            // sim/magic.cpp (magic, enchanting, brewing, skills, rpg)
void registerBossBrains();       // sim/boss_brains.cpp
void registerVillageTheft();     // sim/village_theft.cpp
void registerTreasureMaps();     // sim/treasure_maps.cpp
void registerStrung();           // sim/strung.cpp
void registerFallen();           // sim/fallen.cpp (the remains' posed bones as their shapes)
void registerItemUses();         // sim/item_uses.cpp (the War Horn, the dragons' armour sets, the capes)
void registerGuidance();         // sim/guidance.cpp (the first-time hints)
void registerMovingBodies();     // sim/moving.cpp (hinges and sliders)
void registerAttachments();      // sim/attach_play.cpp (parts sharing a cell)
void registerGlue();             // sim/glue.cpp (moving structures: glue between faces)
void registerContent();          // sim/content.cpp (maps, farming, fishing, camping, travel, elytra, stable, places)
void registerEndStory();      // sim/endstory.cpp: the End, dragons, the Deep Dark, the watcher and the rift
void registerVillagers();        // sim/villagers.cpp (villages, villagers, quests, shops, paths, gates, raids)
void registerCreatures();        // sim/creature_spawn.cpp (creature/projectile entities + the spawning system)
void registerRedstone();         // sim/rsplace.cpp: redstone, minecarts, boats/ships, machines

} // namespace dq
