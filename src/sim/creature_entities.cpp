// sim/creature_entities.cpp — dropped items, projectiles, felled trees and severed parts
// (game/entities.js DroppedItem / Projectile / FallingTree, game/blocks2.js Arrow, game/monsters.js HexFlask,
// game/bestiary.js llama spit, game/dismember.js Gib, game/theend.js snowballs' landing puff).
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
#include "sim/explosion.h"
#include "sim/physics.h"
#include "sim/player.h"
#include "core/registry.h"

namespace dq {

static bool solidAt(const World& w, vec3 p) { return reg().tSolid[w.getBlock(ifloor(p.x), ifloor(p.y), ifloor(p.z))]; }
static bool boxFree(const World& w, vec3 p, float hw, float h) {
  std::vector<AABB> tmp; AABB box{p - vec3(hw, 0, hw), p + vec3(hw, h, hw)};
  for (int y = ifloor(box.lo.y) - 1; y <= ifloor(box.hi.y); y++) for (int z = ifloor(box.lo.z); z <= ifloor(box.hi.z); z++) for (int x = ifloor(box.lo.x); x <= ifloor(box.hi.x); x++) {
    tmp.clear(); blockBoxes(w, x, y, z, tmp);
    for (auto& b : tmp) if (b.overlaps(box)) return false;
  }
  return true;
}
static Creature* creatureHit(Level& lv, int realm, vec3 p, u32 skip, float pad) {
  for (auto& e : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c || c->removed || !c->alive() || c->realm != realm || c->id == skip) continue;
    if (p.x > c->pos.x - c->w - pad && p.x < c->pos.x + c->w + pad && p.z > c->pos.z - c->w - pad && p.z < c->pos.z + c->w + pad && p.y > c->pos.y && p.y < c->pos.y + c->h) return c;
  }
  return nullptr;
}

// ------------------------------------------------------------------ dropped items
// (dropped items are sim/gameplay.cpp's ItemEntity: one "item" entity kind)

