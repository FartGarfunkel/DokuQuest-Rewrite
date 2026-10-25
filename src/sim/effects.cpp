// sim/effects.cpp — game/brewing.js "what the effects do", arcana.js (clarity, fortune, haste, warding),
// monsters.js (venom ticks), rpg.js (Steeped), paths.js (Iron Will keeps slowness off).
#include "sim/effects.h"
#include "sim/magic.h"
#include "sim/magic_fx.h"
#include "sim/rpg.h"

namespace dq {

static json& fxJ(Player& p) { json& f = p.stats["fx"]; if (!f.is_object()) f = json::object(); return f; }
static const json& fxJc(const Player& p) { static const json E = json::object(); return p.stats.contains("fx") && p.stats["fx"].is_object() ? p.stats["fx"] : E; }

float effectTime(const Player& p, const std::string& eff) {
  const json& f = fxJc(p);
  auto it = f.find(eff);
  return it != f.end() && it->is_array() && !it->empty() ? (*it)[0].get<float>() : 0.f;
}
int effectLevel(const Player& p, const std::string& eff) {
  const json& f = fxJc(p);
  auto it = f.find(eff);
  return it != f.end() && it->is_array() && it->size() > 1 ? (*it)[1].get<int>() : 0;
}
std::vector<ActiveEffect> effectsOn(const Player& p) {
  std::vector<ActiveEffect> out;
  for (auto& k : magicData().effectOrder) { float t = effectTime(p, k); if (t > 0) out.push_back({k, t, effectLevel(p, k)}); }
  return out;
}
void clearEffects(Player& p) { p.stats["fx"] = json::object(); }

void giveEffect(Level& lv, Player& p, const std::string& eff, float secs, int level, Entity* src) {
  if (p.dead) return;
  const MagicData& md = magicData();
  const EffectDef* E = md.effect(eff);
  // rpg.js Steeped: the good ones last a fifth longer
  if (eff != "heal" && eff != "mana" && eff != "harm" && !(E && E->bad) && hasPerk(p, "alchemy", 30)) secs *= 1.2f;
  // paths.js Iron Will: slowness can't touch you
  if (eff == "slow" && hasTal(p, "ironwill")) return;
  if (eff == "heal") { combatHeal(lv, p, secs); return; }
  if (eff == "mana") { MagicState& m = magicState(lv, p); float mx = manaMax(p); m.mana = std::min(mx, (m.mana < 0 ? mx : m.mana) + secs); return; }
  if (eff == "harm") { combatHurt(lv, p, secs, json{{"type", "magic"}, {"bypassArmor", true}, {"knock", 0}}, src && src->isPlayer() ? static_cast<Player*>(src) : nullptr); return; }
  if (!E) return;
  float cur = effectTime(p, eff); int curLv = effectLevel(p, eff);
  if (cur > secs && curLv >= level) return;
  fxJ(p)[eff] = json::array({std::max(secs, curLv > level ? 0.f : cur), level});
  if (eff == "poison") { MagicState& m = magicState(lv, p); m.poison = std::max(m.poison, secs); }
  if (eff == "warding") { MagicState& m = magicState(lv, p); m.ward = std::max(m.ward, 6.f * (level + 1)); m.wardGot = true; }
}

void tickEffects(Level& lv, Player& p, double dt) {
  MagicState& m = magicState(lv, p);
  if (p.dead) { clearEffects(p); m.poison = 0; return; }
  json& f = fxJ(p);
  std::vector<std::string> gone;
  for (auto it = f.begin(); it != f.end(); ++it) {
    if (!it->is_array() || it->empty()) { gone.push_back(it.key()); continue; }
    float t = (*it)[0].get<float>() - (float)dt;
    if (t <= 0) gone.push_back(it.key()); else (*it)[0] = t;
  }
  for (auto& k : gone) f.erase(k);
  if (hasEffect(p, "regen")) {
    m.regT += (float)dt;
    float every = effectLevel(p, "regen") ? 1.2f : 2.5f;
    if (m.regT >= every) { m.regT = 0; combatHeal(lv, p, 1); }
  }
  float fp = effectTime(p, "poison");
  if (fp > 0 && !(m.poison > 0.5f)) m.poison = fp;
  if (m.poison > 0) {   // monsters.js: venom wears you down, but never past the last half heart
    m.poison -= (float)dt; m.poisonT += (float)dt;
    if (m.poisonT >= 1.25f) { m.poisonT = 0; if (p.hp > 1) combatHurt(lv, p, 1, json{{"type", "poison"}, {"bypassArmor", true}}); }
  }
  if (hasEffect(p, "slowfall") && !p.onGround && p.vel.y < 0 && !p.flying) {
    m.slowFallT = std::max(m.slowFallT, 0.25f);
    if (p.vel.y < -2.2f) p.vel.y += (-2.2f - p.vel.y) * std::min(1.f, (float)dt * 8);
  }
  // fire resistance: burning goes out at once (the gameplay's burn timer lives in data.burnT)
  if (hasEffect(p, "fireres") && mobStatus(p, "burnT") > 0) p.data["burnT"] = std::min(mobStatus(p, "burnT"), 0.3f);
  if (hasEffect(p, "water")) p.air = p.maxAir;
  // Warding: the ward comes up once per potion (arcana.js)
  if (hasEffect(p, "warding")) { if (!m.wardGot) { m.wardGot = true; m.ward = std::max(m.ward, 6.f * (effectLevel(p, "warding") + 1)); } }
  else if (m.wardGot) m.wardGot = false;
}

void mobEffect(Level& lv, Entity& e, const std::string& eff, float secs, int level, Player* src) {
  if (!isAlive(e)) return;
  bool undead = isUndead(e);
  if (eff == "heal" || eff == "harm") {
    bool hurts = (eff == "harm") != undead;
    if (hurts) combatHurt(lv, e, secs, json{{"type", "magic"}, {"knock", 0}}, src);
    else combatHeal(lv, e, secs);
    return;
  }
  if (eff == "poison" && !undead) setMobStatus(lv, e, "poisonT", secs, src);
  if (eff == "slow") setMobStatus(lv, e, "slowT", secs, src);
  if (eff == "regen" || eff == "resist" || eff == "strength") combatHeal(lv, e, 2);
}

float effectSpeedFactor(const Player& p) {
  float k = 1;
  if (hasEffect(p, "speed")) k *= 1 + 0.2f * (effectLevel(p, "speed") + 1);
  if (hasEffect(p, "slow")) k *= std::max(0.1f, 1 - 0.15f * (effectLevel(p, "slow") + 1));
  return k;
}
float effectJumpFactor(const Player& p) { return hasEffect(p, "jump") ? 1 + 0.2f * (effectLevel(p, "jump") + 1) : 1.f; }
float effectDamageFactor(const Player& p) { return (hasEffect(p, "strength") ? 1 + 0.2f * (effectLevel(p, "strength") + 1) : 1.f) * (hasEffect(p, "weak") ? 0.7f : 1.f); }
float effectBreakDivisor(const Player& p) { return hasEffect(p, "haste") ? 1.25f + 0.2f * effectLevel(p, "haste") : 1.f; }
float effectNightVision(const Player& p) { float t = effectTime(p, "night"); return t <= 0 ? 0 : t > 10 ? 1 : (std::sin(t * 8) > 0 ? 1 : 0.4f); }
bool effectBreathes(const Player& p) { return hasEffect(p, "water"); }
bool hiddenFrom(const Player& p, float dist, bool hurt) { return hasEffect(p, "invis") && dist > 3.5f && !hurt; }

std::string fmtTime(float s) { int n = std::max(0, (int)std::ceil(s)); return strFormat("%d:%02d", n / 60, n % 60); }
std::string fxRoman(int lv) {
  const auto& R = magicData().roman;
  if (!lv) return "";
  return " " + (lv + 1 < (int)R.size() ? R[lv + 1] : std::to_string(lv + 1));
}

} // namespace dq
