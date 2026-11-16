// sim/moving.h — moving structures (stage 1: docs/superpowers/specs/2026-11-16-moving-structures-design.md §2). A hinge or
// slider told to move lifts its glued piece out of the world into a moving body (each block's id, meta and record: chest
// contents, signs...), turns or slides it smoothly, and sets it back as ordinary blocks at the end, their facings and glue
// turned with them. A body is finished at once when the game saves or a chunk it touches unloads, so nothing is lost.
// The machine keeps its state in its record: {"type":"machine_state","open","quarters","distance"} (glue may sit beside it).
#pragma once
#include "sim/level.h"
#include "sim/moving_piece.h"

namespace dq {

struct BodyBlock { ivec3 rel; BlockId id; u8 meta; json entity; u8 carve = 0; };   // carve: a chiseled block's cut corners   // rel: offset from the machine
struct Body {
  u32 id = 0; int realm = 0; Motion m; float t = 0, dur = 1;
  std::vector<BodyBlock> blocks;
  float fraction() const;      // 0..1, eased
  mat4 transform() const;      // the piece's placement now, in the machine's frame (its block's corner at the origin)
};

bool startMove(Level& lv, int realm, ivec3 machine, std::string* why);   // toggles: opens if shut, shuts if open
bool machineOpen(const World& w, ivec3 machine);
bool isMachine(BlockId id);    // a hinge or a slider
int machineFace(const World& w, ivec3 machine);   // its moving face (0..5) from its facing
json& machineState(World& w, ivec3 machine);      // its record (made if missing; glue already there kept)
void finishAll(Level& lv);
const std::vector<Body>& movingBodiesList(Level& lv);
void registerMovingBodies();

} // namespace dq
