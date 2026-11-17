// render/ragdoll_layer.cpp — see ragdoll_layer.h.
#include "render/ragdoll_layer.h"
#include "render/creature_layer.h"
#include "render/creature_draw.h"
#include "render/creature_models.h"
#include "render/char_anim.h"
#include "render/char_layer.h"
#include "render/gl.h"
#include "assets/char_model.h"
#include "assets/char_creator.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "sim/player.h"

namespace dq {

RagdollSet& clientRagdolls() { static RagdollSet s; return s; }

namespace {

// a player's body: the voxel character's ten parts, the torso first (parents before children)
constexpr int ORDER[CP_COUNT] = {CP_BODY, CP_HEAD, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D, CP_LEGR_U, CP_LEGR_D, CP_LEGL_U, CP_LEGL_D};
struct BodyDoll { Ragdoll rd; CharRig rig; vec3 at{0}; bool seen = false; CharWear wear; std::string skin; };

// what the body wears, as a living player would show it (render/char_layer charWearOf): its armour, the backpack, the cape
// (an item's, the cape editor's, or the skin's own), the quiver, the shield strapped on. Nothing in its hands.
CharWear bodyWear(const Entity& c, const CharacterSpec& spec) {
  Player p;
  const json items = c.data.value("items", json::array());
  for (auto& it : items) {
    if (!it.is_object()) continue;
    const std::string g = it.value("g", std::string()); const int i = it.value("i", -1);
    std::vector<ItemStack>* v = g == "armor" ? &p.armor : g == "back" ? &p.back : g == "hook" ? &p.hook : g == "cape" ? &p.cape : g == "quiver" ? &p.quiver : g == "shield" ? &p.shield : nullptr;
    if (v && i >= 0 && i < (int)v->size()) (*v)[i] = ItemStack::fromJson(it.value("s", json()));
  }
  if (items.empty()) if (const json& wr = c.data.value("wear", json()); wr.is_array() && !c.data.value("empty", false))
    for (size_t i = 0; i < 4 && i < wr.size(); i++) if (wr[i].is_string()) p.armor[i] = ItemStack(wr[i].get<std::string>(), 1);
  for (const char* k : {"customCape", "hideGear"}) if (c.data.contains(k)) p.data[k] = c.data[k];
  CharWear w = charWearOf(p, spec, false);
  w.held.clear(); w.off.clear(); w.heldModel.clear(); w.offModel.clear(); w.rodOwner = 0; w.heldGlint = w.offGlint = false;
  return w;
}

void buildBody(BodyDoll& d, const CharModel& M, const Entity& c, World& w) {
  CharPose ps; ps.yaw = c.yaw;
  charRig(M, ps, mat4(1), d.rig, false, false);   // standing, at the origin: where the body was when it went down
  const float aw = M.aw;
  // the part boxes in part space (model pixels from each part's pivot: assets/char_model.cpp)
  auto box = [&](int p, vec3& lo, vec3& hi) {
    switch (p) {
      case CP_HEAD: lo = {-4, 0, -4}; hi = {4, 8, 4}; break;
      case CP_BODY: lo = {-4, 0, -2}; hi = {4, 12, 2}; break;
      case CP_ARMR_U: case CP_ARML_U: lo = {-aw / 2, -4, -2}; hi = {aw / 2, 2, 2}; break;
      case CP_ARMR_D: case CP_ARML_D: lo = {-aw / 2, -6, -2}; hi = {aw / 2, 0, 2}; break;
      default: lo = {-2, -6, -2}; hi = {2, 0, 2}; break;   // legs, upper and lower
    }
  };
  int at[CP_COUNT]; for (int k = 0; k < CP_COUNT; k++) at[ORDER[k]] = k;
  std::vector<RagdollPartIn> in(CP_COUNT);
  for (int k = 0; k < CP_COUNT; k++) {
    const int p = ORDER[k];
    RagdollPartIn& r = in[k];
    r.m = d.rig.part[p] * d.rig.scale[p];
    box(p, r.lo, r.hi);
    r.parent = p == CP_BODY ? -1 : p == CP_HEAD || p == CP_ARMR_U || p == CP_ARML_U || p == CP_LEGR_U || p == CP_LEGL_U ? at[CP_BODY] : at[p - 1];
    r.limit = p == CP_HEAD ? 0.7f : (p == CP_ARMR_U || p == CP_ARML_U) ? 1.6f : 1.2f;
  }
  vec3 kick(0);
  if (auto k = c.data.find("rk"); k != c.data.end() && k->is_array() && k->size() == 3) kick = vec3((*k)[0].get<float>(), (*k)[1].get<float>(), (*k)[2].get<float>());
  d.at = c.pos;
  d.rd.build(c.pos, in, vec3(0), kick, (u32)std::hash<std::string>()(c.data.value("cid", std::string())));
  if (c.age > 2.5f) d.rd.settle(w);   // it fell long ago (a reload, coming back to it): lying still already
}

class RagdollLayer : public RenderLayer {
public:
  const char* name() const override { return "ragdolls"; }
  int order() const override { return 11; }

