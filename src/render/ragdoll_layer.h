// render/ragdoll_layer.h — limp bodies (sim/ragdoll.h), drawn from their simulated part frames:
//   creatures that just died (each game simulates its own copy from the moment it saw the death: cosmetic, so a guest's
//   copy never puts anything out of step) — they lie there a while, then sink away; the creature layer skips them;
//   players' bodies ("corpse" entities, sim/corpse.h) — their skin and the armour they wore, limp where they fell, for as
//   long as the body is there. A body first seen long after it fell is settled at once instead of falling again.
#pragma once
#include "client/session.h"
#include "sim/ragdoll.h"

namespace dq {

RagdollSet& clientRagdolls();   // the creatures lying limp here (render/creature_layer.cpp skips drawing those)
void registerRagdollLayer();    // idempotent (called from registerCreatureLayers)

} // namespace dq