// ------------------------------------------------------------------ arrows (blocks2.js)
static bool arrowHitsPawn(Level& lv, Entity& arrow, vec3 n, Entity* owner) {
  if (owner && owner->isPlayer()) return false;   // a player's own arrows don't hit players
  for (Entity* q : pawnsOf(lv, arrow.realm)) {
    if (q == owner) continue;
    if (std::fabs(n.x - q->pos.x) < 0.4f && std::fabs(n.z - q->pos.z) < 0.4f && n.y > q->pos.y && n.y < q->pos.y + 1.85f) {
      float speed = glm::length(arrow.vel);
      DamageSrc s; s.tool = "arrow";
      damageEntity(lv, *q, std::max(1.f, std::round(arrow.data.value("damage", 5.f) * speed / 30)), owner ? owner : &arrow, "arrow", s);
      lv.fx->sound("hit", q->pos + vec3(0, 1, 0));
      return true;
    }
  }
  return false;
}
class Arrow : public Entity {
public:
  Arrow() { type = "arrow"; w = 0.05f; h = 0.05f; noGravity = true; }
  void aim() { float s = std::hypot(vel.x, vel.z); if (s > 0.01f || std::fabs(vel.y) > 0.01f) { yaw = std::atan2(-vel.x, -vel.z); pitch = std::atan2(vel.y, s); } }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& wd = lv.world(realm);
    const Registry& R = reg();
    if (data.value("stuck", false)) {
      if (age > 60) { removed = true; return; }
      ivec3 b(data["bx"].get<int>(), data["by"].get<int>(), data["bz"].get<int>());
      if (!R.tSolid[wd.getBlock(b.x, b.y, b.z)]) { data["stuck"] = false; vel = vec3(0); return; }
      if (data.value("pickup", false)) for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) {
        if (p->dead || p->removed || p->realm != realm) continue;
        if (std::fabs(p->pos.x - pos.x) < 1.6f && std::fabs(p->pos.z - pos.z) < 1.6f && pos.y > p->pos.y - 1.2f && pos.y < p->pos.y + 2.8f)
          if (p->give(ItemStack("arrow", 1)) <= 0) { removed = true; lv.fx->soundUi(p->id, "pop"); return; }
      }
      return;
    }
    if (age > 30) { removed = true; return; }
    Entity* owner = lv.entity(data.value("owner", 0u));
    for (int s = 0; s < 4; s++) {
      float hh = dt / 4;
      vel.y -= 20 * hh;
      float drag = std::max(0.f, 1 - hh * 0.25f); vel *= drag;
      vec3 n = pos + vel * hh;
      if (arrowHitsPawn(lv, *this, n, owner)) { removed = true; return; }
      if (Creature* c = creatureHit(lv, realm, n, owner ? owner->id : 0, 0.1f)) {
        float speed = glm::length(vel), sl = speed > 0 ? speed : 1;
        DamageSrc ds; ds.knock = 3; ds.tool = "arrow"; ds.hasRay = true; ds.rayO = pos; ds.rayD = vel / sl;
        if (data.value("burning", false)) ds.element = "fire";
        float dealt = damageEntity(lv, *c, std::max(1.f, std::round(data.value("damage", 5.f) * speed / 30)), owner ? owner : this, "arrow", ds);
        if (dealt > 0 && owner && owner->isPlayer()) floatText(lv, owner->id, c->pos + vec3(0, c->h + 0.2f, 0), std::to_string((int)std::round(dealt)), "#ffffff");
        lv.fx->sound("hit", c->pos);
        removed = true; return;
      }
      ivec3 b(ifloor(n.x), ifloor(n.y), ifloor(n.z));
      BlockId id = wd.getBlock(b.x, b.y, b.z);
      if (R.tSolid[id]) {
        bool hitIt = true;
        if (R.tShape[id]) { std::vector<AABB> bs; blockBoxes(wd, b.x, b.y, b.z, bs); hitIt = bs.empty(); for (auto& bb : bs) if (n.x >= bb.lo.x && n.x <= bb.hi.x && n.y >= bb.lo.y && n.y <= bb.hi.y && n.z >= bb.lo.z && n.z <= bb.hi.z) hitIt = true; }
        if (hitIt) {
          aim(); data["stuck"] = true; data["bx"] = b.x; data["by"] = b.y; data["bz"] = b.z; age = 0; vel = vec3(0);
          lv.fx->sound("arrow_hit", n);
          if (lv.events) lv.events->emit("arrowHit", json::array({id, b.x, b.y, b.z, n.x, n.y, n.z}));   // targets, buttons, TNT (gameplay)
          return;
        }
      }
      pos = n;
    }
    aim();
  }
};

