// sim/hands.h — the hands-on stations' way of working the stations, in our own first person (docs/HANDS_ON.md): no screen,
// what's held laid in the spot you look at, an empty hand takes it back, a long hold pours the whole stack in. The places
// and zones are the hands-on (its block entities and blocks, read from the mod); what they do is our own simulation (our
// furnace's three slots smelt as ever, so hoppers, saves and the classic screen are untouched).
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"

namespace dq {
class Player;

// the hands-on facing of a block (0 south, 1 east, 2 north, 3 west, as the station models' import orders them) from ours
int handsFacing(World& w, ivec3 at);
float handsFacingDegrees(int facing);   // the hands-on turn of its renderer about the block's middle (north 0, east -90...)
// a point in the block (0..1) turned back into the hands-on north-facing frame (x across its front, z into it), and back
vec3 handsNorthLocal(int facing, vec3 inBlock);
vec3 handsFromNorth(int facing, vec3 north);

// a furnace, smoker or blast furnace used where you look (the hands-on furnace, smoker and blast furnace): the low part takes fuel,
// the high part what cooks (the smoker's and the blast furnace's door opened first: use the door), an empty hand takes back
// (what's done first, then what cooks; the fuel down low; the blast furnace's ingots from its mould). all: the whole stack.
// false: not one of them (or nothing to do there)
bool handsFurnaceUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all);
bool handsDoorOpen(World& w, ivec3 at);

// the anvil (the hands-on anvil over our "anvilwork" record {items: [work, material], hits, req}): what's held goes on it (the
// work first, then the material beside it), an empty hand takes back (the material first); struck with a hammer (half a
// second apart) it forges once the blows are in — five to join two alike, two for each piece of material used — the
// result left on the anvil, the levels spent
bool handsAnvilUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);
bool handsAnvilStrike(Level& lv, Player& p, World& w, ivec3 at, double now);   // false: nothing on it to strike

// the grindstone (the hands-on grindstone): a worn tool or armour held to the turning wheel (use held) is ground back to good, a
// third of a hundredth of its wear a second, paid in experience (one point for twelve), each grinding leaving a little wear
// for good; an enchanted piece held there ten seconds loses its enchantments for their experience. The wheel turns while
// anyone grinds ("grind" counts up in the block's record, so everyone sees it)
struct GrindSession { ivec3 at{0, -9999, 0}; double last = -10, repair = 0, xp = 0, dis = 0; };
bool handsGrind(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held, double now, GrindSession& gs);
bool handsGrindTarget(const ItemStack& s);   // worn (past its ground-in wear) or enchanted

// a chest worked as the hands-on chest (a plain chest standing alone; a large chest stays ours): use opens it, and shuts it
// (on its lid or rim); open, a tray of fifteen shows one page (the other fifteen heaped loose below): what's held goes in
// where you look at the tray (held a moment: the stack), a strike takes one (sneaking: the stack), sneak + use on the tray
// puts in all you carry that's already there; its buttons sort the page (sneaking: both) and turn to the other page
struct HandsChestAim { enum { None, Shut, Lid, Tray, Sort, Swap, Take }; int what = None, slot = -1; };
bool handsChest(World& w, ivec3 at);
// how a chest's slots lie on its tray: a single chest's 27 in pages of fifteen; a large chest one chest of 54 in pages of
// thirty, fifteen on each half's tray (off: where this half's start in a page), its page kept by its left half (key), the
// one sheet on the left lid and the one pair of buttons on the right lid
struct ChestLayout { int slots = 27, pg = 15, off = 0; ivec3 key{0}; bool buttons = true, sheet = true; };
ChestLayout handsChestLayout(World& w, ivec3 at);
HandsChestAim handsChestAim(World& w, ivec3 at, vec3 eye, vec3 dir);
bool handsChestUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak);
// how far along the ray an open chest's lid and buttons are (they stand up out of its block), -1 if it misses them
float handsChestLidRay(World& w, ivec3 at, vec3 eye, vec3 dir);
// any hands-on part that stands out of its block's own shape (a chest's lid, the still's recipe book): how far along the ray
float handsExtraRay(World& w, ivec3 at, vec3 eye, vec3 dir);
// an open hands-on chest's outline (its body and its raised lid, world boxes): false when it isn't one
bool handsChestOutline(World& w, ivec3 at, std::vector<std::pair<vec3, vec3>>& out);
bool handsChestTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole);

// the brewing stand worked as the hands-on still (over our brewing record: bottles 0-2, the ingredient 3, cinder powder 4):
// the bottle rack on its left (three spots across it), the still's dome for the ingredient, its burner below for the
// powder; what's held goes where you look (held a moment: the whole stack of powder), an empty hand takes back from there
bool handsBrewUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all);

// the jukebox (hands-on's): a record laid on its platter waits; using its needle arm sets it playing (again, stops it);
// anywhere else takes the record back. The record {type:"jukebox", disc, stopped}
bool handsJukeboxUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);
bool handsJukeboxPlaying(World& w, ivec3 at);

// a barrel worked the same way (open by use, its front toward whoever opened it): a tray of nine under its rim showing one
// layer of its 27 (the others below), an empty hand takes what lies at a spot, the rim's two buttons sort it and turn to
// the next layer, use on the rim shuts it
bool handsBarrel(World& w, ivec3 at);
int handsBarrelFacing(World& w, ivec3 at);
vec3 handsBarrelButton(World& w, ivec3 at, int k);
vec3 handsBarrelLidPoint(World& w, ivec3 at, vec3 chestLidPx);   // a chest-lid point (its sixteenths, hinge at y 10, z 12) on the barrel's raised top   // a rim button's middle on the raised top (north frame, blocks): 0 sort, 1 next layer
HandsChestAim handsBarrelAim(World& w, ivec3 at, vec3 eye, vec3 dir);
bool handsBarrelUse(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, ItemStack& held, bool all, bool sneak);
bool handsBarrelTake(Level& lv, Player& p, World& w, ivec3 at, vec3 eye, vec3 dir, bool whole);

// a cupboard's door, open (render/chest_layer.cpp: its model facing +z turned a quarter per meta, the door swung 1.85 out on
// its left edge): a point on the door's inside (its own sixteenths, as shut) where it is now, block-relative; the sort
// button on the door (its middle 12, 9) and the door's own box, aimed at
vec3 handsCabinetDoorPoint(World& w, ivec3 at, vec3 doorPx);
float handsCabinetButtonRay(World& w, ivec3 at, vec3 eye, vec3 dir);   // -1: missed
float handsCabinetDoorRay(World& w, ivec3 at, vec3 eye, vec3 dir);
void handsCabinetSort(Level& lv, Player& p, World& w, ivec3 at);

// the cartography desk (the hands-on cartography desk; our "mapwork" record {items: [the map worked, a blank to copy onto, blank
// sheets to enlarge with], ink, feather}): an ink sac fills its pot (three uses), a feather goes in its holder (three);
// a map goes on the board (front: the one worked, back: a blank to copy onto), blanks and paper pile on the right; an empty
// hand at the quill draws a new map of where you stand (a blank on the front) or copies the map onto the blank, at the
// compass enlarges the map with eight from the pile; anywhere else takes back what lies there
bool handsCartographyUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held);
int handsCartoFacing(int facing);
void handsGive(Level& lv, Player& p, ItemStack s);   // into the bag, the rest dropped at your feet   // the desk's model faces west: hands-on turns it by facing.getClockWise()

} // namespace dq
