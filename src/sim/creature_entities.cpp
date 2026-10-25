// sim/creature_entities.cpp — dropped items, projectiles, felled trees and severed parts
// (game/entities.js DroppedItem / Projectile / FallingTree, game/blocks2.js Arrow, game/monsters.js HexFlask,
// game/bestiary.js llama spit, game/dismember.js Gib, game/theend.js snowballs' landing puff).
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
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
static bool keepDrop(const std::string& id) {
  static const std::set<std::string> K = {"realm_fragment", "heart_crystal", "watcher_blade", "rift_staff", "ember_blade", "cinder_core", "elytra", "dragon_egg_ender", "pocket_key"};
  return K.count(id) > 0;
}
static bool fireproof(const std::string& id) {
  return id.rfind("cinderite", 0) == 0 || id == "ember_blade" || id == "cinder_core" || id == "realm_fragment" || id == "heart_crystal" || id == "watcher_blade" || id == "rift_staff";
}
class DroppedItem : public Entity {
public:
  DroppedItem() { type = "item"; w = 0.15f; h = 0.3f; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& wd = lv.world(realm);
    if (!wd.isLoaded(ifloor(pos.x), clampv(ifloor(pos.y), 0, WORLD_H - 1), ifloor(pos.z))) return;
    ItemStack st = ItemStack::fromJson(data.value("stack", json()));
    if (st.empty()) { removed = true; return; }
    float delay = data.value("pickupDelay", 0.f);
    if (delay > 0) data["pickupDelay"] = delay - dt;
    if (!boxFree(wd, pos, w, h)) unstick(wd);   // never stay buried
    const Registry& R = reg();
    BlockId here = wd.getBlock(ifloor(pos.x), ifloor(pos.y + 0.1f), ifloor(pos.z));
    if (R.tLiquid[here] == 2 && !fireproof(st.id)) { removed = true; lv.fx->sound("fizz", pos); return; }
    bool floating = false;
    if (R.tLiquid[here] == 1) {
      int by = ifloor(pos.y + 0.15f);
      float top = by + (R.tLiquid[wd.getBlock(ifloor(pos.x), by + 1, ifloor(pos.z))] ? 1.f : 0.88f);
      bool under = pos.y + 0.12f < top;
      vel.y += (under ? 14 : -6) * dt;
      vel.y *= std::max(0.f, 1 - dt * 3);
      vel.x *= std::max(0.f, 1 - dt * 1.5f); vel.z *= std::max(0.f, 1 - dt * 1.5f);
      floating = true;
    } else vel.y -= 18 * dt;
    inWater = floating; data["floating"] = floating;
    float k = onGround ? std::min(1.f, dt * 6) : std::min(1.f, dt * 0.5f);
    vel.x -= vel.x * k; vel.z -= vel.z * k;
    if (data.value("pickupDelay", 0.f) <= 0) {   // any player nearby pulls the item in
      Player* p = nullptr; float best = 2.4f;
      for (auto& e : lv.entities()) if (auto* q = dynamic_cast<Player*>(e.get())) {
        if (q->removed || q->dead || q->realm != realm || q->mode == "spectator") continue;
        float d = glm::distance(q->pos + vec3(0, 0.8f, 0), pos);
        if (d < best) { best = d; p = q; }
      }
      if (p) {
        vec3 dv = p->pos + vec3(0, 0.8f, 0) - pos; float d = std::max(best, 0.01f);
        float s = (2.4f - d) * 10 * dt; vel += dv / d * s * 8.f;
        if (d < 0.9f) {
          int left = p->give(st);
          if (left < st.n) lv.fx->soundUi(p->id, "pop");
          if (left <= 0) { removed = true; return; }
          st.n = left; data["stack"] = st.toJson(); data["pickupDelay"] = 1;
        }
      }
    }
    MoveResult r = moveBox(wd, pos, w, h, vel * dt, 0);
    if (r.hitX) vel.x = 0; if (r.hitZ) vel.z = 0; if (r.hitY) vel.y = 0;
    onGround = r.onGround;
    // a sapling left lying on soil takes root
    if (onGround && !floating) {
      if (!data.contains("plantT")) data["plantT"] = 10 + frand() * 25;
      if (age > data["plantT"].get<float>()) { data["plantT"] = age + 15 + frand() * 30; takeRoot(lv, wd, st); if (removed) return; }
    }
    if ((age > 300 && !keepDrop(st.id)) || pos.y < -30) removed = true;
  }
  void unstick(const World& wd) {
    for (int up = 1; up <= 12; up++) { vec3 p(pos.x, std::floor(pos.y) + up + 0.01f, pos.z); if (boxFree(wd, p, w, h)) { pos = p; vel.y = 0; return; } }
    for (vec3 d : {vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 0, -1)}) if (boxFree(wd, pos + d, w, h)) { pos += d; return; }
  }
  void takeRoot(Level& lv, World& wd, ItemStack st) {
    const ItemDef* d = st.def();
    if (!d || d->block.empty() || lv.info.rules.value("saplingsPlant", true) == false) return;
    BlockId bid = reg().blockId(d->block); const BlockDef& b = reg().block(bid);
    auto ends = [&](const char* s) { size_t n = strlen(s); return b.key.size() >= n && b.key.compare(b.key.size() - n, n, s) == 0; };
    if (!bid || !(b.has("tree") || ends("_propagule") || ends("_fungus"))) return;
    int x = ifloor(pos.x), y = ifloor(pos.y + 0.1f), z = ifloor(pos.z);
    BlockId cur = wd.getBlock(x, y, z);
    if (cur != AIR && !(reg().tReplaceable[cur] && !reg().tLiquid[cur])) return;
    const std::string& below = reg().block(wd.getBlock(x, y - 1, z)).key;
    static const std::set<std::string> SOIL = {"grass", "dirt", "podzol", "coarse_dirt", "rooted_dirt", "mud", "moss_block", "mycelium", "sky_grass", "snowy_grass", "dirt_with_dry_grass", "dirt_with_coniferous_litter", "dirt_with_rainforest_litter"};
    bool ok = ends("_fungus") ? (below == "crimson_nylium" || below == "warped_nylium" || below == "soul_soil") : SOIL.count(below) > 0;
    if (!ok) return;
    for (ivec2 dd : {ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1)}) { const BlockDef& n = reg().block(wd.getBlock(x + dd.x, y, z + dd.y)); if (n.has("tree") || n.has("isLog") || reg().tWlog[n.id]) return; }
    wd.setBlock(x, y, z, bid, 0);
    st.n--; if (st.n <= 0) removed = true; else data["stack"] = st.toJson();
  }
};

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
        if (std::fabs(p->pos.x - pos.x) < 1 && std::fabs(p->pos.z - pos.z) < 1 && pos.y > p->pos.y - 0.5f && pos.y < p->pos.y + 2)
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
      if (kind == "spit" && frand() < dt * 20) burst(lv, pos, "white", 1, 0.1f);
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
  void explodeFx(Level& lv) { removed = true; burst(lv, pos, "fireball", 10, 0.8f); lv.fx->sound("burn", pos); }
  void splat(Level& lv, const std::string& kind) { removed = true; if (kind == "snow") burst(lv, pos, "snow", 8, 2); else burst(lv, pos, "white", 10, 0.8f); }
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
    burst(lv, pos, kind == "poison" ? "venom_sac" : "ice", 16, 1.6f);
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
      burst(lv, vec3(x, landingY(wd, x, z, height) + 0.2f, z), bl[(size_t)(frand() * bl.size()) % bl.size()][3].get<std::string>(), 1, 0.8f);
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
  registerEntityFactory("item", [](const std::string&) { return std::make_unique<DroppedItem>(); });
  registerEntityFactory("arrow", [](const std::string&) { return std::make_unique<Arrow>(); });
  registerEntityFactory("projectile", [](const std::string&) { return std::make_unique<Projectile>(); });
  registerEntityFactory("hex_flask", [](const std::string&) { return std::make_unique<HexFlask>(); });
  registerEntityFactory("falling_tree", [](const std::string&) { return std::make_unique<FallingTree>(); });
  registerEntityFactory("gib", [](const std::string&) { return std::make_unique<Gib>(); });
}

} // namespace dq
