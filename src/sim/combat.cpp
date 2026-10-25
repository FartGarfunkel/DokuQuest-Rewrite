// sim/combat.cpp — damage, death and respawn (local.js hurt/die/respawn, player.js hurt/wearArmor,
// entities.js Mob.hurt/die, fairplay.js raw-blow floor).
#include "sim/combat.h"
#include "sim/gameplay.h"
#include "sim/rules.h"
#include <random>

namespace dq {

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
  e.vel.x += kx / l * k; e.vel.z += kz / l * k;
  if (up > 0) e.vel.y = std::max(e.vel.y, up);
}

const ToolInfo* heldTool(const Player& p) {
  const ItemStack& h = p.inv[p.selected];
  if (h.empty()) return nullptr;
  auto* d = reg().item(h.id);
  return d && d->hasTool ? &d->tool : nullptr;
}
std::string heldToolElement(const Player& p) {
  const ItemStack& h = p.inv[p.selected];
  auto* d = h.empty() ? nullptr : reg().item(h.id);
  if (!d || !d->props.contains("tool") || !d->props["tool"].is_object()) return "";
  return d->props["tool"].value("element", std::string());
}
bool wearHeld(Level& lv, Player& p, int n) {
  if (!n || p.creative()) return false;
  ItemStack& s = p.inv[p.selected];
  if (s.empty()) return false;
  auto* d = reg().item(s.id);
  if (!d || !d->hasTool || d->tool.durability <= 0) return false;
  s.d += n;
  if (s.d >= d->tool.durability) { toast(lv, p, "Your " + toLower(d->name) + " broke", "warn"); sfxUi(lv, p, "break_tool"); s.clear(); return true; }
  return false;
}
void wearArmor(Player& p) {
  for (int i = 0; i < 4; i++) {
    ItemStack& s = p.armor[i];
    if (s.empty()) continue;
    auto* d = reg().item(s.id);
    if (!d || !d->props.contains("armor")) continue;
    s.d++;
    if (s.d >= d->props["armor"].value("durability", 1 << 30)) { p.brokeSomething = d->name; s.clear(); }
  }
}

// Player.hurt: armour and fire resistance, then the invulnerable moment
static float playerHurtRaw(Player& p, float amount, const DamageSource& src) {
  if (p.dead || amount <= 0) return 0;
  if (p.invuln > 0 && !src.bypass) return 0;
  float dmg = amount;
  if (!src.bypassArmor) dmg *= 1 - std::min(0.8f, armorPoints(p) * 0.032f);
  if (src.raw > 0 && !src.bypassArmor) dmg = std::max(dmg, src.raw * 0.2f);
  if (src.element == "fire" || src.type == "lava" || src.type == "burn") dmg *= 1 - fireResist(p);
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
  if ((src.type == "fall" && !rule(lv, "fallDamage")) || (src.type == "drown" && !rule(lv, "drowning")) ||
      ((src.type == "lava" || src.type == "burn") && !rule(lv, "fireDamage"))) return 0;
  src.raw = amount;
  p.lastHurtT = gameClock(lv);
  if (p.sleeping) p.sleeping = false;
  float dealt = playerHurtRaw(p, amount, src);
  if (!dealt) return 0;
  sfxUi(lv, p, "hurt");
  if (lv.events) lv.events->emit("rumble", json::array({p.name, std::min(1.f, 0.25f + dealt * 0.08f), 140}));
  if (src.attacker && src.knock != 0) if (Entity* a = lv.entity(src.attacker)) knockback(p, a->pos, src.knock < 0 ? 5.f : src.knock, 4);
  if (src.element == "fire" && src.type != "burn") p.burn = std::max(p.burn, 2.f);
  if (p.hp <= 0) playerDie(lv, p, src);
  return dealt;
}

