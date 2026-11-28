// sim/mob_carry.h — who may lift which creature, by the data (mobs.json / data/mob_traits.json "carry", sim/creature.h):
//   "any"    anyone lifts it in their arms (sneak + use with an empty hand; on a controller the same buttons); once
//            tamed only its owner does
//   "pet"    its owner lifts it once tamed (a dog, a cat, a fox, a parrot, a little critter); a wild one goes in a basket
//   "basket" only in a wicker basket (fish, bugs, little wild birds)
// Any young animal fits a basket too. What's lifted rides in Player::carrying {kind:"mob", type, hp, data, save} and
// is set down again exactly as it was (its owner, its name, its coat, its place in the stable).
#pragma once
#include "sim/level.h"
#include <string>

namespace dq {

class Player;
class PlayerController;
class Creature;   // (sim/creature.h: not included here, so gameplay files can use this header beside sim/gameplay.h)

bool canCarryInArms(const Creature& c, const Player& p, std::string* why = nullptr);   // why: what to tell them when not
bool fitsBasket(const Creature& c, const Player* p);
// lifts it into the player's arms (Player::carrying); false (and why) when it can't be
bool pickUpCreature(Level& lv, Player& p, Creature& c, std::string* why = nullptr);
// sneak + use with an empty hand on a creature: lifted (true; or true having told them why it won't come; false: not
// this use). sim/basket.cpp; the controller calls it after every other use, runGuestUse for a joined player
bool liftInArms(PlayerController& pc, Entity& e);
// the carried creature back in the world at `at` facing `yaw` (nullptr: nothing carried, or not a creature)
Creature* setDownCarriedCreature(Level& lv, Player& p, vec3 at, float yaw);
// the same, as an entity, for a file that cannot include the creature header (its item drop clashes)
Entity* setDownCarried(Level& lv, Player& p, vec3 at, float yaw);

} // namespace dq
