// sim/attach_play.h — attachments in play (the rules are in world/attach.h, storage on World): putting one into a cell
// that already has a main block, using one (levers, buttons, trapdoors, signs, item frames), breaking one on its own,
// popping the ones whose support went, buttons letting go, collision and climbing (sim/physics.h hooks).
//
// Moving pieces (pistons, hinges, sliders) and explosions: a cell's attachments come off as items when their cell
// is blown up or pushed (never carried along): the simplest rule that can't leave a part floating or inside a block.
#pragma once
#include "sim/level.h"
#include "sim/physics.h"
#include "core/itemstack.h"

namespace dq {

class Player;
struct ToolInfo;

void registerAttachments();   // core/init.cpp: physics hooks and the level system

// A block put into an occupied cell (x,y,z): 1 placed as an attachment, 2 the cell's main block is a small part that
// can step aside for this one (call attachStepAside just before setting it), 0 refused (a toast to p says why), -1 not a case for attachments (the usual rules).
// meta: as the usual rules worked it out for that cell. Consumes nothing: the caller does.
int placeAttachment(Level& lv, Player* p, World& w, int x, int y, int z, BlockId bid, u8 meta);
void attachStepAside(World& w, int x, int y, int z);   // (placeAttachment said 2) the main block becomes an attachment
// Use on an attachment (hit.part >= 0): levers, buttons, trapdoors, signs, item frames. false: nothing to do with it.
bool useAttachment(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack* held);
// Break one part on its own: its own drops (drops false: none), sound and dust; the rest of the cell stays.
void breakAttachment(Level& lv, Player* p, World& w, int x, int y, int z, int slot, bool drops, const ToolInfo* tool = nullptr);
// Every attachment of the cell comes off (explosions, a piston moving the cell): as items when drops.
void dropAllAttachments(Level& lv, World& w, int x, int y, int z, bool drops);
// Would turning the part (slot; -1 the main block) to newMeta hit the cell's other parts? (a trapdoor swinging down onto a chest)
bool partBlocked(const World& w, int x, int y, int z, int slot, BlockId id, u8 newMeta);

} // namespace dq
