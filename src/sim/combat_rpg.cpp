// sim/combat_rpg.cpp — the combat batch's rules. Bars and animations read the fields; they are not drawn here.
#include "sim/combat_rpg.h"
#include "sim/shield_shape.h"
#include "sim/skills.h"
#include "sim/gameplay.h"
#include "core/registry.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace dq {

namespace {
static double rnd() { static thread_local std::mt19937 g(std::random_device{}()); return std::uniform_real_distribution<double>(0, 1)(g); }
constexpr float STAMINA_MAX = 100;
constexpr float STAMINA_REGEN = 28;
constexpr float STAMINA_DELAY = 0.45f;
constexpr float DODGE_COST = 30;
constexpr float DODGE_TIME = 0.40f;
constexpr float BLOCK_COST = 14;
constexpr float PARRY_WINDOW = 0.18f;
constexpr float PARRY_COST = 18;
constexpr float BLOCK_KEEP = 0.35f;
constexpr int STAT_CAP = 15;
constexpr float SECOND_CHANCE_CD = 180;
constexpr float BOSS_CAP_FRAC = 0.08f;

bool holdingShield(const Player& p) {
  auto is = [](const ItemStack& s) { return !s.empty() && (s.id == "shield" || (reg().item(s.id) && reg().item(s.id)->has("shield"))); };
  return is(p.inv[p.selected]) || (!p.offhand.empty() && is(p.offhand[0]));
}
const json& mobDef(const std::string& type) {
  static const json none = json::object();
  const json& m = reg().mobs;
  return m.is_object() && m.contains(type) ? m[type] : none;
}
void spend(Player& p, float n) {
  p.stamina = std::max(0.f, p.stamina - n);
  p.data["stamIdle"] = 0;
}
}

void combatRpgTick(Level& lv, Player& p, float dt) {
  if (p.dead || dt <= 0) return;
  p.maxStamina = STAMINA_MAX + p.statCon * 2.f;
  if (p.stamina > p.maxStamina) p.stamina = p.maxStamina;
  float idle = p.data.value("stamIdle", 0.0f) + dt;
  if (idle >= STAMINA_DELAY) p.stamina = std::min(p.maxStamina, p.stamina + STAMINA_REGEN * dt);
  p.data["stamIdle"] = idle;
  if (p.dodgeT > 0) p.dodgeT = std::max(0.f, p.dodgeT - dt);
  if (p.parryFlash > 0) p.parryFlash = std::max(0.f, p.parryFlash - dt);
  if (p.guardFlash > 0) p.guardFlash = std::max(0.f, p.guardFlash - dt);
  if (p.staggerT > 0) p.staggerT = std::max(0.f, p.staggerT - dt);
  if (p.posture > 0 && p.staggerT <= 0) p.posture = std::max(0.f, p.posture - 8.f * dt);
  if (p.secondChanceCd > 0) p.secondChanceCd = std::max(0.f, p.secondChanceCd - dt);
  const int earned = std::max(0, totalLevel(p) / 5);
  const int spent = p.statCon + p.statStr + p.statAgi + p.statInt + p.statLck + p.statFoc;
  p.statPoints = std::max(0, earned - spent);
  float scan = p.data.value("lvlScan", 0.0f) + dt;
  if (scan >= 2.f) {
    scan = 0;
    const int lvl = std::max(1, totalLevel(p) / 8);
    for (auto& up : lv.entities()) {
      Entity* e = up.get();
      if (!e || e->isPlayer() || e->removed || e->realm != p.realm) continue;
      if (std::hypot(e->pos.x - p.pos.x, e->pos.z - p.pos.z) > 48) continue;
      e->maxPosture = 40.f + std::max(1, e->mobLevel) * 2.f;
      if (e->mobLevel >= lvl && e->data.value("lvlHp", false)) continue;
      const int was = std::max(1, e->mobLevel);
      e->mobLevel = std::max(e->mobLevel, lvl);
      e->maxPosture = 40.f + e->mobLevel * 2.f;
      if (!e->data.value("lvlHp", false)) {
        const float k = 1.f + 0.03f * (e->mobLevel - 1);
        e->maxHp *= k; e->hp = std::min(e->maxHp, e->hp * k);
        e->data["lvlHp"] = true;
      } else if (e->mobLevel > was) {
        const float k = (1.f + 0.03f * (e->mobLevel - 1)) / (1.f + 0.03f * (was - 1));
        e->maxHp *= k; e->hp = std::min(e->maxHp, e->hp * k);
      }
    }
  }
  p.data["lvlScan"] = scan;
}

void combatRpgInput(Level& lv, Player& p, const PlayerInput& in, float dt) {
  if (p.dead) return;
  if (in.dodge && p.dodgeT <= 0 && p.stamina >= DODGE_COST && !p.hang && !p.climb && !p.riding) {
    spend(p, DODGE_COST);
    p.dodgeT = DODGE_TIME;
    const float s = std::sin(p.yaw), co = std::cos(p.yaw);
    const float fx = -s, fz = -co, rx = co, rz = -s;
    float tx = rx * in.mx - fx * in.mz, tz = rz * in.mx - fz * in.mz;
    const float len = std::hypot(tx, tz);
    if (len < 0.2f) { tx = -fx; tz = -fz; }   // standing still: back away
    else { tx /= len; tz /= len; }
    p.dodgeYaw = std::atan2(-tx, -tz);        // the heading whose front is that way
    sfxUi(lv, p, "step");
  }
  const bool block = holdingShield(p) && in.use && p.stamina > 0 && p.dodgeT <= 0;
  if (block) {
    if (p.blockT <= 0) p.blockT = 0.001f;
    else p.blockT += dt;
    spend(p, BLOCK_COST * dt);
    if (p.stamina <= 0) p.blockT = 0;
  } else p.blockT = 0;
}

