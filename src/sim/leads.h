// sim/leads.h — the lead (Minecraft's): a creature on a lead follows whoever holds it, or stays tied to a fence post.
// The creature's data["leash"] is the holder's name, or [x, y, z] of the post.
#pragma once
#include "common/common.h"

namespace dq {
class Level; class Entity;
// where the lead's other end is: a player's hand height or the fence post's knot (false: nothing holds it now)
bool leashHolder(Level& lv, const Entity& e, vec3& at);
void registerLeads();
} // namespace dq
