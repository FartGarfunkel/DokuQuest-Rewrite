// world/attach.h — the rules for attachments: small parts (torches, ladders, trapdoors, levers, buttons, signs, item
// frames, wall covers, pressure plates, lanterns) that share a cell with its main block, each fixed to a face of a
// neighbouring cell. Storage and the raw API are on Chunk / World (World::attachments...); this file says which blocks may
// be one, what holds each up, and each part's own boxes (pixels, the same shapes as the block would have alone), so
// placing, picking, breaking, collision and the outline use every part's real shape, not the whole cell.
// No GL here: the dedicated server uses it too.
#pragma once
#include "world/shapes.h"

namespace dq {

bool attachable(BlockId id);                       // may be an attachment
// the cell offset of what holds it up (false: it needs nothing, a trapdoor)
bool attachSupportDir(BlockId id, u8 meta, ivec3& d);
bool attachSupported(const World& w, int x, int y, int z, BlockId id, u8 meta);

// One part's boxes in pixels (block-local), as the shape code gives them for that id/meta in that cell, with the
// same sink and set-back the block would have. false = a plain full cube.
bool partBoxes(const World& w, int x, int y, int z, BlockId id, u8 meta, int purpose, std::vector<PixBox>& out);
// What a body meets of a cell's attachments: every solid part's collision boxes (pixels, block-local), added to out.
// The one truth for players, creatures and the townsfolk's way finding (sim/physics.cpp, sim/npc_path.cpp).
void attachCollideBoxes(const World& w, int x, int y, int z, std::vector<PixBox>& out);
// Is one of the cell's attachments climbed (a ladder, a vine on the wall beside its main block)?
bool attachClimb(const World& w, int x, int y, int z);
// The room a part takes in its cell (Collide ∪ Draw, or Select for parts drawn on their own like torches and signs).
// main: it's the cell's main block (its carve counts; a chest drawn from its model takes only the model's box).
bool partRoom(const World& w, int x, int y, int z, BlockId id, u8 meta, bool main, std::vector<PixBox>& out);   // false: the whole cell
// Is there room in (x,y,z) for a part id/meta beside the main block and the other attachments? skipSlot: an attachment
// to leave out (one being changed, -1 none); skipMain: leave the main block out (it is the one changing).
bool attachRoomFree(const World& w, int x, int y, int z, BlockId id, u8 meta, int skipSlot = -1, bool skipMain = false);
// Do all the cell's attachments still fit beside its main block (a main block set over them)?
bool attachRoomFor(const World& w, int x, int y, int z);
// World-space select boxes of one part (slot -1 = the main block), for the outline
bool partWorldBoxes(const World& w, int x, int y, int z, int slot, int purpose, std::vector<BoxF>& out);

// The ray against every part of a cell that has attachments: the nearest box's part (-1 main), its distance and normal.
// false: the ray misses everything in the cell.
bool attachRayCell(const World& w, glm::dvec3 o, glm::dvec3 d, int x, int y, int z, double maxDist, int& part, double& t, ivec3& n);

// The wire/mod form of a cell's attachments: [[key, meta, entity?]...] (keys, never runtime ids)
json attachmentsToJson(const std::vector<Attachment>* parts);
std::vector<Attachment> attachmentsFromJson(const json& j);

} // namespace dq
