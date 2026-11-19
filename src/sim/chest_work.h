// sim/chest_work.h — a chest worked by hand in the world (the chest scene, ui/cinematic_table.cpp; after Immersive
// Craft's chest; a cabinet's doors open on its shelves): the lid up, its 27 slots lying inside in three rows of nine (a large chest's 54 across both halves, in
// the container screen's order: the half nearer -x / -z first), put into from your hand and taken back out. Every
// store with a lid: chests and painted chests, barrels (their top comes off), on their own block entities
// {type:"chest", items[27]} like the container screen; the ender chest (your own 27, Player::ender) and the party chest
// (the world's one store, info.data.partyChest) — so they save and sync as ever.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {
class Player;

// the chest's halves (1, or 2 for a large chest) in slot order; 0 when `at` isn't a chest worked this way
int chestHalves(World& w, ivec3 at, ivec3 out[2]);
int chestSlotsPerHalf(World& w, ivec3 at);
std::vector<ItemStack> chestItems(Level& lv, Player& p, World& w, ivec3 at);   // every slot, both halves
void chestSetItems(Level& lv, Player& p, World& w, ivec3 at, const std::vector<ItemStack>& v);   // every slot written back
// one half's slots (the renderer: what's seen inside; viewer: whose ender chest it is — null shows none in it)
std::vector<ItemStack> chestHalfItems(Level& lv, const Player* viewer, World& w, ivec3 half);
// where slot i lies in the world (the middle of its square on the chest's floor), its half's front direction
vec3 chestSlotCenter(World& w, ivec3 at, int slot);
float chestSlotSize(World& w, ivec3 at);                           // one square's side (blocks)
float chestFloorY(World& w, ivec3 at);                             // the floor the things lie on, above the block's base
int chestLidRow(World& w, ivec3 at);                               // where its lid starts (of the block's 32 rows)
// the slot under a point on the floor (-1 none)
int chestSlotAt(World& w, ivec3 at, vec3 p);
// the slot a ray (from o along dir) points at: onto the floor of a chest or barrel, onto the open front of a cabinet
int chestSlotAtRay(World& w, ivec3 at, vec3 o, vec3 dir);
// a cabinet (furniture "cabinet"): its doors swing open on three shelves of three (render/chest_layer.cpp draws them)
bool isCabinet(World& w, ivec3 at);
constexpr int CABINET_COLS = 3;
constexpr float CABINET_SHELF[3] = {9.f, 5.5f, 1.5f};   // the shelves' tops (of 16), top shelf first
constexpr float CABINET_TOP = 12.5f, CABINET_COL_W = 0.25f;
// put what's in your hand into a slot (one: just one of it); false if it can't go there
bool chestPut(Level& lv, Player& p, World& w, ivec3 at, int slot, bool one);
// take a slot's stack (one: just one) — into your empty hand, or your bag (toBag, or the hand full of something else)
bool chestTake(Level& lv, Player& p, World& w, ivec3 at, int slot, bool one, bool toBag);
int chestTakeAll(Level& lv, Player& p, World& w, ivec3 at);        // everything into your bag (what fits); how many stacks
int chestStoreAll(Level& lv, Player& p, World& w, ivec3 at);       // your bag (not the hotbar) into the chest; how many stacks

// lids left open (Options → Gameplay → Leave Chests Open): a chest's or a barrel's lid kept up in the world, its things seen
// inside; kept in the block's meta (saved and shared as ever). Both halves of a large chest go together
constexpr u8 LID_OPEN = 0x20;
void chestProgressLoot(World& w, const Player& p, ivec3 at);   // a structure chest first opened: the opener's progress tier adds to its own table (host-side; "lk" marks it)
bool chestHasLid(World& w, ivec3 at);              // a chest or barrel (not a cabinet, an ender or a party chest)
bool chestLidOpen(World& w, ivec3 at);
void setChestLidOpen(World& w, ivec3 at, bool open);
// the lid itself under a click on an open chest (its back face, or the back of its top where the lid stands up)
bool chestLidClicked(World& w, ivec3 at, ivec3 normal, vec3 point);

} // namespace dq
