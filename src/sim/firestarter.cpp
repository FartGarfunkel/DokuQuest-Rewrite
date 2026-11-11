// sim/firestarter.cpp — see firestarter.h
#include "sim/firestarter.h"
#include "sim/firepit.h"
#include "sim/pit_kiln.h"
#include "sim/charcoal_pit.h"
#include "sim/forge.h"
#include "sim/bloomery.h"
#include "sim/camping.h"
#include "sim/player.h"
#include "sim/content.h"
#include "core/registry.h"
#include <random>

namespace dq {

static std::vector<LightFn>& lightables() { static std::vector<LightFn> v; return v; }
void registerLightable(LightFn f) { for (LightFn g : lightables()) if (g == f) return; lightables().push_back(f); }
static float g_roll = -1;
void setFirestarterRoll(float r) { g_roll = r; }
static float roll() { static thread_local std::mt19937 g{std::random_device{}()}; return g_roll >= 0 ? g_roll : std::uniform_real_distribution<float>(0, 1)(g); }

bool firestarterUse(Level& lv, Player& p, const RayHit* hit, bool repeat, float step) {
  if (p.held().id != "firestarter") return false;
  if (!hit) return true;
  World& w = lv.world(p.realm);
  const json key = {hit->block.x, hit->block.y, hit->block.z};
  json& d = p.data;
  if (!d.is_object()) d = json::object();
  if (!repeat || d.value("fireAt", json()) != key) { d["fireAt"] = key; d["fireT"] = 0.0; return true; }
  const float t = d.value("fireT", 0.f) + step;
  d["fireT"] = t;
  const vec3 at = vec3(hit->block) + vec3(0.5f, 1.05f, 0.5f);
  lv.fx->particles("campfire_smoke", at, 2, {{"spread", 0.1}, {"vel", {0, 0.6, 0}}, {"life", 1}, {"size", 0.08}, {"rise", true}});
  if (t < FIRESTARTER_HOLD) return true;
  d["fireT"] = 0.0;
  wearHeld(lv, p, 1);
  const ivec3 b = hit->block, up = b + ivec3(0, 1, 0);
  const float chance = 0.3f + (strawLiesAt(lv, p.realm, up.x, up.y, up.z) || strawLiesAt(lv, p.realm, b.x, b.y, b.z) ? 0.4f : 0.f);
  if (roll() >= chance) { lv.fx->particles("campfire_smoke", at, 6, {{"spread", 0.2}, {"vel", {0, 0.8, 0}}, {"life", 1.2}, {"size", 0.1}, {"rise", true}}); return true; }
  // a spark: what is it on?
  bool lit = false;
  if (isCampfire(hit->id) && (w.getMeta(b.x, b.y, b.z) & CAMPFIRE_OUT)) {
    w.setBlock(b.x, b.y, b.z, hit->id, w.getMeta(b.x, b.y, b.z) & ~CAMPFIRE_OUT);
    if (json* e = w.blockEntity(b.x, b.y, b.z)) { if (e->value("burn", 0.f) < 20) (*e)["burn"] = 20; (*e)["temp"] = std::max(250.f, e->value("temp", 20.f)); (*e)["peak"] = e->value("peak", 450.f); w.blockEntityChanged(b.x, b.y, b.z); }
    lit = true;
  }
  if (!lit) lit = kilnLight(lv, w, b.x, b.y, b.z);
  if (!lit) lit = charcoalLight(lv, w, b.x, b.y, b.z);
  if (!lit) lit = forgeLight(lv, w, b.x, b.y, b.z);
  if (!lit) lit = bloomeryLight(lv, w, b.x, b.y, b.z);
  for (LightFn f : lightables()) if (!lit && f(lv, w, b.x, b.y, b.z)) lit = true;
  if (!lit && hit->normal.y == 1) {
    // the sticks on the ground are the host's: a guest asks it to make the firepit (sim/content.cpp's netAction)
    if (lv.isRemote()) { if (lv.net) lv.net->action(p, "content", json{{"k", "firepitSticks"}, {"x", up.x}, {"y", up.y}, {"z", up.z}}); }
    else lit = firepitFromSticks(lv, w, up.x, up.y, up.z);
  }
  if (lit) lv.fx->sound("ignite", at, 16);
  return true;
}

} // namespace dq
