// sim/tactile.h — Tactile Blocks' way of working the stations, in our own first person (docs/TACTILE_PORT.md): no screen,
// what's held laid in the spot you look at, an empty hand takes it back, a long hold pours the whole stack in. The places
// and zones are Tactile's (its block entities and blocks, read from the mod); what they do is our own simulation (our
// furnace's three slots smelt as ever, so hoppers, saves and the classic screen are untouched).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"

namespace dq {
class Player;

// Tactile's facing of a block (0 south, 1 east, 2 north, 3 west, as tools/port/tactile_import.py orders them) from ours
int tactileFacing(World& w, ivec3 at);
float tactileFacingDegrees(int facing);   // Tactile's turn of its renderer about the block's middle (north 0, east -90...)
// a point in the block (0..1) turned back into Tactile's north-facing frame (x across its front, z into it), and back
vec3 tactileNorthLocal(int facing, vec3 inBlock);
vec3 tactileFromNorth(int facing, vec3 north);

// a furnace, smoker or blast furnace used where you look (Tactile's four, fumoir, haut_fourneau): the low part takes fuel,
// the high part what cooks (the smoker's and the blast furnace's door opened first: use the door), an empty hand takes back
// (what's done first, then what cooks; the fuel down low; the blast furnace's ingots from its mould). all: the whole stack.
// false: not one of them (or nothing to do there)
bool tactileFurnaceUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all);
bool tactileDoorOpen(World& w, ivec3 at);

// the anvil (Tactile's enclume over our "anvilwork" record {items: [work, material], hits, req}): what's held goes on it (the
// work first, then the material beside it), an empty hand takes back (the material first); struck with a hammer (half a
// second apart) it forges once the blows are in — five to join two alike, two for each piece of material used — the
// result left on the anvil, the levels spent
bool tactileAnvilUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);
bool tactileAnvilStrike(Level& lv, Player& p, World& w, ivec3 at, double now);   // false: nothing on it to strike

// the grindstone (Tactile's meule): a worn tool or armour held to the turning wheel (use held) is ground back to good, a
// third of a hundredth of its wear a second, paid in experience (one point for twelve), each grinding leaving a little wear
// for good; an enchanted piece held there ten seconds loses its enchantments for their experience. The wheel turns while
// anyone grinds ("grind" counts up in the block's record, so everyone sees it)
struct GrindSession { ivec3 at{0, -9999, 0}; double last = -10, repair = 0, xp = 0, dis = 0; };
bool tactileGrind(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held, double now, GrindSession& gs);
bool tactileGrindTarget(const ItemStack& s);   // worn (past its ground-in wear) or enchanted

// a chest worked as Tactile's coffre (a plain chest standing alone; a large chest stays ours): use opens it, and shuts it
// (on its lid or rim); open, a tray of fifteen shows one page (the other fifteen heaped loose below): what's held goes in
// where you look at the tray (held a moment: the stack), a strike takes one (sneaking: the stack), sneak + use on the tray
// puts in all you carry that's already there; its buttons sort the page (sneaking: both) and turn to the other page
struct TactileChestAim { enum { None, Shut, Lid, Tray, Sort, Swap }; int what = None, slot = -1; };
bool tactileChest(World& w, ivec3 at);
TactileChestAim tactileChestAim(World& w, ivec3 at, vec3 eye, vec3 dir);
bool tactileChestUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak);
bool tactileChestTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole);

// the brewing stand worked as Tactile's alambic (over our brewing record: bottles 0-2, the ingredient 3, cinder powder 4):
// the bottle rack on its left (three spots across it), the still's dome for the ingredient, its burner below for the
// powder; what's held goes where you look (held a moment: the whole stack of powder), an empty hand takes back from there
bool tactileBrewUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all);

// the cartography desk (Tactile's cartographe; our "mapwork" record {items: [the map worked, a blank to copy onto, blank
// sheets to enlarge with], ink, feather}): an ink sac fills its pot (three uses), a feather goes in its holder (three);
// a map goes on the board (front: the one worked, back: a blank to copy onto), blanks and paper pile on the right; an empty
// hand at the quill draws a new map of where you stand (a blank on the front) or copies the map onto the blank, at the
// compass enlarges the map with eight from the pile; anywhere else takes back what lies there
bool tactileCartographyUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);
int tactileCartoFacing(int facing);
void tactileGive(Level& lv, Player& p, ItemStack s);   // into the bag, the rest dropped at your feet   // the desk's model faces west: Tactile turns it by facing.getClockWise()

} // namespace dq
