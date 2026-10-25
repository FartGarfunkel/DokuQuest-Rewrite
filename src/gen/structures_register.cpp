// gen/structures_register.cpp — every structure type registered in the original's order (that order is the
// priority when two would overlap, and the build order), the /locate command, and the lookups other areas use.
#include "gen/structures_api.h"
#include "gen/structures_common.h"
#include "sim/commands.h"
#include "sim/player.h"
#include <cctype>
#include <cstring>

namespace dq {

void registerStructureVillages();     // structures_villages.cpp: villages / towns / cities (villages.js, towns.js)
void registerStructureDungeons();     // structures_dungeons.cpp: dungeons.js, dragon lairs (dragons.js)
void registerStructureEndCities();    // structures_far.cpp: theend.js End cities (and their ships)
void registerStructurePlaces();       // structures_places.cpp: structures.js (STRUCTS 0..6)
void registerStructureDeepA();        // structures_deep.cpp: stronghold (7), ancient city (8)
void registerStructureFarA();         // structures_far.cpp: places.js (9..16)
void registerStructureDeepB();        // structures_deep.cpp: deep halls (17), twin vault (18), pirate cove (19)
void registerStructureFarB();         // structures_far.cpp: nether fortress (20), floating sanctum (21)

const StructureIndex* structureIndexOf(const DimGenerator* g) {
  if (auto* o = dynamic_cast<const OverworldGen*>(g)) return &o->structures;
  if (auto* d = dynamic_cast<const DepthsGen*>(g)) return &d->structures;
  if (auto* s = dynamic_cast<const SkylandsGen*>(g)) return &s->structures;
  if (auto* e = dynamic_cast<const EndGen*>(g)) return &e->structures;
  return nullptr;
}

static std::string normKey(std::string s) {
  for (auto& c : s) c = c == ' ' || c == '-' ? '_' : (char)std::tolower((unsigned char)c);
  return s;
}
// a kind by its key, or by its name ("Mage Tower", "the Deep Halls", "a Nether Fortress")
static int findKind(const std::string& kind) {
  const std::string k = normKey(kind);
  const auto& all = structureTypes();
  for (int i = 0; i < (int)all.size(); i++) if (all[i].key == k) return i;
  for (int i = 0; i < (int)all.size(); i++) {
    std::string n = normKey(all[i].name);
    for (const char* art : {"the_", "a_", "an_"}) if (n.rfind(art, 0) == 0) n = n.substr(strlen(art));
    if (n == k) return i;
  }
  return -1;
}

const StructurePlan* locateStructure(const WorldGen& wg, int dim, const std::string& kind, int x, int z, int maxRegions) {
  const int k = findKind(kind);
  const StructureIndex* idx = structureIndexOf(wg.forDim(dim));
  if (k < 0 || !idx) return nullptr;
  return idx->nearest(k, x, z, maxRegions);
}

bool debugSpawnPoint(const WorldGen& wg, const std::string& spec, vec3& pos, float& yaw) {
  int x, y, z;
  if (sscanf(spec.c_str(), "%d,%d,%d", &x, &y, &z) == 3) { pos = vec3(x + 0.5f, y, z + 0.5f); return true; }
  std::string kind = spec; const bool inside = !kind.empty() && kind.back() == '!';
  if (inside) kind.pop_back();
  const int k = findKind(kind);
  if (k < 0) return false;
  for (int d = 0; d < DIM_COUNT; d++) {
    if (!(structureTypes()[k].dims & (1u << d))) continue;
    const StructurePlan* S = locateStructure(wg, d, kind, 0, 0, 24);
    if (!S) continue;
    const int cx = (S->x0 + S->x1) / 2, cz = (S->z0 + S->z1) / 2;
    const TerrainQuery* t = wg.forDim(d) ? wg.forDim(d)->terrain() : nullptr;
    if (inside) { pos = vec3(S->cx + 0.5f, dimBase(d) + S->y + 1, S->cz + 0.5f); yaw = 0; return true; }
    const int sx = cx, sz = std::max(S->z1, cz) + 24;
    int gy = t ? std::max(t->finalSurfaceY(sx, sz), S->y) : S->y;
    if (gy < 0) gy = S->y;
    pos = vec3(sx + 0.5f, dimBase(d) + gy + 6, sz + 0.5f); yaw = 0;   // facing north, at it
    return true;
  }
  return false;
}

static void registerLocate() {
  registerChatCommand("locate", "Find the nearest structure of a kind (e.g. /locate tower)", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.level) return "";
    Level& lv = *src.level;
    vec3 p = src.player ? src.player->pos : lv.info.spawn;
    const int realm = src.player ? src.player->realm : REALM_MAIN;
    const int dim = realm == REALM_MAIN ? dimOfY((int)std::floor(p.y)) : DIM_OVERWORLD;
    World& w = lv.world(realm);
    const StructureIndex* idx = w.gen ? structureIndexOf(w.gen->forDim(dim)) : nullptr;
    if (!idx || idx->typeList().empty()) return "There's nothing to find in " + std::string(dimName(dim)) + ".";
    std::string kinds;
    for (int k : idx->typeList()) kinds += (kinds.empty() ? "" : ", ") + structureTypes()[k].key;
    if (a.empty()) return "Usage: /locate <kind>. Here: " + kinds;
    std::string want; for (auto& s : a) want += (want.empty() ? "" : " ") + s;
    const int k = findKind(want);
    if (k < 0 || std::find(idx->typeList().begin(), idx->typeList().end(), k) == idx->typeList().end())
      return "No such place here. Kinds in " + std::string(dimName(dim)) + ": " + kinds;
    const StructurePlan* S = idx->nearest(k, (int)std::floor(p.x), (int)std::floor(p.z), 24);
    if (!S) return "No " + structureTypes()[k].name + " anywhere near.";
    const int cx = (S->x0 + S->x1) / 2, cz = (S->z0 + S->z1) / 2;
    const int dist = (int)std::lround(std::hypot(cx - p.x, cz - p.z));
    return "The nearest " + structureTypes()[k].key + " (" + S->name + ") is at " + std::to_string(cx) + ", " + std::to_string(S->y + (realm == REALM_MAIN ? dimBase(dim) : 0)) + ", " +
           std::to_string(cz) + " (" + std::to_string(dist) + " blocks away)";
  }, true, "<kind>");
}

void registerStructures() {
  static bool done = false;
  if (done) return;
  done = true;
  // non-STRUCTS kinds first: the original's structAt keeps every STRUCTS kind clear of villages (24), dungeons (8)
  // and dragon lairs (6), and of the STRUCTS kinds before it (6) — registration order gives exactly that
  registerStructureVillages();
  registerStructureDungeons();
  registerStructureEndCities();
  registerStructurePlaces();
  registerStructureDeepA();
  registerStructureFarA();
  registerStructureDeepB();
  registerStructureFarB();
  registerLocate();
}

} // namespace dq
