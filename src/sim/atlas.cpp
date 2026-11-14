// sim/atlas.cpp — see atlas.h
#include "sim/atlas.h"
#include "sim/maps.h"
#include "sim/player.h"
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

static const json* atlasOf(const Player& p) { return p.stats.contains("atlas") && p.stats["atlas"].is_object() ? &p.stats["atlas"] : nullptr; }
ItemStack atlasSlot(const Player& p, const char* which) { const json* a = atlasOf(p); return a && a->contains(which) ? ItemStack::fromJson((*a)[which]) : ItemStack(); }
bool atlasHasMap(const Player& p) { return !atlasSlot(p, "map").empty(); }
bool atlasHasCompass(const Player& p) { return !atlasSlot(p, "compass").empty(); }
bool atlasMinimap(const Player& p) { const json* a = atlasOf(p); return a && a->value("mini", false) && atlasHasCompass(p) && atlasHasMap(p); }
void setAtlasMinimap(Player& p, bool on) { if (!atlasOf(p)) p.stats["atlas"] = json::object(); p.stats["atlas"]["mini"] = on; }

std::string atlasPut(Player& p, const char* which, ItemStack& from, ItemStack& back) {
  const std::string w = which;
  if (from.empty()) return "Nothing to put in";
  if (w == "map" && from.id != "empty_map" && from.id != "filled_map") return "The map slot takes a map";
  if (w == "compass" && from.id != "compass") return "The compass slot takes a compass";
  back = atlasSlot(p, which);
  ItemStack one = from; one.n = 1;
  if (--from.n <= 0) from.clear();
  if (!atlasOf(p)) p.stats["atlas"] = json::object();
  p.stats["atlas"][which] = one.toJson();
  return "";
}
ItemStack atlasTake(Player& p, const char* which) {
  ItemStack s = atlasSlot(p, which);
  if (atlasOf(p)) p.stats["atlas"].erase(which);
  return s;
}

std::string atlasTileId(int jsDim, int x, int z) {
  return "atlas_" + std::to_string(jsDim) + "_" + std::to_string(mapAlign(x, ATLAS_SCALE)) + "_" + std::to_string(mapAlign(z, ATLAS_SCALE));
}
PaperMap* atlasTileAt(Level& lv, int jsDim, int x, int z, bool create) {
  MapStore& st = mapStore(lv);
  const std::string id = atlasTileId(jsDim, x, z);
  if (PaperMap* m = st.get(id)) return m;
  return create ? st.create(id, jsDim, x, z, ATLAS_SCALE) : nullptr;
}
void atlasExplore(Level& lv, Player& p) {
  if (!atlasHasMap(p) || p.dead) return;
  const int d = jsDimOf(p), span = MAP_N << ATLAS_SCALE, reach = MAP_R << ATLAS_SCALE;
  const int x = ifloor(p.pos.x), z = ifloor(p.pos.z);
  // the tile you're on, and those within the mapping's reach of you (at an edge or a corner)
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int tx = x + dx * reach, tz = z + dz * reach;
    if ((dx || dz) && atlasTileId(d, tx, tz) == atlasTileId(d, x, z)) continue;
    PaperMap* m = atlasTileAt(lv, d, tx, tz, true);
    if (!m) continue;
    const u32 v0 = m->ver;
    mapExplore(lv, p, *m, 6);
    if (m->ver != v0) mapStore(lv).dirty.insert(m->id);
    (void)span;
  }
}

} // namespace dq
