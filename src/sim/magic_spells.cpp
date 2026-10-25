// sim/magic_spells.cpp — casting: game/magic.js castSpell (ten spells), arcana.js (six more, the robes), paths.js
// (the schools: half a percent a level, a sixth less mana from 50; the Mage path), rpg.js (Thrift, Archmage) and
// spellcraft.js castNew (thirteen more, bare-handed casting, per-spell cooldowns). Numbers are the original's;
// so are its quirks (Arcane Might counts twice for the newer spells, Archmage's free cast earns no school xp).
#include "sim/magic.h"
#include "sim/magic_fx.h"
#include "sim/magic_entities.h"
#include "sim/effects.h"
#include "sim/enchanting.h"
#include "sim/rpg.h"
#include "world/world.h"

namespace dq {

static double rnd() { return mrand(); }

struct CastCtx {
  vec3 look{0}, eye{0};
  float cd = 0.6f;
  bool ok = true;
  json bolts = json::array();   // [[x,y,z,vx,vy,vz]...] for every machine to draw
  u32 target = 0;
};

static vec3 lookVec(const Player& p) { return vec3(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch)); }
static void toast(Level& lv, Player& p, const std::string& t, const std::string& k = "") { lv.fx->message(p.id, t, k); }
static void ui(Level& lv, Player& p, const char* s) { lv.fx->soundUi(p.id, s); }
static int iround(double v) { return (int)std::floor(v + 0.5); }

// the held tool wears (local.js wearHeld + enchanting.js Unbreaking + skills.js Smithing)
static void wearHeld(Level& lv, Player& p, int n) {
  ItemStack& h = p.held();
  const ItemDef* d = h.def();
  if (h.empty() || !d || !d->hasTool || d->tool.durability <= 0 || p.creative()) return;
  int k = 0;
  for (int i = 0; i < n; i++) if (!enchUnbreakingSaves(h)) k++;
  if (k > 0 && smithSavesWear(p)) k = 0;
  h.d += k;
  if (h.d >= d->tool.durability) { toast(lv, p, "Your " + d->name + " broke", "warn"); lv.fx->sound("break_tool", p.pos + vec3(0, 1.4f, 0), 12); h.clear(); }
}

// the circle under your feet, the sparks at the staff, and every other machine seeing it (and its bolts)
static void castFx(Level& lv, Player& p, const SpellDef& S, CastCtx& c, const std::string& k) {
  sfx::burst(lv, c.eye + c.look * 0.8f - vec3(0, 0.2f, 0), S.fx, 14, 0.8f, json{{"life", 0.4}});
  json o{{"m", "cast"}, {"s", k}, {"caster", p.name}, {"b", c.bolts}, {"t", c.target}, {"x", p.pos.x}, {"y", p.pos.y}, {"z", p.pos.z}, {"fx", S.fx}};
  lv.fx->particles(S.fx, p.pos, 1, o);
  if (lv.isRemote() && lv.net) lv.net->action(p, "magicCast", o);
}

static void schoolXp(Level& lv, Player& p, const std::string& sc, double xp, int before) {
  json& S = p.stats["schools"];
  if (!S.is_object()) S = json::object();
  S[sc] = S.value(sc, 0.0) + xp;
  int after = schoolLv(p, sc);
  if (after > before) {
    std::string n = magicData().schoolName(sc);
    toast(lv, p, n + " magic level " + std::to_string(after) + "!", "good");
    if (after == 50) toast(lv, p, n + " spells now cost a sixth less mana", "epic");
  }
}
static void thriftAndArchmage(Level& lv, Player& p, MagicState& m, float spent) {
  if (spent <= 0 || p.creative()) return;
  if (hasPerk(p, "magic", 90) && rnd() < 0.1) { m.mana += spent; lv.fx->particles("text", p.pos + vec3(0, 2.1f, 0), 1, json{{"text", "Free!"}, {"col", "#c8a0ff"}}); }
  else if (hasPerk(p, "magic", 60)) m.mana += spent * 0.15f;
}

static const std::unordered_set<std::string> ARCANA = {"missiles", "regrowth", "haste", "fireball", "icenova", "guardian"};
static const std::unordered_set<std::string> OLD = {"firebolt", "frost", "light", "heal", "levitate", "stoneskin", "blink", "familiar", "ward", "lightning"};

