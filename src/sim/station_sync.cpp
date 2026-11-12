// sim/station_sync.cpp — see station_sync.h
#include "sim/station_sync.h"
#include "sim/level.h"
#include "world/world.h"

namespace dq {

namespace {
// the list a block entity belongs on when it's burning (or sealed), "" for none
const char* listFor(const json& e) {
  if (!e.is_object()) return "";
  const std::string t = e.value("type", std::string());
  if (t == "sbarrel") return e.value("sealed", false) ? "barrels" : "";
  if (!e.value("lit", false)) return "";
  if (t == "forge") return "forges";
  if (t == "kiln") return "kilns";
  if (t == "bloomery") return "bloomeries";
  if (t == "logs") return "charcoalPits";
  return "";
}
class StationSyncHook : public WorldSystem {
public:
  explicit StationSyncHook(Level& lv) : lv(lv) {}
  const char* name() const override { return "station-sync"; }
  void onBlockEntityChanged(World& w, int x, int y, int z) override {
    const json* e = w.blockEntity(x, y, z);
    const char* key = e ? listFor(*e) : "";
    if (!*key) return;
    json& l = lv.info.data[key];
    if (!l.is_array()) l = json::array();
    for (auto& q : l) if (q.is_array() && q.size() >= 4 && q[0] == w.realm && q[1] == x && q[2] == y && q[3] == z) return;   // listed already
    l.push_back({w.realm, x, y, z});
  }
private:
  Level& lv;
};
}

void stationSyncTick(Level& lv) {
  if (lv.isRemote()) return;
  for (auto& [r, w] : lv.allRealms()) if (!w->system<StationSyncHook>()) w->addSystem(std::make_shared<StationSyncHook>(lv));
}

} // namespace dq
