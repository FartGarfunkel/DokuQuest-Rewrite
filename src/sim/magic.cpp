// sim/magic.cpp — mana, learning and choosing spells, the Alteration spells at work, the damage hooks, item
// uses, creature statuses, summons' time running out, brewing stands; the MagicSystem that runs them.
// (game/magic.js, arcana.js, spellcraft.js, paths.js schools, brewing.js, monsters.js venom, stable.js spirits)
#include "sim/magic.h"
#include "sim/magic_fx.h"
#include "sim/magic_entities.h"
#include "sim/effects.h"
#include "sim/enchanting.h"
#include "sim/brewing.h"
#include "sim/rpg.h"
#include "sim/seamless.h"
#include "sim/commands.h"
#include "world/world.h"

namespace dq {

static double rnd() { return mrand(); }

// ---------------------------------------------------------------------------------------------------
class MagicSystem : public LevelSystem {
public:
  explicit MagicSystem(Level& l) : lv(l) {
    if (!lv.events) return;
    hUse = lv.events->on("itemUse", [this](const json& a) { return onItemUse(a); });
    hHit = lv.events->on("netHit", [this](const json& a) { return onNetHit(a); });
    hAct = lv.events->on("netAction", [this](const json& a) { return onNetAction(a); });
    hDeath = lv.events->on("mobDeath", [this](const json& a) { onMobDeath(a); return true; });
  }
  ~MagicSystem() override { if (lv.events) for (int h : {hUse, hHit, hAct, hDeath}) if (h) lv.events->off(h); }
  const char* name() const override { return "magic"; }
  void tick(Level& l, double dt) override;
  MagicState& state(const Player& p) {
    auto it = st.find(p.id);
    if (it != st.end()) return it->second;
    MagicState& s = st[p.id];
    if (p.stats.contains("mana") && p.stats["mana"].is_number()) s.mana = p.stats["mana"].get<float>();
    return s;
  }
  Level& lv;
  std::unordered_map<u32, MagicState> st;
private:
  int hUse = 0, hHit = 0, hAct = 0, hDeath = 0;
  double brewAcc = 0;
  bool onItemUse(const json& a);
  bool onNetHit(const json& a);
  bool onNetAction(const json& a);
  void onMobDeath(const json& a);
  void tickPlayer(Player& p, double dt);
  void tickMob(Entity& e, double dt);
};

MagicState& magicState(Level& lv, const Player& p) {
  if (auto* s = lv.system<MagicSystem>()) return s->state(p);
  static std::unordered_map<u32, MagicState> loose;   // a level without the system (tools)
  return loose[p.id];
}

// ---- mana ---------------------------------------------------------------------------------------------
float manaMax(const Player& p) {
  float m = 20 + std::floor(skillLv(p, "magic") / 2.f);
  for (auto& s : p.armor) if (const ItemDef* d = s.def()) if (d->has("armor")) m += d->props["armor"].value("mana", 0.f);
  return m + (onPath(p, "mage") ? 20 : 0) + (hasTal(p, "well") ? 25 : 0);
}
float manaRegen(const Player& p) {
  float r = 0.5f;
  for (auto& s : p.armor) if (const ItemDef* d = s.def()) if (d->has("armor")) r += d->props["armor"].value("regen", 0.f);
  const ItemStack& h = const_cast<Player&>(p).held();
  if (const json* st = heldStaff(p)) r += st->value("regen", 0.f);
  r += 0.3f * enchLv(h, "flow");                                         // enchanting.js Mana Flow
  if (hasPerk(p, "magic", 30)) r *= 1.25f;                                // rpg.js Mana Flow perk
  if (hasEffect(p, "clarity")) r *= 1.8f + 0.4f * effectLevel(p, "clarity");   // arcana.js Clarity
  if (hasTal(p, "flow")) r *= 1.5f;                                      // paths.js Archmage's Flow
  return r;
}
const json* heldStaff(const Player& p) {
  const ItemDef* d = const_cast<Player&>(p).held().def();
  return d && d->props.contains("staff") && d->props["staff"].is_object() ? &d->props["staff"] : nullptr;
}
float robeBoost(const Player& p, const std::string& spell) {
  std::string set;
  for (auto& s : p.armor) {
    const ItemDef* d = s.def();
    if (!d) return 1;
    std::string ic = d->icon.empty() ? d->key : d->icon;
    std::string mk = ic.substr(0, ic.find('_'));
    if (!set.empty() && mk != set) return 1;
    set = mk;
  }
  if (set == "archmage") return 1.12f;
  const json& R = magicData().robes;
  if (R.contains(set) && R[set].value("school", "") != "" && R[set]["school"] == schoolOfSpell(spell)) return 1.33f;
  return 1;
}

// ---- schools (paths.js) ------------------------------------------------------------------------------
std::string schoolOfSpell(const std::string& k) { const SpellDef* S = magicData().spell(k); return S ? S->school : "arcane"; }
int schoolLv(const Player& p, const std::string& s) {
  const json& sc = p.stats.contains("schools") ? p.stats["schools"] : json();
  return skillLevel(sc.is_object() && sc.contains(s) && sc[s].is_number() ? sc[s].get<double>() : 0);
}

// ---- spells known -------------------------------------------------------------------------------------
const std::vector<std::string>& knownSpells(const Player& p) {
  thread_local std::vector<std::string> v;
  v.clear();
  if (p.stats.contains("spells") && p.stats["spells"].is_array()) for (auto& k : p.stats["spells"]) if (k.is_string()) v.push_back(k);
  return v;
}
std::string activeSpell(Player& p) {
  auto ks = knownSpells(p);
  if (ks.empty()) return "";
  int i = floormod(p.stats.value("spell", 0), (int)ks.size());
  p.stats["spell"] = i;
  return ks[i];
}
void cycleSpell(Level& lv, Player& p) {
  auto ks = knownSpells(p);
  if (ks.empty()) { lv.fx->message(p.id, "You know no spells yet \xE2\x80\x94 read a spell tome", ""); return; }
  int i = (p.stats.value("spell", 0) + 1) % (int)ks.size();
  p.stats["spell"] = i;
  if (const SpellDef* S = magicData().spell(ks[i])) lv.fx->message(p.id, S->name, "");
  lv.fx->soundUi(p.id, "click");
}
bool pickSpell(Level& lv, Player& p, const std::string& k) {
  auto ks = knownSpells(p);
  auto it = std::find(ks.begin(), ks.end(), k);
  if (it == ks.end()) return false;
  p.stats["spell"] = (int)(it - ks.begin());
  if (const SpellDef* S = magicData().spell(k)) lv.fx->message(p.id, S->name + " is your spell", "good");
  lv.fx->soundUi(p.id, "click");
  return true;
}
bool learnTome(Level& lv, Player& p, ItemStack& tome) {
  const ItemDef* d = tome.def();
  std::string k = d ? d->str("tome") : "";
  const SpellDef* S = magicData().spell(k);
  if (!S) return false;
  auto ks = knownSpells(p);
  if (std::find(ks.begin(), ks.end(), k) != ks.end()) { lv.fx->message(p.id, "You already know " + S->name, ""); return true; }
  int lvl = skillLv(p, "magic");
  if (lvl < S->lvl) { lv.fx->message(p.id, "The words swim on the page. You need Magic " + std::to_string(S->lvl) + " to learn " + S->name, "warn"); return true; }
  json& sp = p.stats["spells"];
  if (!sp.is_array()) sp = json::array();
  sp.push_back(k);
  p.stats["spell"] = (int)sp.size() - 1;
  if (!p.creative() && --tome.n <= 0) tome.clear();
  lv.fx->message(p.id, "You have learned " + S->name + "!" + (sp.size() == 1 ? " Hold a staff and use it to cast" : ""), "epic");
  lv.fx->soundUi(p.id, "levelup");
  skillGain(lv, p, "magic", 40.0 * S->lvl);
  sfx::spiral(lv, p.pos, S->fx, 70, json{{"r", 0.9}, {"h", 2.6}, {"turns", 4}, {"life", 1.3}});
  sfx::burst(lv, p.pos + vec3(0, 1.2f, 0), "mana_arcane", 30, 1.2f, json{{"life", 1.2}, {"size", 0.06}, {"flutter", 1}});
  return true;
}
bool spellRepeats(const std::string& k) {
  static const std::unordered_set<std::string> no = {"heal", "blink", "ward", "light", "levitate", "stoneskin", "familiar", "regrowth", "haste", "guardian", "icenova", "healother"};
  const SpellDef* S = magicData().spell(k);
  return S && !S->util && !no.count(k);
}
void castQuick(Level& lv, Player& p) {
  std::string k = activeSpell(p);
  if (k.empty()) { lv.fx->message(p.id, "You know no spells yet \xE2\x80\x94 read a spell tome", ""); return; }
  if (heldStaff(p)) { castSpell(lv, p); return; }
  const SpellDef* S = magicData().spell(k);
  static const std::unordered_set<std::string> old = {"firebolt", "frost", "light", "heal", "levitate", "stoneskin", "blink", "familiar", "ward", "lightning", "missiles", "regrowth", "haste", "fireball", "icenova", "guardian"};
  if (old.count(k)) { lv.fx->message(p.id, "Hold a staff to cast " + (S ? S->name : k), ""); return; }
  MagicState& m = magicState(lv, p);
  m.bare = true;
  castSpell(lv, p);
  m.bare = false;
}

// ---- harm taken / dealt -------------------------------------------------------------------------------
float magicIncomingDamage(Level& lv, Player& p, float amount, json& src) {
  if (amount <= 0) return amount;
  MagicState& m = magicState(lv, p);
  std::string t = src.value("type", ""), el = src.value("element", "");
  bool bypass = src.value("bypass", false), bypassArmor = src.value("bypassArmor", false);
  // paths.js: Shield Wall, Iron Will, Mana Shield
  if (!bypass) {
    if (hasTal(p, "shieldwall") && !bypassArmor) amount *= 0.92f;
    if (hasTal(p, "ironwill") && src.contains("attacker") && src.value("knock", 1.f) != 0) src["knock"] = src.value("knock", 5.f) * 0.5f;
    if (hasTal(p, "shield") && m.mana > 0) { float part = amount / 3, cost = std::min(m.mana, part); m.mana -= cost; amount -= cost; }
  }
  // rpg.js: Surefooted, Thick Skin
  if (t == "fall" && hasPerk(p, "exploring", 60)) amount = std::floor(amount * 0.67f * 2) / 2;
  if (amount > 0 && amount <= 2 && hasPerk(p, "defence", 30) && t != "fall" && t != "void") amount = std::max(0.5f, amount - 0.5f);
  src["_raw"] = amount;   // fairplay.js: a fifth always gets through (of what the ward lets past)
  // magic.js: stoneskin, then the ward soaks it up
  if (m.stoneT > 0 && amount > 0 && !bypass && t != "starve" && t != "void") {
    amount *= 0.66f;
    if (src.contains("knock") && src["knock"].is_number()) src["knock"] = src["knock"].get<float>() * 0.25f; else src["knock"] = 1;
    sfx::blockBurst(lv, p.pos + vec3(0, 1, 0), "stone", 4, 1);
  }
  if (m.ward > 0 && amount > 0 && !bypass) {
    float soak = std::min(m.ward, amount);
    m.ward -= soak; amount -= soak;
    src["_raw"] = std::max(0.f, src.value("_raw", 0.f) - soak);
    sfx::burst(lv, p.pos + vec3(0, 1, 0), "mana_light", 10, 1.2f, json{{"life", 0.4}, {"spread", 1}});
    if (m.ward <= 0) { m.ward = 0; lv.fx->message(p.id, "Your ward breaks", ""); }
    if (amount <= 0) return 0;
  }
  // skills.js: Defence and well-worn armour
  bool worn = false; for (auto& s : p.armor) worn |= !s.empty();
  if (worn && t != "fall" && t != "void" && t != "drown" && t != "starve") {
    float k = 1 - skillBonus(p, "defence", 0.12f);
    for (auto& s : p.armor) if (!s.empty()) k -= gearBonus(s, 0.02f);
    amount *= k;
  }
  // brewing.js: fire resistance, leaping softens falls, resistance
  if (hasEffect(p, "fireres") && (t == "lava" || t == "burn" || el == "fire" || t == "fire")) return 0;
  if (t == "fall" && hasEffect(p, "jump")) { amount -= effectLevel(p, "jump") + 1; if (amount <= 0) return 0; }
  if (hasEffect(p, "resist") && t != "void") amount *= std::max(0.f, 1 - 0.2f * (effectLevel(p, "resist") + 1));
  if (amount <= 0) return 0;
  // enchanting.js: protection of every kind
  if (!bypassArmor || t == "fall") amount *= protectionFactor(p, t, el);
  return amount;
}

void afterPlayerHurt(Level& lv, Player& p, float took, const json& src) {
  if (took <= 0) return;
  std::string t = src.value("type", "");
  bool worn = false; for (auto& s : p.armor) worn |= !s.empty();
  if (worn && t != "fall" && t != "void" && t != "drown" && t != "starve") {   // skills.js: only a blow that lands teaches
    skillGain(lv, p, "defence", std::round(took * 3));
    for (auto& s : p.armor) if (!s.empty()) gearGain(lv, p, s, std::ceil(took));
  }
  Entity* a = src.contains("attacker") && src["attacker"].is_number() ? lv.entity(src["attacker"].get<u32>()) : nullptr;
  enchThorns(lv, p, a, p.hp + took);
  MagicState& m = magicState(lv, p);   // rpg.js Second Wind
  if (!p.dead && p.hp > 0 && p.hp < p.maxHp * 0.25f && hasPerk(p, "defence", 60) && !(m.secondWindT > 0)) {
    m.secondWindT = 300; combatHeal(lv, p, 6);
    lv.fx->message(p.id, "Second wind!", "good"); lv.fx->soundUi(p.id, "heal");
    sfx::blockBurst(lv, p.pos + vec3(0, 1, 0), "sparkle", 14, 1.2f);
  }
}

float magicOutgoingDamage(Level& lv, Player& p, float amount, const json& src) {
  std::string t = src.value("type", "");
  if (amount <= 0 || (t != "magic" && t != "spell")) return amount;
  std::string el = src.value("element", "");
  if (hasTal(p, "focus") && (el == "fire" || el == "frost")) amount *= 1.15f;
  if (hasTal(p, "overload") && rnd() < 0.15) { amount *= 2; lv.fx->particles("text", p.pos + vec3(0, 2.1f, 0), 1, json{{"text", "Overload!"}, {"col", "#c8a0ff"}}); }
  return amount;
}

// ---- item uses ----------------------------------------------------------------------------------------
bool magicItemUse(Level& lv, Player& p, const std::string& key, ivec3 at, int face) {
  if (key.empty() || p.held().id != key) return true;
  ItemStack& h = p.held();
  const ItemDef* d = h.def();
  if (!d) return true;
  // a block you can use (a chest, a table, a station, a door, a bed) is used, not the thing in your hand
  if (face >= 0 && !p.sneaking) {
    const BlockDef& b = reg().block(lv.world(p.realm).getBlock(at.x, at.y, at.z));
    if (b.has("container") || b.has("table") || b.has("door") || b.has("station") || b.has("questBoard") || b.has("bed") || b.has("furnace")) return true;
  }
  if (d->has("tome")) { learnTome(lv, p, h); return false; }
  if (d->has("staff")) {
    if (p.sneaking) { cycleSpell(lv, p); return false; }
    castSpell(lv, p);
    return false;
  }
  if ((key == "glass_bottle" || key == "potion_water") && fillBottle(lv, p, h, at, face)) return false;
  if (potionInfo(key)) { drinkPotion(lv, p, h); return false; }
  if (d->has("xpBottle")) { breakXpBottle(lv, p, h); return false; }
  if (key == "milk_bucket") { clearEffects(p); magicState(lv, p).poison = 0; }
  return true;
}

bool MagicSystem::onItemUse(const json& a) {
  if (!a.is_array() || a.size() < 6) return true;
  Player* p = playerNamed(lv, a[0].is_string() ? a[0].get<std::string>() : "");
  if (!p || p->ghost) return true;
  std::string key = a[1].is_string() ? a[1].get<std::string>() : "";
  ivec3 at(a[2].is_number() ? a[2].get<int>() : 0, a[3].is_number() ? a[3].get<int>() : 0, a[4].is_number() ? a[4].get<int>() : 0);
  return magicItemUse(lv, *p, key, at, a[5].is_number() ? a[5].get<int>() : -1);
}

// the server side of a guest's spells
bool MagicSystem::onNetHit(const json& a) {
  if (!a.is_array() || a.size() < 4) return true;
  Entity* e = lv.entity(a[1].get<u32>());
  const json& src = a[3];
  if (!e || !src.is_object() || src.value("type", "") != "magic") return true;
  std::string el = src.value("element", "");
  if (el == "fire" && !isFireImmune(*e)) setMobStatus(lv, *e, "burnT", 3);
  if (el == "frost") setMobStatus(lv, *e, "slowT", 3);
  return true;   // the plain hit goes ahead
}
bool MagicSystem::onNetAction(const json& a) {
  if (!a.is_array() || a.size() < 3 || !a[1].is_string()) return true;
  std::string n = a[1];
  const json& args = a[2];
  if (n == "magicMobset" && args.is_object()) {
    Entity* e = lv.entity(args.value("id", 0u));
    if (e && !e->isPlayer() && args.contains("f") && args["f"].is_object())
      for (auto& [k, v] : args["f"].items()) if (v.is_number() && (k == "rootT" || k == "slowT" || k == "burnT" || k == "poisonT")) setMobStatus(lv, *e, k.c_str(), std::min(30.f, v.get<float>()));
    return false;
  }
  if (n == "magicFamiliar" && args.is_object()) {
    if (Player* p = playerNamed(lv, a[0].is_string() ? a[0].get<std::string>() : "")) spawnFamiliar(lv, *p, clampv(args.value("power", 1.f), 0.5f, 4.f));
    return false;
  }
  if (n == "magicCast" && args.is_object()) {   // everyone sees a guest's spells
    json o = args; o["m"] = "cast"; o["relay"] = true;
    lv.fx->particles(args.value("fx", "mana_arcane"), vec3(args.value("x", 0.f), args.value("y", 0.f), args.value("z", 0.f)), 1, o);
    return false;
  }
  return true;
}
void MagicSystem::onMobDeath(const json& a) {
  if (lv.isRemote() || !a.is_array() || a.empty() || !a[0].is_object()) return;
  Entity tmp; tmp.type = a[0].value("type", ""); tmp.pos = vec3(a[0].value("x", 0.f), a[0].value("y", 0.f), a[0].value("z", 0.f)); tmp.realm = a[0].value("realm", 0);
  if (Entity* e = lv.entity(a[0].value("id", 0u))) { tmp.data = e->data; tmp.realm = e->realm; }
  onGreatFoeDied(lv, tmp);
}

// ---- ticking --------------------------------------------------------------------------------------------
void MagicSystem::tick(Level& l, double dt) {
  for (size_t i = 0; i < lv.entities().size(); i++) {
    Entity* e = lv.entities()[i].get();
    if (e->removed || e->ghost) continue;
    if (e->isPlayer()) tickPlayer(*static_cast<Player*>(e), dt);
    else if (!lv.isRemote()) tickMob(*e, dt);
  }
  for (auto it = st.begin(); it != st.end();) { Entity* e = lv.entity(it->first); if (!e || e->removed) it = st.erase(it); else ++it; }
  if (lv.isRemote()) return;
  brewAcc += dt;
  if (brewAcc < 0.1) return;
  double bdt = brewAcc; brewAcc = 0;
  for (auto& [r, wp] : lv.allRealms()) {
    World& w = *wp;
    std::vector<ivec3> changed;
    for (auto& [key, ch] : w.chunks()) {
      if (ch->blockEntities.empty()) continue;
      for (auto& [i, e] : ch->blockEntities) {
        if (!e.is_object() || e.value("type", "") != "brew") continue;
        int x = ch->worldX() + (i & 15), z = ch->worldZ() + ((i >> 4) & 15), y = ch->baseY() + (i >> 8);
        if (reg().block(ch->get(i & 15, i >> 8, (i >> 4) & 15)).key != "brewing_stand") continue;
        if (tickBrew(lv, w, e, bdt, x, y, z)) changed.push_back({x, y, z});
      }
    }
    for (auto& c : changed) w.blockEntityChanged(c.x, c.y, c.z);
  }
}

void MagicSystem::tickPlayer(Player& p, double dt) {
  MagicState& m = state(p);
  if (m.castCd > 0) m.castCd -= (float)dt;
  float max = manaMax(p);
  if (m.mana < 0) m.mana = max;
  if (m.mana < max) m.mana = std::min(max, m.mana + manaRegen(p) * (float)dt);
  if (m.mana > max) m.mana = max;
  if (m.wardT > 0) { m.wardT -= (float)dt; if (m.wardT <= 0) m.ward = 0; }
  if ((m.saveT -= (float)dt) <= 0) { m.saveT = 1; p.stats["mana"] = std::round(m.mana * 10) / 10; }
  if (m.secondWindT > 0) m.secondWindT -= (float)dt;
  tickEffects(lv, p, dt);
  if (p.dead || p.hp <= 0) { m.lightT = m.levT = m.stoneT = 0; m.ward = 0; return; }
  if (m.lightT > 0) {
    m.lightT -= (float)dt;
    float a = p.yaw + PI * 0.72f;
    vec3 want(p.pos.x - std::sin(a) * 0.75f, p.pos.y + 2.05f + std::sin((float)lv.info.time * 2.1f) * 0.12f, p.pos.z - std::cos(a) * 0.75f);
    if (!m.witchSet) { m.witch = want; m.witchSet = true; }
    m.witch += (want - m.witch) * std::min(1.f, 0.12f * (float)dt * 60);
    if (m.lightT <= 0) { m.witchSet = false; lv.fx->message(p.id, "Your witchlight fades", ""); }
  }
  if (m.levT > 0) {
    m.levT -= (float)dt; p.fallDist = 0;
    float want = m.jumpHeld ? 3.4f : p.sneaking ? -2.6f : 0;
    if (!p.onGround || want > 0) p.vel.y = want + 32.f * (float)dt;   // the controller takes gravity off again
    if (m.levT <= 0) {
      SeamlessSystem* ss = lv.system<SeamlessSystem>();
      if (auto* s = ss ? ss->state(p.id) : nullptr) s->slowFallT = std::max(s->slowFallT, 5.f); else m.slowFallT = std::max(m.slowFallT, 5.f);
      lv.fx->message(p.id, "Your levitation fades \xE2\x80\x94 the air lets you down gently", "");
    }
  }
  if (m.slowFallT > 0) {   // (only when the stacked-worlds system isn't there to do it: worldupdate.js)
    m.slowFallT -= (float)dt; p.fallDist = 0;
    if (p.vel.y < -5) { p.vel.y += (-5 - p.vel.y) * std::min(1.f, (float)dt * 5); if (p.vel.y > -7) p.vel.y = -5; }
    if (p.onGround) m.slowFallT = 0;
  }
  if (m.stoneT > 0) { m.stoneT -= (float)dt; if (m.stoneT <= 0) lv.fx->message(p.id, "Your stoneskin crumbles away", ""); }
  rpgTick(lv, p, dt);
}

void MagicSystem::tickMob(Entity& e, double dt) {
  if (!isMob(e) || !isAlive(e)) return;
  auto dec = [&](const char* k) { float t = mobStatus(e, k); if (t > 0) { t -= (float)dt; if (t <= 0) e.data.erase(k); else e.data[k] = t; } return t; };
  float slow = dec("slowT"); (void)slow;
  if (dec("rootT") > 0) { e.vel.x = 0; e.vel.z = 0; }
  if (mobStatus(e, "burnT") > 0) {
    float acc = e.data.value("_burnTick", 0.f) + (float)dt;
    if (e.inWater) e.data.erase("burnT"); else dec("burnT");
    if (acc > 0.8f) { acc = 0; if (!isFireImmune(e)) combatHurt(lv, e, 1, json{{"type", "burn"}}); }
    e.data["_burnTick"] = acc;
  }
  if (mobStatus(e, "poisonT") > 0) {
    dec("poisonT");
    float acc = e.data.value("_pT", 0.f) + (float)dt;
    if (acc >= 1.25f) { acc = 0; if (e.hp > 1) combatHurt(lv, e, 1, json{{"type", "poison"}, {"knock", 0}}); }
    e.data["_pT"] = acc;
  }
  // spirits (stable.js): they go when their time is up, or when their maker is far away
  if (e.data.contains("spiritT")) {
    float t = e.data.value("spiritT", 60.f) - (float)dt;
    e.data["spiritT"] = t;
    Player* o = playerNamed(lv, e.data.value("owner", ""));
    if (t <= 0 || !o || glm::length(vec2(o->pos.x - e.pos.x, o->pos.z - e.pos.z)) > 64) {
      sfx::burst(lv, e.pos + vec3(0, 0.5f, 0), "mana_frost", 24, 0.3f, json{{"vy", 1.5}, {"life", 0.7}, {"size", 0.07}, {"spread", 1}});
      e.removed = true;
    }
  }
}

// ---- alchemy value (spellcraft.js) ----------------------------------------------------------------------
double alchValue(const std::string& id) {
  const auto& A = magicData().alch;
  auto it = A.find(id);
  if (it != A.end()) return it->second;
  const ItemDef* d = reg().item(id);   // a mod's item
  if (!d) return 0;
  std::string r = d->str("rarity");
  return r == "epic" ? 6 : r == "rare" ? 3 : d->hasTool ? 1 : 0;
}

// ---- registration and the testing commands -----------------------------------------------------------------
void registerMagic() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return std::make_shared<MagicSystem>(lv); });
  registerMagicEntities();
  // (engine additions for testing — cheats, like /give)
  registerChatCommand("learn", "Learn a spell (or all)", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.player || a.empty()) return "Usage: /learn <spell|all>";
    json& sp = src.player->stats["spells"];
    if (!sp.is_array()) sp = json::array();
    int n = 0;
    for (auto& k : magicData().spellOrder) if ((a[0] == "all" || a[0] == k) && std::find(sp.begin(), sp.end(), k) == sp.end()) { sp.push_back(k); n++; }
    return n ? "Learned " + std::to_string(n) + " spell" + (n > 1 ? "s" : "") : "Nothing new";
  }, true, "<spell|all>");
  registerChatCommand("effect", "Give yourself a potion effect", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.player || !src.level || a.empty()) return "Usage: /effect <effect|clear> [seconds] [level]";
    if (a[0] == "clear") { clearEffects(*src.player); return "Effects cleared"; }
    giveEffect(*src.level, *src.player, a[0], a.size() > 1 ? std::stof(a[1]) : 60, a.size() > 2 ? std::max(0, std::stoi(a[2]) - 1) : 0);
    return "";
  }, true, "<effect|clear> [seconds] [level]");
  registerChatCommand("enchant", "Enchant the held item", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.player || a.empty() || !magicData().enchant(a[0])) return "Usage: /enchant <enchantment> [level]";
    ItemStack& h = src.player->held();
    if (h.empty()) return "Hold something to enchant";
    if (!h.tag.is_object()) h.tag = json::object();
    h.tag["en"][a[0]] = a.size() > 1 ? clampv(std::stoi(a[1]), 1, 10) : 1;
    return "Enchanted: " + enchName(a[0], h.tag["en"][a[0]]);
  }, true, "<enchantment> [level]");
  registerChatCommand("skillxp", "Add skill experience", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.player || !src.level || a.size() < 2) return "Usage: /skillxp <skill> <xp>";
    bool creative = src.player->mode == "creative";
    if (creative) src.player->mode = "survival";
    skillGain(*src.level, *src.player, a[0], std::stod(a[1]));
    if (creative) src.player->mode = "creative";
    return a[0] + " is level " + std::to_string(skillLv(*src.player, a[0]));
  }, true, "<skill> <xp>");
}

} // namespace dq
