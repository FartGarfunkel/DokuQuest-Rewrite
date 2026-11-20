#include "sim/weapon_quality.h"
#include "sim/attack_moves.h"
#include "core/tool_parts.h"

namespace dq {

WQual weaponQuality(const ItemStack& s) {
  if (!s.tag.is_object() || !s.tag.contains("qual") || !s.tag["qual"].is_string()) return WQual::Normal;
  const std::string q = s.tag["qual"].get<std::string>();
  if (q == "poor") return WQual::Poor;
  if (q == "fine") return WQual::Fine;
  if (q == "great") return WQual::Great;
  if (q == "legendary") return WQual::Legendary;
  return WQual::Normal;
}

float qualityDamageFactor(WQual q) {
  switch (q) {
    case WQual::Poor: return 0.85f;
    case WQual::Fine: return 1.12f;
    case WQual::Great: return 1.28f;
    case WQual::Legendary: return 1.5f;
    case WQual::Normal: return 1.f;
  }
  return 1.f;
}

void rollWeaponQuality(ItemStack& s, uint32_t seed) {
  const uint32_t r = seed % 100u;
  const char* q = r < 18u ? "poor" : r < 60u ? "normal" : r < 85u ? "fine" : r < 97u ? "great" : "legendary";
  if (!s.tag.is_object()) s.tag = json::object();
  s.tag["qual"] = q;
}

void stampGearQuality(ItemStack& s) {
  if (s.empty() || !(isPartTool(s) || isPartArmor(s))) return;
  if (s.tag.is_object() && s.tag.contains("qual") && s.tag["qual"].is_string()) return;
  static uint32_t n = 0xA341316Cu;
  n = n * 1664525u + 1013904223u;
  rollWeaponQuality(s, n);
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
