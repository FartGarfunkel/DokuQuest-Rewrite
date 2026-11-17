// sim/corpse.h — a player's body. With the "graves" rule on, a player who dies leaves their body where they fell (it
// goes limp: render/ragdoll_layer.cpp) holding everything they carried, each thing remembered with the exact slot it
// was in (hotbar / bag index, armour, off hand, backpack, belt hook, charm, cape, quiver, shield). Using the body (or walking
// into it) gives it all back: each thing to its own slot, or to a free one if that's taken now, or at your feet — never
// lost. The death compass points to it.
//
// More than one body: every body still lying somewhere is kept, oldest first, in stats["bodies"] ({realm, dim, x, y, z,
// cid}). The compass (and Player::grave / stats["grave"], which the compass and the maps read) always points at the
// OLDEST one — the first body is the one whose gear you lose track of first, so it leads you there; take it back and
// it turns to the next. The map marks every body. You keep the compass until the last body is taken back, even if you
// die again on the way (it is never put in a body).
//
// The body is an entity ("corpse") saved with the level's entities (entities.json: not tied to a chunk, so it is still
// there when its chunk unloads), never despawns, takes no damage, and if it falls out of the world or into lava it is
// moved to where its owner last stood safely (or to the world's spawn). Host-authoritative: a guest's body is made on
// the host (the guest's game hands it over like anything else it spawns), and a guest using it asks the host
// (useOnHost), which gives the things back into their slots; the gear slots go back to the guest with their bags.
// Old worlds' grave blocks still open as before (sim/rules.cpp openGrave). Its record: data "of" (the owner's name: not
// "owner", which marks a pet for the stable), "cid", "items" [{g, i, s}], "skin", "wear", "safe", "rk".
#pragma once
#include "sim/combat.h"

namespace dq {

// The player's body. withItems: take every item (with its slot) off them into it; otherwise an empty body (the
// "graves" rule off / keepInventory) that fades after a while. Returns the entity (never null).
Entity* spawnCorpse(Level& lv, Player& p, const DamageSource& src, bool withItems);
// Give a body's things back to its owner (false: not a body, or someone else's — they get told whose).
bool retrieveCorpse(Level& lv, Player& p, Entity& corpse);
// The bodies still lying somewhere (oldest first); keeps Player::grave / stats["grave"] on the first.
json& deathBodies(Player& p);
void syncDeathBodies(Level& lv, Player& p, float dt = 0);   // follow bodies that moved, forget bodies taken back
bool isCorpse(const Entity& e);
void registerCorpses();   // entity factory, the use hook, the level system that keeps the bodies list in step

} // namespace dq
