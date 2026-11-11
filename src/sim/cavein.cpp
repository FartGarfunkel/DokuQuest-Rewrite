// sim/cavein.cpp — see cavein.h
#include "sim/cavein.h"
#include "sim/blockfx.h"
#include "core/registry.h"

namespace dq {

bool isRock(BlockId id) {
  if (!id) return false;
  const Registry& R = reg();
  const BlockDef& b = R.block(id);
  if (!R.tOpaque[id] || b.tool != "pickaxe" || b.has("station")) return false;
  const std::string& k = b.key;
  for (const char* no : {"brick", "polished", "smooth", "chiseled", "cut_", "_ore", "block", "tile", "pillar", "concrete", "glass"}) if (k.find(no) != std::string::npos) return false;
  for (const char* yes : {"stone", "granite", "diorite", "andesite", "basalt", "deepslate", "slate", "tuff", "limestone", "marble", "shale", "chalk", "gneiss", "schist",
                          "quartzite", "gabbro", "rhyolite", "dacite", "claystone", "conglomerate", "sandstone", "calcite", "blackstone"})
    if (k.find(yes) != std::string::npos) return true;
  return false;
}

static bool supported(World& w, int x, int y, int z) {
  static BlockId B = 0; if (!B) B = reg().blockId("support_beam");
  if (!B) return false;
  for (int dy = -3; dy <= 2; dy++) for (int dz = -SUPPORT_REACH; dz <= SUPPORT_REACH; dz++) for (int dx = -SUPPORT_REACH; dx <= SUPPORT_REACH; dx++)
    if (w.getBlock(x + dx, y + dy, z + dz) == B) return true;
  return false;
}

bool caveInRisk(World& w, int x, int y, int z) {
  // rock over the open space, the open space wide: more than a 5x5 of air round about at this height
  int roofY = y + 1; while (roofY < y + 4 && w.getBlock(x, roofY, z) == AIR) roofY++;
  if (!isRock(w.getBlock(x, roofY, z))) return false;
  int open = 0;
  for (int dz = -5; dz <= 5; dz++) for (int dx = -5; dx <= 5; dx++) if (w.getBlock(x + dx, roofY - 1, z + dz) == AIR) open++;
  return open > CAVEIN_SPAN && !supported(w, x, roofY, z);
}

int caveIn(Level& lv, World& w, int x, int y, int z, float roll) {
  if (roll >= CAVEIN_CHANCE || !caveInRisk(w, x, y, z)) return 0;
  int roofY = y + 1; while (roofY < y + 4 && w.getBlock(x, roofY, z) == AIR) roofY++;
  const BlockId cobble = reg().blockId("cobblestone");
  const int want = 1 + (int)(std::fmod(std::fabs(roll) * 9973.f + x * 31 + z * 17, 3.f));
  int fell = 0;
  const int order[9][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
  for (auto& o : order) {
    if (fell >= want) break;
    const int ax = x + o[0], az = z + o[1];
    if (!isRock(w.getBlock(ax, roofY, az)) || w.getBlock(ax, roofY - 1, az) != AIR) continue;
    w.setBlock(ax, roofY, az, AIR, 0, SetFlags::Urgent);
    spawnFallingBlock(lv, w.realm, vec3(ax + 0.5f, roofY, az + 0.5f), reg().block(cobble).key);
    fell++;
  }
  if (fell) lv.fx->sound("break_stone", vec3(x + 0.5f, roofY, z + 0.5f), 24);
  return fell;
}

} // namespace dq
