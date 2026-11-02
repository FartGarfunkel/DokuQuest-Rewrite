// sim/creature.cpp — the creature core (game/entities.js Mob: update, hurt, die, drawing state) with the
// layers the later modules wrapped round it: dismember.js, bestiary.js, sounds2.js, monsters.js,
// quarrels.js, pets.js (hurt), nightmobs.js (jockeys, slime split), husbandry.js (kept animals).
#include <cstdlib>
#include "sim/creature.h"
#include "sim/vehicles.h"
#include "common/holiday.h"
#include "sim/hallow.h"
#include "sim/mob_combat.h"
#include "sim/physics.h"
#include "sim/player.h"
#include "core/registry.h"
#include "gen/structures.h"
#include "gen/structures_api.h"
#include "gen/generator.h"
#include <random>
#include <cstring>

namespace dq {
// (declared out here: MSVC gives a function declared inside a block the global namespace, so these never linked on Windows)
void stormFalls(Level& lv, Creature& m);   // sim/wither.cpp

// ------------------------------------------------------------------ types
static std::unordered_map<std::string, CreatureType>& typeMap() { static std::unordered_map<std::string, CreatureType> m; return m; }
static bool typesLoaded = false;

static CreatureType parseType(const std::string& key, const json& j) {
  CreatureType t; t.key = key; t.j = j;
  auto s = [&](const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
  auto f = [&](const char* k, float d) { return j.contains(k) && j[k].is_number() ? j[k].get<float>() : d; };
  auto b = [&](const char* k) { return j.contains(k) && !j[k].is_null() && j[k] != false && j[k] != 0; };
  auto list = [&](const char* k) { std::vector<std::string> v; if (j.contains(k) && j[k].is_array()) for (auto& e : j[k]) if (e.is_string()) v.push_back(e); return v; };
  t.model = s("model"); t.label = s("label"); t.voice = s("voice"); t.sound = s("sound"); t.ai = s("ai"); t.faction = s("faction");
  if (j.contains("skin") && j["skin"].is_string()) { t.skin = j["skin"].get<std::string>(); t.explicitSkin = true; }
  else { std::string m = s("model"); if (m.empty()) m = key; if (m.find(".png") != std::string::npos) t.skin = m; }
  if (j.contains("skinGlow") && j["skinGlow"].is_array()) for (auto& v : j["skinGlow"]) if (v.is_string()) t.skinGlowParts.push_back(v.get<std::string>());
  t.family = s("family"); t.element = s("element"); t.ride = s("ride");
  t.variants = list("variants"); t.food = list("food"); t.prey = list("prey");
  if (t.model.empty()) t.model = t.variants.empty() ? key : t.variants[0];
  t.hp = f("hp", 10); t.w = f("w", 0.3f); t.h = f("h", 0.9f); t.speed = f("speed", 1); t.run = f("run", 0); t.fly = f("fly", 0);
  t.swimSpeed = f("swimSpeed", 0); t.damage = f("damage", 0); t.knockResist = f("knockResist", 0); t.skittish = f("skittish", 0);
  t.glows = f("glows", 0); t.xp = (int)f("xp", 0);
#define B(n) t.n = b(#n);
  B(hostile) B(special) B(boss) B(undead) B(fireImmune) B(ranged) B(climber) B(flyer) B(hopper) B(slimy) B(swims) B(walks) B(water) B(farm)
  B(woolly) B(layer) B(floaty) B(grazer) B(pecker) B(herd) B(pack) B(protective) B(angersWhenHurt) B(stalker) B(nocturnal) B(retaliates) B(rams)
  B(spit) B(curls) B(roosts) B(perches) B(stings) B(school) B(puffs) B(playful) B(needsAir) B(playsDead) B(frogTongue) B(ambient) B(displays)
  B(bear) B(snake) B(dayNeutral) B(noPush) B(fleesSun) B(npc) B(noDismember) B(carry) B(spirit) B(phase)
#undef B
  if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() >= 4) { t.hasTint = true; for (int i = 0; i < 4; i++) t.tint[i] = j["tint"][i].get<float>(); }
  if (j.contains("drops") && j["drops"].is_array())
    for (auto& d : j["drops"]) if (d.is_array() && d.size() >= 4 && d[0].is_string() && reg().item(d[0].get<std::string>()))
      t.drops.push_back({d[0].get<std::string>(), d[1].get<int>(), d[2].get<int>(), d[3].get<float>()});
  if (j.contains("variantDefs") && j["variantDefs"].is_array())
    for (auto& v : j["variantDefs"]) if (v.is_object() && v.contains("model") && v["model"].is_string()) {
      CreatureType::VariantDef d; d.model = v["model"].get<std::string>();
      d.w = v.value("w", 1.f); d.label = v.value("label", std::string()); d.held = v.value("held", std::string());
      if (v.contains("lands") && v["lands"].is_array()) for (auto& l : v["lands"]) if (l.is_string()) d.lands.push_back(l.get<std::string>());
      d.cave = v.value("cave", false); d.surface = v.value("surface", false); d.village = v.value("village", false); d.water = v.value("water", false);
      d.dim = v.value("dim", -1);
      if (v.contains("drops") && v["drops"].is_array())
        for (auto& e : v["drops"]) if (e.is_array() && e.size() >= 4 && e[0].is_string() && reg().item(e[0].get<std::string>()))
          d.drops.push_back({e[0].get<std::string>(), e[1].get<int>(), e[2].get<int>(), e[3].get<float>()});
      t.vdefs.push_back(std::move(d));
    }
  // The original left a few creatures without a voice (their AI made its own noises, or none): give them a
  // fitting one so every creature is heard when it calls, is hit and dies. ("slime" was never a sound name.)
  if (t.sound == "slime") t.sound = "squish";
  if (t.voice.empty() && t.sound.empty()) {
    auto ends = [&](const char* s) { size_t n = strlen(s); return key.size() >= n && key.compare(key.size() - n, n, s) == 0; };
    if (ends("_dragon") || ends("_drake")) t.voice = "dragon_roar";
    else if (key == "aerwhale" || key == "zephyr") t.voice = key;
    else if (key == "swet" || key == "golden_swet") t.voice = "squish";
    else if (key == "shark") t.voice = "fish";
    else if (key == "warden_moon" || key == "warden_sun") t.voice = "warden";
    else if (key == "ember_lord" || key == "cinder_golem") t.voice = "ogre";
    else if (key == "iron_golem" || (key.size() > 6 && key.compare(key.size() - 6, 6, "_golem") == 0 && key != "cinder_golem")) t.voice = "golem";
  }
  if (j.contains("onHit") && j["onHit"].is_object()) {
    auto& o = j["onHit"]; t.onHit.any = true;
    t.onHit.slow = o.value("slow", 0.f); t.onHit.chill = o.value("chill", 0.f); t.onHit.poison = o.value("poison", 0.f); t.onHit.fire = o.value("fire", 0.f);
  }
  return t;
}
void reloadCreatureTypes() {
  // update in place, never clear: creatures hold CreatureType pointers (a world reopened in the same session
  // loads its creatures before its CreatureSystem reloads the table — clearing it left them dangling)
  auto& m = typeMap();
  if (reg().mobs.is_object()) for (auto& [k, v] : reg().mobs.items()) m[k] = parseType(k, v);
  typesLoaded = true;
}
const CreatureType* creatureType(const std::string& key) {
  if (!typesLoaded) reloadCreatureTypes();
  auto it = typeMap().find(key);
  return it == typeMap().end() ? nullptr : &it->second;
}
// a mob dressed in a PLAYER SKIN instead of its data/models.json rig. An explicit mobs.json "skin" WINS over
// the stock voxel model (herobrine is now a skinned biped); an inferred "model": "<x>.png" stays subordinate
// to any stock model, as it did before.
static bool isSkinMobKey(const std::string& k) {
  const CreatureType* T = creatureType(k);
  if (!T || T->skin.empty()) return false;
  return T->explicitSkin || !reg().models.contains(k);
}
bool isSkinMob(const std::string& key) { return isSkinMobKey(key); }
bool isCreatureType(const std::string& key) { return creatureType(key) != nullptr; }
const std::string& creatureVoice(const CreatureType& T) { return !T.voice.empty() ? T.voice : T.sound; }
static const CreatureType& stubType() { static CreatureType t = [] { CreatureType s; s.key = s.model = "?"; return s; }(); return t; }

// ------------------------------------------------------------------ helpers
// frand(): defined once in sim/content.cpp

float envDay(const Level& lv, int dim) {
  if (dim == DIM_NETHER) return 0.22f;
  if (dim == DIM_END) return 0.7f;
  // the Overworld's sun (game.js computeEnv); the Skylands share the column's sky (agreed change)
  float sh = std::sin((float)lv.dayFraction() * TAU);
  return 0.2f + 0.8f * smoothstepf(-0.2f, 0.25f, sh);
}
int dimAt(const Entity& e) { return dimOfY(clampv(ifloor(e.pos.y), 0, WORLD_H - 1)); }
bool isPawn(const Entity* e) { auto* p = dynamic_cast<const Player*>(e); return p && !p->dead && p->mode != "spectator"; }
bool pawnSneaking(const Entity* e) { auto* p = dynamic_cast<const Player*>(e); return p && p->sneaking; }
bool pawnCreative(const Entity* e) { auto* p = dynamic_cast<const Player*>(e); return p && p->creative(); }
std::vector<Entity*> pawnsOf(Level& lv, int realm) {
  std::vector<Entity*> out;
  for (auto& e : lv.entities()) if (!e->removed && e->realm == realm && isPawn(e.get())) out.push_back(e.get());
  return out;
}
Entity* nearestPawn(Level& lv, int realm, vec3 p, float maxD) {
  Entity* best = nullptr; float bd = maxD;
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != realm || !isPawn(e.get())) continue;
    float d = glm::distance(e->pos, p);
    if (d < bd) { bd = d; best = e.get(); }
  }
  return best;
}
Entity* dropItem(Level& lv, int realm, const ItemStack& st, vec3 p, bool scatter, const vec3* vel, float delay) {
  if (st.empty() || !st.def()) return nullptr;
  auto e = createEntity("item");
  e->realm = realm; e->pos = p;
  e->data["stack"] = st.toJson();
  e->vel = vec3((frand() - 0.5f) * 3, 3.5f, (frand() - 0.5f) * 3);
  if (!scatter) { e->vel.x *= 0.3f; e->vel.z *= 0.3f; e->vel.y = 2; }
  if (vel) e->vel = *vel;
  e->data["pickupDelay"] = delay > 0 ? delay : 0.6f;
  e->w = 0.15f; e->h = 0.3f;
  return lv.spawn(std::move(e));
}
// The look it comes in, by where it turns up (mobs.json variantDefs): a miner down in the dark, a frozen one in the snow,
// a farmer by a village. Any that fit, by weight; the ones that fit anywhere are always among them.
static void placeVariant(Level& lv, Creature& c) {
  const CreatureType& T = *c.T;
  if (T.vdefs.empty() || c.realm != REALM_MAIN) return;
  World& w = lv.world(c.realm);
  const int x = ifloor(c.pos.x), y = ifloor(c.pos.y), z = ifloor(c.pos.z), dim = dimOfY(y);
  const int top = w.surfaceY(x, z, dim);
  const bool cave = top > y + 3 && (w.getLight(x, y, z) >> 4) < 4;
  const bool wet = reg().tLiquid[w.getBlock(x, y, z)] == 1;
  std::string land; bool landDone = false;
  int village = -1;
  float tot = 0; std::vector<const CreatureType::VariantDef*> ok;
  for (auto& v : T.vdefs) {
    if (v.dim >= 0 && v.dim != dim) continue;
    if (v.cave && !cave) continue;
    if (v.surface && cave) continue;
    if (v.water && !wet) continue;
    if (!v.lands.empty()) {
      if (dim != DIM_OVERWORLD) continue;
      if (!landDone) { land = habitatAt(w, x, z, w.getBlock(x, y - 1, z)); landDone = true; }
      if (std::find(v.lands.begin(), v.lands.end(), land) == v.lands.end()) continue;
    }
    if (v.village) {
      if (village < 0) {
        village = 0;
        if (lv.gen) if (const StructureIndex* idx = structureIndexOf(lv.gen->forDim(dim))) if (idx->active()) {
          std::vector<const StructurePlan*> plans; idx->near(x, z, 72, plans, NEAR_PLACES);
          for (const StructurePlan* S : plans) if (S && S->type >= 0 && S->type < (int)structureTypes().size() && structureTypes()[S->type].key == "village") village = 1;
        }
      }
      if (!village) continue;
    }
    ok.push_back(&v); tot += v.w;
  }
  if (ok.empty() || tot <= 0) return;
  float q = frand() * tot;
  const CreatureType::VariantDef* pick = ok.back();
  for (auto* v : ok) if ((q -= v->w) <= 0) { pick = v; break; }
  c.variant = pick->model == T.model ? "" : pick->model;
  if (!pick->held.empty()) c.heldItem = pick->held;
}
bool setCreatureLook(Entity& e, const std::string& look) {
  auto* c = dynamic_cast<Creature*>(&e);
  if (!c) return false;
  std::string v = look; if (!reg().models.contains(v)) v = c->type + "_" + v;
  if (!reg().models.contains(v)) return false;
  c->variant = v == c->T->model ? "" : v;
  if (const auto* d = c->T->vdef(v)) if (!d->held.empty()) c->heldItem = d->held;
  return true;
}
// /looks <creature>: every look it comes in, side by side in front of you, standing still to be looked at
int spawnLookRow(Level& lv, int realm, vec3 at, float yaw, const std::string& type) {
  const CreatureType* T = creatureType(type);
  if (!T) return 0;
  std::vector<std::string> keys;
  if (!T->vdefs.empty()) for (auto& v : T->vdefs) keys.push_back(v.model); else if (!T->variants.empty()) keys = T->variants; else keys.push_back(T->model);
  const vec3 fwd(-std::sin(yaw), 0, -std::cos(yaw)), side(std::cos(yaw), 0, -std::sin(yaw));
  const float gap = std::max(1.2f, T->w * 2.6f + 0.6f);
  int n = 0;
  for (size_t i = 0; i < keys.size(); i++) {
    const vec3 p = at + fwd * (3.f + T->w * 2) + side * ((float)i - (keys.size() - 1) * 0.5f) * gap;
    Creature* c = spawnCreature(lv, realm, type, p);
    if (!c) continue;
    c->variant = keys[i] == T->model ? "" : keys[i];
    if (const auto* d = T->vdef(keys[i])) { if (!d->held.empty()) c->heldItem = d->held; }
    c->calm = true; c->yaw = yaw + PI; c->kept = false; c->data["noDrops"] = true;
    n++;
  }
  return n;
}
// /lineup <page> [per page]: every creature in turn, a page of them side by side (checking their skins and looks)
std::string spawnLineup(Level& lv, int realm, vec3 at, float yaw, int page, int per) {
  std::vector<std::string> all;
  if (reg().mobs.is_object()) for (auto& [k, v] : reg().mobs.items()) if (k != "herobrine" && isCreatureType(k)) all.push_back(k);
  std::sort(all.begin(), all.end());
  per = clampv(per, 1, 16);
  const int pages = ((int)all.size() + per - 1) / per;
  if (page < 1 || page > pages) return "Pages 1 to " + std::to_string(pages);
  const vec3 fwd(-std::sin(yaw), 0, -std::cos(yaw)), side(std::cos(yaw), 0, -std::sin(yaw));
  std::vector<std::string> row(all.begin() + (page - 1) * per, all.begin() + std::min((int)all.size(), page * per));
  float total = 0; std::vector<float> wd;
  for (auto& k : row) { const CreatureType* T = creatureType(k); float w = T ? std::max(0.8f, T->w * 2.2f + 0.5f) : 1; wd.push_back(w); total += w; }
  float x = -total * 0.5f; std::string names;
  for (size_t i = 0; i < row.size(); i++) {
    x += wd[i] * 0.5f;
    Creature* c = spawnCreature(lv, realm, row[i], at + fwd * 7.f + side * x);
    x += wd[i] * 0.5f;
    if (!c) continue;
    c->variant = ""; c->calm = true; c->yaw = yaw + PI; c->kept = false; c->data["noDrops"] = true;
    names += (names.empty() ? "" : ", ") + row[i];
  }
  return "Page " + std::to_string(page) + "/" + std::to_string(pages) + ": " + names;
}
std::string creatureLabelOf(Entity& e) {
  if (auto* c = dynamic_cast<Creature*>(&e)) return c->label();
  return std::string();
}
std::string Creature::label() const {
  if (!variant.empty()) if (const auto* v = T->vdef(variant)) if (!v->label.empty()) return v->label;
  return T->label;
}

