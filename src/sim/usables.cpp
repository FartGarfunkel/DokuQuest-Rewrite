// sim/usables.cpp — items the original used through its "drink: milk" hook that nothing else here handled:
// firework rockets sent up from the ground (batche.js FireworkRocket; the elytra boost is sim/elytra.cpp),
// the gust puff's great leap (aether.js), and the Ember Effigy that wakes a Cinder Golem at night (local.js summon).
#include "common/holiday.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/effects.h"
#include "core/registry.h"
#include "sim/creature_entities.h"
#include <unordered_map>
#include <chrono>
#include <set>
#include <cstring>
#include "sim/content.h"

namespace dq {
bool chestPartner(World& w, int x, int y, int z, ivec3& out);   // sim/build.cpp
// swap a block for another of its family (plain <-> painted) keeping its state: its meta and block entity (a chest's
// contents, a sign's words), a door's other half, a double chest's other half
static void repaintBlock(World& w, ivec3 at, BlockId to) {
  const BlockId from = w.getBlock(at.x, at.y, at.z);
  std::vector<ivec3> cells{at};
  if (reg().block(from).has("door")) cells.push_back(at + ivec3(0, (w.getMeta(at.x, at.y, at.z) & 16) ? -1 : 1, 0));
  ivec3 partner; if (chestPartner(w, at.x, at.y, at.z, partner)) cells.push_back(partner);
  for (const ivec3& c : cells) {
    if (w.getBlock(c.x, c.y, c.z) != from) continue;
    json* e = w.blockEntity(c.x, c.y, c.z); json keep = e ? *e : json();
    w.setBlock(c.x, c.y, c.z, to, w.getMeta(c.x, c.y, c.z));
    if (!keep.is_null()) w.setBlockEntity(c.x, c.y, c.z, keep);
  }
}

bool tryWitherSummon(Level& lv, int realm, ivec3 at);   // sim/wither.cpp

namespace {
Player* named(Level& lv, const std::string& n) {
  for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && p->name == n) return p;
  return nullptr;
}
void useUp(Player& p) { ItemStack& h = p.held(); if (--h.n <= 0) h.clear(); }

class FireworkRocket : public Entity {
public:
  FireworkRocket() { type = "firework"; w = 0.1f; h = 0.2f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    float fuse = data.value("fuse", 1.3f);
    vel.y = std::min(vel.y + 8 * dt, 22.f);
    vec3 n = pos + vel * dt;
    World& w = lv.world(realm);
    if (age >= fuse || reg().tSolid[w.getBlock(ifloor(n.x), ifloor(n.y + 0.2f), ifloor(n.z))]) {
      json ex = json::object(); std::string c = data.value("color", std::string()); if (!c.empty()) ex["color"] = c;
      lv.fx->particles("firework", pos, 60, ex);
      lv.fx->sound("firework_blast", pos, 90);
      if (rand() & 1) lv.fx->sound("firework_twinkle", pos, 70);
      removed = true; return;
    }
    pos = n;
    lv.fx->particles("flame", pos - vec3(0, 0.15f, 0), 1);
  }
};
} // namespace