void mobDeath(Level& lv, Entity& m, const DamageSource& src) {
  const json& T = mobType(m.type);
  bool baby = m.data.value("baby", false);
  if (!baby && T.contains("drops") && T["drops"].is_array()) for (auto& d : T["drops"]) {
    if (!d.is_array() || d.size() < 4) continue;
    if (rnd() < d[3].get<double>()) { int a = d[1], b = d[2]; int n = a + (int)(rnd() * (b - a + 1)); if (n > 0) dropItem(lv, m.realm, ItemStack(d[0], n), m.pos + vec3(0, 0.5f, 0), true); }
  }
  if (T.value("woolly", false) && !m.data.value("sheared", false) && !baby) {
    std::string col = m.data.value("color", std::string("white"));
    dropItem(lv, m.realm, ItemStack(col == "white" ? "wool" : col + "_wool", 1), m.pos + vec3(0, 0.5f, 0), true);
  }
  Entity* killer = src.attacker ? lv.entity(src.attacker) : nullptr;
  if (killer && killer->isPlayer()) {
    Player& kp = static_cast<Player&>(*killer);
    vec3 at = m.pos + vec3(0, m.h, 0);
    grantXP(lv, kp, T.value("xp", 0), &at);
    kp.stats["mobsKilled"] = kp.stats.value("mobsKilled", 0) + 1;
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
  if (auto* h = handlerFor(e.type)) return (*h)(lv, e, amount, src);
  if (e.type == "item" || e.type == "falling_block" || e.type == "falling_tree" || e.type == "seat" || e.type == "tnt") return 0;
  return mobHurtDefault(lv, e, amount, src);
}

float attackEntity(Level& lv, Player& p, Entity& e) {
  if (p.swing == 0 || p.swing > 0.6f) p.swing = 0.0001f;
  if (p.attackCd > 0) return 0;
  const ToolInfo* tool = heldTool(p);
  float dmg = tool ? tool->damage : 1;
  bool crit = !p.onGround && p.vel.y < 0;
  if (crit) dmg = std::ceil(dmg * 1.5f);
  DamageSource src; src.attacker = p.id; src.element = heldToolElement(p); src.knock = 5;
  if (tool) src.tool = tool->type;
  if (!p.inv[p.selected].empty()) src.weapon = p.inv[p.selected].id;
  float dealt;
  if (e.ghost && lv.net && lv.net->isClient()) {   // a joined world: the server's creature; it takes the hit there
    lv.net->hitEntity(p, e.id, dmg, {{"element", src.element}, {"knock", src.knock}, {"tool", src.tool}, {"weapon", src.weapon}, {"crit", crit}});
    dealt = dmg;
  } else dealt = damageEntity(lv, e, dmg, src);
  p.attackCd = tool && tool->type == "sword" ? 0.45f : 0.6f;
  if (dealt > 0) {
    sfxUi(lv, p, crit ? "crit" : "hit");
    char buf[32]; snprintf(buf, sizeof buf, "%s%g", crit ? "\xE2\x9C\xA6" : "", std::round(dealt * 10) / 10);
    floatText(lv, p, e.pos + vec3(0, e.h + 0.2f, 0), buf, src.element == "fire" ? "#ffb070" : src.element == "frost" ? "#9fe8ff" : crit ? "#ffe07a" : "#ffffff");
    if (src.element == "frost") sfxUi(lv, p, "frost");
    wearHeld(lv, p, tool ? (tool->type == "sword" ? 1 : 2) : 0);
    burst(lv, e.pos + vec3(0, e.h * 0.6f, 0), src.element == "fire" ? "fireball" : src.element == "frost" ? "frost_bolt" : "white", crit ? 8 : 4, 0.8f);
  }
  return dealt;
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
    auto m = createEntity(c.value("type", std::string("boar")));
    m->realm = p.realm; m->pos = p.pos;
    if (c.contains("hp") && c["hp"].is_number()) m->hp = c["hp"];
    lv.spawn(std::move(m));
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
    {"void", "You fell out of the world"}, {"cactus", "You hugged a cactus"}, {"projectile", "You were struck by a fireball"}};
  std::string cause = causes.count(src.type) ? causes.at(src.type) : "You died";
  Entity* a = src.attacker ? lv.entity(src.attacker) : nullptr;
  if (a && !a->isPlayer()) {
    std::string label = mobType(a->type).value("label", std::string());
    std::string t = a->type; std::replace(t.begin(), t.end(), '_', ' ');
    cause = "You were slain by " + (a->type == "cinder_golem" ? std::string("the Cinder Golem") : !label.empty() ? label : "a " + t);
  } else if (a && a->isPlayer()) cause = "You were slain by " + static_cast<Player*>(a)->name;
  else if (src.type == "arrow") cause = "You were shot";
  else if (src.type == "explosion") cause = "You were blown up";
  if (!rule(lv, "keepInventory")) {
    std::vector<ItemStack> all;
    for (auto* v : {&p.inv, &p.armor, &p.back, &p.offhand, &p.hook, &p.charm}) for (auto& s : *v) if (!s.empty() && s.id != "death_compass") all.push_back(s);
    json grave = rule(lv, "graves") ? makeGrave(lv, p, all) : json();
    if (!grave.is_null()) p.grave = grave;
    else for (auto& s : all) dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 6);
    for (auto* v : {&p.inv, &p.armor, &p.back, &p.offhand, &p.hook, &p.charm}) for (auto& s : *v) s.clear();
    if (!grave.is_null() && reg().item("death_compass")) p.inv[0] = ItemStack("death_compass", 1);
    p.xp = 0;
  }
  if (!p.carrying.is_null()) dropCarried(lv, p);
  p.hang.reset(); p.climb.reset(); p.vel = vec3(0);
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
  }
  p.vel = vec3(0); p.hasFallStart = false;
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