Creature* spawnCreature(Level& lv, int realm, const std::string& type, vec3 p) {
  if (!isCreatureType(type)) return nullptr;
  auto e = createEntity(type);
  auto* c = dynamic_cast<Creature*>(e.get());
  if (!c) return nullptr;
  c->realm = realm; c->pos = p; c->prevPos = p;
  placeVariant(lv, *c);
  const CreatureType& T = *c->T;
  // husbandry.js: a land animal, once it exists, is here to stay
  if (!T.hostile && !T.water && !T.ambient && T.ai != "swimmer" && !T.roosts && type != "bee") c->kept = true;
  lv.spawn(std::move(e));
  return c;
}
void mobBurst(Level& lv, vec3 p, const std::string& kind, int n, float spread) { lv.fx->particles("burst", p, n, json{{"tex", kind}, {"spread", spread}}); }
void flame(Level& lv, vec3 p) { lv.fx->particles("flame", p, 1); }
void hearts(Level& lv, vec3 p, int n) { lv.fx->particles("heart", p, n); }
void floatText(Level& lv, u32 playerId, vec3 p, const std::string& text, const std::string& color) {
  lv.fx->particles("floatText", p, 1, json{{"text", text}, {"color", color}, {"player", playerId}});
}

// ------------------------------------------------------------------ creature
Creature::Creature(const std::string& type) {
  this->type = type;
  const CreatureType* t = creatureType(type);
  T = t ? t : &stubType();
  w = T->w; h = T->h; hp = maxHp = T->hp;
  yaw = frand() * TAU;
  wanderT = frand() * 3; wanderDir = frand() * TAU;
  soundT = 3 + frand() * 8;
  stag = type == "deer" && frand() < 0.45f;
  if (T->woolly) { float r = frand(); color = r < 0.82f ? "white" : r < 0.87f ? "black" : r < 0.92f ? "gray" : r < 0.97f ? "light_gray" : r < 0.998f ? "brown" : "pink"; }
  if (T->layer) eggT = 300 + frand() * 300;
  if (T->j.contains("held") && T->j["held"].is_string()) heldItem = T->j["held"];
  if (!T->vdefs.empty()) {   // one that turns up anywhere (spawnCreature picks again with the place in mind)
    float tot = 0; for (auto& v : T->vdefs) if (v.open()) tot += v.w;
    float q = frand() * tot;
    for (auto& v : T->vdefs) if (v.open() && (q -= v.w) <= 0) { variant = v.model == T->model ? "" : v.model; break; }
  } else if (!T->variants.empty()) variant = T->variants[(size_t)(frand() * T->variants.size()) % T->variants.size()];
  if ((type == "ghoul" || type == "charred_ghoul" || type == "husk") && frand() < 0.1f) {
    const char* W[] = {"iron_sword", "iron_shovel", "stone_axe", "gold_sword", "copper_sword", "stone_sword", "iron_axe", "wooden_sword"};
    heldItem = W[(int)(frand() * 8) & 7];
  }
  rollArmour();
  if (type == "slime" || type == "magma_slime" || type == "sulfur_slime") setSize(2);
}

