// sim/station_work.h — the grindstone and the stonecutter worked in the world (their scenes, ui/cinematic_table.cpp).
// What's being worked lies on the block as its block
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
StationResult smithResult(const ItemStack& gear, const ItemStack& material, const ItemStack& tmpl);   // (the template: trims, the cinderite upgrade)
std::string trimMaterialOf(const std::string& item);   // a trim material's colour ("iron", "gold"...), "" not one
// the hands-on forge blows on the smithing table: the work done (the result left on the table where the gear was, the material
// used, the template kept, a level spent). false and why (toasted) if not
bool smithWorkForge(Level& lv, Player& p, World& w, ivec3 at);
// does the work: the result into the bag, the pieces used, any experience given back. false (and why, toasted) if not
bool pairWorkDo(Level& lv, Player& p, World& w, ivec3 at, ItemStack* made = nullptr);   // made: what was handed over
// a hammer to strike a station's work with (the smith's, the stone and flint ones...: a tool, not a head or a mould)
bool isStrikingHammer(const ItemStack& s);

ItemStack cutWorkItem(World& w, ivec3 at);                                                   // the stone on the table
bool cutWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);                 // all of what's held, if it cuts
bool cutWorkTake(Level& lv, Player& p, World& w, ivec3 at);
std::vector<std::pair<std::string, int>> cutWorkOptions(World& w, ivec3 at);                // what it cuts into {item, count}
// one block cut into option k (all: every block on the table); the pieces into the bag. Returns how many blocks went
int cutWorkDo(Level& lv, Player& p, World& w, ivec3 at, int k, bool all);

// the forge station (pairs as "forgework": slot 0 a tool to set a gem in, slot 1 the gem; or slot 0 a material it forges heads
// of, tools/port/forging.py): the heads the material lying there makes {head, its cost}, and one forged (into the bag)
std::vector<std::pair<std::string, int>> forgeWorkHeads(World& w, ivec3 at);
bool forgeWorkHead(Level& lv, Player& p, World& w, ivec3 at, int k, ItemStack* made = nullptr);

// the inscription table {type:"inscribework", items:[spellbook, scroll, paper, lapis], f}: what's laid on its board (the
// spell_items.h work: a scroll inscribed into the book, a spell taken out of it as a scroll, a scroll written on the paper
// in the lapis's ink). The work's functions return "" done, else why not (toasted)
std::vector<ItemStack> inscribeWorkItems(World& w, ivec3 at);
int inscribeSlotFor(const ItemStack& s);   // -1: not one of them
bool inscribeWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);
bool inscribeWorkTake(Level& lv, Player& p, World& w, ivec3 at, int k);
std::string inscribeWorkInscribe(Level& lv, Player& p, World& w, ivec3 at);
std::string inscribeWorkExtract(Level& lv, Player& p, World& w, ivec3 at, int slot);
std::string inscribeWorkWrite(Level& lv, Player& p, World& w, ivec3 at, const std::string& spell);
vec3 inscribeLiePos(World& w, ivec3 at, int k);

// the jukebox {type:"jukebox", disc}: a record in its slot plays (the "jukebox" event; the classic use and its scene)
std::string jukeboxDisc(World& w, ivec3 at);                                                // "" none
bool jukeboxInsert(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);             // one record from what's held
bool jukeboxEject(Level& lv, Player& p, World& w, ivec3 at, bool toBag);                   // toBag: into the bag, else popped out
bool jukeboxUse(Level& lv, Player& p, World& w, ivec3 at);                                 // the classic use: in, or out

// the campfire's food (sim/camping.h {type:"campfire", items[4], t[4]}; the cooking and the food popping off when it's
// done: sim/camping.cpp): its scene sets raw food on the four spots by hand and takes it back
std::vector<ItemStack> campfireFood(World& w, ivec3 at);                                    // the four spots
float campfireCooked(World& w, ivec3 at, int k);                                            // 0..1 of the way cooked
bool campfireCooks(const ItemStack& s);                                                     // raw food that cooks into food
bool campfirePut(Level& lv, Player& p, World& w, ivec3 at, int k, ItemStack& held);         // one of it on spot k (-1: the first free)
bool campfireTake(Level& lv, Player& p, World& w, ivec3 at, int k);                         // back into the bag
vec3 campfireSpot(ivec3 at, int k);                                                         // where food k lies on the logs

// where the pieces lie, in the block's own frame (out toward the side its placer stood, or for a plain block the side
// the first piece was laid from, kept in the entity as "f"; right across it), so everyone sees them in the same places
// (the scenes' remaining layouts / the stations' LAYOUTs)
void stationWorkFrame(World& w, ivec3 at, vec3& out, vec3& right);
vec3 pairLiePos(World& w, ivec3 at, int slot);
float pairLieSize(const std::string& station, int slot);   // a flat item's width there
vec3 cutLiePos(World& w, ivec3 at);               // on the table, 0.25 left of the saw and 0.24 in front of it

} // namespace dq
