// gen/register.cpp — world types known to the game. The full port of the original world generator
// registers "default" here (see gen/worldgen.h); "debug" (simple noise) lives in generator.cpp.
#include "core/init.h"
#include "gen/generator.h"
#include "gen/dimgens.h"
#include "gen/structures_api.h"

namespace dq {
// (declared out here: MSVC gives a function declared inside a block the global namespace, so these never linked on Windows)
void registerSkyblockWorldGen();
void registerFlatWorldGen();   // gen/world_types.cpp
void registerDriftWorldGen();  // gen/drift.cpp
void registerWorldTypes() {
  registerStructures();        // every structure type (gen/structures_register.cpp), before any generator exists
  registerDefaultWorldGen();   // "default", "pocket", "realm" (gen/realms.cpp)
  registerSkyblockWorldGen();   // "skyblock" (gen/skyblock.cpp)
  registerFlatWorldGen();       // "flat" (gen/world_types.cpp); amplified, islands, winter, hell, floating: OverworldGen
  registerDriftWorldGen();      // "drift": BTA's Drift, the side realm REALM_DRIFT (gen/drift.cpp)
}
} // namespace dq
