// render/fp_motion.cpp — see fp_motion.h
#include "render/fp_motion.h"
#include "core/registry.h"

namespace dq {

static bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }

float fpSwingRate(const std::string& id, bool mining) {
  float r = 3.6f;
  if ((has(id, "_axe") && !has(id, "pickaxe")) || has(id, "battleaxe") || has(id, "mace") || has(id, "hammer") || has(id, "halberd") || has(id, "trident")) r = 2.9f;
  else if (has(id, "dagger") || has(id, "knife")) r = 4.4f;
  return mining ? std::max(r, 3.6f) : r;
}

static constexpr float SPLIT = 0.4f;
float fpSwapLower(float t) {
  if (t <= 0 || t >= 1) return 0;
  if (t < SPLIT) { const float u = t / SPLIT; return u * u; }
  const float u = (t - SPLIT) / (1 - SPLIT) - 1, c1 = 1.2f, c3 = c1 + 1;
  return -(c3 * u * u * u + c1 * u * u);   // 1 - easeOutBack
}
bool fpSwapShowsOld(float t) { return t > 0 && t < SPLIT; }

FpOffset fpStateOffset(const FpState& s, float t) {
  FpOffset o;
  if (s.sprint > 0) { o.dy -= 0.06f * s.sprint; o.pitch += 0.28f * s.sprint; o.roll -= (0.1f + std::sin(t * 7) * 0.03f) * s.sprint; }
  if (s.land > 0) o.dy -= 0.09f * s.land;
  if (s.climb > 0) { o.dy -= 0.35f * s.climb; o.pitch -= 0.3f * s.climb; }
  if (s.swim > 0) { o.dy -= (0.12f + std::sin(t * 2) * 0.02f) * s.swim; o.roll += std::sin(t * 1.5f) * 0.05f * s.swim; }
  if (s.drink > 0) { o.pitch += 0.55f * s.drink; o.dy += 0.06f * s.drink; o.dz += 0.05f * s.drink; }
  return o;
}

bool isDrinkItem(const std::string& id) {
  if (id.empty() || id == "snowball" || id.rfind("splash_", 0) == 0) return false;
  const ItemDef* d = reg().item(id);
  return (d && d->has("drink")) || has(id, "bottle") || has(id, "mug") || has(id, "flask") || id.rfind("potion", 0) == 0;
}

} // namespace dq
