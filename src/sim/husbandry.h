// sim/husbandry.h — animal husbandry (after TerraFirmaCraft), in an immersive world: farm animals are male or female, and
// come to know you: feeding one its food once a day raises its familiarity (a missed day or two and it slowly forgets).
// Only a familiar male and female breed. Wild animals stay wild. Kept in the creature's own data (no change to saves).
#pragma once
#include "sim/level.h"

namespace dq {

class Creature;
constexpr float FAM_FEED = 0.15f, FAM_FORGET = 0.05f, FAM_BREED = 0.3f;
bool isMale(const Creature& c);
void setMale(Creature& c, bool male);
float familiarity(const Creature& c);
bool husbandryFeed(Level& lv, Creature& c);          // false if it's been fed today already
void husbandryDaily(Level& lv, Creature& c);         // forgetting, once a day
bool canBreed(Level& lv, const Creature& a, const Creature& b);   // classic worlds: always
std::string husbandryLabel(const Creature& c);       // "Bull · familiarity 40%" (the look caption)

} // namespace dq
