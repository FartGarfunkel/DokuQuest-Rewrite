// sim/snow_sentry.cpp — the Snow Sentry (BTA's snowman, our own make): set a carved pumpkin or a jack-o'-lantern on two
// stacked snow blocks and they come alive as a sentry that throws snowballs at monsters near it (they push them back,
// and burn the creatures of fire). It leaves a dusting of snow where it walks in the cold, and melts away in hot lands
// and in the Depths. Two snowballs of snow sit at its base when it falls.
#include "sim/gameplay.h"
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
#include "sim/player.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

namespace {
void injectSentry() {
  json& mobs = reg().mobs; json& models = reg().models;
  if (!mobs.is_object() || !models.is_object() || mobs.contains("snow_sentry")) return;
  mobs["snow_sentry"] = {{"model", "snow_sentry"}, {"label", "a snow sentry"}, {"hp", 4}, {"w", 0.35}, {"h", 1.75}, {"speed", 1.4}, {"xp", 0},
                         {"noDismember", true}, {"voice", "golem"}, {"drops", json::array({json::array({"snowball", 0, 15, 1.0})})}};
  auto part = [](const char* name, json parent, std::array<float, 3> at, std::array<float, 6> box, json tex, const char* role, const char* limb) {
    json p = {{"name", name}, {"parent", parent}, {"at", {at[0], at[1], at[2]}}, {"box", {box[0], box[1], box[2], box[3], box[4], box[5]}}, {"tex", tex},
              {"pivot", {at[0] / 16, at[1] / 16, at[2] / 16}}, {"min", {box[0] / 16, box[1] / 16, box[2] / 16}}, {"max", {box[3] / 16, box[4] / 16, box[5] / 16}}};
    if (role) p["role"] = role;
    if (limb) p["limb"] = limb;
    return p;
  };
  models["snow_sentry"] = {{"rig", "biped"}, {"scale", 1.0}, {"parts", json::array({
    part("body", nullptr, {0, 0, 0}, {-5, 0, -5, 5, 10, 5}, "snow", "body", nullptr),
    part("chest", "body", {0, 10, 0}, {-4, 0, -4, 4, 8, 4}, "snow", nullptr, nullptr),
    part("head", "body", {0, 18, 0}, {-4, 0, -4, 4, 8, 4}, json{{"all", "pumpkin_side"}, {"top", "pumpkin_top"}, {"front", "carved_pumpkin"}}, "head", "head"),
    part("armR", "body", {4, 15, 0}, {0, -0.5f, -0.5f, 9, 0.5f, 0.5f}, "dark_oak_log_side", "armR", "armR"),
    part("armL", "body", {-4, 15, 0}, {-9, -0.5f, -0.5f, 0, 0.5f, 0.5f}, "dark_oak_log_side", "armL", "armL")})}};
}

struct Mind { u32 foe = 0; float scan = 0, hot = 0; vec3 lastSnow{1e9f}; };
std::unordered_map<u32, Mind>& minds() { static std::unordered_map<u32, Mind> m; return m; }

OptMove sentryBrain(Level& lv, Creature& m, Entity*, float, float, float, float dt) {
  Mind& M = minds()[m.id];
  World& w = lv.world(m.realm);
  // melting: the Depths, and lands too warm for snow
  const int d = dimOfY(ifloor(m.pos.y));
  const Chunk* ch = w.chunk(d, ifloor(m.pos.x) >> 4, ifloor(m.pos.z) >> 4);
  const int temp = ch ? ch->climate[((ifloor(m.pos.z) & 15) * 16 + (ifloor(m.pos.x) & 15)) * 2] : 128;
  if (d == DIM_NETHER || temp > 200) {
    if ((M.hot += dt) > 1) { M.hot = 0; DamageSrc s; damageEntity(lv, m, 1, nullptr, "burn", s); mobBurst(lv, m.pos + vec3(0, 1, 0), "snow", 6, 0.6f); }
  }
  // a dusting of snow where it walks in the cold
  if (m.onGround && temp < 120 && d == DIM_OVERWORLD && glm::distance(m.pos, M.lastSnow) > 0.9f) {
    M.lastSnow = m.pos;
    static const BlockId layer = reg().blockId("snow_layer");
    const int x = ifloor(m.pos.x), y = ifloor(m.pos.y + 0.05f), z = ifloor(m.pos.z);
    if (layer && w.getBlock(x, y, z) == AIR && reg().tSolid[w.getBlock(x, y - 1, z)] && reg().tOpaque[w.getBlock(x, y - 1, z)]) w.setBlock(x, y, z, layer, 0);
  }
  // a monster near: face it and throw
  Entity* foe = M.foe ? lv.entity(M.foe) : nullptr;
  if (foe && (foe->removed || foe->hp <= 0 || glm::distance(foe->pos, m.pos) > 16)) { foe = nullptr; M.foe = 0; }
  if (!foe && (M.scan -= dt) <= 0) {
    M.scan = 0.5f; float bd = 12;
    for (auto& e : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c == &m || c->removed || !c->alive() || c->realm != m.realm || !c->T->hostile) continue;
      const float dd = glm::distance(c->pos, m.pos);
      if (dd < bd && std::fabs(c->pos.y - m.pos.y) < 6) { bd = dd; foe = c; }
    }
    M.foe = foe ? foe->id : 0;
  }
  if (!foe) return std::nullopt;   // nothing to do: it wanders like any creature
  const vec3 to = foe->pos + vec3(0, foe->h * 0.6f, 0) - (m.pos + vec3(0, 1.4f, 0));
  const float flat = std::hypot(to.x, to.z), face = std::atan2(-to.x, -to.z);
  m.yaw = face; m.lookAt = foe->id;
  if (m.attackCd <= 0) {
    m.attackCd = 0.9f + frand() * 0.3f; m.swingT = 0.25f;
    const float sp = 15, t = flat / sp;   // aimed a little high for the fall
    const vec3 v(to.x / std::max(0.1f, t), to.y / std::max(0.1f, t) + 10 * t, to.z / std::max(0.1f, t));
    spawnProjectile(lv, m.realm, m.pos + vec3(0, 1.4f, 0) + vec3(to.x, 0, to.z) / std::max(0.1f, flat) * 0.4f, v, "snow", &m);
    lv.fx->sound("bow", m.pos, 16, 0.6f, 1.3f);
  }
  return Move{flat > 9 ? m.T->speed : 0.f, face};
}

