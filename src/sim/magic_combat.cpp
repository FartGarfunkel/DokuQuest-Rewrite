// sim/magic_combat.cpp — the default (pre-merge) implementation of the adapter. At merge, point combatHurt /
// combatHeal / combatKnock / grantXP at sim/combat.h; keep the magicIncomingDamage call inside the player hurt.
#include "sim/magic_combat.h"
#include "sim/magic.h"
#include "sim/enchanting.h"
#include "sim/skills.h"
#include "sim/creature.h"
#include <regex>

namespace dq {

static const json NULLJ;

const json& mobTemplate(const Entity& e) {
  const json& m = reg().mobs;
  if (!m.is_object()) return NULLJ;
  auto it = m.find(e.type);
  return it == m.end() ? NULLJ : *it;
}
bool isAlive(const Entity& e) { return !e.removed && e.hp > 0; }
static bool tflag(const json& t, const char* k) { return t.is_object() && t.contains(k) && t[k].is_boolean() ? t[k].get<bool>() : t.is_object() && t.contains(k) && t[k].is_number() && t[k].get<double>() != 0; }
bool isSpellTarget(const Entity& e) {
  if (!isMob(e) || !isAlive(e)) return false;
  const json& T = mobTemplate(e);
  return !(tflag(T, "npc") && !tflag(T, "hostile"));
}
bool isFoe(const Entity& e) {
  if (!isSpellTarget(e)) return false;
  auto has = [&](const char* k) { return e.data.contains(k) && !e.data[k].is_null() && e.data[k] != false && e.data[k] != ""; };
  return !has("owner") && !has("tamed");
}
bool isUndead(const Entity& e) {
  static const std::regex re("ghoul|skeleton|stray|husk|drowned|wraith|zombie|wight|lich|revenant");
  return tflag(mobTemplate(e), "undead") || std::regex_search(e.type, re);
}
bool isArthropod(const Entity& e) { static const std::regex re("spider|scorpion|mite|bee|silverfish|beetle|tick"); return std::regex_search(e.type, re); }
bool isFireImmune(const Entity& e) { return tflag(mobTemplate(e), "fireImmune"); }

Player* playerNamed(Level& lv, const std::string& name) {
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && static_cast<Player*>(e.get())->name == name) return static_cast<Player*>(e.get());
  return nullptr;
}

float combatHurt(Level& lv, Entity& t, float amount, const json& src0, Player* by) {
  if (amount <= 0 || t.removed) return 0;
  json src = src0.is_object() ? src0 : json::object();
  if (by) { src["attacker"] = by->id; src["attackerName"] = by->name; }
  if (t.ghost) {   // someone else's entity: the server applies it (and our statuses ride along in src)
    if (lv.net && by) lv.net->hitEntity(*by, t.id, amount, src);
    return 0;
  }
  if (t.isPlayer()) {
    Player& p = static_cast<Player&>(t);
    if (p.dead || p.mode == "creative" || p.mode == "spectator") return 0;
    float before = p.hp;
    amount = magicIncomingDamage(lv, p, amount, src);
    if (amount <= 0) return 0;
    p.hp = std::max(0.f, p.hp - amount); p.hurtTime = 0.5f;
    float took = before - p.hp;
    afterPlayerHurt(lv, p, took, src);
    return took;
  }
  if (by) amount = magicOutgoingDamage(lv, *by, amount, src);
  // creatures: their own hurt (death topple, drops, XP, sounds, severing), not a second copy here
  if (auto* c = dynamic_cast<Creature*>(&t)) {
    DamageSrc s;
    s.type = src.value("type", std::string("magic")); s.element = src.value("element", std::string());
    s.knock = src.contains("knock") && src["knock"].is_number() ? src["knock"].get<float>() : -1.f;
    s.attacker = by ? by->id : (src.contains("attacker") && src["attacker"].is_number_unsigned() ? src["attacker"].get<u32>() : 0);
    s.bypassArmor = src.value("bypassArmor", false);
    return c->hurt(lv, amount, s);
  }
  float hp0 = std::max(0.f, t.hp);
  t.hp -= amount; t.hurtTime = 0.5f;
  if (src.contains("knock") && src["knock"].is_number() && by) combatKnock(lv, t, by->pos, src["knock"].get<float>());
  if (src.value("element", "") == "fire" && !isFireImmune(t)) setMobStatus(lv, t, "burnT", 3);
  if (src.value("element", "") == "frost") setMobStatus(lv, t, "slowT", 3);
  float dealt = std::min(hp0, amount);
  if (t.hp <= 0) {
    t.removed = true;
    if (lv.events) lv.events->emit("mobDeath", json::array({json{{"type", t.type}, {"x", t.pos.x}, {"y", t.pos.y}, {"z", t.pos.z}, {"id", t.id}, {"realm", t.realm}}, by ? by->name : ""}));
  }
  return dealt;
}

