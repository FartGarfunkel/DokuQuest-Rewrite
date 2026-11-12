// sim/elytra.cpp — gliding on elytra, boosts, crashes (game/elytra.js; firework boost from game/batche.js).
#include "sim/elytra.h"
#include "sim/physics.h"
#include "sim/weather.h"
#include <unordered_map>

namespace dq {

bool hasElytra(const Player& p) {
  // worn in the cape slot (the original's place for them) or the chest slot
  if (auto& cs = contentHooks().capeSlot) { const ItemStack* c = cs(p); if (c && !c->empty() && c->id == "elytra") return true; }
  if (!p.cape.empty() && !p.cape[0].empty() && p.cape[0].id == "elytra") return true;
  return p.armor.size() > 1 && !p.armor[1].empty() && p.armor[1].id == "elytra";
}

bool elytraJump(Level& lv, Player& p) {
  if (p.gliding || p.onGround || p.flying || p.riding || p.dead || !hasElytra(p) || p.vel.y >= 1.5f || p.inWater || p.inLava) return false;
  p.gliding = true;
  sfxUi(lv, p, "elytra_open");
  return true;
}

namespace { std::unordered_map<u32, float>& windT() { static std::unordered_map<u32, float> m; return m; } }

void elytraMove(Level& lv, Player& p, double dtd) {
  if (!p.gliding) return;
  float dt = (float)dtd;
  if (p.onGround || p.inWater || p.inLava || p.flying || p.riding || p.dead || !hasElytra(p)) { p.gliding = false; p.h = PLAYER_H; return; }
  p.sneaking = false; p.swimming = false; p.crawling = false;
  // Minecraft's glide, step for step at its 20 ticks a second (in blocks per tick): gravity eased by how level you
  // look, a dive turned into speed along your heading, a climb paid for with speed, the air's drag; a boost pulls
  // you along where you look
  vec3 l = lookVec(p);
  static std::unordered_map<u32, float> acc;
  float& a = acc[p.id]; a = std::min(a + dt, 0.25f);
  vec3 v = p.vel / 20.f;
  // the weather's wind, out under the open sky of the Overworld and the Skylands: blowing toward (-sin dir, -cos dir)
  vec2 W(0); float gustUp = 0;
  if (WeatherSystem* ws = weatherOf(lv)) {
    const int d = dimOfY(ifloor(p.pos.y));
    World& w0 = lv.world(p.realm);
    const u8 lt = w0.getLight(ifloor(p.pos.x), ifloor(p.pos.y + 0.5f), ifloor(p.pos.z));
    if (p.realm == REALM_MAIN && (d == DIM_OVERWORLD || d == DIM_AETHER) && (lt >> 4) >= 12) {
      W = vec2(-std::sin(ws->windDir), -std::cos(ws->windDir)) * ws->windStrength * 0.45f;   // blocks a tick
      gustUp = ws->gust;
    }
  }
  while (a >= 0.05f) {
    a -= 0.05f;
    const float pitch = -std::asin(clampv(l.y, -1.f, 1.f));   // Minecraft's: looking down is positive
    const float hl = std::hypot(l.x, l.z), hs = std::hypot(v.x, v.z);
    float cp = std::cos(pitch); cp = cp * cp * std::min(1.f, glm::length(l) / 0.4f);
    v.y += 0.08f * (-1 + cp * 0.75f);
    if (v.y < 0 && hl > 0) { float lift = v.y * -0.1f * cp; v += vec3(l.x * lift / hl, lift, l.z * lift / hl); }
    if (pitch < 0 && hl > 0) { float climb = hs * -std::sin(pitch) * 0.04f; v += vec3(-l.x * climb / hl, climb * 3.2f, -l.z * climb / hl); }
    if (hl > 0) { v.x += (l.x / hl * hs - v.x) * 0.1f; v.z += (l.z / hl * hs - v.z) * 0.1f; }
    // the wind (Phase 8): a headwind lifts you, a tailwind pushes you on, a crosswind drifts you, a gust carries you up
    if ((W.x != 0 || W.y != 0) && hl > 0) {
      const vec2 hd(l.x / hl, l.z / hl);
      const float along = W.x * hd.x + W.y * hd.y;   // + behind you, - in your face
      const vec2 cross = W - hd * along;
      v.y += std::max(0.f, -along) * 0.022f * cp + gustUp * 0.012f;
      v.x += hd.x * std::max(0.f, along) * 0.006f + cross.x * 0.008f;
      v.z += hd.y * std::max(0.f, along) * 0.006f + cross.y * 0.008f;
    }
    v *= vec3(0.99f, 0.98f, 0.99f);
    if (p.glideBoost > 0) v += l * 0.1f + (l * 1.5f - v) * 0.5f;
  }
  if (p.glideBoost > 0) p.glideBoost -= dt;
  p.vel = v * 20.f;
  p.fallDist = 0; p.h = SWIM_H;
  World& w = lv.world(p.realm);
  float before = glm::length(p.vel);
  MoveResult r = moveBox(w, p.pos, p.w, p.h, p.vel * dt, 0.f);
  if (r.hitX) p.vel.x = 0;
  if (r.hitZ) p.vel.z = 0;
  if (r.hitY) p.vel.y = 0;
  p.onGround = r.onGround;
  float after = glm::length(p.vel);
  if ((r.hitX || r.hitY || r.hitZ) && before - after > 13 && !p.dead) {   // hit a wall fast and it hurts
    hurtPlayer(lv, p, std::round((before - after - 13) * 0.7f), "fall");
    sfxUi(lv, p, "thud");
  }
  // the rush of air
  float& wt = windT()[p.id];
  if ((wt -= dt) <= 0) { wt = 0.5f; if (glm::length(p.vel) > 14) sfxUi(lv, p, "elytra_wind"); }
  if (p.glideBoost > 0 && frand() < dt * 40) lv.fx->particles("gust_ball", p.pos + vec3(0, 0.3f, 0), 1, {{"spread", 0.5}});
  if (p.onGround) { p.gliding = false; p.h = PLAYER_H; }
}

bool elytraUse(UseCtx& u) {
  Player& p = u.p;
  const ItemDef* d = p.held().def();
  if (!d || !p.gliding || u.item != p.held().id) return false;
  if (d->has("gustPuff")) { useUpHeld(p); p.glideBoost = 0.8f; sfxUi(u.lv, p, "wind_charge"); return true; }
  if (d->has("firework")) { useUpHeld(p); p.glideBoost = 1.4f; sfxUi(u.lv, p, "firework_launch"); return true; }
  return false;
}

void elytraTick(Level& lv, double dt) {
  for (Player* p : levelPlayers(lv))
    if (p->gliding && (p->dead || p->inWater || p->inLava || p->flying || !hasElytra(*p))) { p->gliding = false; p->h = PLAYER_H; }
}

std::array<ElytraWing, 2> elytraWings(bool open) {
  std::array<ElytraWing, 2> out;
  float spread = open ? ELYTRA_SPAN : 0;
  for (int i = 0; i < 2; i++) {
    float side = i == 0 ? 1.f : -1.f;
    out[i].side = side; out[i].ang = side * (0.18f + (open ? 1.40f : 0.f)); out[i].tilt = open ? 0.28f : 0.f;
    out[i].min = vec3(side > 0 ? 0 : -9, -21, 0); out[i].max = vec3(side > 0 ? 9 : 0, 0, 1.2f);
  }
  return out;
}

} // namespace dq