// ------------------------------------------------------------------ fireballs, spit, snowballs
class Projectile : public Entity {
public:
  Projectile() { type = "projectile"; w = 0.2f; h = 0.4f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& wd = lv.world(realm);
    std::string kind = data.value("kind", "fireball");
    Entity* owner = lv.entity(data.value("owner", 0u));
    if (kind == "spit" || kind == "snow") {
      if (age > (kind == "spit" ? 3 : 8)) { removed = true; return; }
      vel.y -= (kind == "spit" ? 12 : 20) * dt;
      for (int s = 0; s < 3; s++) {
        pos += vel * (dt / 3);
        for (Entity* p : pawnsOf(lv, realm)) if (p != owner && std::fabs(p->pos.x - pos.x) < 0.5f && std::fabs(p->pos.z - pos.z) < 0.5f && pos.y > p->pos.y - 0.2f && pos.y < p->pos.y + 1.9f) {
          DamageSrc d; d.knock = 2;
          damageEntity(lv, *p, kind == "spit" ? 1 : 0.001f, owner, "projectile", d);
          if (kind == "snow") knockback(*p, glm::normalize(vec3(vel.x, 0, vel.z) + vec3(1e-4f)) * 2.f);
          splat(lv, kind); return;
        }
        if (kind == "snow") if (Creature* c = creatureHit(lv, realm, pos, owner ? owner->id : 0, 0.1f)) { DamageSrc d; d.knock = 2; damageEntity(lv, *c, c->type == "cinder_golem" ? 1 : 0.001f, owner, "projectile", d); splat(lv, kind); return; }
        if (solidAt(wd, pos)) { splat(lv, kind); return; }
      }
      if (kind == "spit" && frand() < dt * 20) mobBurst(lv, pos, "white", 1, 0.1f);
      return;
    }
    if (kind == "gust") {   // aether.js: a zephyr's gust — a little hurt and a big shove
      if (age > 4) { gustPop(lv); return; }
      if (frand() < dt * 30) mobBurst(lv, pos, "gust_ball", 1, 0.15f);
      for (int s = 0; s < 3; s++) {
        pos += vel * (dt / 3);
        for (Entity* p : pawnsOf(lv, realm)) if (std::fabs(p->pos.x - pos.x) < 0.7f && std::fabs(p->pos.z - pos.z) < 0.7f && pos.y > p->pos.y - 0.3f && pos.y < p->pos.y + 2) {
          float l = std::max(1e-3f, std::hypot(vel.x, vel.z));
          DamageSrc d; d.knock = 0; damageEntity(lv, *p, 1, owner, "projectile", d);
          knockback(*p, vec3(vel.x / l * 13, 6.5f, vel.z / l * 13));
          gustPop(lv); return;
        }
        if (Creature* c = creatureHit(lv, realm, pos, owner ? owner->id : 0, 0.4f)) if (!c->T->flyer) { c->vel.x += vel.x * 0.9f; c->vel.z += vel.z * 0.9f; c->vel.y = std::max(c->vel.y, 5.f); gustPop(lv); return; }
        if (solidAt(wd, pos)) { gustPop(lv); return; }
      }
      return;
    }
    if (age > 5) { removed = true; return; }
    for (int s = 0; s < 3; s++) {
      pos += vel * (dt / 3);
      for (Entity* p : pawnsOf(lv, realm)) if (std::fabs(p->pos.x - pos.x) < 0.55f && std::fabs(p->pos.z - pos.z) < 0.55f && pos.y > p->pos.y - 0.2f && pos.y < p->pos.y + 1.9f) {
        DamageSrc d; d.element = "fire"; d.knock = 4;
        damageEntity(lv, *p, 4, owner, "projectile", d);
        igniteEntity(*p, 2.5f);
        explodeFx(lv); return;
      }
      if (solidAt(wd, pos)) { explodeFx(lv); return; }
    }
    if (frand() < dt * 40) flame(lv, pos);
  }
  void explodeFx(Level& lv) {
    removed = true; mobBurst(lv, pos, "fireball", 10, 0.8f); lv.fx->sound("burn", pos);
    if (data.value("kind", "") == "ghast") { ExplodeOpts o; o.source = data.value("owner", 0u); explode(lv, realm, pos, 1, o); }   // nether2.js: a ghast's fireball bursts open what it hits
  }
  void gustPop(Level& lv) { removed = true; mobBurst(lv, pos, "gust_ball", 16, 2.5f); lv.fx->sound("gust_pop", pos, 20); }
  void splat(Level& lv, const std::string& kind) { removed = true; if (kind == "snow") mobBurst(lv, pos, "snow", 8, 2); else mobBurst(lv, pos, "white", 10, 0.8f); }
};

