// sim/register.cpp — the game's chat commands and WorldEdit (sim/gamecommands), and the gameplay systems and
// entity kinds (sim/gameplay). Called from initCore() (core/init.cpp), so the game, the launcher's server and
// the headless server all get them (never through static constructors).
#include "core/init.h"
#include "sim/gamecommands.h"
#include "sim/gameplay.h"
namespace dq {
void registerCommands() {
  registerGameCommands();
  registerGameplay();
}
} // namespace dq