float Creature::weaponBonus() const {
  const std::string& id = !heldItem.empty() ? heldItem : offItem;
  const ItemDef* d = id.empty() ? nullptr : reg().item(id);
  return d && d->hasTool ? std::max(0.f, std::round((d->tool.damage - 1) * 0.6f)) : 0.f;
}
// BTA's armoured zombies (and Minecraft's): one undead walker in nine comes dressed, the helmet first and each next piece
// on a coin toss, mostly leather and copper, now and then iron or gold, rarely diamond. Worn in data["armor"] (4 keys)
void Creature::rollArmour() {
  static const char* WEARS[] = {"ghoul", "charred_ghoul", "husk", "drowned", "skeleton", "stray"};
  bool wears = false; for (const char* w : WEARS) if (type == w) wears = true;
  static const bool all = std::getenv("DQ_ARMOUR") != nullptr;   // tests: every one dressed
  if (!wears || frand() >= (all ? 2.f : 0.11f)) return;
  float r = frand();
  const char* mat = r < 0.36f ? "leather" : r < 0.62f ? "copper" : r < 0.80f ? "gold" : r < 0.97f ? "iron" : "diamond";
  static const char* PIECE[] = {"helmet", "chestplate", "leggings", "boots"};
  json a = json::array({"", "", "", ""});
  for (int i = 0; i < 4; i++) {
    if (i > 0 && frand() < 0.5f) continue;
    std::string k = std::string(mat) + "_" + PIECE[i];
    if (reg().item(k)) a[i] = k;
  }
  data["armor"] = a;
}
const std::string& Creature::armourPiece(int slot) const {
  static const std::string none;
  if (!data.contains("armor") || !data["armor"].is_array() || slot >= (int)data["armor"].size() || !data["armor"][slot].is_string()) return none;
  return data["armor"][slot].get_ref<const std::string&>();
}
float Creature::armourPoints() const {
  float pts = 0;
  for (int i = 0; i < 4; i++) {
    const std::string& k = armourPiece(i);
    const ItemDef* d = k.empty() ? nullptr : reg().item(k);
    if (d && d->props.contains("armor") && d->props["armor"].is_object()) pts += d->props["armor"].value("points", 0.f);
  }
  return pts;
}
void Creature::makeBaby() { baby = true; growT = 300; w = T->w * 0.55f; h = T->h * 0.55f; kept = true; }
void Creature::grow(Level& lv) {
  baby = false; w = T->w; h = T->h;
  if (type == "turtle") dropItem(lv, realm, ItemStack("turtle_scute", 1), pos + vec3(0, 0.3f, 0), true);
}
void Creature::setSize(int n) {
  size = n; float s = n > 1 ? 1 : 0.5f;
  w = T->w * s; h = T->h * s;
  if (n == 1) hp = maxHp = 4;
}
Entity* Creature::ent(Level& lv, u32 id) const { if (!id) return nullptr; Entity* e = lv.entity(id); return e && !e->removed ? e : nullptr; }
vec2 Creature::light(Level& lv, float dy) const {
  u8 l = lv.world(realm).getLight(ifloor(pos.x), ifloor(pos.y + dy), ifloor(pos.z));
  return vec2(l >> 4, l & 15);
}
// Wandering about (not chasing or fleeing), a walker keeps off drops of more than three blocks and out of water and
// lava: Skylands creatures stay on their islands, herds don't walk into wells or off cliffs (Minecraft's mobs avoid them).
static bool safeAhead(Level& lv, const Creature& c, float dir) {
  if (c.noGravity || c.ghost || c.inWater || c.T->water || c.T->fly > 0) return true;
  const Registry& R = reg();
  World& w = lv.world(c.realm);
  float reach = c.w + 0.75f;
  int x = ifloor(c.pos.x - std::sin(dir) * reach), z = ifloor(c.pos.z - std::cos(dir) * reach), y = ifloor(c.pos.y + 0.05f);
  if (R.tSolid[w.getBlock(x, y, z)]) return true;   // a wall or a step: the usual collision and step-up deal with it
  for (int d = 0; d <= 3; d++) {
    BlockId b = w.getBlock(x, y - d, z);
    if (R.tLiquid[b]) return false;
    if (d > 0 && R.tSolid[b]) return true;
  }
  return false;   // more than three blocks down
}

void Creature::wander(float dt) {
  wanderT -= dt;
  if (wanderT <= 0) { wanderT = 2 + frand() * 5; wanderOn = frand() < 0.55f; wanderDir = frand() * TAU; }
}
Move Creature::wanderMove() const { return wanderOn ? Move{T->speed * 0.6f, wanderDir} : Move{0, yaw}; }
int Creature::groundY(Level& lv) const {
  World& w = lv.world(realm);
  int y = ifloor(pos.y);
  for (int i = 0; i < 6 && !reg().tSolid[w.getBlock(ifloor(pos.x), y - 1, ifloor(pos.z))]; i++) y--;
  return y;
}
void Creature::say(Level& lv, const std::string& kind, float dy) {
  const std::string& v = !T->voice.empty() ? T->voice : T->sound;
  if (v.empty()) return;
  lv.fx->voice(v, kind, pos + vec3(0, dy < 0 ? h * 0.7f : dy, 0));
}
Player* Creature::ownerPawn(Level& lv) const {
  if (owner.empty()) return nullptr;
  for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->dead && !p->removed && p->name == owner && p->realm == realm) return p;
  return nullptr;
}
bool Creature::isPetOf(const Entity* who) const { auto* p = dynamic_cast<const Player*>(who); return p && !owner.empty() && owner == p->name; }

// ------------------------------------------------------------------ contacts (physics.js sampleContacts)
struct MobContacts { bool water = false, lava = false, cactus = false, acid = false; };
static MobContacts contacts(World& w, const Entity& e) {
  MobContacts c;
  static BlockId cactus = reg().blockId("cactus");
  int x0 = ifloor(e.pos.x - e.w), x1 = ifloor(e.pos.x + e.w), y0 = ifloor(e.pos.y), y1 = ifloor(e.pos.y + e.h * 0.9f), z0 = ifloor(e.pos.z - e.w), z1 = ifloor(e.pos.z + e.w);
  for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    BlockId id = w.getBlock(x, y, z);
    u8 l = reg().tLiquid[id];
    if (l == 1) c.water = true; else if (l == 2) c.lava = true; else if (l == 3) c.water = c.acid = true;
  }
  // cactus: touching the sides
  if (cactus) for (int z = ifloor(e.pos.z - e.w - 0.05f); z <= ifloor(e.pos.z + e.w + 0.05f); z++) for (int x = ifloor(e.pos.x - e.w - 0.05f); x <= ifloor(e.pos.x + e.w + 0.05f); x++)
    if (w.getBlock(x, y0, z) == cactus || w.getBlock(x, y0 - 1, z) == cactus) c.cactus = true;
  static const BlockId spikes = reg().blockId("spike_trap");
  if (spikes && w.getBlock(ifloor(e.pos.x), y0, ifloor(e.pos.z)) == spikes) c.cactus = true;   // walked into a spike trap
  return c;
}

// ------------------------------------------------------------------ hurt
bool realmPreHurt(Level& lv, Creature& m, DamageSrc& src);                 // creature_monsters.cpp (endermen, swets, aechor plants)
void realmAfterHurt(Level& lv, Creature& m, const DamageSrc& src, float dealt);   // (endermen blink, pigmen gang up)

static std::unordered_map<std::string, CreatureHooks>& hookMap() { static std::unordered_map<std::string, CreatureHooks> m; return m; }
void setCreatureHooks(const std::string& type, CreatureHooks h) { hookMap()[type] = std::move(h); }
const CreatureHooks* creatureHooks(const std::string& type) { auto& m = hookMap(); if (m.empty()) return nullptr; auto it = m.find(type); return it == m.end() ? nullptr : &it->second; }
CreatureGlobalHooks& globalCreatureHooks() { static CreatureGlobalHooks h; return h; }

