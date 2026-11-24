// sim/combat.cpp — damage, death and respawn (local.js hurt/die/respawn, player.js hurt/wearArmor,
// entities.js Mob.hurt/die, fairplay.js raw-blow floor).
#include "core/tool_parts.h"
#include "sim/rpg.h"
#include "sim/item_uses.h"
#include "sim/skilltree.h"
#include "sim/talent_fx.h"
#include "sim/balance.h"
#include "sim/skills.h"
#include "sim/combat.h"
#include "sim/mob_carry.h"
#include "sim/combat_rpg.h"
#include "sim/attack_moves.h"
#include "sim/shield_shape.h"
#include "sim/weapon_quality.h"
#include "core/tool_parts.h"
#include "sim/magic.h"
#include "sim/gameplay.h"
#include "sim/rules.h"
#include "sim/corpse.h"
#include "sim/physics.h"
#include "gen/generator.h"
#include <algorithm>
#include <random>

namespace dq {
std::string creatureLabelOf(Entity& e);   // sim/creature.cpp (creature.h clashes with gameplay.h here)
bool creatureUnaware(const Entity& e, const Entity& p);   // sim/creature_monsters.cpp: a living creature that hasn't caught on to p
float pathMeleeFactor(const Player& p); bool pathCheatDeath(Level& lv, Player& p);   // sim/villager_quests.cpp (the paths' talents)

bool creatureTakesHit(Level& lv, Entity& e, float amount, const DamageSource& src, float& dealt);   // sim/mob_combat.cpp

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

static std::vector<std::pair<std::string, HurtHandler>>& hurtHandlers() { static std::vector<std::pair<std::string, HurtHandler>> v; return v; }
void registerHurtHandler(const std::string& t, HurtHandler h) { hurtHandlers().insert(hurtHandlers().begin(), {t, std::move(h)}); }
static const HurtHandler* handlerFor(const std::string& type) {
  for (auto& [k, f] : hurtHandlers()) if (k == type) return &f;
  for (auto& [k, f] : hurtHandlers()) if (!k.empty() && k.back() == '*' && startsWith(type, k.substr(0, k.size() - 1))) return &f;
  for (auto& [k, f] : hurtHandlers()) if (k.empty()) return &f;
  return nullptr;
}
static const json& mobType(const std::string& type) { static const json none = json::object(); const json& m = reg().mobs; return m.is_object() && m.contains(type) ? m[type] : none; }

void knockback(Entity& e, vec3 from, float k, float up) {
  float kx = e.pos.x - from.x, kz = e.pos.z - from.z, l = std::hypot(kx, kz);
  if (l == 0) l = 1;
  if (e.isPlayer()) k *= 1 - knockResist(static_cast<const Player&>(e));   // (knockback-resistance talents)
  e.vel.x += kx / l * k; e.vel.z += kz / l * k;
  if (up > 0) e.vel.y = std::max(e.vel.y, up);
}

const ToolInfo* heldTool(const Player& p) {
  const ItemStack& h = p.inv[p.selected];
  if (h.empty()) return nullptr;
  auto* d = reg().item(h.id);
  if (!d || !d->hasTool) return nullptr;
  if (!h.tag.is_object()) return &d->tool;
  thread_local ToolInfo mixed; mixed = toolOf(h);   // a mixed tool's own stats, a gem's (core/tool_parts.h)
  if (isPartTool(h)) {
    const float q = gearQualityFactor(h);
    mixed.speed *= q; mixed.damage *= q;
  }
  return &mixed;
}
std::string heldToolElement(const Player& p) {
  const ItemStack& h = p.inv[p.selected];
  auto* d = h.empty() ? nullptr : reg().item(h.id);
  if (isPartTool(h) && h.tag["pt"].contains("el")) return h.tag["pt"].value("el", std::string());
  if (!h.empty() && h.tag.is_object() && h.tag.contains("gem")) { const std::string g = h.tag.value("gem", std::string()); if (g == "ruby") return "fire"; if (g == "sapphire") return "frost"; }
  if (!d || !d->props.contains("tool") || !d->props["tool"].is_object()) return "";
  return d->props["tool"].value("element", std::string());
}
bool wearHeld(Level& lv, Player& p, int n) {
  if (!n || p.creative()) return false;
  ItemStack& s = p.inv[p.selected];
  if (s.empty()) return false;
  auto* d = reg().item(s.id);
  const int max = toolMaxDurability(s);
  if (!d || !d->hasTool || max <= 0) return false;
  s.d += n;
  if (s.d >= max) { toast(lv, p, "Your " + toLower(stackName(s)) + " broke", "warn"); sfxUi(lv, p, "break_tool"); s.clear(); return true; }
  return false;
}
void wearArmor(Player& p) {
  if (armourSavesWear(p)) return;   // Unbreakable: half the blows leave it be (sim/skills.h)
  for (int i = 0; i < 4; i++) {
    ItemStack& s = p.armor[i];
    if (s.empty()) continue;
    auto* d = reg().item(s.id);
    if (!d || !d->props.contains("armor")) continue;
    s.d++;
    const int max = toolMaxDurability(s);
    if (s.d >= (max > 0 ? max : 1 << 30)) { p.brokeSomething = stackName(s); s.clear(); }
  }
}

// Player.hurt: armour and fire resistance, then the invulnerable moment
static float playerHurtRaw(Player& p, float amount, const DamageSource& src) {
  if (p.dead || amount <= 0) return 0;
  if (p.invuln > 0 && !src.bypass) return 0;
  float dmg = amount;
  if (!src.bypassArmor) {
    auto slotPts = [&](int slot) -> float {
      if (slot < 0 || slot >= (int)p.armor.size() || p.armor[slot].empty()) return 0.f;
      return armorPointsOf(p.armor[slot]) * gearQualityFactor(p.armor[slot]);
    };
    float pts = armorPoints(p);
    if (src.armorSlot >= 0 && src.armorSlot < 4) {
      const float struck = slotPts(src.armorSlot) + (src.armorSlot == 2 ? slotPts(3) * 0.5f : 0.f);
      pts = struck * 1.8f + pts * 0.35f;
    }
    pts = (pts + treeAttr(p, "armor")) * (1 + treeAttrPct(p, "armor"));
    dmg *= 1 - std::min(0.8f, pts * (1 - src.pierce) * 0.032f);
  }
  if (src.type == "magic") dmg *= 1 - std::min(0.6f, treeAttrPct(p, "spell_resist") + treeAttr(p, "spell_resist"));
  if (src.raw > 0 && !src.bypassArmor) dmg = std::max(dmg, src.raw * 0.2f);
  if (src.element == "fire" || src.type == "lava" || src.type == "burn") dmg *= 1 - fireResist(p);
  dmg *= gearHarmFactor(p, src);   // a dragon set's bonus, a cape (sim/item_uses.h)
  dmg = std::max(0.5f, std::round(dmg * 2) / 2);
  p.hp = std::max(0.f, p.hp - dmg);
  p.invuln = src.type == "burn" || src.type == "lava" ? 0.45f : 0.6f;
  p.hurtFlash = 1; p.hurtTilt = (rnd() < 0.5 ? -1 : 1) * 0.12f;
  if (!src.bypassArmor) wearArmor(p);
  return dmg;
}

float damagePlayer(Level& lv, Player& p, float amount, DamageSource src) {
  if (p.dead) return 0;
  if (p.creative() && src.type != "void") return 0;
  if (p.mode == "spectator") return 0;   // only watching
  if ((src.type == "fall" && !rule(lv, "fallDamage")) || (src.type == "drown" && !rule(lv, "drowning")) ||
      ((src.type == "lava" || src.type == "burn") && !rule(lv, "fireDamage"))) return 0;
  src.raw = amount;
  if (Entity* a = src.attacker ? lv.entity(src.attacker) : nullptr) amount = combatRpgMobScale(*a, amount);
  amount = combatRpgIncoming(lv, p, amount, src);
  p.lastHurtT = gameClock(lv);
  if (p.sleeping) p.sleeping = false;
  // the ward, stoneskin, potions, Protection/Feather Falling/Fire Protection, the Defence skill and worn-in armour
  // (sim/magic.cpp): every harm goes through them, not only a spell's
  json mj = {{"type", src.type}, {"element", src.element}, {"bypass", src.bypass}, {"bypassArmor", src.bypassArmor}, {"knock", src.knock < 0 ? 5.f : src.knock}};
  if (src.attacker) mj["attacker"] = src.attacker;
  if (amount > 0) amount = magicIncomingDamage(lv, p, amount, mj);
  if (amount <= 0) return 0;
  if (mj.contains("knock") && mj["knock"].is_number()) src.knock = mj["knock"].get<float>();
  float dealt = playerHurtRaw(p, amount, src);
  if (!dealt) return 0;
  if (dealt >= 4.f && src.attacker && src.type != "magic" && src.type != "fall" && src.type != "drown") {
    if (Entity* a = lv.entity(src.attacker)) {
      const vec3 to = a->pos - p.pos;
      const float side = to.x * std::cos(p.yaw) - to.z * std::sin(p.yaw);
      const bool leg = a->pos.y + a->h * 0.35f < p.pos.y + 0.9f;
      auto slotPts = [&](int slot) {
        if (slot < 0 || slot >= (int)p.armor.size() || p.armor[slot].empty()) return 0.f;
        return armorPointsOf(p.armor[slot]) * gearQualityFactor(p.armor[slot]);
      };
      const float worn = leg ? slotPts(2) + slotPts(3) * 0.5f : slotPts(1);
      const float dur = 14.f * (1.f - std::min(0.65f, worn * 0.03f));
      if (leg) p.data["hurtLeg"] = side >= 0 ? 1 : -1;
      else p.data["hurtArm"] = side >= 0 ? 1 : -1;
      p.data["hurtLimbT"] = std::max(p.data.value("hurtLimbT", 0.f), dur);
    }
  }
  afterPlayerHurt(lv, p, dealt, mj);
  gearOnHurt(lv, p, src, dealt);   // a dragon set strikes back (sim/item_uses.h)
  if (dealt >= 5) DQ_LOG("%s took %.1f (%s)", p.name.c_str(), dealt, src.type.c_str());   // big hits, for playtests and the bridge's log
  sfxUi(lv, p, "hurt");
  if (lv.events) lv.events->emit("rumble", json::array({p.name, std::min(1.f, 0.25f + dealt * 0.08f), 140}));
  if (src.attacker && src.knock != 0) if (Entity* a = lv.entity(src.attacker)) knockback(p, a->pos, src.knock < 0 ? 5.f : src.knock, 4);
  if (src.element == "fire" && src.type != "burn") p.burn = std::max(p.burn, 2.f);
  // (the void and /kill are sure: no second chance, no cheating death there)
  if (p.hp <= 0 && (src.type == "void" || (!pathCheatDeath(lv, p) && !combatRpgSecondChance(lv, p)))) playerDie(lv, p, src);
  return dealt;
}

void mobDeath(Level& lv, Entity& m, const DamageSource& src) {
  const json& T = mobType(m.type);
  bool baby = m.data.value("baby", false);
  Entity* by = src.attacker ? lv.entity(src.attacker) : nullptr;
  const MobLoot L = mobLoot(m.mobLevel, mobIsElite(m.data), by && by->isPlayer() ? static_cast<Player*>(by) : nullptr);   // (its level, an elite, your luck: sim/balance.h)
  const int rolls = L.rolls + (L.twice > 0 && rnd() < L.twice ? 1 : 0);   // (the trees' loot_double: the table once more)
  for (int roll = 0; roll < rolls; roll++)
  if (!baby && T.contains("drops") && T["drops"].is_array()) for (auto& d : T["drops"]) {
    if (!d.is_array() || d.size() < 4) continue;
    if (rnd() < d[3].get<double>() * L.chanceMul) { int a = d[1], b = d[2]; int n = a + (int)(rnd() * (b - a + 1)) + (rnd() < L.extra ? 1 : 0); if (n > 0) dropItem(lv, m.realm, ItemStack(d[0], n), m.pos + vec3(0, 0.5f, 0), true); }
  }
  if (!baby && L.gearChance > 0 && rnd() < L.gearChance) if (ItemStack g = mobGearRoll(L, rnd(), rnd(), rnd()); !g.empty()) dropItem(lv, m.realm, g, m.pos + vec3(0, 0.5f, 0), true);
  if (T.value("woolly", false) && !m.data.value("sheared", false) && !baby) {
    std::string col = m.data.value("color", std::string("white"));
    dropItem(lv, m.realm, ItemStack(col == "white" ? "wool" : col + "_wool", 1), m.pos + vec3(0, 0.5f, 0), true);
  }
  Entity* killer = src.attacker ? lv.entity(src.attacker) : nullptr;
  if (killer && killer->isPlayer()) {
    Player& kp = static_cast<Player&>(*killer);
    vec3 at = m.pos + vec3(0, m.h, 0);
    grantXP(lv, kp, (int)std::lround(T.value("xp", 0) * mobXpScale(m)), &at);   // (its level, an elite: sim/combat_rpg.h)
    kp.stats["mobsKilled"] = kp.stats.value("mobsKilled", 0) + 1;
    dexNote(kp, m.type, true);   // the Compendium (sim/rpg.h)
  }
  m.data["deathTime"] = 0.0001;
  if (lv.events) lv.events->emit("mobDeath", json::array({{{"type", m.type}, {"x", m.pos.x}, {"y", m.pos.y}, {"z", m.pos.z}, {"id", m.id}},
                                                             killer && killer->isPlayer() ? json(static_cast<Player*>(killer)->name) : json()}));
}

// Mob.hurt (the default for creatures without their own handler)
static float mobHurtDefault(Level& lv, Entity& e, float amount, const DamageSource& src) {
  if (e.removed || e.hp <= 0 || e.data.value("invulnT", 0.0f) > 0) return 0;
  const json& T = mobType(e.type);
  float dmg = amount;
  bool fireImmune = T.value("fireImmune", false);
  if (src.element == "fire" && fireImmune && src.weapon.empty()) dmg = 0;
  if (src.element == "frost" && e.type == "cinder_golem") dmg *= 2;
  if (src.type == "lava" && fireImmune) dmg = 0;
  if (dmg <= 0) {
    if (Entity* a = src.attacker ? lv.entity(src.attacker) : nullptr) if (a->isPlayer()) floatText(lv, *static_cast<Player*>(a), e.pos + vec3(0, e.h + 0.3f, 0), "Immune", "#ffb070");
    return 0;
  }
  e.hp -= dmg;
  e.hurtTime = 0.35f;
  if (Entity* a = src.attacker ? lv.entity(src.attacker) : nullptr) {
    float kr = T.value("knockResist", 0.0f);
    float kb = (src.knock < 0 ? 5.f : src.knock) * (1 - kr);
    knockback(e, a->pos, kb, 0);
    if (e.onGround) e.vel.y = std::max(e.vel.y, 4.5f * (1 - kr));
    e.data["panicT"] = 4; e.data["fleeFrom"] = a->id;
  }
  if (src.element == "fire" && !fireImmune) e.data["burn"] = std::max(e.data.value("burn", 0.0f), 3.f);
  if (src.element == "frost") e.data["slowT"] = 3;
  if (e.hp <= 0) { mobDeath(lv, e, src); e.removed = true; burst(lv, e.pos + vec3(0, e.h / 2, 0), "white", 14, 0.6f); }
  return dmg;
}

float damageEntity(Level& lv, Entity& e, float amount, const DamageSource& src) {
  if (e.isPlayer()) return damagePlayer(lv, static_cast<Player&>(e), amount, src);
  amount = combatRpgBossCap(lv, e, amount);
  if (e.type == "corpse") return 0;   // a player's body (sim/corpse.h) is never hurt or destroyed
  float dealt = 0;
  if (creatureTakesHit(lv, e, amount, src, dealt)) return dealt;
  if (auto* h = handlerFor(e.type)) return (*h)(lv, e, amount, src);
  if (e.type == "item" || e.type == "falling_block" || e.type == "falling_tree" || e.type == "seat" || e.type == "tnt" || e.type == "corpse") return 0;
  return mobHurtDefault(lv, e, amount, src);
}

bool creatureIsCursed(const Entity& e);   // sim/mob_combat.cpp: undead, ghouls, werewolves, wraiths

float attackEntity(Level& lv, Player& p, Entity& e) {
  if (p.attackCd > 0) return 0;
  return attackEntity(lv, p, e, 1.f, 1.f, !p.onGround && p.vel.y < 0, true);
}
float attackEntity(Level& lv, Player& p, Entity& e, float dmgMul, float knockMul, bool forceCrit, bool setCd) {
  if (p.swing == 0 || p.swing > 0.6f) p.swing = 0.0001f;
  const ToolInfo* tool = heldTool(p);
  float dmg = (tool ? tool->damage : 1) * dmgMul;
  bool crit = forceCrit;
  if (crit) dmg = std::ceil(dmg * 1.5f);
  // a sneak attack: a creature that hasn't caught on to you (its back to you, after nobody), struck before it turns
  const bool sneakHit = creatureUnaware(e, p);
  if (sneakHit) dmg = std::ceil(dmg * 1.75f);
  DamageSource src; src.attacker = p.id; src.element = heldToolElement(p); src.knock = 5 * knockMul * knockbackFactor(p);
  src.pierce = armorPierce(p);
  if (p.atkMove && p.atkMove < MV_COUNT) src.pierce = std::min(0.55f, src.pierce + attackMove(p.atkMove).pierce);   // a heavy head bites armour; a dagger does not
  if (tool) src.tool = tool->type;
  if (!p.inv[p.selected].empty()) src.weapon = p.inv[p.selected].id;
  dmg = spellbladeHit(lv, p, e, dmg, src.element);   // a spell carried on the blade (sim/magic_hands.cpp)
  dmg *= pathMeleeFactor(p);                          // the Knight's Heavy Blows (sim/villager_quests.cpp)
  dmg *= skillHitFactor(p, "melee", p.inv[p.selected]);   // the Melee skill and a well-used weapon (sim/skills.h)
  if (!isPartTool(p.inv[p.selected])) dmg *= weaponQualityFactor(p.inv[p.selected]);   // a plain sword's tag; a mixed tool already carries it in its stats
  if (p.data.value("sneakHeavy", 0.f) > 1.f) dmg *= p.data.value("sneakHeavy", 1.f);   // the heavy opener, while you are sneaking
  const float hpBefore = e.hp;
  bool finishing = false;
  dmg = combatRpgOutgoing(lv, p, e, dmg, finishing);
  // silver (items.json "holy"): bites the undead and the cursed half as hard again
  if (!p.inv[p.selected].empty() && gearIsHoly(p.inv[p.selected]))   // (a procedural blade of silver too, core/tool_parts.h)
    if (creatureIsCursed(e)) {
      dmg *= 1.5f;
      lv.fx->particles("end_mote", e.pos + vec3(0, e.h * 0.6f, 0), 6);
    }
  src.hasRay = true;
  if (p.atkMove && p.atkMove < MV_COUNT) {
    vec3 h, tp; bladeAt(p.atkMove, std::max(p.atkT, attackMove(p.atkMove).hitFrom), h, tp);
    src.rayO = attackFrameToWorld(h, p.pos, p.atkYaw, p.atkPitch);
    const vec3 tw = attackFrameToWorld(tp, p.pos, p.atkYaw, p.atkPitch);
    const vec3 dd = tw - src.rayO;
    const float L = glm::length(dd);
    src.rayD = L > 1e-4f ? dd / L : lookVector(p.atkYaw, p.atkPitch);
  } else {
    src.rayO = p.pos + vec3(0, p.eye, 0); src.rayD = lookVector(p.yaw, p.pitch);
  }
  if (p.data.contains("hitArmorSlot")) src.armorSlot = p.data.value("hitArmorSlot", -1);
  if (p.data.value("metGuard", false)) src.metGuard = true;
  if (p.data.contains("hitPart")) src.hitPart = p.data.value("hitPart", -1);
  if (p.data.contains("hitRayO") && p.data["hitRayO"].is_array() && p.data["hitRayO"].size() >= 3) {
    src.hasRay = true;
    src.rayO = vec3(p.data["hitRayO"][0].get<float>(), p.data["hitRayO"][1].get<float>(), p.data["hitRayO"][2].get<float>());
    if (p.data.contains("hitRayD") && p.data["hitRayD"].is_array() && p.data["hitRayD"].size() >= 3) {
      const vec3 d(p.data["hitRayD"][0].get<float>(), p.data["hitRayD"][1].get<float>(), p.data["hitRayD"][2].get<float>());
      const float L = glm::length(d);
      if (L > 1e-4f) src.rayD = d / L;
    }
  }
  float dealt;
  if (e.ghost && lv.net && lv.net->isClient()) {   // a joined world: the server's creature; it takes the hit there
    lv.net->hitEntity(p, e.id, dmg, {{"element", src.element}, {"knock", src.knock}, {"tool", src.tool}, {"weapon", src.weapon}, {"crit", crit},
                                     {"ro", {src.rayO.x, src.rayO.y, src.rayO.z}}, {"rd", {src.rayD.x, src.rayD.y, src.rayD.z}}});   // (the part it struck: the host severs that limb)
    dealt = dmg;
  } else dealt = damageEntity(lv, e, dmg, src);
  if (setCd) p.attackCd = (tool && tool->type == "sword" ? 0.45f : 0.6f) / attackSpeedFactor(p);   // (Agility, attack-speed talents)
  if (dealt > 0 && !e.isPlayer() && !e.ghost) onPlayerHitMob(lv, p, e, "melee", dealt, hpBefore);   // (Melee grows with the blows that land)
  if (dealt > 0 && !e.isPlayer() && !e.ghost) talentOnHit(lv, p, e, dealt);   // (the trees: bleeding, poison, slowness, fire, life drawn: sim/talent_fx.h)
  if (dealt > 0 && !e.isPlayer()) {   // the Melee perks (sim/skills.h): Cleave's foe beside it, Bloodthirst's health back
    if (Entity* o = cleaveTarget(lv, p, e)) {
      const float part = dealt * bal::PERK_CLEAVE;
      DamageSource cs; cs.attacker = p.id; cs.element = src.element; cs.knock = src.knock * 0.5f; cs.tool = src.tool; cs.weapon = src.weapon; cs.pierce = src.pierce;
      if (o->ghost && lv.net && lv.net->isClient()) lv.net->hitEntity(p, o->id, part, {{"element", cs.element}, {"knock", cs.knock}, {"tool", cs.tool}, {"weapon", cs.weapon}});   // (a joined world: the host's creature)
      else damageEntity(lv, *o, part, cs);
      lv.fx->particles("crit", o->pos + vec3(0, o->h * 0.6f, 0), 4);
    }
    p.heal(bloodthirstHeal(p, dealt));
  }
  if (dealt > 0) {
    sfxUi(lv, p, crit ? "crit" : "hit");
    if (crit) lv.fx->particles("crit", e.pos + vec3(0, e.h * 0.6f, 0), 10);   // a burst of stars on a critical blow
    if (sneakHit) { lv.fx->particles("crit", e.pos + vec3(0, e.h * 0.7f, 0), 8); floatText(lv, p, e.pos + vec3(0, e.h + 0.55f, 0), "Sneak attack!", "#c8a0ff"); }
    if (finishing) floatText(lv, p, e.pos + vec3(0, e.h + 0.7f, 0), "Finisher", "#ffd0a0");
    char buf[32]; snprintf(buf, sizeof buf, "%s%g", crit ? "\xE2\x9C\xA6" : "", std::round(dealt * 10) / 10);
    floatText(lv, p, e.pos + vec3(0, e.h + 0.2f, 0), buf, src.element == "fire" ? "#ffb070" : src.element == "frost" ? "#9fe8ff" : crit ? "#ffe07a" : "#ffffff");
    if (src.element == "frost") sfxUi(lv, p, "frost");
    wearHeld(lv, p, tool ? (tool->type == "sword" ? 1 : 2) : 0);
    burst(lv, e.pos + vec3(0, e.h * 0.6f, 0), src.element == "fire" ? "fireball" : src.element == "frost" ? "frost_bolt" : "white", crit ? 8 : 4, 0.8f);
  }
  return dealt;
}

bool isHandWeapon(const ItemStack& s) {
  if (s.empty()) return false;
  const ItemDef* d = reg().item(s.id);
  if (!d || !d->hasTool) return false;
  static const char* NOT[] = {"shears", "igniter", "chisel", "brush", "rod"};
  for (const char* n : NOT) if (d->tool.type == n) return false;
  return !(s.id == "shield" || d->has("shield"));
}
static bool dualWield(const Player& p) {
  return isHandWeapon(p.inv[p.selected]) && !p.offhand.empty() && isHandWeapon(p.offhand[0]);
}
bool offHandSwings(const Player& p, bool attackPressed, bool usePressed) {
  if (p.offhand.empty() || !isHandWeapon(p.offhand[0])) return false;
  if (isHandWeapon(p.inv[p.selected])) return false;   // both hands: one attack button chains them; use stays the block
  (void)usePressed;
  return attackPressed && p.inv[p.selected].empty();
}
static void beginSwingOf(Player& p, bool sprint, bool air, bool off);
static void beginSwing(Player& p, bool sprint, bool air) { beginSwingOf(p, sprint, air, false); }
static void beginSwingRaw(Player& p, bool sprint, bool air, int moveN, int nextCombo) {
  const ItemStack& held = p.inv[p.selected];
  const Moveset ms = movesetOf(held.empty() ? nullptr : held.def());
  const int len = std::max(1, comboLength(ms));
  const int n = ((moveN % len) + len) % len;
  u8 id = attackMoveFor(ms, n, sprint && p.onGround, air);
  p.data.erase("sneakHeavy");
  // the extra move: a dagger lunges when you sneak; an axe, mace or greatsword opens with its heavy cut
  if (p.sneaking && p.onGround && !air) {
    if (ms == Moveset::Dagger) id = MV_DAGGER3;
    else if (ms == Moveset::Axe || ms == Moveset::Mace || ms == Moveset::Greatsword) {
      id = attackMoveFor(ms, 1, false, false);
      p.data["sneakHeavy"] = 1.25f;
    }
  }
  if (id == MV_NONE) return;
  p.atkMove = id;
  p.atkT = 0.0001f;
  if (p.data.value("counterT", 0.f) > 0) {   // a parry just opened this: the cut is already out
    p.data["counterT"] = 0;
    p.atkT = std::max(p.atkT, attackMove(id).hitFrom);
  }
  p.atkYaw = p.yaw;
  p.atkPitch = p.pitch;
  p.atkCombo = (u8)(nextCombo & 255);
  p.atkHit.clear();
  p.atkGap = 0;
  p.data.erase("atkQueue");
  p.swing = 0.0001f;
  p.swinging = true;
}
// the off hand's swing: the same cut the main hand just used, with its own weapon (swapped in for the start and each
// blow) and drawn mirrored (render/attack_poses.cpp). Two weapons alternate hands every press, so the off hand is
// every other cut, not one step of the weapon's own chain.
static void beginSwingOf(Player& p, bool sprint, bool air, bool off) {
  const bool dual = dualWield(p);
  const int chain = p.atkGap < COMBO_WINDOW ? (int)p.atkCombo : 0;
  if (off && !p.offhand.empty()) std::swap(p.inv[p.selected], p.offhand[0]);
  const ItemStack& held = p.inv[p.selected];
  const int len = std::max(1, comboLength(movesetOf(held.empty() ? nullptr : held.def())));
  const int moveN = dual ? (chain / 2) % len : ((chain % len) + len) % len;
  const int next = dual ? (chain + 1) & 255 : (moveN + 1) % len;
  beginSwingRaw(p, sprint, air, moveN, next);
  if (off && !p.offhand.empty()) std::swap(p.inv[p.selected], p.offhand[0]);
  p.atkOff = off && p.atkMove != MV_NONE;
}
// a queued cut is the weapon in the hand now. A press stored on a fist must not follow through on a greatsword.
static bool queuedSwingAllowed(Level& lv, Player& p) {
  if (!p.data.value("atkQueue", false)) return false;
  const bool off = p.data.value("atkQueueOff", false);
  const ItemStack& hand = off && !p.offhand.empty() ? p.offhand[0] : p.inv[p.selected];
  if (weaponSkillMet(p, hand)) return true;
  toast(lv, p, "Needs more melee practice.", "info");
  p.data.erase("atkQueue");
  p.data.erase("atkQueueOff");
  return false;
}
void tickMelee(Level& lv, Player& p, float dt, const PlayerInput& in, bool allowStart, bool offStart) {
  const ToolInfo* tool = heldTool(p);
  (void)tool;
  const bool dual = dualWield(p);
  const int step = p.atkGap < COMBO_WINDOW ? (int)p.atkCombo : 0;
  // a held attack button is one press. Without this, the second frame of a click queued the off hand and it
  // played by itself, so the next real click was the main hand again.
  const bool repeat = dual && in.attackPressed && p.data.value("dualHold", false);
  if (dual) { if (in.attackPressed) p.data["dualHold"] = true; else p.data.erase("dualHold"); }
  const bool attack = in.attackPressed && !repeat;
  // both hands armed: attack alternates hands (main, off, main, off). The off hand repeats the main hand's cut, mirrored.
  // Use stays the block.
  const bool offPress = offStart || (dual && attack && (step & 1)) || (!dual && attack && offHandSwings(p, true, false));
  bool started = false;
  if ((attack || offStart) && allowStart) {
    const ItemStack& hand = offPress && !p.offhand.empty() ? p.offhand[0] : p.inv[p.selected];
    if (!weaponSkillMet(p, hand)) toast(lv, p, "Needs more melee practice.", "info");
    else if (p.atkMove != MV_NONE && p.atkT < attackMove(p.atkMove).recovery) { p.data["atkQueue"] = true; p.data["atkQueueOff"] = offPress; }
    else {
      if (p.data.value("counterT", 0.f) > 0) toast(lv, p, "Counter", "good");
      beginSwingOf(p, p.sprinting, !p.onGround, offPress);
      started = true;
    }
  }
  if (p.atkMove == MV_NONE) { p.atkGap += dt; if (queuedSwingAllowed(lv, p)) beginSwingOf(p, p.sprinting, !p.onGround, p.data.value("atkQueueOff", false)); return; }
  const AttackMove& m = attackMove(p.atkMove);
  if ((p.dodgeT > 0 || p.blockT > 0.001f) && p.atkT < m.hitFrom) {
    p.atkMove = MV_NONE; p.atkT = 0; p.atkHit.clear(); p.atkGap = COMBO_WINDOW; p.data.erase("atkQueue"); p.data.erase("sneakHeavy");
    return;
  }
  if (p.atkT < m.turnLock) { p.atkYaw = p.yaw; p.atkPitch = p.pitch; }
  const float t0 = p.atkT;
  const bool sore = p.data.value("hurtLimbT", 0.f) > 0 && ((p.atkOff && p.data.value("hurtArm", 0) < 0) || (!p.atkOff && p.data.value("hurtArm", 0) > 0));
  p.atkT += sore ? dt * 0.55f : dt;   // a hurt arm is slow, and it does not reach the cut
  if (t0 < m.hitFrom && p.atkT >= m.hitFrom) {   // the blade through the air: its whoosh (a heavy head's deeper; fists a soft one)
    const ItemStack& w = p.atkOff && !p.offhand.empty() ? p.offhand[0] : p.inv[p.selected];
    const ItemDef* wd = w.empty() ? nullptr : reg().item(w.id);
    const std::string t = wd && wd->hasTool ? wd->tool.type : "";
    const bool heavy = t == "axe" || t == "pickaxe" || t == "hammer" || t == "hoe" || t == "shovel" || w.id.find("club") != std::string::npos || w.id.find("mace") != std::string::npos || w.id.find("great") != std::string::npos;
    sfxAt(lv, heavy ? "swing_heavy" : "swing_light", p.pos + vec3(0, p.eye * 0.8f, 0), w.empty() ? 6.f : 12.f);
  }
  const float dl = lungeAt(p.atkMove, p.atkT) - lungeAt(p.atkMove, t0);
  if (dl > 0.0001f) {
    bool close = false;
    for (auto& e : lv.entities()) {
      if (e->removed || e->id == p.id || e->realm != p.realm || e->hp <= 0 || (!isMob(*e) && !e->isPlayer())) continue;
      const vec3 d = e->pos - p.pos;
      const float dist = std::hypot(d.x, d.z);
      if (dist < 1.35f && dist > 0.15f && std::fabs(std::remainder(std::atan2(-d.x, -d.z) - p.atkYaw, 6.283185f)) < 1.1f) close = true;
    }
    if (!close) {
      const vec3 fwd(-std::sin(p.atkYaw), 0, -std::cos(p.atkYaw));
      moveBox(lv.world(p.realm), p.pos, p.w, p.h, fwd * dl, 0.6f);
    }
  }
  if ((int)p.atkHit.size() < (int)m.strikes) {
    for (auto& e : lv.entities()) {
      if ((int)p.atkHit.size() >= (int)m.strikes) break;
      if (e->removed || e->id == p.id || e->realm != p.realm || e->hp <= 0 || (!isMob(*e) && !e->isPlayer())) continue;
      if (std::find(p.atkHit.begin(), p.atkHit.end(), e->id) != p.atkHit.end()) continue;
      vec3 ep = e->pos;
      const float reach = sore ? m.reach * 0.35f : m.reach;
      const bool left = p.atkOff;
      if (auto* hitP = dynamic_cast<Player*>(e.get())) {
        ShieldBox guard;
        if (hitP->blockT > 0 && playerGuardBox(*hitP, guard) &&
            bladeSweepHitsShield(p.atkMove, t0, p.atkT, p.pos, p.atkYaw, p.atkPitch, guard, reach, left)) {
          const bool swapIn = left && !p.offhand.empty();
          if (swapIn) std::swap(p.inv[p.selected], p.offhand[0]);
          p.data["metGuard"] = true;
          attackEntity(lv, p, *e, m.dmg, m.impact, p.atkMove == MV_SWORD_AIR, false);
          p.data.erase("metGuard");
          if (swapIn) std::swap(p.inv[p.selected], p.offhand[0]);
          p.atkHit.push_back(e->id);
          continue;
        }
        PlayerLimbBox limbs[6];
        const int nL = playerLimbBoxes(*hitP, limbs);
        bool hitLimb = false; int slot = -1;
        for (int i = 0; i < nL; i++) {
          if (bladeSweepHits(p.atkMove, t0, p.atkT, p.pos, p.atkYaw, p.atkPitch, limbs[i].lo, limbs[i].hi, reach, nullptr, left)) {
            hitLimb = true; slot = limbs[i].armorSlot; break;
          }
        }
        if (!hitLimb) continue;
        const bool swapIn = left && !p.offhand.empty();
        if (swapIn) std::swap(p.inv[p.selected], p.offhand[0]);
        p.data["hitArmorSlot"] = slot;
        attackEntity(lv, p, *e, m.dmg, m.impact, p.atkMove == MV_SWORD_AIR, false);
        p.data.erase("hitArmorSlot");
        if (swapIn) std::swap(p.inv[p.selected], p.offhand[0]);
        p.atkHit.push_back(e->id);
        continue;
      }
      vec3 at(0), dir(0);
      const int part = bladeSweepHitsEntity(p.atkMove, t0, p.atkT, p.pos, p.atkYaw, p.atkPitch, *e, reach, &at, left, &dir);
      if (part == -1) continue;
      if (part == -2) {
        const vec3 lo = ep - vec3(e->w, 0.05f, e->w), hi = ep + vec3(e->w, e->h, e->w);
        if (!bladeSweepHits(p.atkMove, t0, p.atkT, p.pos, p.atkYaw, p.atkPitch, lo, hi, reach, nullptr, left)) continue;
      } else {
        p.data["hitRayO"] = json::array({at.x, at.y, at.z});
        p.data["hitRayD"] = json::array({dir.x, dir.y, dir.z});
        p.data["hitPart"] = part;
      }
      const bool swapIn = left && !p.offhand.empty();
      if (swapIn) std::swap(p.inv[p.selected], p.offhand[0]);
      attackEntity(lv, p, *e, m.dmg, m.impact, p.atkMove == MV_SWORD_AIR, false);
      p.data.erase("hitRayO"); p.data.erase("hitRayD"); p.data.erase("hitPart");
      if (swapIn) std::swap(p.inv[p.selected], p.offhand[0]);
      p.atkHit.push_back(e->id);
      if (isMob(*e)) {
        if (!e->data.is_object()) e->data = json::object();
        e->data["hitStun"] = std::max(e->data.value("hitStun", 0.f), std::clamp(0.22f * m.impact, 0.15f, 0.35f));
      }
    }
  }
  // a press during the swing is the next cut. With two weapons the press that just started this cut is not
  // also the next one: that was spending the off-hand step, so the following click came back to the main hand.
  if (!(started && dual) && (attack || offStart) && p.atkT < m.recovery) {
    const bool qOff = dual ? ((int)p.atkCombo & 1) != 0 : offPress;
    const ItemStack& qHand = qOff && !p.offhand.empty() ? p.offhand[0] : p.inv[p.selected];
    if (weaponSkillMet(p, qHand)) { p.data["atkQueue"] = true; p.data["atkQueueOff"] = qOff; }
    else toast(lv, p, "Needs more melee practice.", "info");
  }
  if (p.atkT >= m.len) {
    p.atkMove = MV_NONE; p.atkT = 0; p.atkHit.clear(); p.atkGap = 0; p.atkOff = false; p.data.erase("sneakHeavy");
    if (queuedSwingAllowed(lv, p)) beginSwingOf(p, p.sprinting, !p.onGround, p.data.value("atkQueueOff", false));
  }
}

void dropCarried(Level& lv, Player& p) {
  const json& c = p.carrying;
  if (!c.is_object()) { p.carrying = nullptr; return; }
  World& w = lv.world(p.realm);
  if (c.value("kind", "") == "block") {
    std::string key = c.value("key", std::string());
    if (!key.empty()) dropItem(lv, p.realm, ItemStack(key, 1), p.pos + vec3(0, 1, 0), true);
    if (c.contains("ent") && c["ent"].is_object()) spillEntity(lv, w, c["ent"], ifloor(p.pos.x), ifloor(p.pos.y), ifloor(p.pos.z));
  } else {
    setDownCarriedCreature(lv, p, p.pos, p.yaw);   // (sim/mob_carry.h: the same animal, its owner and name kept; it had come back a stranger)
  }
  p.carrying = nullptr;
}

void playerDie(Level& lv, Player& p, const DamageSource& src) {
  if (p.dead) return;
  p.dead = true; p.sleeping = false;
  p.stats["deaths"] = p.stats.value("deaths", 0) + 1;
  sfxUi(lv, p, "death");
  static const std::unordered_map<std::string, std::string> causes = {
    {"drown", "You drowned"}, {"fall", "You fell from a high place"}, {"lava", "You tried to swim in lava"}, {"burn", "You burned to death"},
    {"void", "You fell out of the world"}, {"cactus", "You hugged a cactus"}, {"projectile", "You were struck by a fireball"},
    {"acid", "You dissolved in acid"}, {"poison", "You were poisoned"}, {"wither", "You withered away"}, {"magic", "You were killed by magic"},
    {"freeze", "You froze to death"}, {"lightning", "You were struck by lightning"}, {"suffocate", "You suffocated in a wall"},
    {"starve", "You starved"}, {"thorns", "You were pricked to death"}, {"sting", "You were stung to death"}, {"heat", "You were cooked by the heat"},
    {"falling_block", "You were squashed by a falling block"}, {"falling_tree", "A falling tree flattened you"}};
  std::string cause = causes.count(src.type) ? causes.at(src.type) : "You died";
  Entity* a = src.attacker ? lv.entity(src.attacker) : nullptr;
  if (a && !a->isPlayer()) {
    std::string label = creatureLabelOf(*a);   // a variant's own ("a miner skeleton"), else the type's
    std::string t = a->type; std::replace(t.begin(), t.end(), '_', ' ');
    cause = "You were slain by " + (a->type == "cinder_golem" ? std::string("the Cinder Golem") : !label.empty() ? label : "a " + t);
  } else if (a && a->isPlayer()) cause = "You were slain by " + static_cast<Player*>(a)->name;
  else if (src.type == "arrow") cause = "You were shot";
  else if (src.type == "explosion") cause = "You were blown up";
  // the body stays where it fell (sim/corpse.h): with the "graves" rule it keeps everything they carried, each thing
  // remembered in its slot, and the death compass points to it (to the oldest body, while there are several)
  if (!rule(lv, "keepInventory")) {
    if (rule(lv, "graves")) {
      spawnCorpse(lv, p, src, true);
      for (auto* v : {&p.inv, &p.armor, &p.back, &p.offhand, &p.hook, &p.charm, &p.cape, &p.quiver, &p.shield}) for (auto& s : *v) s.clear();   // (the old compass too)
      if (!deathBodies(p).empty() && reg().item("death_compass")) p.inv[0] = ItemStack("death_compass", 1);
    } else {
      std::vector<ItemStack> all;
      for (auto* v : {&p.inv, &p.armor, &p.back, &p.offhand, &p.hook, &p.charm, &p.shield}) for (auto& s : *v) if (!s.empty() && s.id != "death_compass") all.push_back(s);
      for (auto& s : all) dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 6);
      for (auto* v : {&p.inv, &p.armor, &p.back, &p.offhand, &p.hook, &p.charm, &p.shield}) for (auto& s : *v) s.clear();
      if (!deathBodies(p).empty() && reg().item("death_compass")) p.inv[0] = ItemStack("death_compass", 1);   // (bodies from before)
      spawnCorpse(lv, p, src, false);
    }
    p.xp = 0;
  } else spawnCorpse(lv, p, src, false);
  if (!p.carrying.is_null()) dropCarried(lv, p);
  p.hang.reset(); p.climb.reset(); p.vel = vec3(0);
  if (!p.data.is_object()) p.data = json::object();
  p.data["deathCause"] = cause;   // the death screen's line under "You died" (ui/hud.cpp)
  DQ_LOG("%s died: %s (%s%s)", p.name.c_str(), cause.c_str(), src.type.c_str(), a ? (", " + a->type).c_str() : "");
  if (lv.events) lv.events->emit("playerDeath", json::array({p.name, cause, p.level}));
  toast(lv, p, cause, "death");
}

