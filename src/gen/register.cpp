// gen/register.cpp — world types known to the game. The full port of the original world generator
// registers "default" here (see gen/worldgen.h); "debug" (simple noise) lives in generator.cpp.
#include "core/init.h"
#include "gen/generator.h"
#include "gen/dimgens.h"

namespace dq {
void registerWorldTypes() {
  registerDefaultWorldGen();   // "default", "pocket", "realm" (gen/realms.cpp)
}
} // namespace dq