  void update(GameSession& s, double dt) override {
    if (!s.player || !s.level) return;
    if (s.level.get() != lastLevel) { clientRagdolls().clear(); bodies.clear(); lastLevel = s.level.get(); }
    if (s.paused()) return;
    RagdollSet& R = clientRagdolls();
    if (!R.onNew) R.onNew = [](CreatureRagdoll& r, Creature& m) {
      r.drawKey = creatureDrawKey(m);
      r.tint = m.T->hasTint ? m.T->tint : vec4(0);
      r.wool = creatureWoolTint(m);
      r.glows = m.T->glows > 0;
    };
    R.update(*s.level, s.player->realm, s.player->pos, (float)dt);
    // players' bodies
    World& w = s.world();
    for (auto& [k, d] : bodies) d.seen = false;
    for (auto& e : s.level->entities()) {
      if (e->removed || e->type != "corpse" || e->realm != s.player->realm || glm::distance(e->pos, s.player->pos) > 96) continue;
      const std::string cid = e->data.value("cid", std::to_string(e->id));
      auto it = bodies.find(cid);
      const std::string skinNow = e->data.value("skin", std::string());
      // new, moved (out of the lava, back from the void), or its owner's skin only just arrived (a guest's, by its net name:
      // another shape of body, so it's built again; it was lying still, so it lies still again at once)
      if (it == bodies.end() || glm::distance(it->second.at, e->pos) > 1.5f || (it->second.skin != "\x01" && it->second.skin != skinNow)) {
        auto spec = resolveCharacter(e->data.value("skin", std::string()));
        BodyDoll d; buildBody(d, charModel(*spec), *e, w); d.skin = "\x01";   // (its wear is worked out below)
        it = bodies.insert_or_assign(cid, std::move(d)).first;
      }
      BodyDoll& d = it->second;
      d.seen = true;
      if (d.skin != skinNow) { d.skin = skinNow; d.wear = bodyWear(*e, *resolveCharacter(skinNow)); }
      d.rd.step(w, (float)dt);
    }
    for (auto it = bodies.begin(); it != bodies.end();) it = it->second.seen ? std::next(it) : bodies.erase(it);
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player || !s.level) return;
    World& w = s.world();
    RagdollSet& R = clientRagdolls();
    if (!R.list.empty()) {
      CreatureDraw& D = creatureDraw();
      if (D.begin(f)) {
        for (auto& r : R.list) drawCreature(r, w, f, D, false);
        D.end();
      }
    }
    if (bodies.empty()) return;
    bool begun = false;
    for (auto& e : s.level->entities()) {
      if (e->removed || e->type != "corpse" || e->realm != s.player->realm) continue;
      auto it = bodies.find(e->data.value("cid", std::to_string(e->id)));
      if (it == bodies.end()) continue;
      BodyDoll& d = it->second;
      const vec3 rel = f.cam.rel(glm::dvec3(d.rd.origin));
      if (!f.cam.frustum.sphere(rel + d.rd.centre(), 1.6f)) continue;
      CharRig Rr = d.rig;
      for (int k = 0; k < CP_COUNT; k++) { const int p = ORDER[k]; Rr.part[p] = glm::translate(mat4(1), rel) * d.rd.partMatrix(k); Rr.scale[p] = mat4(1); }
      Rr.body = Rr.bodyRaw = Rr.part[CP_BODY];
      for (auto& [g, m] : Rr.gear) m = Rr.body;   // (a 3D look's capes, skirts and tails ride on the torso)
      Rr.gear["head"] = Rr.part[CP_HEAD]; Rr.gear["neck"] = glm::translate(Rr.body, vec3(0, 12, 0));
      Rr.gear["armR"] = Rr.part[CP_ARMR_U]; Rr.gear["foreR"] = Rr.part[CP_ARMR_D]; Rr.gear["armL"] = Rr.part[CP_ARML_U]; Rr.gear["foreL"] = Rr.part[CP_ARML_D];
      Rr.gear["legR"] = Rr.part[CP_LEGR_U]; Rr.gear["shinR"] = Rr.part[CP_LEGR_D]; Rr.gear["legL"] = Rr.part[CP_LEGL_U]; Rr.gear["shinL"] = Rr.part[CP_LEGL_D];
      Rr.fpTorso = false;
      CharPose ps; ps.deathT = 1; ps.time = (float)e->age;   // (dead: no blinking or talking face; nothing carried; the cape still hangs)
      const CharWear& wear = d.wear;
      CharDrawOpts o; o.light = lightAt(w, d.rd.origin + d.rd.centre() + vec3(0, 0.3f, 0)); o.rig = &Rr; o.time = e->age;
      if (!begun) { voxelRenderer().begin(f, f.cam.viewProj); begun = true; }
      drawCharacter(*resolveCharacter(e->data.value("skin", std::string())), ps, wear, glm::translate(mat4(1), rel), o);
    }
    if (begun) voxelRenderer().end();
  }

