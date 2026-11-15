// assets/local_models.h — stations drawn from imported block models, local only (data/local_models/tactile:
// tools/port/tactile_import.py, Tactile Blocks' smithing table, cartography desk and brewing still): which of our blocks each
// one draws and how it turns with the way the block faces. Without the files, nothing changes. Read by the world mesher
// (render/mesher.cpp) on its workers: built whole after the tiles are final, swapped whole when they're rebuilt.
#pragma once
#include "assets/pack_models.h"
#include "core/registry.h"

namespace dq {
class TileSet;

struct StationModel {
  PackModel model;                              // the north-facing one's (models[0]'s)
  std::shared_ptr<PackModel> models[4];         // per meta & 3 (south, east, north, west)
  int turn[4] = {0, 0, 0, 0};                   // quarters (clockwise from above) for meta & 3
  const PackModel& of(int facing) const { return models[facing & 3] ? *models[facing & 3] : model; }
  std::shared_ptr<PackModel> closed;
  std::unordered_map<std::string, std::shared_ptr<PackModel>> parts;   // its own parts (a chest's lid and buttons, in its own wood)            // a chest shut (its lid in the model: "coffre_full"); open, the lid is drawn on its hinge
  int facingKind = 0;                           // the block's own facing: 0 none, 1 a front (meta & 3: S E N W), 2 a turned model (N E S W)
  int idx(u8 meta) const { static const int FM[4] = {2, 1, 0, 3}; return facingKind == 1 ? meta & 3 : facingKind == 2 ? FM[meta & 3] : 2; }   // -> south, east, north, west
};
bool tactilePorted(const std::string& key);       // its Tactile port is whole (docs/TACTILE_PORT.md): drawn from its model
const PackModel* tactilePart(const std::string& name);   // a Tactile model by name (parts: "haut_fourneau_porte", "meule_wheel")
void loadStationModels(TileSet& ts);
const StationModel* stationModel(BlockId id);   // null: drawn the game's own way
// a cube block drawn from a model leaves the cells round it open (its neighbours keep their faces toward it)
bool stationModelOpen(BlockId id);
// the model's top at a spot on the block (block-local x, z in 0..1; meta's facing as the mesher turns it), in blocks over its
// floor: where a thing laid there rests (a sloped board, a shelf). false: nothing of the model under that spot
bool stationTopAt(BlockId id, u8 meta, vec2 xz, float& top);
// where a pair station's piece lies (sim/station_work.h pairLiePos), set down onto its model's top where it has one
class World;
vec3 pairLieOnModel(World& w, ivec3 at, int slot);

} // namespace dq