float combatHeal(Level& lv, Entity& t, float amount) {
  if (amount <= 0 || t.removed) return 0;
  float before = t.hp;
  t.hp = std::min(t.maxHp, t.hp + amount);
  return t.hp - before;
}

void combatKnock(Level& lv, Entity& t, vec3 from, float strength) {
  if (strength <= 0) return;
  float kr = mobTemplate(t).is_object() ? mobTemplate(t).value("knockResist", 0.f) : 0.f;
  vec3 d = t.pos - from; d.y = 0;
  float l = glm::length(d);
  if (l < 1e-3f) return;
  d /= l;
  float k = strength * 0.9f * (1 - kr);
  t.vel.x += d.x * k; t.vel.z += d.z * k; t.vel.y = std::max(t.vel.y, 2.5f + strength * 0.4f);
}

void setMobStatus(Level& lv, Entity& e, const char* key, float secs, Player* by) {
  if (secs <= 0) return;
  if (e.ghost) {
    if (lv.net && by) lv.net->action(*by, "magicMobset", json{{"id", e.id}, {"f", {{key, secs}}}});
    return;
  }
  float old = e.data.contains(key) && e.data[key].is_number() ? e.data[key].get<float>() : 0.f;
  e.data[key] = std::max(old, secs);
}
float mobStatus(const Entity& e, const char* key) { return e.data.contains(key) && e.data[key].is_number() ? e.data[key].get<float>() : 0.f; }

int addXP(Player& p, int n) {
  int ups = 0;
  p.xp += std::max(0, n);
  while (p.xp >= xpForLevel(p.level)) { p.xp -= xpForLevel(p.level); p.level++; ups++; }
  return ups;
}
void grantXP(Level& lv, Player& p, int n, const vec3* at) {
  bool mended = false;
  n = enchXpGain(p, n, &mended);
  if (n <= 0) { if (mended && at) lv.fx->particles("text", *at, 1, json{{"text", "Mended"}, {"col", "#c9a6ff"}}); return; }
  int ups = addXP(p, n);
  if (at) lv.fx->particles("text", *at, 1, json{{"text", "+" + std::to_string(n) + " XP"}, {"col", "#b6f06a"}});
  if (ups) { lv.fx->soundUi(p.id, "levelup"); lv.fx->message(p.id, "Level " + std::to_string(p.level) + "!", "good"); }
}
void spendLevels(Player& p, int n) { p.level = std::max(0, p.level - n); p.xp = std::min(p.xp, xpForLevel(p.level) - 1); }

Entity* spawnCreature(Level& lv, const std::string& type, int realm, vec3 pos) {
  auto e = createEntity(type);
  e->type = type; e->realm = realm; e->pos = pos;
  const json& T = mobTemplate(*e);
  if (T.is_object()) { e->hp = e->maxHp = T.value("hp", 20.f); e->w = T.value("w", 0.3f); e->h = T.value("h", 1.8f); }
  return lv.spawn(std::move(e));
}

void dropStack(Level& lv, int realm, ItemStack s, vec3 pos) {
  if (s.empty()) return;
  auto e = createEntity("item");
  e->type = "item"; e->realm = realm; e->pos = pos; e->w = 0.125f; e->h = 0.25f;
  e->vel = vec3(0, 2, 0);
  e->data["stack"] = s.toJson(); e->data["pickupDelay"] = 0.5;
  lv.spawn(std::move(e));
}
void giveOrDrop(Level& lv, Player& p, ItemStack s) {
  int left = p.give(s);
  if (left > 0) { s.n = left; dropStack(lv, p.realm, s, p.pos + vec3(0, 1, 0)); }
}

} // namespace dq
