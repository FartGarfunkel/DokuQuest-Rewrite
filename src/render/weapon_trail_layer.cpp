// render/weapon_trail_layer.cpp — a weapon's trail (Epic Fight's swing trails): while a blow is live, and a moment
// after, a pale ribbon follows the outer part of the blade over the last tenth of a second, the newest part brightest,
// fading as it ages. Its path is the blade's own (sim/attack_moves.h bladeAt: sampled from the drawn poses), turned to
// the swing's facing (atkYaw) and the look, from the player's feet: so it marks exactly when and where a swing can land.
#include "client/session.h"
#include "render/gl.h"
#include "sim/attack_moves.h"
#include "sim/player.h"

namespace dq {

namespace {
struct TV { float x, y, z; u8 r, g, b, a; };
constexpr float TRAIL_LEN = 0.10f;   // seconds of blade path shown
constexpr float FADE_AFTER = 0.06f;  // the trail lingers this long past the live window, fading

class WeaponTrailLayer : public RenderLayer {
public:
  const char* name() const override { return "weapon-trail"; }
  int order() const override { return 60; }
  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    v.clear();
    for (auto& e : s.level->entities()) {
      if (e->removed || !e->isPlayer() || e->realm != s.player->realm) continue;
      const Player& p = *static_cast<const Player*>(e.get());
      if (p.dead || !p.atkMove || p.atkMove >= MV_COUNT) continue;
      const AttackMove& m = attackMove(p.atkMove);
      const float t = p.atkT, from = m.hitFrom - 0.02f, to = m.hitTo + FADE_AFTER;
      if (t < from || t > to + TRAIL_LEN) continue;
      const float a0 = std::max(from, t - TRAIL_LEN), a1 = std::min(t, m.hitTo + 0.02f);
      if (a1 <= a0) continue;
      // the whole trail fades once the blade has gone past its live part
      const float tail = t > m.hitTo ? std::clamp(1 - (t - m.hitTo) / (FADE_AFTER + TRAIL_LEN), 0.f, 1.f) : 1.f;
      const bool fist = p.atkMove >= MV_FIST1 && p.atkMove <= MV_FIST3;
      const int n = std::max(2, (int)std::ceil((a1 - a0) * 240));
      vec3 pi0, po0; float al0 = 0;
      for (int i = 0; i <= n; i++) {
        const float u = a0 + (a1 - a0) * i / n;
        vec3 h, tp; bladeAt(p.atkMove, u, h, tp);
        if (p.atkOff) { h.x = -h.x; tp.x = -tp.x; }   // (the off hand's: the other side)
        const vec3 inner = h + (tp - h) * (fist ? 0.f : 0.35f), outer = fist ? tp + (tp - h) * 0.6f : tp;
        const vec3 wi = attackFrameToWorld(inner, p.pos, p.atkYaw, p.pitch), wo = attackFrameToWorld(outer, p.pos, p.atkYaw, p.pitch);
        const vec3 ri = f.cam.rel(glm::dvec3(wi)), ro = f.cam.rel(glm::dvec3(wo));
        const float age = (t - u) / TRAIL_LEN, al = std::clamp(1 - age, 0.f, 1.f) * tail * (fist ? 0.35f : 0.55f);
        if (i > 0) quad(pi0, po0, ro, ri, al0, al);
        pi0 = ri; po0 = ro; al0 = al;
      }
    }
    if (v.empty()) return;
    if (!tried) {
      tried = true; sh = std::make_unique<Shader>();
      if (!sh->load("weapon_trail.glsl", "weapon_trail.glsl")) { DQ_WARN("weapon trail: shader failed"); sh.reset(); }
      mesh.setup(sizeof(TV), {{0, 3, GL_FLOAT, GL_FALSE, false, 0}, {1, 4, GL_UNSIGNED_BYTE, GL_TRUE, false, 12}});
    }
    if (!sh) return;
    sh->use(); f.apply(*sh); sh->set("uViewProj", f.cam.viewProj);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mesh.uploadQuads(v.data(), v.size() * sizeof(TV), v.size() / 4, GL_DYNAMIC_DRAW); mesh.draw();
    glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
  }

private:
  std::vector<TV> v;
  std::unique_ptr<Shader> sh; bool tried = false;
  Mesh mesh;
  // inner (towards the hilt) fainter than the edge
  void quad(vec3 i0, vec3 o0, vec3 o1, vec3 i1, float a0, float a1) {
    auto put = [&](vec3 p, float a) { v.push_back({p.x, p.y, p.z, 236, 242, 255, (u8)std::lround(std::clamp(a, 0.f, 1.f) * 255)}); };
    put(i0, a0 * 0.15f); put(o0, a0); put(o1, a1); put(i1, a1 * 0.15f);
  }
};
}  // namespace

void registerWeaponTrailLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<WeaponTrailLayer>(); }); }

}  // namespace dq
