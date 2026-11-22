// sim/weapon_quality.h — a made or found weapon's quality. A missing tag is a normal weapon.
#pragma once
#include "core/itemstack.h"
#include <cstdint>

namespace dq {

enum class WQual { Poor, Normal, Fine, Great, Legendary };

WQual weaponQuality(const ItemStack& s);
float qualityDamageFactor(WQual q);
// poor 18, normal 42, fine 25, great 12, legendary 3. The same seed writes the same tag.
void rollWeaponQuality(ItemStack& s, uint32_t seed);
// one roll onto a procedural tool or armour piece that does not already have a quality
void stampGearQuality(ItemStack& s);
// 1 for an empty hand or a tool that is not a weapon moveset.
float weaponQualityFactor(const ItemStack& s);
// The same factor for a procedural tool, a procedural armour piece, and a weapon. A missing tag is 1.
float gearQualityFactor(const ItemStack& s);
class Player;
// Wood, flint, stone and copper always pass. Iron and above of a greatsword, spear, mace, longsword, battleaxe or
// warhammer need Melee 15. A sword or an axe always passes.
bool weaponSkillMet(const Player& p, const ItemStack& s);

}  // namespace dq