// ------------------------------------------------------------------ hex flasks (monsters.js)
class HexFlask : public Entity {
public:
  HexFlask() { type = "hex_flask"; w = 0.12f; h = 0.12f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    if (age > 8) { removed = true; return; }
    u32 own = data.value("owner", 0u);
    for (int s = 0; s < 3; s++) {
      float hh = dt / 3; vel.y -= 20 * hh; pos += vel * hh;
      for (Entity* q : pawnsOf(lv, realm)) if (std::fabs(q->pos.x - pos.x) < 0.5f && std::fabs(q->pos.z - pos.z) < 0.5f && pos.y > q->pos.y && pos.y < q->pos.y + 1.9f) { smash(lv); return; }
      if (creatureHit(lv, realm, pos, own, 0.2f)) { smash(lv); return; }
      if (solidAt(lv.world(realm), pos)) { smash(lv); return; }
    }
  }
  void smash(Level& lv) {
    removed = true;
    std::string kind = data.value("kind", "poison");
    Entity* own = lv.entity(data.value("owner", 0u));
    auto* oc = dynamic_cast<Creature*>(own);
    lv.fx->sound("splash_small", pos);
    mobBurst(lv, pos, kind == "poison" ? "venom_sac" : "ice", 16, 1.6f);
    for (Entity* q : pawnsOf(lv, realm)) {
      if (glm::distance(q->pos + vec3(0, 0.9f, 0), pos) > 3) continue;
      if (kind == "poison") setPlayerEffect(*q, "poison", std::max(playerEffect(*q, "poison"), 7.f)); else setPlayerEffect(*q, "slow", std::max(playerEffect(*q, "slow"), 8.f));
      DamageSrc d; d.knock = 0; damageEntity(lv, *q, 1, own, "magic", d);
    }
    for (auto& e : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || !c->alive() || c->removed || c == own || c->realm != realm || glm::distance(c->pos + vec3(0, c->h / 2, 0), pos) > 3) continue;
      if (oc && !c->T->faction.empty() && c->T->faction == oc->T->faction) continue;
      DamageSrc d; d.knock = 1; damageEntity(lv, *c, kind == "poison" ? 4 : 1, own, "magic", d); c->slowT = std::max(c->slowT, 6.f);
    }
  }
};

// ------------------------------------------------------------------ felled trees
class FallingTree : public Entity {
public:
  FallingTree() { type = "falling_tree"; noGravity = true; w = 0.5f; h = 1; }
  float landingY(World& wd, float x, float z, float height) {
    int bx = ifloor(x), bz = ifloor(z);
    for (int y = std::min(WORLD_H - 2, ifloor(pos.y + height + 3)); y > 0; y--) { BlockId id = wd.getBlock(bx, y, bz); if (reg().tSolid[id] || reg().tLiquid[id]) return y + 1.05f; }
    return pos.y + 1;
  }
  void tick(Level& lv, double dtd) override {
    float t = data.value("t", 0.f) + (float)dtd; data["t"] = t;
    if (t < 1.1f) return;
    World& wd = lv.world(realm);
    const json& bl = data["blocks"];
    float height = 1; for (auto& b : bl) height = std::max(height, b[1].get<float>() + 1);
    float dirYaw = data.value("dirYaw", 0.f);
    lv.fx->sound("fell", pos);
    for (Entity* q : pawnsOf(lv, realm)) if (glm::distance(q->pos, pos) < 24) lv.fx->shake(q->id, 0.25f);
    float fx = -std::sin(dirYaw), fz = -std::cos(dirYaw);
    for (auto& sj : data["drops"]) {
      ItemStack st = ItemStack::fromJson(sj);
      float d = 1 + frand() * std::max(1.f, height - 1);
      float x = pos.x + 0.5f + fx * d + (frand() - 0.5f), z = pos.z + 0.5f + fz * d + (frand() - 0.5f);
      dropItem(lv, realm, st, vec3(x, landingY(wd, x, z, height), z), false);
    }
    for (int i = 0; i < 24 && bl.size(); i++) {
      float d = frand() * height, x = pos.x + 0.5f + fx * d, z = pos.z + 0.5f + fz * d;
      mobBurst(lv, vec3(x, landingY(wd, x, z, height) + 0.2f, z), bl[(size_t)(frand() * bl.size()) % bl.size()][3].get<std::string>(), 1, 0.8f);
    }
    removed = true;
  }
};

