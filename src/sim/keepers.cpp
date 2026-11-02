// sim/keepers.cpp — the bosses of dungeons and of the great places (game/dungeons.js dungeonTick, structures.js
// structTick): a dungeon's keeper wakes when someone walks into its hall, a place's master (the lich king, the pirate
// captain, the ember lord...) when someone comes into its box (the plan's data "boss": {at, type[, box]}, band-local y).
// Once a time: if it's lost while nobody watches (everyone left), it's back at its post next time; killed, the dungeon or
// the place is cleared for good (world data "dungeons" / "structs" {id: {cleared}}: the maps and the quest board read it).
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/content.h"
#include "gen/structures.h"
#include "gen/structures_api.h"
#include "gen/generator.h"
#include "core/registry.h"
#include <set>

namespace dq {

void grantXP(Level& lv, Player& p, int n, const vec3* at);   // sim/gameplay.h (not included: content.h declares its kin)

namespace {
class KeeperSystem : public LevelSystem {
public:
  explicit KeeperSystem(Level& l) : lv(l) {
    if (lv.events) hDeath = lv.events->on("mobDeath", [this](const json& a) { onDeath(a); return true; });
  }
  ~KeeperSystem() override { if (lv.events && hDeath) lv.events->off(hDeath); }
  const char* name() const override { return "keepers"; }