float Creature::hurt(Level& lv, float amount, const DamageSrc& src) {
  const CreatureHooks* HK = creatureHooks(type);
  if (HK && HK->beforeHurt && !HK->beforeHurt(lv, *this, amount, src)) return 0;
  float dealt = hurtBase(lv, amount, src);
  if (HK && HK->afterHurt) HK->afterHurt(lv, *this, dealt, src);
  return dealt;
}
float Creature::hurtBase(Level& lv, float amount, const DamageSrc& srcIn) {
  DamageSrc src = srcIn;
  if (alive() && !ghost && realmPreHurt(lv, *this, src)) return 0;
  Entity* a = ent(lv, src.attacker);
  auto* ac = dynamic_cast<Creature*>(a);
  // pets.js: pets don't take your blows by accident
  if (!owner.empty() && a) {
    if (!ac && isPetOf(a) && !pawnSneaking(a)) return 0;
    if (ac && ac->owner == owner) return 0;
  }
  if (riddenBy && a && a->id == riddenBy) return 0;
  if (globalCreatureHooks().beforeHurt && !globalCreatureHooks().beforeHurt(lv, *this, amount, src)) return 0;
  // entities.js base
  if (!alive() || invulnT > 0) return 0;
  float dmg = amount;
  if (src.element == "fire" && T->fireImmune && !src.weapon) dmg = 0;
  if (src.element == "frost" && type == "cinder_golem") dmg *= 2;
  if (src.type == "lava" && T->fireImmune) dmg = 0;
  if (dmg <= 0) { if (a && a->isPlayer()) floatText(lv, a->id, pos + vec3(0, h + 0.3f, 0), "Immune", "#ffb070"); return 0; }
  if (!src.bypassArmor) if (float ap = armourPoints(); ap > 0) dmg *= 1 - std::min(20.f, ap) * 0.04f;   // as a player's armour: 4% a point
  hp -= dmg;
  hurtTime = 0.35f;
  if (a) {
    float kx = pos.x - a->pos.x, kz = pos.z - a->pos.z, l = std::hypot(kx, kz); if (l < 1e-4f) l = 1;
    float kb = (src.knock < 0 ? 5 : src.knock) * (1 - T->knockResist);
    vel.x += kx / l * kb; vel.z += kz / l * kb;
    if (onGround) vel.y = std::max(vel.y, 4.5f * (1 - T->knockResist));
    panicT = 4; fleeFrom = a->id;
  }
  if (src.element == "fire" && !T->fireImmune) burn = std::max(burn, 3.f);
  if (src.element == "frost") slowT = 3;
  if (auto fl = data.find("hpFloor"); fl != data.end() && fl->is_number() && hp < fl->get<float>()) hp = fl->get<float>();   // a boss that can't be worn below this yet (the Storm's heart)
  if (hp <= 0) die(lv, src);
  float dealt = dmg;
  // dismember.js
  if (dealt > 0) tryDismember(lv, src, dealt);
  // bestiary.js: neutral animals fight back, the rest run
  if (dealt > 0 && !T->ai.empty() && a && a != this) {
    Entity* who = a;
    if (ac && !ac->owner.empty()) if (Player* o = ac->ownerPawn(lv)) who = o;
    const auto& t = *T;
    if (t.ai == "neutral" || (t.ai == "predator" && t.damage >= 2 && t.pack) || t.angersWhenHurt || t.stings || (t.ai == "swimmer" && t.damage > 0) || (!t.prey.empty() && t.ai == "amphibian"))
      if (!(t.skittish > 0 && hp < maxHp * 0.4f)) provoke(lv, who);
    if (t.roosts) roosting = false;
    sitting = sleeping = perched = false;
  }
  // bestiary.js / sounds2.js: a hurt call (every creature has one; the death cry is in die())
  if (dealt > 0 && alive() && !creatureVoice(*T).empty()) lv.fx->voice(creatureVoice(*T), "hurt", pos + vec3(0, h * 0.7f, 0));
  // monsters.js: a hard blow can knock a marauder's axe loose; snakes and crocodiles answer; venom and frost
  if (dealt >= 4 && type == "raider" && alive() && !heldItem.empty() && frand() < 0.35f) dropWeapon(lv, true);
  if (dealt > 0 && T->retaliates && alive() && a && a != this) { angryT = 15; foe = a->id; }
  if (dealt > 0 && ac && ac->T->onHit.any && alive()) {
    const auto& fx = ac->T->onHit;
    if (fx.slow > 0 || fx.chill > 0) slowT = std::max(slowT, fx.slow > 0 ? fx.slow : fx.chill);
    if (fx.poison > 0 && !T->undead) poisonT = std::max(poisonT, fx.poison);
    if (fx.fire > 0 && !T->fireImmune) burn = std::max(burn, fx.fire);
  }
  // quarrels.js: who did it?
  if (dealt > 0 && alive() && a && a != this) {
    if (ac) { if (ac->alive()) takeQuarrel(*ac); }
    else {
      quarrel = 0; huntT = 10;    // a player: forget any squabble, it's you now
      if (!T->family.empty()) for (auto& e : lv.entities()) if (auto* m = dynamic_cast<Creature*>(e.get()))
        if (m != this && m->alive() && !m->removed && m->T->family == T->family && std::hypot(m->pos.x - pos.x, m->pos.z - pos.z) < 16) { m->alertT = 20; m->huntT = 10; }
    }
  }
  // pets.js: they answer anyone who hurts them
  if (dealt > 0 && !owner.empty() && a && alive() && !isPetOf(a)) { petFoe = a->id; panicT = 0; angryT = 0; quarrel = 0; }
  if (dealt > 0) realmAfterHurt(lv, *this, src, dealt);
  if (dealt > 0 && globalCreatureHooks().afterHurt) globalCreatureHooks().afterHurt(lv, *this, dealt, src);
  return dealt;
}

void Creature::takeQuarrel(Creature& a) {
  if (!(T->hostile || T->retaliates) || !T->ai.empty() || T->boss || type == "fuseling") return;
  if (!T->faction.empty() && T->faction == a.T->faction) return;
  quarrel = a.id; quarrelT = 30; huntT = 10;
}

// ------------------------------------------------------------------ death
void Creature::die(Level& lv, const DamageSrc& src) {
  // pets.js: the rider gets off first
  if (riddenBy) if (auto* r = dynamic_cast<Player*>(ent(lv, riddenBy))) dismount(lv, *this, *r);
  deathTime = 0.0001f;
  if (globalCreatureHooks().died) globalCreatureHooks().died(lv, *this, src);
  auto roll = [](int a, int b) { return a + (int)std::floor(frand() * (b - a + 1)); };
  vec3 at = pos + vec3(0, 0.5f, 0);
  bool noDrops = data.contains("noDrops") && data["noDrops"] == true;   // summons, a called-back dragon (its own table)
  if (!baby && !noDrops) for (auto& d : T->drops) if (frand() < d.chance) { int n = roll(d.lo, d.hi); if (n > 0) dropItem(lv, realm, ItemStack(d.id, n), at, true); }
  if (!baby && !noDrops && !variant.empty()) if (const auto* vd = T->vdef(variant))   // what its story left on it
    for (auto& d : vd->drops) if (frand() < d.chance) { int n = roll(d.lo, d.hi); if (n > 0) dropItem(lv, realm, ItemStack(d.id, n), at, true); }
  // mob heads, only to a player's blow: a wither skeleton's skull 2.5 % of the time (Minecraft), any other head 1 %
  if (!baby && !noDrops && src.attacker && dynamic_cast<Player*>(ent(lv, src.attacker))) {
    std::string hk = type + "_skull"; if (!reg().item(hk)) hk = type + "_head";
    if (reg().item(hk) && frand() < (type == "wither_skeleton" ? 0.025f : 0.01f)) dropItem(lv, realm, ItemStack(hk, 1), at, true);
  }
  if (!noDrops) for (int i = 0; i < 4; i++) if (!armourPiece(i).empty() && frand() < 0.085f) dropItem(lv, realm, ItemStack(armourPiece(i), 1), at, true);
  if (data.contains("bags") && data["bags"].is_array()) {   // saddle bags: everything in them, and the bags
    for (auto& s : data["bags"]) { ItemStack st = ItemStack::fromJson(s); if (!st.empty()) dropItem(lv, realm, st, at, true); }
    dropItem(lv, realm, ItemStack("saddle_bags", 1), at, true); data.erase("bags");
  }
  if (T->woolly && !sheared && !baby) dropItem(lv, realm, ItemStack(color == "white" || color.empty() ? "wool" : color + "_wool", 1), at, true);
  if (type == "cinder_golem") {
    json& flags = lv.info.data["flags"]; if (!flags.is_object()) flags = json::object();
    int slain = flags.value("golemSlain", 0);
    if (!slain || frand() < 0.35f) dropItem(lv, realm, ItemStack("ember_blade", 1), pos + vec3(0, 1, 0), true);
    if (!slain || frand() < 0.15f) dropItem(lv, realm, ItemStack("heart_crystal", 1), pos + vec3(0, 1, 0), true);
    flags["golemSlain"] = slain + 1;
    if (lv.events) lv.events->emit("bossDefeated", json::array({type, pos.x, pos.y, pos.z}));
  }
  Entity* a = ent(lv, src.attacker);
  if (a && a->isPlayer()) {
    grantXP(lv, *a, T->xp, pos + vec3(0, h, 0));
    auto* p = static_cast<Player*>(a);
    p->stats["mobsKilled"] = p->stats.value("mobsKilled", 0) + 1;
  }
  // death cries (bestiary.js / sounds2.js)
  if (!creatureVoice(*T).empty()) lv.fx->voice(creatureVoice(*T), "death", pos + vec3(0, h * 0.6f, 0));
  // nightmobs.js / monsters.js: big slimes split
  if ((type == "slime" || type == "magma_slime" || type == "sulfur_slime") && size > 1) {
    int n = type == "slime" ? 2 + (int)(frand() * 3) : 2 + (int)(frand() * 2);
    for (int i = 0; i < n; i++) if (auto* m = spawnCreature(lv, realm, type, pos + vec3((frand() - 0.5f) * 0.6f, 0.3f, (frand() - 0.5f) * 0.6f))) { m->setSize(1); m->vel.y = type == "slime" ? 3 : 3.5f; }
  }
  if (type == "wither_storm") stormFalls(lv, *this);   // sim/wither.cpp
  if (!owner.empty()) if (Player* o = ownerPawn(lv)) lv.fx->message(o->id, "Your " + (petName.empty() ? T->label.empty() ? type : T->label.substr(T->label.find(' ') + 1) : petName) + " died", "warn");
  // mods.js: DQ.on('mobDeath', (info, killer) => ...)
  if (lv.events) lv.events->emit("mobDeath", json::array({json{{"type", type}, {"x", pos.x}, {"y", pos.y}, {"z", pos.z}, {"id", id}},
                                                           a && a->isPlayer() ? json(static_cast<Player*>(a)->name) : json(nullptr)}));
}

