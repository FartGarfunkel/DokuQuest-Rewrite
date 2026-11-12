// sim/station_work.h — the grindstone and the stonecutter worked in the world (their scenes, ui/cinematic_table.cpp; after
// Cinematic Interactions' GrindstoneStation and StonecutterStation). What's being worked lies on the block as its block
// entity, so it syncs to everyone, shows on the block (render/pile_layer.cpp) and spills when the block is broken:
//   the grindstone, smithing table, loom, fletching and cartography tables {type:"grindwork"|..., items:[a, b], f}: what's
//     worked and what goes into it, lying on top (their screens' two inputs), made with stationResult (pairWork* below)
//   the stonecutter {type:"cutwork", items:[stack]}: the stone on its table; cutting is stonecutterOptions(): one piece
//     (or as many as it makes) into the bag
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"
#include "sim/enchanting.h"

namespace dq {
class Player;

// the stations worked with two things lying on them, each its own block entity type: the grindstone ("grindwork"), the
// smithing table ("smithwork"), the loom ("loomwork"), the fletching table ("fletchwork"), the cartography table
// ("mapwork"). Slot 0 the piece worked (the gear, the cloth, the map), slot 1 what goes into it (the material, the
// dye, the paper); the grindstone's are two pieces alike. What they make: stationResult(), the cartography table's
// pairResult below (a map and paper: zoomed out; a map and empty maps: copies)
const char* pairWorkType(const std::string& station);   // nullptr: not one of them
std::string pairStationAt(World& w, ivec3 at);           // its station ("grindstone", "smithing", ...), "" none
int pairSlotFor(const std::string& station, const ItemStack& s);   // where a thing goes (-1: either, the grindstone)
std::vector<ItemStack> pairWorkItems(World& w, ivec3 at);                                   // [piece, addition]
bool pairWorkPut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held);     // slot -1: where it goes
bool pairWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot);                     // back into the bag
StationResult pairResult(Level& lv, const std::string& station, const ItemStack& a, const ItemStack& b);
// the cartography table (maps.js matchGrid, a pair at a time; sim/station_maps.cpp): a map and a paper zoom it out (the
// new map made only when take); a map and empty maps make that many copies, the map itself back with them
StationResult mapPairResult(Level& lv, const ItemStack& map, const ItemStack& with, bool take);
StationResult pairWorkResult(Level& lv, World& w, ivec3 at);
// does the work: the result into the bag, the pieces used, any experience given back. false (and why, toasted) if not
bool pairWorkDo(Level& lv, Player& p, World& w, ivec3 at, ItemStack* made = nullptr);   // made: what was handed over

ItemStack cutWorkItem(World& w, ivec3 at);                                                   // the stone on the table
bool cutWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);                 // all of what's held, if it cuts
bool cutWorkTake(Level& lv, Player& p, World& w, ivec3 at);
std::vector<std::pair<std::string, int>> cutWorkOptions(World& w, ivec3 at);                // what it cuts into {item, count}
// one block cut into option k (all: every block on the table); the pieces into the bag. Returns how many blocks went
int cutWorkDo(Level& lv, Player& p, World& w, ivec3 at, int k, bool all);

// the jukebox {type:"jukebox", disc}: a record in its slot plays (the "jukebox" event; the classic use and its scene)
std::string jukeboxDisc(World& w, ivec3 at);                                                // "" none
bool jukeboxInsert(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);             // one record from what's held
bool jukeboxEject(Level& lv, Player& p, World& w, ivec3 at, bool toBag);                   // toBag: into the bag, else popped out
bool jukeboxUse(Level& lv, Player& p, World& w, ivec3 at);                                 // the classic use: in, or out

// where the pieces lie, in the block's own frame (out toward the side its placer stood, or for a plain block the side
// the first piece was laid from, kept in the entity as "f"; right across it), so everyone sees them in the same places
// (Cinematic Interactions' RemainingLayouts / the stations' LAYOUTs)
void stationWorkFrame(World& w, ivec3 at, vec3& out, vec3& right);
vec3 pairLiePos(World& w, ivec3 at, int slot);
float pairLieSize(const std::string& station, int slot);   // a flat item's width there
vec3 cutLiePos(World& w, ivec3 at);               // on the table, 0.25 left of the saw and 0.24 in front of it

} // namespace dq
