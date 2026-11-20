// sim/combat_rpg.cpp — the combat batch's rules. Bars and animations read the fields; they are not drawn here.
#include "sim/combat_rpg.h"
#include "sim/skilltree.h"
#include "sim/balance.h"
#include "sim/shield_shape.h"
#include "sim/skills.h"
#include "sim/gameplay.h"
#include "sim/combat.h"
#include "sim/attack_moves.h"
#include "sim/creature.h"
#include "core/registry.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace dq { bool itemHasUse(const ItemDef& d); }   // sim/controller.cpp
namespace dq {

namespace {
static double rnd() { static thread_local std::mt19937 g(std::random_device{}()); return std::uniform_real_distribution<double>(0, 1)(g); }
using namespace bal;   // (the stats', talents' and levels' numbers: sim/balance.h)
constexpr float STAMINA_DELAY = 0.45f;
constexpr float BLOCK_COST = 14;
constexpr float PARRY_WINDOW = 0.18f;
constexpr float PARRY_COST = 18;
constexpr float BLOCK_KEEP = 0.35f;
constexpr float SECOND_CHANCE_CD = 180;
constexpr float BOSS_CAP_FRAC = 0.08f;

bool holdingShield(const Player& p) { return shieldSide(p) != 0; }
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

static bool twoHandWeapon(const Player& p);
static bool canWeaponGuard(const Player& p);

void combatRpgTick(Level& lv, Player& p, float dt) {
  if (p.dead || dt <= 0) return;
  p.maxStamina = (STAMINA_MAX + conStamina(p) + treeAttr(p, "stamina")) * (1 + talent(p, "stamina"));
  if (p.stamina > p.maxStamina) p.stamina = p.maxStamina;
  float idle = p.data.value("stamIdle", 0.0f) + dt;
  if (idle >= STAMINA_DELAY) p.stamina = std::min(p.maxStamina, p.stamina + STAMINA_REGEN * staminaRegenFactor(p) * dt);
  p.data["stamIdle"] = idle;
  if (p.dodgeT > 0) p.dodgeT = std::max(0.f, p.dodgeT - dt);
  if (p.parryFlash > 0) p.parryFlash = std::max(0.f, p.parryFlash - dt);
  if (p.guardFlash > 0) p.guardFlash = std::max(0.f, p.guardFlash - dt);
  if (p.staggerT > 0) p.staggerT = std::max(0.f, p.staggerT - dt);
  if (p.posture > 0 && p.staggerT <= 0) p.posture = std::max(0.f, p.posture - 8.f * dt);
  if (p.secondChanceCd > 0) p.secondChanceCd = std::max(0.f, p.secondChanceCd - dt);
  const int earned = statPointsEarned(p);
  if (p.data.value("conSeen", -1) != p.statCon) { p.data["conSeen"] = p.statCon; p.recalcMax(); }   // (Constitution's health)
  const int spent = p.statCon + p.statStr + p.statAgi + p.statInt + p.statLck + p.statFoc;
  p.statPoints = std::max(0, earned - spent);
  float scan = p.data.value("lvlScan", 0.0f) + dt;
  if (scan >= 2.f) {
    scan = 0;
    if (!lv.isRemote()) for (auto& up : lv.entities()) {   // (the host decides; a guest's copies follow its updates)
      Entity* e = up.get();
      if (!e || e->isPlayer() || e->removed || e->realm != p.realm || e->hp <= 0) continue;
      if (std::hypot(e->pos.x - p.pos.x, e->pos.z - p.pos.z) > 48) continue;
      mobLevelUpdate(lv, *e);
    }
  }
  p.data["lvlScan"] = scan;
}

// ---- levels (an RPG's): a creature takes its level once, when first met: the party near it (between its average and its
// best, so a newcomer's friend doesn't make the world trivial or impossible), give or take a little; one in fourteen hostile
// ones is an elite, three levels up, tougher and richer. Each level: +8% health, +5% damage, +6% experience. With more
// players about, its health grows (40% for each beyond the first, while they stay near) so a party has a fight too.
int playerCombatLevel(const Player& p) { return std::max(1, totalLevel(p) / 8); }
namespace {
float levelHp(int lvl) { return mobLevelHp(lvl); }
float levelDmg(int lvl) { return mobLevelDmg(lvl); }
}
float mobXpScale(const Entity& e) { return (1.f + MOB_XP_PER_LEVEL * (std::max(1, e.mobLevel) - 1)) * (mobIsElite(e.data) ? ELITE_XP : 1.f); }
void mobApplyLevel(Entity& e, int lvl, bool elite, float base) {
  if (elite) e.data["elite"] = true;
  e.mobLevel = std::max(1, lvl);
  e.data["hp0"] = base; e.data["coop"] = 1.f; e.data["lvlSet"] = true; e.data.erase("lvlHp");
  const float k = levelHp(e.mobLevel) * (elite ? ELITE_HP : 1.f);
  const float frac = e.maxHp > 0 ? e.hp / e.maxHp : 1.f;
  e.maxHp = base * k; e.hp = std::max(0.5f, frac * e.maxHp);
  e.maxPosture = (MOB_POSTURE_BASE + mobEffLevel(e.mobLevel) * MOB_POSTURE_PER_LEVEL) * (elite ? ELITE_POSTURE : 1.f);
}
void mobLevelUpdate(Level& lv, Entity& e) {
  if (e.isPlayer() || e.hp <= 0) return;
  if (!petOwnerOf(e).empty()) return;   // (a pet or a summon: yours, not the world's)
  int n = 0, best = 1; float sum = 0;
  for (auto& up : lv.entities()) {
    if (!up->isPlayer() || up->removed || up->realm != e.realm) continue;
    const Player& q = static_cast<const Player&>(*up);
    if (q.dead || q.mode == "spectator" || std::hypot(q.pos.x - e.pos.x, q.pos.z - e.pos.z) > 48) continue;
    const int l = playerCombatLevel(q); n++; sum += l; best = std::max(best, l);
  }
  if (!n) return;
  const json& T = mobDef(e.type);
  const CreatureType* CT = creatureType(e.type);
  const bool hostile = CT ? CT->hostile : T.value("hostile", false), boss = (CT && CT->boss) || T.value("boss", false);
  if (!e.data.value("lvlSet", false)) {
    // its health at level 1 (an older save's creature: its old +3% a level taken back out)
    float base = e.maxHp;
    if (e.data.value("lvlHp", false)) base /= 1.f + 0.03f * (std::max(1, e.mobLevel) - 1);
    const int party = (int)std::lround((sum / n + best) * 0.5f);
    // (no extra for progress here: the party's combat level already grows with the skills; progressTier only scales loot)
    int lvl = std::max(1, party + (int)std::floor(rnd() * 4) - 1);   // (-1 .. +2)
    const bool elite = hostile && !boss && !e.data.value("boss", false) && rnd() < ELITE_CHANCE;
    if (elite) lvl += ELITE_LEVELS;
    if (e.data.value("lvlHp", false)) lvl = std::max(lvl, e.mobLevel);   // (never lower than an older save gave it)
    mobApplyLevel(e, lvl, elite, base);
  }
  // the party near it: its health grows and shrinks with the players about it (kept as a share of the whole)
  const float coop = 1.f + 0.4f * (std::min(n, 6) - 1), was = e.data.value("coop", 1.f);
  if (std::fabs(coop - was) > 1e-3f) { const float frac = e.maxHp > 0 ? e.hp / e.maxHp : 1.f; e.maxHp *= coop / was; e.hp = std::max(0.5f, frac * e.maxHp); e.data["coop"] = coop; }
  e.maxPosture = (MOB_POSTURE_BASE + mobEffLevel(e.mobLevel) * MOB_POSTURE_PER_LEVEL) * (mobIsElite(e.data) ? ELITE_POSTURE : 1.f);
}

void combatRpgInput(Level& lv, Player& p, const PlayerInput& in, float dt) {
  if (p.dead) return;
  const float dodgeCostNow = dodgeCost(p);   // (Agility, load)
  if (in.dodge && p.dodgeT <= 0 && p.stamina >= dodgeCostNow && !p.hang && !p.climb && !p.riding) {
    spend(p, dodgeCostNow);
    p.dodgeT = dodgeTime(p);
    const float s = std::sin(p.yaw), co = std::cos(p.yaw);
    const float fx = -s, fz = -co, rx = co, rz = -s;
    float tx = rx * in.mx - fx * in.mz, tz = rz * in.mx - fz * in.mz;
    const float len = std::hypot(tx, tz);
    if (len < 0.2f) { tx = -fx; tz = -fz; }   // standing still: back away
    else { tx /= len; tz /= len; }
    p.dodgeYaw = std::atan2(-tx, -tz);        // the heading whose front is that way
    sfxUi(lv, p, "step");
  }
  // the shield comes up only when the main hand has nothing of its own to do: a sapling, a block, food, a bow...
  // in the main hand is used, the shield in the other hand waits
  const ItemStack& mh = p.inv[p.selected];
  const ItemDef* md = mh.empty() ? nullptr : reg().item(mh.id);
  const bool mainShield = md && (mh.id == "shield" || md->has("shield"));
  const bool mainBusy = md && !mainShield && itemHasUse(*md);
  const bool twoHand = twoHandWeapon(p);
  const bool shieldGuard = holdingShield(p) && !twoHand;
  const bool block = (shieldGuard || canWeaponGuard(p)) && !mainBusy && in.use && p.stamina > 0 && p.dodgeT <= 0;
  if (block) {
    if (p.blockT <= 0) p.blockT = 0.001f;
    else p.blockT += dt;
    const float cost = shieldGuard ? BLOCK_COST : BLOCK_COST * 1.5f;
    spend(p, cost * (1 - stunResist(p)) * dt);   // (a weapon's guard costs half again; a shield stays as it was)
    if (p.stamina <= 0) p.blockT = 0;
  } else p.blockT = 0;
}

float combatRpgIncoming(Level& lv, Player& p, float amount, DamageSource& src) {
  if (amount <= 0 || src.type == "void" || src.bypass) return amount;
  if (p.dodgeT > 0) return 0;
  if (p.blockT > 0 && p.blockT < guardWindow(p) && p.stamina >= PARRY_COST && src.attacker) {
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
  // a talent's chance to slip a creature's or player's blow outright
  if (src.attacker && src.type != "magic" && dodgeChance(p) > 0 && rnd() < dodgeChance(p)) { toast(lv, p, "Evaded", "good"); return 0; }
  if (p.blockT > 0) { p.guardFlash = 0.35f; amount *= guardKeep(p); }
  return amount * (1 - conReduction(p)) * damageTakenFactor(p);   // Constitution: toughness; the trees' damage reduction
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
  dmg *= strDamageFactor(p);
  dmg = (dmg + treeAttr(p, "damage")) * (1 + talent(p, "damage"));   // (the skill tree)
  dmg *= rageFactor(p);                                                // (the Berserker's rage: more as your health runs out)
  const float crit = critChance(p) + meleeCritBonus(p);   // (Keen Edge, on top of the stats' cap: sim/skills.h)
  if (crit > 0 && rnd() < crit) dmg = std::ceil(dmg * critMultiplier(p));
  e.maxPosture = (MOB_POSTURE_BASE + mobEffLevel(e.mobLevel) * MOB_POSTURE_PER_LEVEL) * (mobIsElite(e.data) ? ELITE_POSTURE : 1.f);
  e.posture = std::min(e.maxPosture, e.posture + dmg * POSTURE_PER_DMG * postureFactor(p));
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
  if (attacker.isPlayer()) return amount;
  return amount * levelDmg(attacker.mobLevel) * (mobIsElite(attacker.data) ? ELITE_DMG : 1.f);
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
  const bool parry = hit->blockT > 0 && hit->blockT < guardWindow(*hit) && hit->stamina >= PARRY_COST;
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
  dropItem(lv, arrow.realm, ItemStack(kind.empty() ? "arrow" : kind, 1), at, false, 0.f);
  sfxAt(lv, "arrow_hit", at);
  arrow.removed = true;
  return true;
}

static bool twoHandWeapon(const Player& p) {
  const ItemStack& h = p.inv[p.selected];
  const ItemDef* d = h.empty() ? nullptr : h.def();
  return d && d->num("hands") >= 2.f;
}
static bool canWeaponGuard(const Player& p) {
  const ItemStack& h = p.inv[p.selected];
  if (h.empty() || !h.def()) return false;
  switch (movesetOf(h.def())) {
    case Moveset::Sword: case Moveset::Dagger: case Moveset::Axe:
    case Moveset::Greatsword: case Moveset::Spear: case Moveset::Mace: return true;
    default: return false;
  }
}
float guardKeep(const Player& p) {
  if (holdingShield(p) && !twoHandWeapon(p)) return BLOCK_KEEP;
  if (twoHandWeapon(p)) return BLOCK_KEEP + 0.25f * (1.f - BLOCK_KEEP);
  return BLOCK_KEEP + 0.5f * (1.f - BLOCK_KEEP);
}
float guardWindow(const Player& p) {
  if (holdingShield(p) && !twoHandWeapon(p)) return PARRY_WINDOW;
  if (twoHandWeapon(p)) return PARRY_WINDOW * 0.8f;
  return PARRY_WINDOW * 0.6f;
}

int shieldSide(const Player& p) {
  auto is = [](const ItemStack& s) { return !s.empty() && (s.id == "shield" || (reg().item(s.id) && reg().item(s.id)->has("shield"))); };
  if (is(p.inv[p.selected])) return 1;
  if (!p.offhand.empty() && is(p.offhand[0])) return -1;
  if (p.shield.empty() || !is(p.shield[0])) return 0;
  // the shield slot: on the forearm of the hand that holds no weapon (a torch in it is fine); both hands armed: slung
  const bool mainW = isHandWeapon(p.inv[p.selected]), offW = !p.offhand.empty() && isHandWeapon(p.offhand[0]);
  if (mainW && offW) return 0;
  return offW ? 1 : -1;
}
} // namespace dq