// ------------------------------------------------------------------ head turning (rigs.js rigLook)
static void rigLook(Creature& m, Entity* target, float dt) {
  float yawRel = 0, pitch = 0;
  if (target) {
    float dx = target->pos.x - m.pos.x, dz = target->pos.z - m.pos.z, eye = target->isPlayer() ? 1.62f : target->h * 0.9f;
    float dy = (target->pos.y + eye) - (m.pos.y + m.h * 0.9f);
    float want = std::atan2(-dx, -dz);
    yawRel = clampv(wrapAngle(want - m.yaw), -1.2f, 1.2f);
    pitch = clampv(std::atan2(dy, std::hypot(dx, dz)), -0.7f, 0.7f);
    if (std::hypot(dx, dz) > 16) yawRel = pitch = 0;
  } else {
    float g = std::sin(m.age * 0.37f + m.id * 1.7f);
    yawRel = g > 0.6f ? (g - 0.6f) * 2.2f * (std::sin((m.id ? m.id : 1) * 3.1f) >= 0 ? 1.f : -1.f) : 0;
  }
  float k = std::min(1.f, dt * 6);
  m.headYawRel += (yawRel - m.headYawRel) * k;
  m.headPitch += (pitch - m.headPitch) * k;
}

// ------------------------------------------------------------------ update
void Creature::ghostStep(Level& lv, float dt) {
  if (!hasTarget) return;
  float k = std::min(1.f, dt * 10);
  vec3 o = pos;
  pos += (tpos - pos) * k;
  yaw += wrapAngle(tyaw - yaw) * k;
  float hs = std::hypot(pos.x - o.x, pos.z - o.z) / std::max(dt, 1e-3f);
  walkAmt += (std::min(1.f, hs / 2) - walkAmt) * std::min(1.f, dt * 6);
  walk += hs * dt * 3.2f;
  speedNow = hs;
}

// A whirlwind (tools/port/skylands_more.py): it wanders, whirls cloud about itself, and whatever it touches — you, a
// creature, a dropped item — is thrown up into the air and spun out; after a minute or two it blows itself out.
static void whirlwindTick(Level& lv, Creature& m, float dt) {
  float life = m.data.value("life", 0.f) + dt; m.data["life"] = life;
  const float lifeMax = m.data.value("lifeMax", 0.f) > 0 ? m.data.value("lifeMax", 0.f) : (float)(m.data["lifeMax"] = 60 + frand() * 60);
  if (life > lifeMax) { lv.fx->particles("poof", m.pos + vec3(0, 1, 0), 16); m.removed = true; return; }
  if (frand() < dt * 10) {   // the swirl
    const float a = m.age * 7 + frand() * 6.28f, r = 0.4f + frand() * 0.8f, y = frand() * 2.2f;
    lv.fx->particles("breath", m.pos + vec3(std::cos(a) * r, y, std::sin(a) * r), 1);
  }
  for (auto& e : lv.entities()) {
    Entity* o = e.get();
    if (o == &m || o->removed || o->realm != m.realm) continue;
    const float dx = o->pos.x - m.pos.x, dz = o->pos.z - m.pos.z, d = std::hypot(dx, dz);
    if (d > 1.3f || o->pos.y < m.pos.y - 0.5f || o->pos.y > m.pos.y + 2.5f) continue;
    if (auto* pl = dynamic_cast<Player*>(o)) { if (pl->creative() || pl->dead || pl->mode == "spectator") continue; }
    else if (auto* c = dynamic_cast<Creature*>(o)) { if (c->T->j.value("whirlwind", false) || c->T->boss || c->T->w > 1.2f) continue; }
    if (o->vel.y > 4) continue;   // already on its way up
    const float a = std::atan2(dz, dx) + 1.5f;   // round and out
    o->vel = vec3(std::cos(a) * 4.5f, 8.5f + frand() * 2, std::sin(a) * 4.5f);
    o->onGround = false;
    lv.fx->sound("gust", o->pos, 18);
  }
}

void Creature::tick(Level& lv, double dtd) {
  float dt = (float)dtd;
  World& w = lv.world(realm);
  if (ghost) { ghostStep(lv, dt); if (hurtTime > 0) hurtTime -= dt; if (deathTime > 0) deathTime += dt; return; }
  // Halloween: about one in four ghouls and skeletons (and their kin) wears a carved pumpkin or a jack-o'-lantern on its head,
  // rolled once per creature on its first tick (so cages, /summon and eggs get it too; drawn by render/creature_layer, the jack glows)
  if (halloween() && !data.contains("hatRolled")) {
    data["hatRolled"] = true;
    if (type == "ghoul" || type == "rift_ghoul" || type == "husk" || type == "drowned" || type == "skeleton" || type == "stray" || type == "rift_skeleton")
      if (frand() < 0.25f) data["hat"] = frand() < 0.4f ? "jack_o_lantern" : "carved_pumpkin";
  }
  // nothing moves in a chunk that isn't there (it would fall out of the world)
  if (!w.isLoaded(ifloor(pos.x), clampv(ifloor(pos.y), 0, WORLD_H - 1), ifloor(pos.z))) { age -= dt; return; }
  // pets.js: riding along in a vehicle is handled by the vehicle (mountedOn) — not ported yet
  // quarrels.js timers
  if (quarrelT > 0) quarrelT -= dt;
  if (alertT > 0) alertT -= dt;
  if (quarrel && (type == "spider" || type == "cave_spider")) panicT = std::max(panicT, 0.5f);
  // monsters.js: venom works on creatures too
  if (poisonT > 0 && alive()) {
    poisonT -= dt; poisonTick += dt;
    if (poisonTick >= 1.25f) { poisonTick = 0; if (hp > 1) { DamageSrc s; s.type = "poison"; s.knock = 0; hurt(lv, 1, s); } }
  }
  // nightmobs.js: jockeys
  Creature* mnt = jockeyOf ? dynamic_cast<Creature*>(ent(lv, jockeyOf)) : nullptr;
  if (jockeyOf && (!mnt || !mnt->alive() || !alive())) { if (mnt && mnt->jockeyRider == id) mnt->jockeyRider = 0; jockeyOf = 0; mnt = nullptr; }
  if (jockeyRider) { auto* r = dynamic_cast<Creature*>(ent(lv, jockeyRider)); if (!r || r->jockeyOf != id) jockeyRider = 0; }
  vec3 from = pos; float st0 = soundT;
  update(lv, dt);
  if (!removed && alive() && T->j.value("whirlwind", false)) whirlwindTick(lv, *this, dt);
  if (!removed && alive()) creatureSounds(lv, *this, from, st0, dt);
  if (mnt) { pos = mnt->pos + vec3(0, mnt->h * 0.8f, 0); vel = vec3(mnt->vel.x, 0, mnt->vel.z); onGround = true; }
  // monsters.js: night creatures nobody is near wander off
  if (!removed && T->hostile && !T->boss && !kept && owner.empty() && !persistent) {
    float near = 1e9f;
    for (Entity* q : pawnsOf(lv, realm)) near = std::min(near, std::hypot(q->pos.x - pos.x, q->pos.z - pos.z));
    if (near > 40) { data["farT"] = data.value("farT", 0.f) + dt; if (data.value("farT", 0.f) > 20 && frand() < dt * 0.05f) removed = true; }
    else if (data.contains("farT")) data.erase("farT");
    if (near > 96) removed = true;
  }
  // pets.js: carry the rider along
  if (riddenBy) {
    auto* r = dynamic_cast<Player*>(ent(lv, riddenBy));
    if (!r || r->riding != id || r->dead) riddenBy = 0;
    else {
      float seatY = h * 0.7f;
      if (reg().models.contains(modelKey())) seatY = reg().models[modelKey()].value("seatY", seatY);
      r->pos = vec3(pos.x, pos.y + seatY - 0.28f, pos.z); r->vel = vec3(vel.x, 0, vel.z); r->onGround = true; r->fallDist = 0;
    }
  }
}