void registerUsables() {
  registerEntityFactory("firework", [](const std::string&) { return std::make_unique<FireworkRocket>(); });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      Level* lv = nullptr; int h = 0, hp = 0;
      std::vector<std::pair<int, ivec3>> sponges;   // just placed: soak up the water round them next tick
      const char* name() const override { return "usables"; }
      ~S() override { if (lv && lv->events) { if (h) lv->events->off(h); if (hp) lv->events->off(hp); } }
      // Minecraft's sponge: the water within 6 steps (up to 65 blocks of it) is soaked up and the sponge turns wet
      double fwT = 0;
      // New Year's Eve and Day (common/holiday newYear()): after dark the villages let off fireworks, now and then,
      // from among their people
      void fireworks(Level& L, double dt) {
        if (L.isRemote() || !newYear() || (fwT -= dt) > 0) return;
        fwT = 1.2 + frand() * 2.2;
        if (envDay(L, DIM_OVERWORLD) > 0.32f) return;
        static const char* COL[] = {"red", "yellow", "lime", "light_blue", "magenta", "white", "orange", "blue"};   // wool colours (render/particles.cpp)
        std::vector<Entity*> folk;
        for (auto& e : L.entities()) if (!e->removed && e->type == "villager" && e->realm == REALM_MAIN && dimOfY(ifloor(e->pos.y)) == DIM_OVERWORLD) {
          for (auto& q : L.entities()) if (q->isPlayer() && !q->removed && q->realm == e->realm && glm::distance(q->pos, e->pos) < 96) { folk.push_back(e.get()); break; }
        }
        if (folk.empty()) return;
        Entity* v = folk[(size_t)(frand() * folk.size()) % folk.size()];
        auto r = std::make_unique<FireworkRocket>();
        r->realm = v->realm; r->pos = v->pos + vec3(frand2() * 2, 2.2f, frand2() * 2); r->vel = vec3(frand2() * 1.5f, 14 + frand() * 6, frand2() * 1.5f);
        r->data["fuse"] = 1.1f + frand() * 0.8f; r->data["color"] = COL[(int)(frand() * 8) & 7];
        L.fx->sound("firework_launch", r->pos, 64);
        L.spawn(std::move(r));
      }
      void tick(Level& L, double dt) override {
        fireworks(L, dt);
        if (sponges.empty()) return;
        static const BlockId sponge = reg().blockId("sponge"), wet = reg().blockId("wet_sponge");
        for (auto& [realm, at] : sponges) {
          World& w = L.world(realm);
          if (!sponge || !wet || w.getBlock(at.x, at.y, at.z) != sponge) continue;
          const Registry& R = reg();
          std::vector<std::pair<ivec3, int>> q{{at, 0}}; std::set<std::tuple<int, int, int>> seen{{at.x, at.y, at.z}};
          int n = 0;
          for (size_t i = 0; i < q.size() && n < 65; i++) {
            const auto [c, dist] = q[i];
            if (dist >= 6) continue;
            static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& d : D) {
              const ivec3 nb(c.x + d[0], c.y + d[1], c.z + d[2]);
              if (!seen.insert({nb.x, nb.y, nb.z}).second) continue;
              if (R.tLiquid[w.getBlock(nb.x, nb.y, nb.z)] != 1) continue;
              w.setBlock(nb.x, nb.y, nb.z, AIR); n++; q.push_back({nb, dist + 1});
              if (n >= 65) break;
            }
          }
          if (n > 0) {
            w.setBlock(at.x, at.y, at.z, wet, 0);
            L.fx->particles("splash", vec3(at) + vec3(0.5f, 1, 0.5f), 24);
            L.fx->sound("splash_small", vec3(at) + vec3(0.5f), 16);
          }
        }
        sponges.clear();
      }
    };
    auto s = std::make_shared<S>(); s->lv = &lv;
    if (lv.events) s->hp = lv.events->on("blockPlace", [&lv, sp = s.get()](const json& a) {   // [player, x, y, z, key]
      if (lv.isRemote() || !a.is_array() || a.size() < 5 || !a[4].is_string() || a[4].get<std::string>() != "sponge") return true;
      Player* p = a[0].is_string() ? named(lv, a[0].get<std::string>()) : nullptr;
      sp->sponges.push_back({p ? p->realm : REALM_MAIN, ivec3(a[1].get<int>(), a[2].get<int>(), a[3].get<int>())});
      return true;
    });
    if (lv.events) s->h = lv.events->on("itemUse", [&lv](const json& a) {   // [player, item, x, y, z, face]
      if (lv.isRemote() || !a.is_array() || a.size() < 6 || !a[1].is_string() || !a[0].is_string()) return true;
      const ItemDef* d = reg().item(a[1].get<std::string>());
      if (!d) return true;
      Player* p = named(lv, a[0].get<std::string>());
      if (!p || p->dead) return true;
      bool hitBlock = a[2].is_number() && a[5].is_number() && a[5].get<int>() >= 0;
      ivec3 hb = hitBlock ? ivec3(a[2].get<int>(), a[3].get<int>(), a[4].get<int>()) : ivec3(0);
      if (d->has("firework") && !p->gliding) {
        if (!hitBlock) return true;
        auto e = std::make_unique<FireworkRocket>();
        e->realm = p->realm; e->pos = vec3(hb) + vec3(0.5f, 1.1f, 0.5f);
        e->vel = vec3((rand() % 100 - 50) / 125.f, 10, (rand() % 100 - 50) / 125.f);
        e->data = {{"fuse", 1.1f + (rand() % 60) / 100.f}, {"color", d->str("fwColor")}};
        lv.fx->sound("firework_launch", e->pos, 40);
        lv.spawn(std::move(e));
        if (!p->creative()) useUp(*p);
        return false;
      }
      if (d->key == "egg" || d->key == "slime_ball") {   // thrown by hand like snowballs (theend.js; the End story throws those)
        static std::unordered_map<u32, double> cd;
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (cd[p->id] > now) return false;
        cd[p->id] = now + 0.25;
        vec3 lk = lookVec(*p);
        vec3 eye = p->pos + vec3(0, 1.62f, 0);
        spawnProjectile(lv, p->realm, eye - vec3(0, 0.1f, 0) + lk * 0.5f, lk * 18.f + vec3(0, 2, 0), d->key == "egg" ? "egg" : d->key == "slime_ball" ? "slime" : "snow", p);
        if (!p->creative()) useUp(*p);
        p->swingT = 0.01f; lv.fx->soundUi(p->id, "bow");
        return false;
      }
      if (d->has("gustPuff")) {
        if (!p->creative()) useUp(*p);
        p->vel.y = std::max(p->vel.y, 12.5f); p->onGround = false; p->hasFallStart = false;
        giveEffect(lv, *p, "slowfall", 3);
        lv.fx->particles("poof", p->pos + vec3(0, 0.1f, 0), 18);
        lv.fx->sound("wind_charge", p->pos, 24);
        return false;
      }
      if (d->has("spawnEgg")) {   // a spawn egg on a block: the creature steps out on that face; on a monster cage it sets the cage
        if (!hitBlock) return true;
        std::string type = d->str("spawnEgg");
        if (!isCreatureType(type)) return true;
        World& w = lv.world(p->realm);
        if (reg().block(w.getBlock(hb.x, hb.y, hb.z)).has("statue")) return true;   // on a statue's plinth it carves the figure (sim/build.cpp)
        if (w.getBlock(hb.x, hb.y, hb.z) == reg().blockId("monster_spawner")) {
          if (json* be = w.blockEntity(hb.x, hb.y, hb.z, true)) { (*be)["type"] = "spawner"; (*be)["mob"] = type; w.blockEntityChanged(hb.x, hb.y, hb.z); }
          if (!p->creative()) useUp(*p);
          return false;
        }
        static const int F[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int* f = F[std::clamp(a[5].get<int>(), 0, 5)];
        vec3 at = vec3(hb.x + f[0], hb.y + f[1], hb.z + f[2]) + vec3(0.5f, 0, 0.5f);
        if (f[1] < 0) at.y -= 1;   // under a ceiling: hang down from it
        if (Creature* c = spawnCreature(lv, p->realm, type, at)) {
          c->yaw = p->yaw + (float)PI;   // facing whoever hatched it
          lv.fx->particles("poof", at + vec3(0, 0.5f, 0), 8);
          if (!p->creative()) useUp(*p);
        }
        return false;
      }
      if (d->has("mobHead")) {   // a mob head on a block: it stands on that face, turned to whoever placed it (16 ways)
        if (!hitBlock) return true;
        static const int F[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int* f = F[std::clamp(a[5].get<int>(), 0, 5)];
        ivec3 at(hb.x + f[0], hb.y + f[1], hb.z + f[2]);
        World& w = lv.world(p->realm);
        BlockId head = reg().blockId("mob_head"), cur = w.getBlock(at.x, at.y, at.z);
        if (!head || (cur && !reg().tReplaceable[cur]) || reg().tLiquid[cur]) return false;
        if (glm::distance(vec3(at) + vec3(0.5f), p->pos + vec3(0, 0.9f, 0)) < 0.9f) return false;   // not inside yourself
        w.setBlock(at.x, at.y, at.z, head, 0);
        int rot = ((int)std::lround((p->yaw + PI) / (TAU / 16)) % 16 + 16) % 16;
        if (json* be = w.blockEntity(at.x, at.y, at.z, true)) { *be = {{"type", "head"}, {"mob", d->str("mobHead")}, {"rot", rot}, {"item", ItemStack(d->key, 1).toJson()}}; w.blockEntityChanged(at.x, at.y, at.z); }
        lv.fx->material("stone", "place", vec3(at) + vec3(0.5f));
        if (!p->creative()) useUp(*p);
        p->swingT = 0.01f;
        if (d->str("mobHead") == "wither_skeleton") tryWitherSummon(lv, p->realm, at);   // the third skull on a T of soul sand
        return false;
      }
      if (d->key == "wet_sponge" && hitBlock) {   // BTA: a wet sponge washes the dye out of wool, glass, terracotta and planks
        World& w = lv.world(p->realm);
        const std::string k = reg().block(w.getBlock(hb.x, hb.y, hb.z)).key;
        static const char* COLS[16] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
        std::string rest;
        for (const char* c : COLS) { const std::string pre = std::string(c) + "_"; if (k.rfind(pre, 0) == 0) { rest = k.substr(pre.size()); break; } }
        const std::string to = rest == "wool" ? "wool" : rest == "terracotta" ? "terracotta" : rest == "stained_glass" ? "glass" :
                               rest == "stained_glass_pane" ? "glass_pane" : rest == "painted_planks" ? "planks" : "";
        BlockId tid = to.empty() ? 0 : reg().blockId(to);
        const BlockDef& wd = reg().block(w.getBlock(hb.x, hb.y, hb.z));
        if (!tid && wd.has("paintOf")) tid = reg().blockId(wd.str("paintOf"));   // painted doors, trapdoors, buttons, plates, chests
        if (!tid && wd.has("sign")) if (json* e = w.blockEntity(hb.x, hb.y, hb.z); e && e->is_object() && e->contains("paint")) {
          e->erase("paint"); w.blockEntityChanged(hb.x, hb.y, hb.z);
          lv.fx->particles("splash", vec3(hb) + vec3(0.5f, 1, 0.5f), 10); lv.fx->sound("splash_small", vec3(hb) + vec3(0.5f), 12); p->swingT = 0.01f;
          return false;
        }
        if (tid && reg().block(tid).key != k) {
          repaintBlock(w, hb, tid);
          lv.fx->particles("splash", vec3(hb) + vec3(0.5f, 1, 0.5f), 10);
          lv.fx->sound("splash_small", vec3(hb) + vec3(0.5f), 12);
          p->swingT = 0.01f;
          return false;
        }
      }
      if (d->has("paintbrush")) {   // a dye in the other hand: planks, wool, glass and terracotta take its colour
        if (!hitBlock) return true;
        ItemStack* dye = p->offhand.empty() || p->offhand[0].empty() ? nullptr : &p->offhand[0];
        const ItemDef* dd = dye ? dye->def() : nullptr;
        std::string col = dd ? dd->str("dye") : "";
        if (col.empty()) { lv.fx->message(p->id, "Hold a dye in your other hand to paint with"); return false; }
        World& w = lv.world(p->realm);
        const std::string k = reg().block(w.getBlock(hb.x, hb.y, hb.z)).key;
        auto ends = [&](const char* e) { size_t n = std::strlen(e); return k.size() >= n && k.compare(k.size() - n, n, e) == 0; };
        std::string to;
        const BlockDef& bd = reg().block(w.getBlock(hb.x, hb.y, hb.z));
        const bool wooden = bd.sound == "wood" || bd.has("painted");
        bool signPaint = false;
        if (k == "planks" || ends("_planks")) to = col + "_painted_planks";
        else if (k == "wool" || ends("_wool")) to = col == "white" ? "wool" : col + "_wool";
        else if (k == "glass" || ends("_stained_glass")) to = col + "_stained_glass";
        else if (k == "terracotta" || ends("_terracotta")) to = col + "_terracotta";
        // BTA's painted wood: doors, trapdoors, buttons, pressure plates and chests of any wood, signs (their board), lamps
        else if (bd.has("door") && wooden) to = col + "_painted_door";
        else if (bd.has("trapdoor") && wooden) to = col + "_painted_trapdoor";
        else if (bd.str("rs") == "button" && (bd.has("wooden") || wooden)) to = col + "_painted_button";
        else if (bd.str("rs") == "plate" && wooden) to = col + "_painted_pressure_plate";
        else if (k == "chest" || bd.str("paintOf") == "chest") to = col + "_painted_chest";
        else if (bd.str("rs") == "lamp" && !bd.has("inverted")) to = col + (ends("_on") ? "_lamp_on" : "_lamp");
        else if (bd.has("sign")) signPaint = true;
        if (signPaint) {
          json* e = w.blockEntity(hb.x, hb.y, hb.z, true);
          if (!e) return false;
          if (!e->is_object()) *e = {{"type", "sign"}};
          if (e->value("paint", std::string()) == col) return false;
          (*e)["paint"] = col; w.blockEntityChanged(hb.x, hb.y, hb.z);
        } else {
          BlockId tid = to.empty() ? 0 : reg().blockId(to);
          if (!tid || to == k) return false;
          repaintBlock(w, hb, tid);
        }
        lv.fx->particles("dust", vec3(hb) + vec3(0.5f), 8);
        lv.fx->material("wool", "place", vec3(hb) + vec3(0.5f));
        if (!p->creative()) {
          if (--dye->n <= 0) dye->clear();
          ItemStack& h = p->held(); h.d++;
          if (d->hasTool && h.d >= d->tool.durability) h.clear();
        }
        p->swingT = 0.01f;
        return false;
      }
      if (d->has("summon")) {
        if (!hitBlock || a[5].get<int>() != 2) { lv.fx->message(p->id, "Place the effigy on top of a block"); return false; }
        for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (c->type == "cinder_golem" && c->alive()) { lv.fx->message(p->id, "A Cinder Golem already walks this world", "boss"); return false; }
        int dim = dimOfY(ifloor(p->pos.y));
        if (p->realm == REALM_MAIN && dim == DIM_OVERWORLD && envDay(lv, dim) > 0.45f) { lv.fx->message(p->id, "The effigy lies cold. It only stirs at night"); return false; }
        useUp(*p);
        summonGolem(lv, p->realm, vec3(hb) + vec3(0.5f, 1, 0.5f), p);
        return false;
      }
      return true;
    });
    return s;
  });
}

} // namespace dq
