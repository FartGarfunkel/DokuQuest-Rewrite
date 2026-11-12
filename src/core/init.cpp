#include "core/init.h"
#include "core/registry.h"
#include "world/carve_forms.h"

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
  registerEndStory();
  registerVillagers();
  registerRedstone();
  return true;
}
void shutdownCore() {}

} // namespace dq