OptMove Creature::brainHook(Level& lv, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  const CreatureHooks* H = creatureHooks(type);
  return H && H->brain ? H->brain(lv, *this, p, dist, dyp, toPlayer, dt) : std::nullopt;
}
void Creature::update(Level& lv, float dt) {
  World& wd = lv.world(realm);
  const Registry& R = reg();
  if (deathTime > 0) {
    deathTime += dt;
    // the topple (creatureBase) is over by 0.64 s; it lies there a moment, then goes in a puff (the original
    // left after 0.95 s — a little longer here so the fall can be seen)
    if (deathTime > DEATH_LINGER) {
      removed = true;
      mobBurst(lv, pos + vec3(0, h / 2, 0), "white", 14, 0.6f);
      lv.fx->particles("poof", pos, 18, json{{"w", std::max(w, h * 0.5f)}, {"h", std::min(h, 0.8f)}});   // lying on its side by now
      return;
    }
    // still falls and slides to a stop (a body killed in mid-air doesn't hang there)
    vel.x *= std::max(0.f, 1 - dt * 6); vel.z *= std::max(0.f, 1 - dt * 6);
    if (!noGravity) vel.y -= 28 * dt;
    MoveResult r = moveBox(wd, pos, w, h, vel * dt, 0);
    if (r.hitY) vel.y = 0;
    if (r.hitX) vel.x = 0;
    if (r.hitZ) vel.z = 0;
    onGround = r.onGround;
    return;
  }
  if (hurtTime > 0) hurtTime -= dt;
  if (invulnT > 0) invulnT -= dt;
  if (attackCd > 0) attackCd -= dt;
  if (slowT > 0) slowT -= dt;
  MobContacts c = contacts(wd, *this);
  if (calm) burn = 0;   // the dev zoo: on show, not on fire
  inLava = c.lava;
  bool lavaBoat = false;   // a pet riding a lava boat is kept out of the lava
  if (!data.value("mountedOn", std::string()).empty()) for (auto& e : lv.entities()) if (auto* b = dynamic_cast<Boat*>(e.get())) if (b->pet == id) { lavaBoat = b->kind == "lava_rowboat"; break; }
  if (lavaBoat) burn = 0;
  else if (c.lava && !T->fireImmune) { DamageSrc s; s.type = "lava"; hurt(lv, 2, s); burn = 4; if (!alive()) return; }
  if (c.acid && !T->fireImmune) { if ((acidT += dt) > 1) { acidT = 0; DamageSrc s; s.type = "acid"; hurt(lv, 1, s); if (!alive()) return; } }
  if (burn > 0) {
    burn -= dt; burnTick += dt;
    if (burnTick > 0.8f) { burnTick = 0; if (!T->fireImmune) { DamageSrc s; s.type = "burn"; hurt(lv, 1, s); if (!alive()) return; } }
    if (c.water) burn = 0;
    if (frand() < dt * 12) flame(lv, pos + vec3((frand() - 0.5f) * w * 2, frand() * h, (frand() - 0.5f) * w * 2));
  }
  if (c.cactus) { hurt(lv, 1, DamageSrc{}); if (!alive()) return; }
  if (pos.y < -20) { removed = true; return; }
  int dim = dimAt(*this);
  float day = envDay(lv, dim);
  if ((type == "ghoul" || T->undead) && dim == DIM_OVERWORLD && day > 0.65f && !c.water && !calm) {
    if (light(lv, h - 0.1f).x >= 14 && armourPiece(0).empty()) burn = std::max(burn, 2.f);   // a helmet shades it
  }

  // AI -> desired horizontal direction & speed
  Entity* p = aiTarget(lv, nearestPawn(lv, realm, pos, 96));
  float want = 0, dir = yaw;
  float dx = p ? p->pos.x - pos.x : 0, dz = p ? p->pos.z - pos.z : 0, dist = p ? std::hypot(dx, dz) : 999, dyp = p ? p->pos.y - pos.y : 0;
  float toPlayer = std::atan2(-dx, -dz);
  lookAt = p ? p->id : 0;
  // after someone who went through a portal (sim/portals.cpp swap): through it after them, unless there's someone to
  // hunt on this side after all, or it takes too long
  bool chasing = false;
  if (data.contains("chase") && !calm) {
    json& C = data["chase"];
    const float t = C.value("t", 0.f) - dt;
    if (t <= 0 || !C.contains("c") || (p && dist < 16 && std::fabs(dyp) < 8)) data.erase("chase");
    else {
      C["t"] = t; chasing = true;
      const vec3 c(C["c"][0].get<float>(), C["c"][1].get<float>(), C["c"][2].get<float>()), n(C["n"][0].get<float>(), C["n"][1].get<float>(), C["n"][2].get<float>());
      const vec3 goal = c - n * (glm::dot(n, pos - c) >= 0 ? 2.5f : -2.5f);   // just through it
      std::optional<float> nd = navDir(lv, goal, dt);
      dir = nd ? *nd : std::atan2(-(goal.x - pos.x), -(goal.z - pos.z)); want = T->speed * 1.15f;
    }
  }
  OptMove hooked; if (!calm && !chasing && globalCreatureHooks().brain) hooked = globalCreatureHooks().brain(lv, *this, p, dist, dt);
  if (calm) { want = 0; dir = yaw; }
  else if (chasing) {}
  else if (OptMove hk = brainHook(lv, p, dist, dyp, toPlayer, dt)) { want = hk->want; dir = hk->dir; }
  else if (hooked) { want = hooked->want; dir = hooked->dir; }
  else if (data.value("corruptHostile", false) && p && dist < 32) {   // one of his: an ordinary animal that comes for you (sim/rift.cpp incursions)
    std::optional<float> nd; if (dist > 2.2f) nd = navDir(lv, p->pos, dt);
    dir = nd ? *nd : toPlayer; want = std::max(T->speed, 2.4f) * (T->run > 0 ? 1.2f : 1.f);
    bite(lv, p, std::max(2.f, T->damage));
    if (dist < 0.9f) want = 0;
  }
  else if (OptMove ctl = controlAI(lv, dt)) { want = ctl->want; dir = ctl->dir; }
  else if (type == "cinder_golem") { Move m = bossAI(lv, p, dist, toPlayer, dt); want = m.want; dir = m.dir; }
  else if (T->special) { Move m = specialAI(lv, p, dist, dyp, toPlayer, dt); want = m.want; dir = m.dir; }
  else if (!T->ai.empty()) { Move m = archetypeAI(lv, p, dist, dyp, toPlayer, dt); want = m.want; dir = m.dir; }
  else if (T->hostile) {
    float range = alertT > 0 ? 40 : 24;
    bool canSee = p && dist < range && std::fabs(dyp) < 10 && senses(lv, p, dist, dyp, dt, range).hunting;
    if (canSee) {
      std::optional<float> nd; if (dist > 2.2f) nd = navDir(lv, p->pos, dt);
      dir = nd ? *nd : toPlayer; want = T->speed;
      if (dist < 1.35f + w && std::fabs(dyp) < 1.6f && attackCd <= 0) {
        attackCd = 1.1f; swingT = 0.3f;
        DamageSrc s; if (type == "charred_ghoul") s.element = "fire";
        damageEntity(lv, *p, T->damage + weaponBonus(), this, "mob", s);
      }
      if (dist < 0.9f) want = 0;
      soundT -= dt;
      if (soundT < 0) { soundT = 4 + frand() * 6; if (dist < 16) say(lv, "say"); }
    } else { wander(dt); Move m = wanderMove(); if (m.want > 0 && !safeAhead(lv, *this, m.dir)) { wanderDir += PI * (0.7f + frand() * 0.6f); m = Move{0, yaw}; } want = m.want; dir = m.dir; }
    if (dist > 72) removed = true;
  } else {
    // passive animals: skittish ones bolt when someone comes close (sneaking helps)
    bool spooked = T->skittish > 0 && p && dist < T->skittish * (pawnSneaking(p) ? 0.45f : 1);
    if (spooked && panicT <= 0) { panicT = 2.5f + frand() * 2; fleeFrom = p->id; }
    OptMove farm; if (T->farm && panicT <= 0) farm = farmAI(lv, p, dist, toPlayer, dt);
    if (farm) { want = farm->want; dir = farm->dir; }
    else if (panicT > 0) {
      panicT -= dt;
      Entity* f = ent(lv, fleeFrom); if (!f) f = p;
      float away = yaw;
      if (f) away = std::atan2(-(pos.x - f->pos.x), -(pos.z - f->pos.z));
      if (hitWall) { if (!fleeTurn) fleeTurn = frand() < 0.5f ? -1 : 1; }
      else if (fleeTurn && frand() < dt * 0.8f) fleeTurn = 0;
      dir = away + fleeTurn * 0.9f + std::sin(age * 1.7f) * 0.18f; want = T->run > 0 ? T->run : T->speed * 2;
    } else { wander(dt); Move m = wanderMove(); if (m.want > 0 && !safeAhead(lv, *this, m.dir)) { wanderDir += PI * (0.7f + frand() * 0.6f); m = Move{0, yaw}; } want = m.want; dir = m.dir; }
    soundT -= dt;
    if (soundT < 0) { soundT = 6 + frand() * 10; if (dist < 14 && (type == "boar" || !T->sound.empty())) lv.fx->voice(T->sound.empty() ? "boar" : T->sound, "say", pos); }
    if (dist > 110 && !kept) removed = true;
  }
  if (removed) return;
  // a dropped weapon is worth going back for
  if (regrab.on && alive()) if (OptMove fw = fetchWeapon(lv, dt)) { want = fw->want; dir = fw->dir; }
  if (slowT > 0) want *= 0.5f;
  if (crawling) want *= 0.35f;
  if (limp) want *= std::max(0.3f, 1 - limp * 0.25f);
  lastWant = want;
  float turn = wrapAngle(dir - yaw);
  yaw += turn * std::min(1.f, dt * 8);
  float tx = -std::sin(yaw) * want, tz = -std::cos(yaw) * want;
  if (T->hopper) {
    hopT -= dt;
    if (!onGround || want < 0.1f) { tx *= onGround ? 0 : 1; tz *= onGround ? 0 : 1; }
    if (onGround && want > 0.1f && hopT <= 0) { vel.y = 5.2f; hopT = 0.25f; }
  }
  // don't walk off a drop you'd not survive (Minecraft's path finder keeps mobs off ledges deeper than 3; in the
  // Skylands that is the edge of an island): a calm walker stops at the edge instead of stepping into the air
  if (onGround && !noGravity && want > 0.05f && !T->swims && !T->flyer && !T->floaty && !(angryT > 0 && foe)) {
    World& wd = lv.world(realm);
    float ax = pos.x - std::sin(yaw) * (w + 0.45f), az = pos.z - std::cos(yaw) * (w + 0.45f);
    int bx = ifloor(ax), bz = ifloor(az), by = ifloor(pos.y);
    if (wd.isLoaded(bx, std::max(by, 0), bz) && !reg().tSolid[wd.getBlock(bx, by, bz)]) {
      int drop = 0;
      while (drop < 5 && by - 1 - drop > 0 && !reg().tSolid[wd.getBlock(bx, by - 1 - drop, bz)] && !reg().tLiquid[wd.getBlock(bx, by - 1 - drop, bz)]) drop++;
      if (drop >= 4) { tx = 0; tz = 0; dir = yaw + PI; }
    }
  }
  float k = std::min(1.f, dt * (onGround ? 10 : 2));
  if (!lockMove) { vel.x += (tx - vel.x) * k; vel.z += (tz - vel.z) * k; }
  if (noGravity && hasFlyVy) vel.y += (flyVy - vel.y) * std::min(1.f, dt * 4);
  else if (c.water && T->swims) vel.y += (swimVy - vel.y) * std::min(1.f, dt * 3);
  else if (c.water) {
    // Minecraft: a land animal floats at the surface and paddles on, steadily — a spring toward the height where
    // a bit over half its body is under (no kick up and fall back), and a hop out when it swims into a bank
    float surf = -1e9f;
    int bx = ifloor(pos.x), bz = ifloor(pos.z);
    for (int y = ifloor(pos.y + h) + 1; y >= ifloor(pos.y) - 1; y--)
      if (R.tLiquid[wd.getBlock(bx, y, bz)] == 1 && R.tLiquid[wd.getBlock(bx, y + 1, bz)] != 1) { surf = y + 0.9f; break; }
    if (surf < -1e8f) { vel.y += 18 * dt; if (vel.y > 2) vel.y = 2; vel.y -= 6 * dt; }
    else {
      float feet = surf - h * 0.55f, vy = clampv((feet - pos.y) * 3.5f, -1.4f, 1.6f);
      if (hitWall && want > 0.1f) vy = std::max(vy, 4.2f);   // against the shore: climb out
      vel.y += (vy - vel.y) * std::min(1.f, dt * 7);
    }
  }
  else if (!noGravity) vel.y -= 28 * dt;
  if (T->floaty && !noFloaty && vel.y < -2.2f) vel.y = -2.2f;
  if (T->farm) farmTick(lv, dt);
  bool wasGround = onGround;
  if (wantJump) { if (onGround && !noGravity) vel.y = T->hopper ? 6 : 7.8f; wantJump = false; }
  {
    vec3 before = pos;
    // a creature hiding inside a block (the October pumpkin creature) stays put: no move, no push out of the block
    MoveResult r = data.value("inBlock", false) ? MoveResult{} : T->phase ? phaseMove(wd, pos, w, h, vel * dt) : moveBox(wd, pos, w, h, vel * dt, onGround || wasGround ? (riddenBy ? 1.05f : 0.6f) : 0.f);   // (ridden: up stairs and slabs at a run, as BTA's pigs keep their momentum)
    hitWall = r.hitX || r.hitZ; hitCeiling = r.hitY && vel.y > 0;
    if (r.hitX) vel.x = 0;
    if (r.hitZ) vel.z = 0;
    if (r.hitY) vel.y = vel.y < 0 ? 0 : std::min(0.f, vel.y);
    onGround = r.onGround && vel.y <= 0.01f;
    if (onGround && vel.y < 0) vel.y = 0;
    if (!onGround && vel.y < 0) fallDist += before.y - pos.y;
    if (onGround) fallDist = 0;
  }
  // walked into something: hop up only if it's a step it can get onto
  if (hitWall && onGround && want > 0 && !(T->swims && !T->walks && c.water) && stepAhead(lv)) vel.y = T->hopper ? 6 : 7.8f;
  if (!wasGround && onGround && type == "cinder_golem" && fallV < -8) lv.fx->shake(0, 0.3f);
  if (!wasGround && onGround && fallV < -5) landT = 0.18f;
  if (landT > 0) landT -= dt;
  inWater = c.water;
  if (inWater && !T->swims) {   // where the surface is (rigs2.js floats land animals low in it)
    int bx = ifloor(pos.x), bz = ifloor(pos.z);
    for (int y = ifloor(pos.y) + 2; y >= ifloor(pos.y) - 1; y--)
      if (R.tLiquid[wd.getBlock(bx, y, bz)] == 1 && R.tLiquid[wd.getBlock(bx, y + 1, bz)] != 1) { data["surf"] = y + 0.9f; break; }
  } else if (data.contains("surf")) data.erase("surf");
  if (!owner.empty()) { Player* o = ownerPawn(lv); data["ownerNear"] = o && std::hypot(o->pos.x - pos.x, o->pos.z - pos.z) < 5; }
  speedNow = std::hypot(vel.x, vel.z);
  Entity* watch = p && (T->hostile ? dist < 18 : (dist < 7 && panicT <= 0)) ? p : nullptr;
  rigLook(*this, watch, dt);
  idleHabits(lv, dt, want);
  fallV = vel.y;
  float hs = std::hypot(vel.x, vel.z);
  walkAmt += (std::min(1.f, hs / 2) - walkAmt) * std::min(1.f, dt * 6);
  walk += hs * dt * (type == "deer" ? 2.2f : 3.2f);
  if (swingT > 0) swingT -= dt;
  (void)R;
}

