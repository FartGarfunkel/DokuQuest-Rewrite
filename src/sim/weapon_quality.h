// sim/weapon_quality.h — a made or found weapon's quality. A missing tag is a normal weapon.
#pragma once
#include "core/itemstack.h"
#include <cstdint>

namespace dq {

enum class WQual { Poor, Normal, Fine, Exceptional };

WQual weaponQuality(const ItemStack& s);
float qualityDamageFactor(WQual q);
// poor / normal / fine at 25 / 55 / 20. The same seed writes the same tag. Exceptional is not rolled.
void rollWeaponQuality(ItemStack& s, uint32_t seed);
// 1 for an empty hand, a tool, or anything that is not a weapon moveset.
float weaponQualityFactor(const ItemStack& s);

}  // namespace dq
