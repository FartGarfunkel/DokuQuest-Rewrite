// sim/creature_entities.h — the other things game/entities.js moves about: dropped items ("item"),
// projectiles (arrows, fireballs, llama spit, snowballs, hex flasks), felled trees and severed parts (gibs).
// All are plain Entities with their state in `data` (so the network and saves see it):
//   "item"          data.stack (ItemStack json), data.pickupDelay, data.floating
//   "arrow"         data.owner, data.damage, data.pickup, data.stuck, data.burning
//   "projectile"    data.kind ("fireball" | "spit" | "snow"), data.owner
//   "hex_flask"     data.kind ("poison" | "slow"), data.owner
//   "falling_tree"  data.blocks [[x,y,z,blockKey]...] relative to pos (the stump), data.dirYaw, data.drops [stack...], data.t
//   "gib"           data.model, data.part, data.rot (16 floats, the part's turn), data.ax, data.az, data.rest, data.tint
#pragma once
#include "sim/creature.h"

namespace dq {

Entity* spawnArrow(Level& lv, int realm, vec3 p, vec3 v, Entity* owner, float damage, bool pickup);
Entity* spawnProjectile(Level& lv, int realm, vec3 p, vec3 v, const std::string& kind, Entity* owner);
Entity* spawnHexFlask(Level& lv, int realm, vec3 p, vec3 v, Entity* owner, const std::string& kind);
// a felled tree tips over around its stump, then breaks into drops where the trunk fell
struct FelledBlock { ivec3 p; BlockId id; };
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<FelledBlock>& blocks, ivec3 stump, float dirYaw, const std::vector<ItemStack>& drops);
Entity* spawnGib(Level& lv, Creature& from, const std::string& part, const mat4& partMatrix);

void registerCreatureEntities();   // factories: item, arrow, projectile, hex_flask, falling_tree, gib

} // namespace dq