bool castSpell(Level& lv, Player& p) {
  const MagicData& md = magicData();
  MagicState& m = magicState(lv, p);
  const json* staff = heldStaff(p);
  std::string k = activeSpell(p);
  bool newer = !k.empty() && !OLD.count(k) && !ARCANA.count(k);
  if (!staff && !(newer && m.bare)) return false;
  if (k.empty()) { toast(lv, p, "You know no spells yet \xE2\x80\x94 read a spell tome"); return true; }
  const SpellDef* S = md.spell(k);
  if (!S || m.castCd > 0) return true;
  if (m.mana < 0) m.mana = manaMax(p);
  i64 now = unixMillis();
  json& cds = p.stats["spellCd"];
  if (!cds.is_object()) cds = json::object();
  if (newer && cds.value(k, (i64)0) > now) {
    toast(lv, p, S->name + " is ready in " + std::to_string((cds.value(k, (i64)0) - now + 999) / 1000) + " s", "warn");
    m.castCd = 0.4f; return true;
  }
  bool cr = p.creative();
  if (m.mana < S->mana && !cr) { toast(lv, p, "Not enough mana", "warn"); ui(lv, p, "spell_fizzle"); m.castCd = 0.4f; return true; }
  ItemStack& held = p.held();
  json st = staff ? *staff : json{{"power", 0.7}};
  int arcane = enchLv(held, "arcane");
  float stPower = st.value("power", 1.f) * (staff ? 1 + 0.15f * arcane : 1.f);   // enchanting.js: heldStaff with Arcane Might
  std::string sc = S->school;
  int slv = schoolLv(p, sc), lvl = skillLv(p, "magic");
  float skill = 1 + (lvl - 1) / 98.f * 0.6f, school = 1 + (slv - 1) * 0.005f, robe = robeBoost(p, k);
  CastCtx c;
  c.look = lookVec(p); c.eye = p.pos + vec3(0, 1.55f, 0);
  float m0 = m.mana, cd0 = m.castCd;
  if (newer) {
    float power = stPower * school * skill * robe * (1 + 0.15f * arcane);
    castNewSpell(lv, p, k, st, power, c);
    if (!c.ok) { m.castCd = 0.3f; return true; }
    if (!cr) {
      m.mana -= S->mana;
      thriftAndArchmage(lv, p, m, (float)S->mana);
      if (slv >= 50) m.mana = std::min(manaMax(p), m.mana + S->mana / 6.f);
    }
    if (S->cd > 0) cds[k] = now + (i64)(S->cd * 1000);
    m.castCd = c.cd;
    if (!m.bare) wearHeld(lv, p, 1);
    double xp = std::max(S->mana, 2) * 6;
    skillGain(lv, p, "magic", xp);
    schoolXp(lv, p, sc, xp, slv);
    sfx::circle(lv, p.pos, S->fx);
    castFx(lv, p, *S, c, k);
    m.lastSpell = k;
    return true;
  }
  float power = stPower * school * robe * skill * (1 + gearBonus(held, 0.2f));
  if (ARCANA.count(k)) power *= 1 + 0.15f * arcane;
  bool ok = ARCANA.count(k) ? castNewSpell(lv, p, k, st, power, c) : castOldSpell(lv, p, k, st, power, c);
  (void)ok;
  if (!c.ok) { m.castCd = 0.3f; return true; }
  if (!cr) m.mana -= S->mana;
  m.castCd = ARCANA.count(k) ? c.cd : k == "lightning" ? 2.2f : 0.45f;
  wearHeld(lv, p, 1);
  skillGain(lv, p, "magic", S->mana * 6.0);
  if (!held.empty()) gearGain(lv, p, held, 2);
  thriftAndArchmage(lv, p, m, cr ? 0.f : (float)S->mana);
  sfx::circle(lv, p.pos, S->fx);
  castFx(lv, p, *S, c, k);
  m.lastSpell = k;
  // paths.js: the school grows with what was really spent; the Mage gets a tenth back; a sixth back from 50
  float spent = m0 - m.mana;
  if (spent > 0) {
    schoolXp(lv, p, sc, S->mana * 6.0, slv);
    float back = 0;
    if (onPath(p, "mage")) back += spent * 0.1f;
    if (slv >= 50) back += spent / 6;
    if (back > 0 && !cr) m.mana = std::min(manaMax(p), m.mana + back);
    if (hasTal(p, "swift") && m.castCd > cd0) m.castCd *= 0.8f;
  }
  return true;
}

// ---- the first ten (magic.js) -----------------------------------------------------------------------------
static bool solidAt(World& w, vec3 v) { return reg().tSolid[w.getBlock(ifloor(v.x), ifloor(v.y), ifloor(v.z))]; }

