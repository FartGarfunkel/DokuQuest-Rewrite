#include "sim/weapon_quality.h"
#include "sim/attack_moves.h"

namespace dq {

WQual weaponQuality(const ItemStack& s) {
  if (!s.tag.is_object() || !s.tag.contains("qual") || !s.tag["qual"].is_string()) return WQual::Normal;
  const std::string q = s.tag["qual"].get<std::string>();
  if (q == "poor") return WQual::Poor;
  if (q == "fine") return WQual::Fine;
  if (q == "exceptional") return WQual::Exceptional;
  return WQual::Normal;
}

float qualityDamageFactor(WQual q) {
  switch (q) {
    case WQual::Poor: return 0.85f;
    case WQual::Fine:
    case WQual::Exceptional: return 1.12f;
    case WQual::Normal: return 1.f;
  }
  return 1.f;
}

void rollWeaponQuality(ItemStack& s, uint32_t seed) {
  const uint32_t r = seed % 100u;
  const char* q = r < 25u ? "poor" : r < 80u ? "normal" : "fine";
  if (!s.tag.is_object()) s.tag = json::object();
  s.tag["qual"] = q;
}

float weaponQualityFactor(const ItemStack& s) {
  if (s.empty()) return 1.f;
  switch (movesetOf(s.def())) {
    case Moveset::Sword:
    case Moveset::Dagger:
    case Moveset::Axe:
    case Moveset::Greatsword:
    case Moveset::Spear:
    case Moveset::Mace:
      return qualityDamageFactor(weaponQuality(s));
    default:
      return 1.f;
  }
}

}  // namespace dq
