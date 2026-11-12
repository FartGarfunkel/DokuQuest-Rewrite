// sim/station_maps.cpp — the cartography table's work (sim/station_work.h mapPairResult)
#include "sim/station_work.h"
#include "sim/maps.h"

namespace dq {

StationResult mapPairResult(Level& lv, const ItemStack& a, const ItemStack& b, bool take) {
  StationResult r;
  if (!isMapStack(a) || b.empty()) return r;
  const std::string id = stackMapId(a);
  if (b.id == "empty_map") {
    r.out = ItemStack("filled_map", b.n + 1); if (!id.empty()) r.out.tag = {{"map", id}};
    r.useA = 1; r.useB = b.n; r.note = b.n == 1 ? "A copy of it" : strFormat("%d copies of it", b.n); r.ok = true;
  } else if (b.id == "paper") {
    PaperMap* src = id.empty() ? nullptr : mapStore(lv).get(id);
    if (!src || src->scale >= MAP_MAX_SCALE) return r;
    r.out = ItemStack("filled_map", 1); r.useA = 1; r.useB = 1;
    r.note = strFormat("Zoomed out to 1:%d, twice the land", 1 << (src->scale + 1)); r.ok = true;
    if (take) r.out.tag = {{"map", mapStore(lv).zoomed(*src)->id}};
  }
  return r;
}

} // namespace dq
