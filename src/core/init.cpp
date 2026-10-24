#include "core/init.h"
#include "core/registry.h"

namespace dq {

bool initCore(const CoreOptions& o) {
  if (!o.userDirOverride.empty()) setUserDirOverride(o.userDirOverride);
  logSetFile((userDir() / (o.server ? "server.log" : "latest.log")).string());
  DQ_LOG("DokuQuest %s — data %s, user %s", DQ_VERSION, dataDir().string().c_str(), userDir().string().c_str());
  if (!reg().loadBase(dataDir())) return false;
  registerWorldTypes();
  registerCommands();
  registerModApi();
  registerPortals();
  return true;
}
void shutdownCore() {}

} // namespace dq