bool castOldSpell(Level& lv, Player& p, const std::string& k, const json& st, float power, CastCtx& c) {
  const SpellDef& S = *magicData().spell(k);
  World& w = lv.world(p.realm);
  vec3 L = c.look, E = c.eye;
  if (k == "firebolt" || k == "frost") {
    float mul = st.value(k == "firebolt" ? "fire" : "frost", 1.f), sp = 26;
    auto b = std::make_unique<SpellBolt>();
    b->realm = p.realm; b->spell = k; b->caster = p.id;
    b->pos = vec3(E.x + L.x * 0.6f, E.y + L.y * 0.6f - 0.25f, E.z + L.z * 0.6f); b->vel = L * sp;
    b->dmg = (float)iround((k == "firebolt" ? 6 : 4.5) * power * mul);
    c.bolts.push_back({b->pos.x, b->pos.y, b->pos.z, b->vel.x, b->vel.y, b->vel.z});
    lv.spawn(std::move(b));
    lv.fx->sound(k == "firebolt" ? "spell_fire" : "spell_frost", E);
  } else if (k == "heal") {
    if (p.hp >= p.maxHp) { toast(lv, p, "You are not hurt"); c.ok = false; return false; }
    combatHeal(lv, p, (float)iround(5 * power));
    sfx::spiral(lv, p.pos + vec3(0, 0.1f, 0), S.fx, 48, json{{"r", 0.75}, {"h", 2.1}, {"turns", 3}, {"life", 1.1}});
    sfx::burst(lv, p.pos + vec3(0, 1, 0), S.fx, 20, 0.5f, json{{"life", 1.4}, {"vy", 0.9}, {"spread", 1.1}, {"size", 0.06}, {"flutter", 1}});
    sfx::ring(lv, p.pos + vec3(0, 0.06f, 0), S.fx, 0.3f, 24, json{{"speed", 2.2}, {"life", 0.6}, {"flat", true}});
    ui(lv, p, "spell_heal");
  } else if (k == "blink") {
    int max = 10 + iround(4 * power);
    bool found = false; vec3 best;
    for (float d = 1.5f; d <= max; d += 0.5f) {
      vec3 v = E + L * d;
      if (solidAt(w, v)) break;
      int x = ifloor(v.x), z = ifloor(v.z);
      for (int fy = ifloor(v.y - 1.4f); fy >= ifloor(v.y - 3); fy--)
        if (reg().tSolid[w.getBlock(x, fy - 1, z)] && !reg().tSolid[w.getBlock(x, fy, z)] && !reg().tSolid[w.getBlock(x, fy + 1, z)]) { best = vec3(x + 0.5f, (float)fy, z + 0.5f); found = true; break; }
    }
    if (!found) { toast(lv, p, "Nowhere to blink to"); c.ok = false; return false; }
    lv.fx->particles(S.fx, p.pos, 36, json{{"m", "vortex"}, {"r", 1.1}});
    sfx::beam(lv, p.pos + vec3(0, 1, 0), best + vec3(0, 1, 0), S.fx);
    p.pos = best; p.vel = vec3(0); p.fallDist = 0;
    sfx::burst(lv, p.pos + vec3(0, 1, 0), S.fx, 40, 2, json{{"life", 0.6}, {"spread", 0.6}});
    sfx::ring(lv, p.pos + vec3(0, 0.06f, 0), S.fx, 0.3f, 24, json{{"speed", 2.6}, {"life", 0.5}, {"flat", true}});
    ui(lv, p, "spell_blink");
  } else if (k == "light") {
    MagicState& m = magicState(lv, p);
    m.lightT = (float)iround(100 + 40 * power);
    sfx::spiral(lv, p.pos + vec3(0, 0.4f, 0), S.fx, 30, json{{"r", 0.5}, {"h", 1.6}, {"turns", 2}, {"life", 0.8}});
    ui(lv, p, "spell_heal"); toast(lv, p, "A witchlight follows you", "good");
  } else if (k == "levitate") {
    MagicState& m = magicState(lv, p);
    m.levT = (float)iround(10 + 6 * power); p.fallDist = 0;
    sfx::ring(lv, p.pos + vec3(0, 0.06f, 0), S.fx, 0.4f, 28, json{{"speed", 2}, {"life", 0.7}, {"flat", true}});
    sfx::burst(lv, p.pos + vec3(0, 0.3f, 0), "mana_arcane", 24, 0.8f, json{{"life", 0.8}, {"vy", 1.5}, {"spread", 0.8}});
    ui(lv, p, "spell_ward"); toast(lv, p, "Levitating (" + std::to_string((int)m.levT) + " s) \xC2\xB7 jump to rise, sneak to sink", "good");
  } else if (k == "stoneskin") {
    MagicState& m = magicState(lv, p);
    m.stoneT = (float)iround(45 + 15 * power);
    lv.fx->particles(S.fx, p.pos, 40, json{{"m", "implode"}, {"r", 0.9}, {"h", 1.9}});
    sfx::blockBurst(lv, p.pos + vec3(0, 1, 0), "stone", 12, 1.2f);
    ui(lv, p, "spell_ward"); toast(lv, p, "Stoneskin (" + std::to_string((int)m.stoneT) + " s)", "good");
  } else if (k == "familiar") {
    if (lv.isRemote()) { if (lv.net) lv.net->action(p, "magicFamiliar", json{{"power", power}}); }
    else spawnFamiliar(lv, p, power);
    ui(lv, p, "spell_heal");
  } else if (k == "ward") {
    MagicState& m = magicState(lv, p);
    m.ward = (float)iround(6 * power); m.wardT = 30;
    lv.fx->particles(S.fx, p.pos + vec3(0, 1, 0), 60, json{{"m", "sphere"}, {"r", 1.1}});
    sfx::ring(lv, p.pos + vec3(0, 0.06f, 0), S.fx, 1.2f, 30, json{{"speed", -0.8}, {"life", 0.8}, {"flat", true}});
    ui(lv, p, "spell_ward"); toast(lv, p, "Warded (" + std::to_string((int)m.ward) + " harm)", "good");
  } else if (k == "lightning") {
    bool found = false; vec3 at;
    for (float d = 2; d <= 40 && !found; d += 0.5f) {
      vec3 v = E + L * d;
      for (auto& e : lv.entities()) if (e->realm == p.realm && isSpellTarget(*e) && std::abs(e->pos.x - v.x) < e->w + 0.4f && std::abs(e->pos.z - v.z) < e->w + 0.4f && v.y > e->pos.y - 0.2f && v.y < e->pos.y + e->h + 0.3f) { at = e->pos; found = true; break; }
      if (!found && solidAt(w, v)) { at = vec3(v.x, (float)ifloor(v.y) + 1, v.z); found = true; }
    }
    if (!found) { toast(lv, p, "Nothing there to strike"); c.ok = false; return false; }
    if (!lv.isRemote()) { auto bolt = createEntity("lightning_bolt"); bolt->type = "lightning_bolt"; bolt->realm = p.realm; bolt->pos = at; bolt->netLocal = false; lv.spawn(std::move(bolt)); }
    lv.fx->sound("spell_zap", at, 80);
    lv.fx->particles("flash", at, 1, json{{"t", 0.25}});
    float dmg = (float)iround(9 * power);
    for (auto& e : lv.entities()) if (e->realm == p.realm && isSpellTarget(*e) && glm::length(vec2(e->pos.x - at.x, e->pos.z - at.z)) < 2.6f && std::abs(e->pos.y - at.y) < 3) {
      combatHurt(lv, *e, dmg, json{{"type", "magic"}, {"element", "lightning"}, {"knock", 2}}, &p);
      setMobStatus(lv, *e, "burnT", 3, &p);
    }
    sfx::burst(lv, at + vec3(0, 0.5f, 0), "mana_light", 50, 3.5f, json{{"life", 0.5}});
    sfx::burst(lv, at + vec3(0, 0.2f, 0), "mana_arcane", 30, 2, json{{"life", 1}, {"size", 0.06}, {"grav", 3}});
    sfx::ring(lv, at + vec3(0, 0.06f, 0), "mana_light", 0.4f, 32, json{{"speed", 4}, {"life", 0.5}, {"flat", true}});
    for (int i = 0; i < 8; i++) sfx::flame(lv, at + vec3(((float)rnd() - 0.5f) * 1.5f, 0.2f, ((float)rnd() - 0.5f) * 1.5f));
  }
  return c.ok;
}

