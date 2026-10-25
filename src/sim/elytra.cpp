// sim/elytra.cpp — gliding on elytra, boosts, crashes (game/elytra.js; firework boost from game/batche.js).
#include "sim/elytra.h"
#include "sim/physics.h"

namespace dq {

bool hasElytra(const Player& p) {
  if (auto& cs = contentHooks().capeSlot) { const ItemStack* c = cs(p); return c && !c->empty() && c->id == "elytra"; }
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
  vec3 l = lookDir(p);
  float sp = glm::length(p.vel);
  sp += -l.y * 17 * dt;                           // diving gathers speed, climbing spends it
  sp -= sp * 0.05f * dt + 0.5f * dt;              // and the air takes a little
  if (p.glideBoost > 0) { p.glideBoost -= dt; sp += 26 * dt; }
  sp = clampv(sp, 0.f, p.glideBoost > 0 ? 46.f : 40.f);
  float k = std::min(1.f, dt * 2.4f);
  p.vel += (l * sp - p.vel) * k;
  if (sp < 8) p.vel.y -= (8 - sp) * 2.2f * dt + 3 * dt;   // too slow to stay up: it drops
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
  if (d->has("gustPuff")) { useUpHeld(p); p.glideBoost = 1.8f; sfxUi(u.lv, p, "wind_charge"); return true; }
  if (d->has("firework")) { useUpHeld(p); p.glideBoost = 2.6f; sfxUi(u.lv, p, "firework_launch"); return true; }
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
    out[i].side = side; out[i].ang = side * (0.16f + spread); out[i].tilt = spread * 0.35f;
    out[i].min = vec3(side > 0 ? 0 : -9, -21, 0); out[i].max = vec3(side > 0 ? 9 : 0, 0, 1.2f);
  }
  return out;
}

} // namespace dq
