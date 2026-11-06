#include "world/light_color.h"

namespace dq {

vec3 blockLightColour(BlockId id) {
  const BlockDef& b = reg().block(id);
  vec3 c(1.0f, 0.85f, 0.62f);
  const std::string& k = b.key;
  auto has = [&](const char* s) { return k.find(s) != std::string::npos; };
  if (b.props.contains("lightColor") && b.props["lightColor"].is_array() && b.props["lightColor"].size() >= 3)
    c = vec3(b.props["lightColor"][0].get<float>(), b.props["lightColor"][1].get<float>(), b.props["lightColor"][2].get<float>());
  else if (has("soul")) c = vec3(0.35f, 0.8f, 1.0f);
  else if (has("redstone")) c = vec3(1.0f, 0.22f, 0.15f);
  else if (has("sea_lantern") || has("prismarine") || has("conduit") || has("sea_pickle")) c = vec3(0.55f, 0.95f, 1.0f);
  else if (has("amethyst") || has("crying_obsidian") || has("ember_portal") || has("rift")) c = vec3(0.72f, 0.4f, 1.0f);
  else if (has("sky_portal") || has("end_rod") || has("beacon")) c = vec3(0.85f, 0.92f, 1.0f);
  else if (has("glow_lichen") || has("verdant") || has("firefly")) c = vec3(0.6f, 1.0f, 0.7f);
  else if (has("lava") || has("magma") || has("ember")) c = vec3(1.0f, 0.42f, 0.12f);
  else if (has("glowstone") || has("shroomlight") || has("sun_cloud")) c = vec3(1.0f, 0.86f, 0.55f);
  else if (has("crystal") || has("aether")) c = vec3(0.7f, 0.85f, 1.0f);
  else {
    static const std::pair<const char*, vec3> D[] = {
      {"light_blue", {0.45f, 0.75f, 1.0f}}, {"light_gray", {0.85f, 0.85f, 0.85f}}, {"white", {1, 1, 1}}, {"orange", {1.0f, 0.55f, 0.15f}}, {"magenta", {0.9f, 0.35f, 0.9f}},
      {"yellow", {1.0f, 0.92f, 0.3f}}, {"lime", {0.55f, 1.0f, 0.25f}}, {"pink", {1.0f, 0.6f, 0.75f}}, {"gray", {0.6f, 0.6f, 0.65f}}, {"cyan", {0.2f, 0.85f, 0.9f}},
      {"purple", {0.65f, 0.3f, 1.0f}}, {"blue", {0.25f, 0.4f, 1.0f}}, {"brown", {0.7f, 0.45f, 0.25f}}, {"green", {0.3f, 0.8f, 0.2f}}, {"red", {1.0f, 0.2f, 0.18f}}, {"black", {0.35f, 0.3f, 0.45f}}};
    if (has("lamp") || has("lantern") || has("candle") || has("glass_light"))
      for (auto& [n, col] : D) if (k.rfind(n, 0) == 0) { c = col; break; }
  }
  const float m = std::max({c.x, c.y, c.z, 1e-3f});
  return c / m;
}

void blockLightRgb(BlockId id, u8 level, u8 out[3]) {
  if (!level) { out[0] = out[1] = out[2] = 0; return; }
  const vec3 c = blockLightColour(id);
  const float s = level * 17.f;   // 15 -> 255, the top of Cubyz's byte light
  auto q = [&](float x) { return (u8)clampv((int)std::lround(x * s), 0, 255); };
  out[0] = q(c.x); out[1] = q(c.y); out[2] = q(c.z);
}

} // namespace dq