  void drawTranslucent(GameSession& s, const FrameCtx& f) override {   // see-through parts (slime jelly, ghostly cloth)
    if (!s.player || !s.level || clientRagdolls().list.empty()) return;
    CreatureDraw& D = creatureDraw();
    if (!D.begin(f)) return;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (auto& r : clientRagdolls().list) drawCreature(r, s.world(), f, D, true);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    D.end();
  }

private:
  std::unordered_map<std::string, BodyDoll> bodies;   // by the body's cid
  Level* lastLevel = nullptr;

  void drawCreature(const CreatureRagdoll& r, World& w, const FrameCtx& f, CreatureDraw& D, bool seeThrough) {
    const RagdollSet& R = clientRagdolls();
    const RigModel* RM = rigModel(r.model);
    const CreatureModelGL* CM = creatureModel(r.drawKey.empty() ? r.model : r.drawKey);
    if (!RM || !CM || CM->parts.size() != RM->parts.size() || (int)RM->parts.size() != r.rd.parts()) return;
    // the last seconds: it settles into the ground and darkens away
    const float k = clampv((r.t - (R.life - R.fade)) / R.fade, 0.f, 1.f);
    const vec3 rel = f.cam.rel(glm::dvec3(r.rd.origin)) - vec3(0, k * k * 0.9f, 0);
    if (!f.cam.frustum.sphere(rel + r.rd.centre(), 3)) return;
    const mat4 T0 = glm::translate(mat4(1), rel);
    VoxelLight L = r.glows ? VoxelLight{1, 1} : lightAt(w, r.rd.origin + r.rd.centre() + vec3(0, 0.3f, 0));
    vec4 tint = r.t < 0.3f ? vec4(1, 0.15f, 0.1f, 0.45f) : k > 0 ? vec4(0, 0, 0, 0.6f * k) : r.tint;
    for (size_t i = 0; i < RM->parts.size(); i++) {
      const RigPart& p = RM->parts[i];
      const CreaturePartMesh& pm = *CM->parts[i];
      if (pm.hidden || !r.rd.posed((int)i) || (i < r.shown.size() && !r.shown[i])) continue;
      if (seeThrough != (p.alpha < 1)) continue;
      const mat4 model = T0 * r.rd.partMatrix((int)i) * pm.local;
      if (seeThrough) { D.draw(pm.gl, model, p.glow ? VoxelLight{1, 1} : L, tint, p.alpha * (1 - k)); continue; }
      if (p.glow) { D.draw(pm.gl, model, {1, 1}, k > 0 ? tint : vec4(0)); continue; }
      D.draw(pm.gl, model, L, (p.wool || p.undercoat) && r.wool.a > 0 && tint.a == 0 ? r.wool : tint);
    }
  }
};

} // namespace

void registerRagdollLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<RagdollLayer>(); });
}

} // namespace dq