static int findSafeY(World& w, int x, int z, int top, int bottom) {
  const Registry& R = reg();
  for (int y = top; y > bottom; y--) {
    BlockId id = w.getBlock(x, y - 1, z);
    if (R.tSolid[id] && !R.tSolid[w.getBlock(x, y, z)] && !R.tSolid[w.getBlock(x, y + 1, z)] && !R.tLiquid[w.getBlock(x, y, z)]) return y;
  }
  return -1;
}

void placeAtSpawn(Level& lv, Player& p, bool initial) {
  p.realm = REALM_MAIN;
  World& w = lv.world(REALM_MAIN);
  vec3 sp = lv.info.spawn;
  if (sp.y < 0) sp = vec3(0.5f, dimBase(DIM_OVERWORLD) + 100, 0.5f);
  p.pos = sp;
  int bx = ifloor(sp.x), bz = ifloor(sp.z);
  if (w.isLoaded(bx, dimBase(DIM_OVERWORLD), bz)) {
    int y = findSafeY(w, bx, bz, dimBase(DIM_OVERWORLD) + DIM_H - 3, dimBase(DIM_OVERWORLD) + 1);
    if (y > 0) p.pos.y = (float)y;
  } else {
    // not loaded (from a side realm: the Drift, the pocket): ask for it now and stand on the land's own height meanwhile,
    // or you fell through the unloaded ground and out of the world
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) w.requestChunk(DIM_OVERWORLD, (bx >> 4) + dx, (bz >> 4) + dz, 1000);
    if (const DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_OVERWORLD) : nullptr) {
      const int h = g->spawnHeight(bx, bz);
      if (h > 0 && h < DIM_H - 2) p.pos.y = std::max(p.pos.y, (float)(dimBase(DIM_OVERWORLD) + h + 1));
    }
  }
  p.vel = vec3(0); p.hasFallStart = false; p.fallDist = 0;
  if (initial) { p.yaw = (float)rnd() * TAU; p.pitch = -0.1f; }
}

