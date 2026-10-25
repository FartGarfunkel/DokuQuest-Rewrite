// gen/structures_api.h — the structures workstream's entry points for the rest of the game:
//   registerStructures()      every structure type, in the original's order (called from registerWorldTypes)
//   structureIndexOf(gen)     a band generator's StructureIndex (plans: footprint, y, data for creatures/villagers)
//   locateStructure(...)      the nearest structure of a kind (the /locate command)
//   villageStartPoint(...)    where a new world starts: the edge of the nearest village (villages.js placeAtSpawn)
//   debugSpawnPoint(...)      DQ_SPAWN_AT: "x,y,z" (global y), "<kind>" (looking at the nearest one from 24 blocks
//                             south) or "<kind>!" (inside it, at its base) — for tests and screenshots
#pragma once
#include "gen/structures.h"
#include "gen/generator.h"

namespace dq {

void registerStructures();
const StructureIndex* structureIndexOf(const DimGenerator* g);
// the nearest plan of a kind (structure key, e.g. "tower", "village", "nether_fortress") to (x, z) in band `dim`
const StructurePlan* locateStructure(const WorldGen& wg, int dim, const std::string& kind, int x, int z, int maxRegions = 12);
// a new world's first spawn at the edge of the nearest village, facing it (global y); false = no village near
bool villageStartPoint(const WorldGen& wg, vec3& pos, float& yaw);
bool debugSpawnPoint(const WorldGen& wg, const std::string& spec, vec3& pos, float& yaw);

} // namespace dq