  void tick(Level& l, double dt) override {
    if (l.isRemote() || !l.gen || (t -= dt) > 0) return;
    t = 1;
    // who is up now (a save brings them back with their marks)
    up.clear();
    for (auto& e : l.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->alive()) {
      if (c->data.contains("dungeon") && c->data["dungeon"].is_string()) up[c->id] = {c->data["dungeon"].get<std::string>(), true};
      else if (c->data.contains("structBoss") && c->data["structBoss"].is_string()) up[c->id] = {c->data["structBoss"].get<std::string>(), false};
    }
    std::vector<Player*> ps;
    for (auto& e : l.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && !p->dead && p->realm == REALM_MAIN && p->mode != "spectator") ps.push_back(p);
    std::set<std::string> seen;
    for (Player* p : ps) {
      const int dim = dimOfY(ifloor(p->pos.y)), base = dimBase(dim);
      const StructureIndex* idx = structureIndexOf(l.gen->forDim(dim));
      if (!idx || !idx->active()) continue;
      std::vector<const StructurePlan*> plans;
      idx->near(ifloor(p->pos.x), ifloor(p->pos.z), 28, plans, NEAR_PLACES);
      for (const StructurePlan* S : plans) {
        if (!S || S->id.empty() || !S->data.is_object() || !S->data.contains("boss") || !S->data["boss"].is_object() || !seen.insert(S->id).second) continue;
        const json& B = S->data["boss"];
        const std::string type = B.value("type", std::string());
        if (!B.contains("at") || !B["at"].is_array() || B["at"].size() < 3 || !isCreatureType(type)) continue;
        const bool dungeon = S->type >= 0 && S->type < (int)structureTypes().size() && structureTypes()[S->type].key == "dungeon";
        json& recs = l.info.data[dungeon ? "dungeons" : "structs"];
        if (!recs.is_object()) recs = json::object();
        json& rec = recs[S->id];
        if (!rec.is_object()) rec = json::object();
        if (rec.contains("cleared") && (rec["cleared"] == true || (rec["cleared"].is_number() && rec["cleared"].get<int>() != 0))) continue;
        const vec3 at(B["at"][0].get<float>() + 0.5f, B["at"][1].get<float>() + base, B["at"][2].get<float>() + 0.5f);
        bool live = false; for (auto& [id, k] : up) if (k.plan == S->id) live = true;
        // the box it keeps: a dungeon's boss hall (5 out each way, 7 high), or the place's own box
        auto inside = [&](const Player& q, float pad) {
          if (B.contains("box") && B["box"].is_array() && B["box"].size() >= 6) {
            const json& b = B["box"];
            return q.pos.x >= b[0].get<float>() - pad && q.pos.x <= b[3].get<float>() + 1 + pad && q.pos.z >= b[2].get<float>() - pad && q.pos.z <= b[5].get<float>() + 1 + pad &&
                   q.pos.y >= b[1].get<float>() + base - 1 - pad && q.pos.y <= b[4].get<float>() + base + pad;
          }
          return std::fabs(q.pos.x - at.x) < 5.5f + pad && std::fabs(q.pos.z - at.z) < 5.5f + pad && q.pos.y >= at.y - 1 - pad && q.pos.y < at.y + 7 + pad;
        };
        if (rec.value("bossUp", 0) && !live) {   // lost while nobody watched: back at its post next time
          bool near = false; for (Player* q : ps) if (inside(*q, 16)) near = true;
          if (!near) rec["bossUp"] = 0;
        }
        if (rec.value("bossUp", 0) || live) continue;
        Player* in = nullptr; for (Player* q : ps) if (inside(*q, 0)) { in = q; break; }
        if (!in) continue;
        Creature* m = spawnCreature(l, REALM_MAIN, type, at);
        if (!m) continue;
        m->data[dungeon ? "dungeon" : "structBoss"] = S->id; m->huntT = 60; m->persistent = true; m->kept = true;
        if (type == "valkyrie_queen" && isCreatureType("valkyrie"))   // her guard wakes with her (tools/port/skylands_more.py)
          for (int k = 0; k < 2; k++) if (Creature* g = spawnCreature(l, REALM_MAIN, "valkyrie", at + vec3(k ? 2.5f : -2.5f, 0, 1.5f))) { g->huntT = 60; g->persistent = true; }
        rec["bossUp"] = 1;
        l.fx->sound(dungeon ? "spell_zap" : "levelup", at + vec3(0, 2, 0), 40);
        l.fx->particles(dungeon ? "smoke" : "mana_arcane", at + vec3(0, 1, 0), 40);
        std::string label = m->T->label.empty() ? type : m->T->label;
        if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
        for (Player* q : ps) if (std::hypot(q->pos.x - at.x, q->pos.z - at.z) < (dungeon ? 30 : 40)) toast(l, *q, label + (dungeon ? " awakens!" : " rises to meet you!"), "warn");
      }
    }
  }

private:
  struct Up { std::string plan; bool dungeon; };
  void onDeath(const json& a) {
    if (lv.isRemote() || !a.is_array() || a.empty() || !a[0].is_object()) return;
    const u32 id = a[0].value("id", 0u);
    auto it = up.find(id);
    if (it == up.end()) return;
    const Up k = it->second; up.erase(it);
    const vec3 at(a[0].value("x", 0.f), a[0].value("y", 0.f), a[0].value("z", 0.f));
    json& recs = lv.info.data[k.dungeon ? "dungeons" : "structs"];
    if (!recs.is_object()) recs = json::object();
    json& rec = recs[k.plan]; if (!rec.is_object()) rec = json::object();
    rec["cleared"] = k.dungeon ? json(true) : json(1); rec["bossUp"] = 0;
    const std::string type = a[0].value("type", std::string());
    const CreatureType* T = creatureType(type);
    std::string label = T && !T->label.empty() ? T->label : type;
    if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
    for (Player* q : levelPlayers(lv)) if (std::hypot(q->pos.x - at.x, q->pos.z - at.z) < (k.dungeon ? 48 : 60)) {
      toast(lv, *q, label + (k.dungeon ? " is defeated! The dungeon is cleared." : " is defeated! Its treasure is yours."), "epic");
      if (!k.dungeon) { const vec3 xp = at + vec3(0, 2, 0); grantXP(lv, *q, 150, &xp); }
    }
    if (k.dungeon) {
      dropItem(lv, REALM_MAIN, ItemStack("emerald", 4 + (int)(frand() * 6)), at + vec3(0, 1, 0), true);
      if (frand() < 0.35f && reg().item("heart_crystal")) dropItem(lv, REALM_MAIN, ItemStack("heart_crystal", 1), at + vec3(0, 1, 0), true);
    }
  }
  Level& lv;
  int hDeath = 0;
  double t = 0;
  std::unordered_map<u32, Up> up;
};
} // namespace

void registerKeepers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level& l) -> std::shared_ptr<LevelSystem> { return std::make_shared<KeeperSystem>(l); });
}

} // namespace dq