void spawnFamiliar(Level& lv, Player& p, float power) {
  World& w = lv.world(p.realm);
  std::vector<Entity*> mine;   // one at a time (two for a master)
  for (auto& e : lv.entities()) if (!e->removed && e->type == "spirit_wolf" && e->data.value("owner", "") == p.name) mine.push_back(e.get());
  size_t max = power >= 2 ? 2 : 1;
  while (mine.size() >= max) { mine.front()->data["spiritT"] = 0; mine.erase(mine.begin()); }
  vec3 L = lookVec(p), at = p.pos + vec3(L.x * 1.5f, 0.2f, L.z * 1.5f);
  if (solidAt(w, at + vec3(0, 0.3f, 0))) at = p.pos + vec3(0, 0.2f, 0);
  if (Entity* wolf = spawnCreature(lv, "spirit_wolf", p.realm, at)) {
    wolf->data["owner"] = p.name; wolf->data["tamed"] = true; wolf->data["kept"] = false;
    wolf->data["spiritT"] = 45 + 30 * power; wolf->data["spiritDmg"] = iround(4 + 2 * power);
    wolf->hp = wolf->maxHp = (float)iround(26 * power); wolf->yaw = p.yaw;
    lv.fx->particles("mana_frost", at, 30, json{{"m", "implode"}, {"r", 0.6}, {"h", 1.2}, {"vy", 1.2}});
    lv.fx->sound("wolf", at);
  }
}

