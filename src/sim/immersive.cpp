// sim/immersive.cpp — see immersive.h
#include "sim/immersive.h"
#include "sim/rules.h"
#include "sim/blockinfo.h"

namespace dq {

bool immersive(Level& lv) { return rule(lv, "immersive"); }
bool needsAxe(Level& lv, BlockId id) { return id && bhas(id, BF::IsLog) && immersive(lv); }
bool recipeAllowed(Level& lv, const json& r) { return !(r.is_object() && r.value(immersive(lv) ? "classicOnly" : "immersiveOnly", false)); }

} // namespace dq