struct BuildWatch : LevelSystem {
  Level* L = nullptr; int h = 0;
  std::vector<std::pair<int, ivec3>> fresh;
  const char* name() const override { return "snow-sentry"; }
  ~BuildWatch() override { if (L && L->events && h) L->events->off(h); }
  void tick(Level& lv, double) override {
    if (fresh.empty()) return;
    static const BlockId snow = reg().blockId("snow_block"), carved = reg().blockId("carved_pumpkin"), lantern = reg().blockId("jack_o_lantern");
    for (auto& [realm, p] : fresh) {
      World& w = lv.world(realm);
      const BlockId top = w.getBlock(p.x, p.y, p.z);
      if (!snow || (top != carved && top != lantern) || w.getBlock(p.x, p.y - 1, p.z) != snow || w.getBlock(p.x, p.y - 2, p.z) != snow) continue;
      for (int k = 0; k < 3; k++) w.setBlock(p.x, p.y - k, p.z, AIR);
      if (Creature* c = spawnCreature(lv, realm, "snow_sentry", vec3(p.x + 0.5f, (float)(p.y - 2), p.z + 0.5f))) { c->kept = true; mobBurst(lv, c->pos + vec3(0, 1, 0), "snow", 24, 1.4f); }
      sfxMaterial(lv, "snow", "place", vec3(p) + vec3(0.5f));
    }
    fresh.clear();
  }
};
} // namespace

void registerSnowSentry() {
  static bool done = false;
  if (done) return;
  done = true;
  injectSentry();
  reloadCreatureTypes();
  { CreatureHooks h; h.brain = sentryBrain; setCreatureHooks("snow_sentry", h); }
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    auto s = std::make_shared<BuildWatch>(); s->L = &lv;
    if (lv.events) s->h = lv.events->on("blockPlace", [&lv, b = s.get()](const json& a) {   // [player, x, y, z, key]
      if (lv.isRemote() || !a.is_array() || a.size() < 5 || !a[4].is_string()) return true;
      const std::string k = a[4];
      if (k != "carved_pumpkin" && k != "jack_o_lantern") return true;
      int realm = REALM_MAIN;
      for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (a[0].is_string() && p->name == a[0].get<std::string>()) realm = p->realm;
      b->fresh.push_back({realm, ivec3(a[1].get<int>(), a[2].get<int>(), a[3].get<int>())});
      return true;
    });
    return s;
  });
}

} // namespace dq