// ---- the newer ones (arcana.js, spellcraft.js) ----------------------------------------------------------------
struct Gaze { Entity* e = nullptr; bool at = false; vec3 pos{0}; };
static Gaze gaze(Level& lv, Player& p, vec3 E, vec3 L, float max) {
  World& w = lv.world(p.realm);
  for (float d = 1.5f; d <= max; d += 0.5f) {
    vec3 v = E + L * d;
    for (auto& e : lv.entities()) if (e->realm == p.realm && isFoe(*e) && std::abs(e->pos.x - v.x) < e->w + 0.4f && std::abs(e->pos.z - v.z) < e->w + 0.4f && v.y > e->pos.y - 0.2f && v.y < e->pos.y + e->h + 0.3f) return {e.get(), true, e->pos};
    if (solidAt(w, v)) return {nullptr, true, vec3(v.x, (float)ifloor(v.y) + 1, v.z)};
  }
  return {};
}
static std::vector<Player*> playersNear(Level& lv, Player& p, float r, bool horizontal) {
  std::vector<Player*> out;
  for (auto& e : lv.entities()) {
    if (e->removed || !e->isPlayer() || e->realm != p.realm) continue;
    Player* q = static_cast<Player*>(e.get());
    if (q->dead) continue;
    float d = horizontal ? glm::length(vec2(q->pos.x - p.pos.x, q->pos.z - p.pos.z)) : glm::distance(q->pos, p.pos);
    if (d < r) out.push_back(q);
  }
  return out;
}
static Entity* raiseSpirit(Level& lv, Player& p, const std::string& type, const char* tag, vec3 at, float secs, float hp) {
  for (auto& e : lv.entities()) if (!e->removed && e->data.value(tag, "") == p.name) { e->removed = true; }
  Entity* m = spawnCreature(lv, type, p.realm, at);
  if (!m) return nullptr;
  m->data["owner"] = p.name; m->data["tamed"] = true; m->data["kept"] = false; m->data["guardianOf"] = p.name; m->data[tag] = p.name;
  m->data["spiritT"] = secs; m->data["spirit"] = true; m->data["noDrops"] = true; m->data["xp"] = 0; m->data["friendly"] = true;
  m->hp = m->maxHp = hp;
  return m;
}

