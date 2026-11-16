#include "core/init.h"
#include "core/registry.h"
#include "world/carve_forms.h"
#include "sim/boss_kit.h"

namespace dq {

bool initCore(const CoreOptions& o) {
  if (!o.userDirOverride.empty()) setUserDirOverride(o.userDirOverride);
  logSetFile((userDir() / (o.server ? "server.log" : "latest.log")).string());
  DQ_LOG("DokuQuest %s — data %s, user %s", DQ_VERSION, dataDir().string().c_str(), userDir().string().c_str());
  if (!reg().loadBase(dataDir())) return false;
  registerChiselForms();   // slabs and stairs are chiseled blocks now (world/carve_forms.cpp)
  registerWorldTypes();
  registerCommands();
  registerCreatures();
  registerModApi();
  registerWeatherSystem();
  registerPortals();
  registerMagic();
  registerContent();
  registerGlue();   // moving structures: glue between faces (sim/glue.h)
  registerMovingBodies();   // moving structures: hinges and sliders move their pieces (sim/moving.h)
  registerEndStory();
  registerVillagers();
  registerRedstone();
  registerBossKits();   // imported bosses fight by their own attack tables (sim/boss_kit.cpp)
  return true;
}
void shutdownCore() {}

} // namespace dq