void Creature::idleHabits(Level& lv, float dt, float want) {
  if (grazeT > 0) {
    grazeT -= dt;
    if (grazeT <= 0 && type == "sheep" && sheared) {
      World& w = lv.world(realm); int x = ifloor(pos.x), y = ifloor(pos.y) - 1, z = ifloor(pos.z);
      static BlockId grass = reg().blockId("grass"), dirt = reg().blockId("dirt");
      if (grass && w.getBlock(x, y, z) == grass) { w.setBlock(x, y, z, dirt, 0); sheared = false; }
    }
  } else if (T->grazer && onGround && want < 0.1f && panicT <= 0 && frand() < dt * 0.12f) grazeT = grazeMax = 1.6f + frand() * 1.8f;
  if (peckT > 0) peckT -= dt;
  else if ((type == "chicken" || T->pecker) && onGround && want < 0.1f && frand() < dt * 0.5f) peckT = 0.35f;
  if (flutterT > 0) flutterT -= dt;
}

// ------------------------------------------------------------------ saving & snapshots
json Creature::save() const {
  json j = Entity::save();
  j["c"] = json{{"v", variant}, {"baby", baby}, {"growT", growT}, {"color", color}, {"sheared", sheared}, {"stag", stag}, {"kept", kept}, {"size", size},
                {"held", heldItem}, {"off", offItem}, {"owner", owner}, {"name", petName}, {"saddled", saddled}, {"stay", stay}, {"fed", fed}, {"eggT", eggT},
                {"breedCd", breedCd}, {"leader", leader}};
  if (hasHive) j["c"]["hive"] = {hive.x, hive.y, hive.z};
  if (!lost.empty()) j["c"]["lost"] = lost;
  return j;
}
void Creature::load(const json& j) {
  Entity::load(j);
  if (!j.contains("c")) return;
  const json& c = j["c"];
  variant = c.value("v", variant); stag = c.value("stag", stag); kept = c.value("kept", kept);
  if (c.value("baby", false)) { makeBaby(); growT = c.value("growT", 300.f); }
  color = c.value("color", color); sheared = c.value("sheared", false);
  if (int s = c.value("size", 0)) { float keepHp = hp; setSize(s); hp = std::min(keepHp, maxHp); }
  heldItem = c.value("held", heldItem); offItem = c.value("off", offItem);
  owner = c.value("owner", ""); petName = c.value("name", ""); saddled = c.value("saddled", false); stay = c.value("stay", false); fed = c.value("fed", false);
  tamed = !owner.empty(); eggT = c.value("eggT", eggT); breedCd = c.value("breedCd", 0.f);
  if (c.contains("hive") && c["hive"].is_array()) { hasHive = true; hive = ivec3(c["hive"][0], c["hive"][1], c["hive"][2]); }
  if (c.contains("lost") && c["lost"].is_array()) for (auto& n : c["lost"]) lost.insert(n.get<std::string>());
  if (type == "cinder_golem") state = "chase";
}