bool castNewSpell(Level& lv, Player& p, const std::string& k, const json& st, float power, CastCtx& c) {
  const SpellDef& S = *magicData().spell(k);
  vec3 L = c.look, E = c.eye;
  float fire = st.value("fire", 1.f), frost = st.value("frost", 1.f), storm = st.value("storm", 1.f);
  auto fail = [&](const char* msg) { toast(lv, p, msg); c.ok = false; return false; };
  auto foes = [&](float r) { std::vector<Entity*> v; for (auto& e : lv.entities()) if (e->realm == p.realm && isFoe(*e) && glm::length(vec2(e->pos.x - p.pos.x, e->pos.z - p.pos.z)) < r) v.push_back(e.get()); return v; };
  // ---- arcana.js
  if (k == "missiles") {
    Entity* best = nullptr; float bs = -1;
    for (Entity* e : foes(24)) {
      float dx = e->pos.x - p.pos.x, dz = e->pos.z - p.pos.z, l = std::max(1e-3f, std::sqrt(dx * dx + dz * dz)), dot = (dx * L.x + dz * L.z) / l;
      if (dot > 0.6f && dot - l / 60 > bs) { bs = dot - l / 60; best = e; }
    }
    for (int i = 0; i < 3; i++) {
      float a = (i - 1) * 0.35f, sp = 18;
      auto b = std::make_unique<SpellBolt>();
      b->realm = p.realm; b->spell = "missiles"; b->caster = p.id; b->target = best ? best->id : 0;
      b->pos = vec3(E.x + L.x * 0.6f, E.y - 0.2f, E.z + L.z * 0.6f);
      b->vel = vec3((L.x * std::cos(a) - L.z * std::sin(a)) * sp, L.y * sp + 1.5f, (L.z * std::cos(a) + L.x * std::sin(a)) * sp);
      b->dmg = (float)iround(2.6 * power);
      c.bolts.push_back({b->pos.x, b->pos.y, b->pos.z, b->vel.x, b->vel.y, b->vel.z});
      lv.spawn(std::move(b));
    }
    c.target = best ? best->id : 0;
    lv.fx->sound("spell_blink", E); c.cd = 0.7f;
  } else if (k == "fireball") {
    float sp = 20;
    auto b = std::make_unique<SpellBolt>();
    b->realm = p.realm; b->spell = "fireball"; b->caster = p.id;
    b->pos = vec3(E.x + L.x * 0.6f, E.y + L.y * 0.6f - 0.25f, E.z + L.z * 0.6f); b->vel = L * sp;
    b->dmg = (float)iround(8 * power * fire);
    c.bolts.push_back({b->pos.x, b->pos.y, b->pos.z, b->vel.x, b->vel.y, b->vel.z});
    lv.spawn(std::move(b));
    lv.fx->sound("spell_fire", E); c.cd = 1.6f;
  } else if (k == "icenova") {
    float dmg = (float)iround(5 * power * frost);
    for (Entity* e : foes(6.5f)) { combatHurt(lv, *e, dmg, json{{"type", "magic"}, {"element", "frost"}, {"knock", 4}}, &p); setMobStatus(lv, *e, "slowT", 6, &p); }
    for (float r = 0.5f; r <= 6; r += 1.1f) sfx::ring(lv, p.pos + vec3(0, 0.3f, 0), "mana_frost", r, iround(10 + r * 6), json{{"speed", 2}, {"life", 0.6}, {"flat", true}, {"size", 0.09}});
    lv.fx->sound("spell_frost", p.pos + vec3(0, 1, 0)); lv.fx->sound("spell_zap", p.pos + vec3(0, 1, 0), 30); c.cd = 2.5f;
  } else if (k == "regrowth") {
    float secs = (float)iround(8 + 4 * power); int lvR = power >= 1.8f ? 1 : 0;
    for (Player* q : playersNear(lv, p, 9, true)) {
      giveEffect(lv, *q, "regen", secs, lvR, &p);
      sfx::spiral(lv, q->pos + vec3(0, 0.1f, 0), "mana_heal", 30, json{{"r", 0.7}, {"h", 2}, {"turns", 2.5}, {"life", 1}});
    }
    combatHeal(lv, p, 2); ui(lv, p, "spell_heal"); c.cd = 1.2f;
  } else if (k == "haste") {
    giveEffect(lv, p, "speed", (float)iround(30 + 10 * power), 1, &p); giveEffect(lv, p, "jump", (float)iround(30 + 10 * power), 0, &p);
    sfx::spiral(lv, p.pos + vec3(0, 0.1f, 0), "mana_light", 40, json{{"r", 0.6}, {"h", 2}, {"turns", 3}, {"life", 0.9}});
    ui(lv, p, "spell_ward"); c.cd = 1;
  } else if (k == "guardian") {
    if (lv.isRemote()) return fail("Only the host can raise a guardian (for now)");
    vec3 at(p.pos.x + L.x * 2, p.pos.y + 0.2f, p.pos.z + L.z * 2);
    if (Entity* m = raiseSpirit(lv, p, "iron_golem", "guardianOf", at, 50 + 15 * power, (float)iround(60 * power))) {
      m->data["golem"] = false;
      lv.fx->particles("mana_stone", at, 40, json{{"m", "implode"}, {"r", 1}, {"h", 2.5}, {"vy", 1.4}});
      lv.fx->sound("break_stone", at + vec3(0, 1, 0));
    }
    c.cd = 3;
  }
  // ---- spellcraft.js
  else if (k == "flames") {
    float dmg = (float)std::max(1, iround(3.5 * power * fire));
    for (auto& up : lv.entities()) {
      Entity& e = *up;
      if (e.realm != p.realm || !isFoe(e)) continue;
      vec3 d(e.pos.x - p.pos.x, e.pos.y + e.h / 2 - E.y, e.pos.z - p.pos.z);
      float l = std::max(1e-3f, glm::length(d));
      if (l > 5.5f || glm::dot(d, L) / l < 0.75f) continue;
      combatHurt(lv, e, dmg, json{{"type", "magic"}, {"element", "fire"}, {"knock", 0.6}}, &p);
      if (!lv.isRemote() && !isFireImmune(e)) setMobStatus(lv, e, "burnT", 3, &p);
    }
    lv.fx->particles("mana_fire", E + L * 0.7f - vec3(0, 0.25f, 0), 26, json{{"m", "cone"}, {"dir", {L.x, L.y, L.z}}});
    for (int i = 0; i < 4; i++) sfx::flame(lv, E + L * (1.f + i) - vec3(0, 0.3f, 0));
    lv.fx->sound("spell_fire", E); c.cd = 0.5f;
  } else if (k == "chainlight") {
    Gaze g = gaze(lv, p, E, L, 30);
    if (!g.e) return fail("No foe there to strike");
    Entity* cur = g.e;
    vec3 from(E.x + L.x * 0.6f, E.y - 0.2f, E.z + L.z * 0.6f);
    float dmg = (float)iround(7 * power * storm);
    std::unordered_set<u32> hit;
    for (int n = 0; n < 4 && cur; n++) {
      hit.insert(cur->id);
      vec3 to = cur->pos + vec3(0, cur->h * 0.6f, 0);
      sfx::beam(lv, from, to, "mana_light"); sfx::burst(lv, to, "mana_light", 18, 2, json{{"life", 0.4}});
      combatHurt(lv, *cur, std::max(1.f, dmg), json{{"type", "magic"}, {"element", "lightning"}, {"knock", 1}}, &p);
      lv.fx->sound("spell_zap", to, 40);
      from = to; dmg = (float)iround(dmg * 0.75);
      Entity* best = nullptr; float bd = 7;
      for (auto& e : lv.entities()) if (e->realm == p.realm && isFoe(*e) && !hit.count(e->id)) { float d = glm::distance(e->pos, cur->pos); if (d < bd) { bd = d; best = e.get(); } }
      cur = best;
    }
    c.cd = 1.4f;
  } else if (k == "healother") {
    std::vector<Player*> ps;
    for (Player* q : playersNear(lv, p, 10, false)) if (q != &p) ps.push_back(q);
    std::sort(ps.begin(), ps.end(), [&](Player* a, Player* b) { return glm::length(vec2(a->pos.x - p.pos.x, a->pos.z - p.pos.z)) < glm::length(vec2(b->pos.x - p.pos.x, b->pos.z - p.pos.z)); });
    Player* q = ps.empty() ? &p : ps[0];
    float amt = (float)iround(6 * power);
    if (q == &p && p.hp >= p.maxHp) return fail("Nobody here is hurt");
    if (q == &p) combatHeal(lv, p, amt);
    else {
      giveEffect(lv, *q, "heal", amt, 0, &p); giveEffect(lv, *q, "regen", 5, 0, &p);
      sfx::beam(lv, E - vec3(0, 0.3f, 0), q->pos + vec3(0, 1, 0), "mana_heal");
      toast(lv, p, "You heal " + q->name, "good");
    }
    sfx::spiral(lv, q->pos + vec3(0, 0.1f, 0), "mana_heal", 40, json{{"r", 0.7}, {"h", 2}, {"turns", 3}, {"life", 1}});
    ui(lv, p, "spell_heal"); c.cd = 1;
  } else if (k == "telekinesis") {
    if (lv.isRemote()) return fail("Only the host can call items (for now)");
    int n = 0;
    for (auto& e : lv.entities()) if (!e->removed && e->type == "item" && e->realm == p.realm && glm::distance(e->pos, p.pos) < 16) {
      sfx::beam(lv, e->pos + vec3(0, 0.3f, 0), p.pos + vec3(0, 1, 0), "mana_frost");
      e->pos = p.pos + vec3(0, 0.5f, 0); e->vel = vec3(0); e->data["pickupDelay"] = 0; n++;
    }
    if (!n) return fail("Nothing lying about to call");
    ui(lv, p, "spell_blink"); toast(lv, p, std::to_string(n) + " thing" + (n > 1 ? "s" : "") + " fly to your hand", "good");
  } else if (k == "featherfall") {
    giveEffect(lv, p, "slowfall", (float)iround(45 + 15 * power), 0, &p);
    sfx::ring(lv, p.pos + vec3(0, 0.06f, 0), S.fx, 0.4f, 24, json{{"speed", 2}, {"life", 0.6}, {"flat", true}}); ui(lv, p, "spell_ward");
  } else if (k == "waterbreath") {
    giveEffect(lv, p, "water", (float)iround(70 + 20 * power), 0, &p);
    sfx::spiral(lv, p.pos + vec3(0, 0.1f, 0), "mana_frost", 30, json{{"r", 0.6}, {"h", 2}, {"turns", 2}, {"life", 0.8}}); ui(lv, p, "spell_ward");
  } else if (k == "nightsight") {
    giveEffect(lv, p, "night", (float)iround(90 + 30 * power), 0, &p);
    sfx::burst(lv, E, "mana_light", 20, 0.6f, json{{"life", 0.7}}); ui(lv, p, "spell_heal");
  } else if (k == "invisibility") {
    giveEffect(lv, p, "invis", (float)iround(20 + 10 * power), 0, &p);
    sfx::burst(lv, p.pos + vec3(0, 1, 0), "mana_arcane", 40, 1.5f, json{{"life", 0.6}, {"spread", 0.8}}); ui(lv, p, "spell_blink");
  } else if (k == "homeport") {
    bool hasSpawn = p.spawnPoint.y >= 0;
    vec3 home = hasSpawn ? p.spawnPoint : lv.info.spawn;
    int homeRealm = hasSpawn ? p.spawnRealm : REALM_MAIN;
    if (homeRealm != p.realm || dimOfY(ifloor(home.y)) != dimOfY(ifloor(p.pos.y))) return fail("The way home is closed from here");
    if (home.y < 0) return fail("You have no home to go to");
    sfx::spiral(lv, p.pos, "mana_arcane", 60, json{{"r", 0.8}, {"h", 2.4}, {"turns", 4}, {"life", 1}});
    p.pos = vec3(home.x + (std::fmod(home.x, 1.f) == 0 ? 0.5f : 0), home.y + (hasSpawn ? 0.2f : 0), home.z + (std::fmod(home.z, 1.f) == 0 ? 0.5f : 0));
    p.vel = vec3(0); p.fallDist = 0;
    sfx::burst(lv, p.pos + vec3(0, 1, 0), "mana_arcane", 50, 2, json{{"life", 0.7}, {"spread", 0.6}});
    ui(lv, p, "spell_blink"); toast(lv, p, "Home again", "good"); c.cd = 1;
  } else if (k == "lowalch") {
    ItemStack& s = p.offhand[0];
    double v = s.empty() ? 0 : alchValue(s.id);
    if (s.empty()) return fail("Hold the thing to transmute in your off hand");
    if (!v || s.id == "emerald") { const ItemDef* d = s.def(); toast(lv, p, (d ? d->name : std::string("That")) + " cannot be transmuted"); c.ok = false; return false; }
    double bank = p.stats.value("alchBank", 0.0) + v * 0.6;
    int n = (int)std::floor(bank); bank -= n;
    p.stats["alchBank"] = bank;
    if (--s.n <= 0) s.clear();
    if (n > 0) giveOrDrop(lv, p, ItemStack("emerald", n));
    toast(lv, p, n > 0 ? "+" + std::to_string(n) + " emerald" + (n > 1 ? "s" : "") : "A little gold dust (saved toward the next emerald)", "good");
    sfx::burst(lv, vec3(E.x + L.x * 0.6f, E.y - 0.3f, E.z + L.z * 0.6f), "mana_light", 30, 1.4f, json{{"life", 0.6}});
    ui(lv, p, "levelup"); c.cd = 0.8f;
  } else if (k == "drainlife") {
    Gaze g = gaze(lv, p, E, L, 16);
    if (!g.e) return fail("No foe there to drain");
    float dmg = (float)std::max(1, iround(4 * power));
    combatHurt(lv, *g.e, dmg, json{{"type", "magic"}, {"element", "death"}, {"knock", 0}}, &p);
    combatHeal(lv, p, (float)std::max(1, iround(dmg / 2)));
    sfx::beam(lv, g.e->pos + vec3(0, g.e->h * 0.6f, 0), E - vec3(0, 0.3f, 0), "mana_heal");
    lv.fx->sound("spell_hit", g.e->pos + vec3(0, 1, 0)); ui(lv, p, "spell_heal"); c.cd = 0.8f;
  } else if (k == "entangle") {
    Gaze g = gaze(lv, p, E, L, 24);
    if (!g.at) return fail("Nothing there to take root in");
    float secs = std::round((3 + 1.5f * power) * 10) / 10, dmg = (float)std::max(1, iround(2 * power));
    for (auto& up : lv.entities()) {
      Entity& e = *up;
      if (e.realm != p.realm || !isFoe(e) || glm::length(vec2(e.pos.x - g.pos.x, e.pos.z - g.pos.z)) > 4 || std::abs(e.pos.y - g.pos.y) > 3) continue;
      combatHurt(lv, e, dmg, json{{"type", "magic"}, {"element", "nature"}, {"knock", 0}}, &p);
      setMobStatus(lv, e, "rootT", secs, &p); setMobStatus(lv, e, "slowT", secs, &p);
      sfx::spiral(lv, e.pos, "mana_heal", 24, json{{"r", 0.5}, {"h", 1.4}, {"turns", 3}, {"life", secs * 0.4}, {"vy", 0.2}});
    }
    sfx::ring(lv, g.pos + vec3(0, 0.06f, 0), "mana_heal", 0.5f, 30, json{{"speed", 3}, {"life", 0.6}, {"flat", true}});
    sfx::blockBurst(lv, g.pos + vec3(0, 0.3f, 0), "oak_leaves", 14, 1.2f);
    lv.fx->sound("spell_heal", g.pos); c.cd = 1.2f;
  } else if (k == "raiseskel") {
    if (lv.isRemote()) return fail("Only the host can raise the dead (for now)");
    vec3 at(p.pos.x + L.x * 2, p.pos.y + 0.2f, p.pos.z + L.z * 2);
    if (Entity* m = raiseSpirit(lv, p, "skeleton", "raisedBy", at, 45 + 15 * power, (float)iround(20 * power))) {
      m->data["hostile"] = false;
      sfx::burst(lv, vec3(at.x, p.pos.y + 0.5f, at.z), "mana_stone", 40, 1.4f, json{{"life", 0.8}, {"vy", 1}});
      lv.fx->sound("break_stone", at + vec3(0, 1, 0));
    }
    c.cd = 3;
  }
  return c.ok;
}

} // namespace dq