// ------------------------------------------------------------------ severed parts
class Gib : public Entity {
public:
  Gib() { type = "gib"; w = 0.12f; h = 0.12f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    if (age > 25) { removed = true; return; }
    if (data.value("rest", false)) return;
    World& wd = lv.world(realm);
    vel.y -= 20 * dt;
    vec3 n = pos + vel * dt;
    if (solidAt(wd, vec3(n.x, pos.y + 0.05f, n.z))) { vel.x *= -0.3f; vel.z *= -0.3f; } else { pos.x = n.x; pos.z = n.z; }
    float sx = data.value("spinX", 0.f), sz = data.value("spinZ", 0.f);
    if (vel.y < 0 && solidAt(wd, vec3(pos.x, n.y - 0.02f, pos.z))) {
      pos.y = std::floor(n.y - 0.02f) + 1.01f; vel.y *= -0.25f; vel.x *= 0.6f; vel.z *= 0.6f; sx *= 0.5f; sz *= 0.5f;
      if (std::fabs(vel.y) < 0.8f) { data["rest"] = true; vel.y = 0; }
    } else pos.y = n.y;
    data["ax"] = data.value("ax", 0.f) + sx * dt; data["az"] = data.value("az", 0.f) + sz * dt;
    data["spinX"] = sx; data["spinZ"] = sz;
  }
};

// ------------------------------------------------------------------ spawning helpers
Entity* spawnArrow(Level& lv, int realm, vec3 p, vec3 v, Entity* owner, float damage, bool pickup) {
  auto e = std::make_unique<Arrow>();
  e->realm = realm; e->pos = p; e->vel = v; e->data = {{"owner", owner ? owner->id : 0}, {"damage", damage}, {"pickup", pickup}};
  e->aim();
  return lv.spawn(std::move(e));
}
Entity* spawnProjectile(Level& lv, int realm, vec3 p, vec3 v, const std::string& kind, Entity* owner) {
  auto e = std::make_unique<Projectile>();
  e->realm = realm; e->pos = p; e->vel = v; e->data = {{"owner", owner ? owner->id : 0}, {"kind", kind}};
  return lv.spawn(std::move(e));
}
Entity* spawnHexFlask(Level& lv, int realm, vec3 p, vec3 v, Entity* owner, const std::string& kind) {
  auto e = std::make_unique<HexFlask>();
  e->realm = realm; e->pos = p; e->vel = v; e->data = {{"owner", owner ? owner->id : 0}, {"kind", kind}};
  return lv.spawn(std::move(e));
}
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<FelledBlock>& blocks, ivec3 stump, float dirYaw, const std::vector<ItemStack>& drops) {
  auto e = std::make_unique<FallingTree>();
  e->realm = realm; e->pos = vec3(stump);
  json bl = json::array(), dr = json::array();
  for (auto& b : blocks) bl.push_back({b.p.x - stump.x, b.p.y - stump.y, b.p.z - stump.z, reg().block(b.id).key});
  for (auto& s : drops) if (!s.empty()) dr.push_back(s.toJson());
  e->data = {{"blocks", bl}, {"dirYaw", dirYaw}, {"drops", dr}, {"t", 0}};
  return lv.spawn(std::move(e));
}
Entity* spawnGib(Level& lv, Creature& from, const std::string& part, const mat4& m) {
  auto e = std::make_unique<Gib>();
  e->realm = from.realm; e->pos = from.pos + vec3(m[3]);
  float a = frand() * TAU;
  e->vel = vec3(std::cos(a) * 2 + from.vel.x, 3 + frand() * 2, std::sin(a) * 2 + from.vel.z);
  json rot = json::array(); for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) rot.push_back(m[c][r]);
  e->data = {{"model", from.modelKey()}, {"part", part}, {"rot", rot}, {"spinX", (frand() - 0.5f) * 12}, {"spinZ", (frand() - 0.5f) * 12}, {"ax", 0}, {"az", 0}, {"from", from.type}};
  return lv.spawn(std::move(e));
}

void registerCreatureEntities() {
  registerEntityFactory("arrow", [](const std::string&) { return std::make_unique<Arrow>(); });
  registerEntityFactory("projectile", [](const std::string&) { return std::make_unique<Projectile>(); });
  registerEntityFactory("hex_flask", [](const std::string&) { return std::make_unique<HexFlask>(); });
  registerEntityFactory("falling_tree", [](const std::string&) { return std::make_unique<FallingTree>(); });
  registerEntityFactory("gib", [](const std::string&) { return std::make_unique<Gib>(); });
}

} // namespace dq