void respawnPlayer(Level& lv, Player& p) {
  p.dead = false; p.hp = p.maxHp; p.burn = 0; p.invuln = 2; p.hasFallStart = false; p.air = AIR_MAX;
  p.vel = vec3(0); p.hang.reset(); p.climb.reset(); p.sleeping = false;
  // back to your bed, if it's still there
  if (p.spawnPoint.y >= 0) {
    World& w = lv.world(p.spawnRealm);
    int bx = ifloor(p.spawnPoint.x), by = ifloor(p.spawnPoint.y), bz = ifloor(p.spawnPoint.z);
    if (!w.isLoaded(bx, by, bz)) { p.realm = p.spawnRealm; p.pos = p.spawnPoint + vec3(0, 0.6f, 0); goto done; }
    if (bhas(w.getBlock(bx, by, bz), BF::Bed)) {
      p.realm = p.spawnRealm;
      for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {0, 0}}) {
        int x = bx + dx, z = bz + dz, y = by;
        if (!collides(w, x + 0.2, y, z + 0.2, x + 0.8, y + 1.8, z + 0.8) && reg().tSolid[w.getBlock(x, y - 1, z)]) { p.pos = vec3(x + 0.5f, y, z + 0.5f); goto done; }
      }
      p.pos = vec3(p.spawnPoint.x, p.spawnPoint.y + 0.6f, p.spawnPoint.z);
      goto done;
    }
    toast(lv, p, "Your bed was missing or blocked", "warn");
    p.spawnPoint = vec3(0, -1, 0);
  }
  placeAtSpawn(lv, p, false);
done:
  p.prevPos = p.pos;
  if (lv.events) lv.events->emit("playerRespawn", json::array({p.name}));
}

void registerCombat() {}

} // namespace dq
