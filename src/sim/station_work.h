// sim/station_work.h — the grindstone and the stonecutter worked in the world (their scenes, ui/cinematic_table.cpp; after
// Cinematic Interactions' GrindstoneStation and StonecutterStation). What's being worked lies on the block as its block
// entity, so it syncs to everyone, shows on the block (render/pile_layer.cpp) and spills when the block is broken:
//   the grindstone {type:"grindwork", items:[a, b]}: two pieces lying on its top, one each (the grindstone screen's two
//     inputs); grinding is stationResult("grindstone", ...): enchantments off (their experience back), two worn pieces of
//     a kind joined
//   the stonecutter {type:"cutwork", items:[stack]}: the stone on its table; cutting is stonecutterOptions(): one piece
//     (or as many as it makes) into the bag
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"
#include "sim/enchanting.h"

namespace dq {
class Player;

std::vector<ItemStack> grindWorkItems(World& w, ivec3 at);                                   // [a, b]
bool grindWorkPut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held);     // one piece from what's held
bool grindWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot);                     // back into the bag
StationResult grindWorkResult(World& w, ivec3 at);
// grinds it: the result into the bag, the pieces used, the experience given back. false (and why, toasted) if it can't
bool grindWorkDo(Level& lv, Player& p, World& w, ivec3 at);

ItemStack cutWorkItem(World& w, ivec3 at);                                                   // the stone on the table
bool cutWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);                 // all of what's held, if it cuts
bool cutWorkTake(Level& lv, Player& p, World& w, ivec3 at);
std::vector<std::pair<std::string, int>> cutWorkOptions(World& w, ivec3 at);                // what it cuts into {item, count}
// one block cut into option k (all: every block on the table); the pieces into the bag. Returns how many blocks went
int cutWorkDo(Level& lv, Player& p, World& w, ivec3 at, int k, bool all);

// where the pieces lie, in the block's own frame (its facing: out toward the side the placer stood, right across it), so
// everyone sees them in the same places (RemainingLayouts' GRINDSTONE / STONECUTTER points)
void stationWorkFrame(World& w, ivec3 at, vec3& out, vec3& right);
vec3 grindLiePos(World& w, ivec3 at, int slot);   // on the wheel's top, 0.1 either side of its middle
vec3 cutLiePos(World& w, ivec3 at);               // on the table, 0.25 left of the saw and 0.24 in front of it

} // namespace dq