static int mobFlags(const Creature& m) {
  const bool b[] = {m.roosting, m.puffed, m.curled, m.sitting, m.sleeping, m.playDead, m.tongueOut, m.angryT > 0, m.inWater, m.dancing, m.ramming, m.hasSting};
  int f = 0; for (int i = 0; i < 12; i++) if (b[i]) f |= 1 << i;
  if (m.displayT > 0) f |= 1 << 12;
  if (m.rearT > 0) f |= 1 << 13;
  if (m.aiming) f |= 1 << 14;
  if (m.saddled) f |= 1 << 15;
  if (m.climbing) f |= 1 << 16;
  return f;
}
json Creature::snap() const {
  auto r2 = [](float v) { return std::round(v * 100) / 100; };
  json o = {{"i", id}, {"k", "m"}, {"t", type}, {"x", r2(pos.x)}, {"y", r2(pos.y)}, {"z", r2(pos.z)}, {"r", r2(yaw)}, {"h", std::round(hp)}, {"s", state},
            {"d", deathTime > 0 ? 1 : 0}, {"b", burn > 0 ? 1 : 0}, {"g", stag ? 1 : 0}, {"sh", sheared ? 1 : 0}, {"c", color}, {"bb", baby ? 1 : 0},
            {"v", variant}, {"fl", mobFlags(*this)}, {"hi", heldItem}, {"oi", offItem}, {"cr", crawling ? 1 : 0}, {"fu", r2(fuse)}, {"sz", size},
            {"sw", r2(swingT)}, {"st", r2(shootT)}, {"gz", r2(grazeT)}, {"pk", r2(peckT)}, {"og", onGround ? 1 : 0}};
  if (attack.on) o["a"] = {attack.kind, r2(attack.progress), r2(attack.yaw)};
  if (data.contains("hat")) o["hd"] = data["hat"];
  if (data.contains("hw")) o["hw"] = data["hw"];
  if (data.contains("armor")) o["ar"] = data["armor"];
  if (!lost.empty()) o["ls"] = lost;
  if (hasTilt) o["sf"] = {r2(surfaceTilt.x), r2(surfaceTilt.y)};
  // (more than the original sent: pets, riders, a dragon's wings, a limp)
  if (!owner.empty()) o["ow"] = owner;
  if (!petName.empty()) o["pn"] = petName;
  if (riddenBy) o["rb"] = riddenBy;
  if (tamed || stay) o["tm"] = (tamed ? 1 : 0) | (stay ? 2 : 0);
  if (breathT > 0) o["br"] = r2(breathT);
  if (hoverT > 0) o["hv"] = r2(hoverT);
  if (limp) o["lm"] = limp;
  return o;
}
void Creature::glide(double dt) {
  const vec3 o = pos;
  Entity::glide(dt);
  const float fdt = std::max((float)dt, 1e-3f), hs = std::hypot(pos.x - o.x, pos.z - o.z) / fdt;
  walkAmt += (std::min(1.f, hs / 2) - walkAmt) * std::min(1.f, fdt * 6);
  walk += hs * fdt * 3.2f;
  speedNow = hs;
  if (hurtTime > 0) hurtTime -= fdt;
  if (deathTime > 0) deathTime += fdt;
}
void Creature::applySnap(const json& o) {
  ghost = true;
  tpos = vec3(o.value("x", pos.x), o.value("y", pos.y), o.value("z", pos.z)); tyaw = o.value("r", yaw);
  if (!hasTarget) { pos = tpos; yaw = tyaw; }
  hasTarget = true;
  float nh = o.value("h", hp); if (nh < hp) hurtTime = 0.3f; hp = nh;
  state = o.value("s", state); stag = o.value("g", 0); sheared = o.value("sh", 0); color = o.value("c", color);
  bool bb = o.value("bb", 0); if (bb && !baby) makeBaby(); else if (!bb && baby) { baby = false; w = T->w; h = T->h; }
  attack.on = o.contains("a") && o["a"].is_array();
  if (attack.on) { attack.kind = o["a"][0]; attack.progress = o["a"][1]; attack.yaw = o["a"][2]; }
  if (o.value("d", 0) && deathTime == 0) deathTime = 0.0001f;
  variant = o.value("v", variant);
  int f = o.value("fl", 0);
  bool* b[] = {&roosting, &puffed, &curled, &sitting, &sleeping, &playDead, &tongueOut, nullptr, &inWater, &dancing, &ramming, &hasSting};
  for (int i = 0; i < 12; i++) if (b[i]) *b[i] = (f >> i) & 1;
  angryT = (f >> 7) & 1 ? 1.f : 0.f; displayT = (f >> 12) & 1 ? 1.f : 0.f; rearT = (f >> 13) & 1 ? 1.f : 0.f;
  aiming = (f >> 14) & 1; saddled = (f >> 15) & 1; climbing = (f >> 16) & 1;
  heldItem = o.value("hi", std::string()); offItem = o.value("oi", std::string()); crawling = o.value("cr", 0);
  fuse = o.value("fu", 0.f); if (int s = o.value("sz", 0)) if (s != size) setSize(s);
  swingT = o.value("sw", 0.f); shootT = o.value("st", 0.f); grazeT = o.value("gz", 0.f); peckT = o.value("pk", 0.f); onGround = o.value("og", 1);
  if (o.contains("hd")) data["hat"] = o["hd"];
  if (o.contains("hw")) data["hw"] = o["hw"];
  if (o.contains("ar")) data["armor"] = o["ar"]; else if (data.is_object()) data.erase("armor");
  lost.clear(); if (o.contains("ls")) for (auto& n : o["ls"]) lost.insert(n.get<std::string>());
  hasTilt = o.contains("sf"); if (hasTilt) surfaceTilt = vec2(o["sf"][0], o["sf"][1]);
  owner = o.value("ow", std::string()); petName = o.value("pn", std::string()); riddenBy = o.value("rb", 0u);
  const int tm = o.value("tm", 0); tamed = tm & 1; stay = tm & 2;
  breathT = o.value("br", 0.f); hoverT = o.value("hv", 0.f); limp = o.value("lm", 0);
}

// ------------------------------------------------------------------ sound3.js: footsteps for everything with feet, and
// the hostile ones calling now and then as they wander, not only when they've seen you
// SND3_FAMILY: which footsteps
static const char* family(const Creature& m) {
  const CreatureType& T = *m.T;
  const std::string& v = T.voice;
  if (m.type == "fuseling") return "creeper";
  if (v == "ghoul" || v == "husk" || v == "drowned" || T.family == "ghoul") return "zombie";
  if (v == "rattle" || v == "stray") return "bone";
  if (v == "spider" || v == "mite" || v == "scorpion" || m.type == "cave_spider") return "spider";
  return "";
}
// farm animals whose own step a resource pack may carry (mob.mstep_<name>; the sound engine falls back to
// the block underfoot when it has none)
static const char* ownStep(const Creature& m) {
  const std::string& v = creatureVoice(*m.T);
  if (v == "moo") return "cow";
  if (v == "cluck") return "chicken";
  if (v == "oink") return "pig";
  if (v == "baa") return "sheep";
  if (v == "wolf") return "wolf";
  if (m.T->j.value("golem", false)) return "golem";
  return "";
}

void creatureSounds(Level& lv, Creature& m, vec3 from, float soundT0, float dt) {
  if (m.ghost || !m.T) return;
  const CreatureType& T = *m.T;
  std::string fam = family(m);
  // footsteps
  float moved = std::hypot(m.pos.x - from.x, m.pos.z - from.z);
  if (moved > 0.001f && moved < 2 && (m.onGround || m.surf == 1) && !m.inWater && !T.flyer && !m.calm) {
    float stride = fam == "spider" ? 0.55f : fam == "bone" ? 1.0f : fam == "zombie" ? 1.2f : m.w > 0.55f ? 1.6f : m.w < 0.3f ? 0.7f : 1.1f;
    m.stepAcc += moved;
    if (m.stepAcc >= stride) {
      m.stepAcc = 0;
      if (moved / std::max(dt, 1e-3f) > 3.5f && m.w >= 0.3f) lv.fx->particles("dust", m.pos, 2, {{"speed", 6}});   // a running beast kicks up the ground
      const char* own = ownStep(m);
      if (fam == "zombie" || fam == "bone" || fam == "spider") lv.fx->voice(fam, "step", m.pos);
      else if (*own) lv.fx->voice(own, "step", m.pos);
      else if (m.w >= 0.25f) {
        World& w = lv.world(m.realm);
        BlockId id = w.getBlock(ifloor(m.pos.x), ifloor(m.pos.y - 0.2f), ifloor(m.pos.z));
        if (id) lv.fx->material(reg().block(id).sound, "step", m.pos);
      }
    }
  }
  // hostile ones make their noises as they wander (the classic game's way)
  if (T.hostile && fam != "creeper" && !T.voice.empty()) {
    if (m.soundT > soundT0 + 0.5f) m.ambT = 5 + frand() * 8;   // it just spoke
    else {
      if (m.ambT < 0) m.ambT = 2 + frand() * 8;
      if ((m.ambT -= dt) <= 0) {
        m.ambT = 5 + frand() * 8;
        float near = 1e9f;
        for (Entity* q : pawnsOf(lv, m.realm)) near = std::min(near, std::hypot(q->pos.x - m.pos.x, q->pos.z - m.pos.z));
        if (near < 20) lv.fx->voice(T.voice, "say", m.pos + vec3(0, m.h * 0.8f, 0));
      }
    }
  }
}


} // namespace dq
