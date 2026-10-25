// sim/camping.h — travel and camping (game/camping.js, 0.9.0.11).
//  · Campfires: a shovel or water puts one out (meta bit 8: smouldering ash, no light), flint and steel lights it
//    again; set up to four pieces of raw food on a lit one to cook (24 s); step on it and you burn. The smoke
//    climbs out of the top of the chimney when the fire is in a hearth (campFlue). Particles: render/content_layers.
//  · The campfire kit (meta bit 16): a campfire you carry; sneak and use it with an empty hand to pack it up.
//  · Rope: use it on a ledge, a ceiling or another rope and it hangs down to the ground. Cut one and the rest
//    below comes down with it.
//  · The grappling hook: throw it; it catches on a ledge/wall/ceiling (block grapple_anchor + a rope column with
//    meta bit 1); use the hook or its rope to take it back.
//  · The sleeping bag: sleep the night anywhere; your spawn for that night only (Player::stats["bagRestore"]).
#pragma once
#include "sim/content.h"
#include <unordered_set>

namespace dq {

constexpr u8 CAMPFIRE_OUT = 8, CAMPFIRE_KIT = 16, HOOK_ROPE = 1;
constexpr float CAMP_COOK = 24, HOOK_RANGE = 30;
extern const vec2 CAMP_SPOTS[4];   // where the food sits on the fire (x, z within the block)

struct IVec3Hash { size_t operator()(const ivec3& v) const { return (size_t)((u32)v.x * 73856093u) ^ (size_t)((u32)v.y * 19349663u) ^ (size_t)((u32)v.z * 83492791u); } };
using CellSet = std::unordered_set<ivec3, IVec3Hash>;

bool isCampfire(BlockId id);
bool campfireLit(World& w, int x, int y, int z);
// A fire with something solid right over it, beside (or under) a solid stack at least three blocks taller than
// the fire, is in a hearth: its smoke comes out of the top of the tallest such stack (looked up every 4 s).
std::optional<vec3> campFlue(World& w, int x, int y, int z);
const CellSet& campfiresIn(Level& lv, int realm);   // every loaded campfire block (lit or out) in a realm

class GrappleHook : public Entity {
public:
  GrappleHook() { type = "grappling_hook"; w = 0.1f; h = 0.1f; netLocal = true; noGravity = true; }
  u32 owner = 0;
  bool back = false;
  json stack;           // the hook (ItemStack json), worn one more
  void tick(Level& lv, double dt) override;
};
GrappleHook* grappleOf(Level& lv, const Player& p);
bool grappleLodge(Level& lv, Player& p, const json& stack, int hx, int hy, int hz, ivec3 n);
void grappleTake(Level& lv, Player& p, ivec3 anchor, bool give);
std::optional<ivec3> grappleFind(World& w, int x, int y, int z);
bool hangRope(UseCtx& u);
// gameplay calls this before someone lies down in a bed block: false = they can't (they've been told why).
// For a sleeping bag it keeps the old spawn to come back to in the morning.
bool sleepingBagSleep(Level& lv, Player& p, int x, int y, int z);

} // namespace dq