float combatRpgIncoming(Level& lv, Player& p, float amount, DamageSource& src) {
  if (amount <= 0 || src.type == "void" || src.bypass) return amount;
  if (p.dodgeT > 0) return 0;
  if (p.blockT > 0 && p.blockT < PARRY_WINDOW && p.stamina >= PARRY_COST && src.attacker) {
    spend(p, PARRY_COST);
    p.parryFlash = 0.35f;
    p.blockT = PARRY_WINDOW;
    if (Entity* a = lv.entity(src.attacker)) {
      a->staggerT = std::max(a->staggerT, 0.8f);
      a->posture = a->maxPosture;
    }
    toast(lv, p, "Parry", "good");
    sfxUi(lv, p, "crit");
    return 0;
  }
  if (p.blockT > 0) { p.guardFlash = 0.35f; return amount * BLOCK_KEEP; }
  return amount;
}

bool combatRpgSecondChance(Level& lv, Player& p) {
  if (p.secondChanceCd > 0) return false;
  p.secondChanceCd = SECOND_CHANCE_CD;
  p.hp = 1;
  p.dead = false;
  p.invuln = std::max(p.invuln, 1.5f);
  toast(lv, p, "You stay standing, on half a heart", "epic");
  sfxUi(lv, p, "levelup");
  return true;
}

float combatRpgOutgoing(Level& lv, Player& p, Entity& e, float dmg, bool& finishing) {
  finishing = e.staggerT > 0;
  if (finishing) dmg = std::ceil(dmg * 1.75f);
  dmg *= 1.f + p.statStr * 0.01f;
  if (p.statLck > 0 && rnd() < p.statLck * 0.004) dmg = std::ceil(dmg * 1.5f);
  e.maxPosture = 40.f + e.mobLevel * 2.f;
  e.posture = std::min(e.maxPosture, e.posture + dmg * (2.f + p.statFoc * 0.02f));
  if (e.posture >= e.maxPosture && e.staggerT <= 0) {
    e.staggerT = 1.2f;
    e.posture = 0;
  }
  (void)lv;
  return dmg;
}

float combatRpgBossCap(Level& lv, Entity& e, float dmg) {
  if (!mobDef(e.type).value("boss", false) && !e.data.value("boss", false)) return dmg;
  const double now = gameClock(lv);
  const double until = e.data.value("bossDmgAt", 0.0);
  const float taken = now < until ? e.data.value("bossDmg", 0.0f) : 0.f;
  const float cap = std::max(4.f, e.maxHp * BOSS_CAP_FRAC);
  const float dealt = std::min(dmg, std::max(0.f, cap - taken));
  e.data["bossDmg"] = taken + dealt;
  e.data["bossDmgAt"] = now + 1.0;
  return dealt;
}

float combatRpgMobScale(const Entity& attacker, float amount) {
  if (attacker.mobLevel <= 1) return amount;
  return amount * (1.f + 0.03f * (attacker.mobLevel - 1));
}

bool arrowStrikeShield(Level& lv, Entity& arrow, const vec3& from, const vec3& to) {
  const vec3 step = to - from;
  if (glm::dot(step, step) < 1e-8f) return false;
  const u32 owner = arrow.data.value("owner", 0u);
  Player* hit = nullptr;
  float best = 2.f;
  for (auto& up : lv.entities()) {
    auto* p = dynamic_cast<Player*>(up.get());
    if (!p || p->removed || p->dead || p->realm != arrow.realm || p->id == owner) continue;
    ShieldBox box;
    if (!playerShieldBox(*p, box)) continue;
    float t = 0;
    if (!shieldRayHit(box, from, step, 1.f, &t) || t >= best) continue;
    best = t; hit = p;
  }
  if (!hit) return false;
  const vec3 at = from + step * best;
  const bool parry = hit->blockT > 0 && hit->blockT < PARRY_WINDOW && hit->stamina >= PARRY_COST;
  if (parry) {
    hit->stamina = std::max(0.f, hit->stamina - PARRY_COST);
    hit->data["stamIdle"] = 0;
    hit->parryFlash = 0.35f;
    hit->blockT = PARRY_WINDOW;
    vec3 back = -arrow.vel;
    float sp = glm::length(back);
    if (sp < 0.5f) back = -step;
    sp = glm::length(back);
    if (sp > 1e-4f) back /= sp;
    arrow.pos = at + back * 0.2f;
    arrow.vel = back * std::max(8.f, glm::length(arrow.vel) * 0.85f);
    arrow.data["owner"] = hit->id;
    toast(lv, *hit, "Parry", "good");
    sfxUi(lv, *hit, "crit");
    sfxAt(lv, "arrow_hit", at);
    return true;
  }
  if (hit->blockT > 0) {
    hit->stamina = std::max(0.f, hit->stamina - 8.f);
    hit->data["stamIdle"] = 0;
  }
  hit->guardFlash = 0.35f;
  const std::string kind = arrow.data.value("item", std::string("arrow"));
  dropItem(lv, arrow.realm, ItemStack(kind.empty() ? "arrow" : kind, 1), at, false);
  sfxAt(lv, "arrow_hit", at);
  arrow.removed = true;
  return true;
}

} // namespace dq
