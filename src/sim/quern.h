// sim/quern.h — TerraFirmaCraft's quern: a handstone turned on a stone base. Hold use on it with something in hand: a
// second's grinding and one of it becomes what data/quern.json says (wheat: flour; ore: powder, holding more metal;
// limestone: flux; bone: bone meal; flowers: dye...), dropped on top.
#pragma once
#include "sim/level.h"
#include "sim/physics.h"

namespace dq {

class Player;
constexpr float QUERN_HOLD = 1.0f;
std::pair<std::string, int> quernResult(const std::string& itemId);   // {"", 0} if it doesn't grind
bool quernUse(Level& lv, Player& p, const RayHit& hit, bool repeat, float step = 0.22f);   // true if it took the click

} // namespace dq
