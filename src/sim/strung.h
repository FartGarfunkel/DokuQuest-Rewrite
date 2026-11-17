// sim/strung.h — chains and ropes strung between two blocks (moving structures stage 5, docs/superpowers/specs/
// 2026-11-16-moving-structures-design.md §6). Sneak-use a chain or rope on a block, then on another up to 16 blocks away:
// it strings between them with a sag (chains heavier), one item per 2 blocks. Breaking either end drops it. Planks or
// slabs used on a near-level rope lay slats: a rope bridge you can walk on. A steep one climbs like a ladder. An end on a
// hinge's or slider's piece goes where the piece goes. Kept in lv.info.data["strung"]; a host sends guests the list.
#pragma once
#include "sim/level.h"

namespace dq {

struct Strung { u32 id = 0; int realm = 0; ivec3 a{0}, b{0}; bool chain = false, slats = false; };
std::vector<Strung> strungList(Level& lv);
vec3 strungEnd(Level& lv, const Strung& s, bool second);   // where an end is now (on a moving piece: with it)
vec3 strungPoint(Level& lv, const Strung& s, float t);      // along it, 0..1, sag included
float strungSag(const Strung& s, float len);
void registerStrung();

} // namespace dq
