// render/held_light_fx.cpp — torches carried in the hand burn (Phase 8's particle overhaul): a flame licking up from the
// head, a thread of smoke, and when the torch moves, a trail of sparks along its path. In first person the flame's place
// comes from the held-item overlay (HeldItemLayer::torchTip); everyone else (other players, you in third person) carries
// it at the hand. Soul torches burn blue, redstone torches give off red dust.
#include "render/item_layers.h"
#include "render/particles.h"
#include "client/session.h"
#include "sim/player.h"
#include <unordered_map>

namespace dq {

namespace {
enum Kind { NONE, FIRE, SOUL, RED };
Kind kindOf(const ItemStack& s) {
  if (s.empty()) return NONE;
  if (s.id == "soul_torch") return SOUL;
  if (s.id == "redstone_torch") return RED;
  if (s.id == "torch" || endsWith(s.id, "_torch")) return FIRE;
  return NONE;
}
float R() { return (float)rand() / RAND_MAX; }
float R2() { return R() * 2 - 1; }
}

class HeldLightFx : public RenderLayer {
public:
  const char* name() const override { return "held-light-fx"; }
  int order() const override { return 901; }   // after the held item has said where its flame is
  void update(GameSession& s, double dtd) override {
    const float dt = (float)std::min(dtd, 0.1);
    ParticleSystem& P = particles();
    if (P.level == 0 || !s.player) { HeldItemLayer::torchTipOn[0] = HeldItemLayer::torchTipOn[1] = false; return; }
    std::unordered_map<u64, bool> seen;
    for (auto& e : s.level->entities()) {
      if (!e || e->removed || e->type != "player" || e->realm != s.player->realm) continue;
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || p->dead || glm::distance(p->pos, s.player->pos) > 48) continue;
      const bool fp = p == s.player && s.viewMode == 0;
      for (int h = 0; h < 2; h++) {
        const Kind k = kindOf(h == 0 ? p->held() : (p->offhand.empty() ? ItemStack() : p->offhand[0]));
        if (k == NONE) continue;
        glm::dvec3 tip;
        if (fp) { if (!HeldItemLayer::torchTipOn[h]) continue; tip = HeldItemLayer::torchTip[h]; }
        else {   // at the hand: a little to the side, forward, the head of the torch above the fist
          const float side = h == 0 ? 1.f : -1.f, c = std::cos(p->yaw), sn = std::sin(p->yaw);
          const vec3 rt(c, 0, -sn), fw(-sn, 0, -c);
          tip = glm::dvec3(p->pos + rt * (0.36f * side) + fw * 0.32f + vec3(0, p->sneaking ? 1.05f : 1.25f, 0));
        }
        const u64 key = (u64)p->id * 2 + h;
        seen[key] = true;
        auto it = last.find(key);
        const glm::dvec3 was = it == last.end() ? tip : it->second;
        last[key] = tip;
        burn(P, k, vec3(tip), vec3(was), dt, fp);
      }
    }
    for (auto it = last.begin(); it != last.end();) { if (!seen.count(it->first)) it = last.erase(it); else ++it; }
    HeldItemLayer::torchTipOn[0] = HeldItemLayer::torchTipOn[1] = false;
  }
private:
  std::unordered_map<u64, glm::dvec3> last;
  void burn(ParticleSystem& P, Kind k, vec3 tip, vec3 was, float dt, bool fp) {
    const char* fl = k == SOUL ? "soul_flame" : k == RED ? "redstone" : "flame";
    const float sz = fp ? 0.45f : 1.f, up = fp ? 0.5f : 1.f;   // in first person the flame is right in front of you: smaller
    // the flame licking up
    if (R() < dt * (k == RED ? 3.f : fp ? 6.f : 9.f))
      P.spawn(fl, tip + vec3(R2() * 0.03f, 0, R2() * 0.03f), 1, {{"vel", {R2() * 0.05f, (k == RED ? 0.2f : 0.55f + R() * 0.3f) * up, R2() * 0.05f}}, {"life", (0.22f + R() * 0.18f) * (fp ? 0.7f : 1.f)},
                                                                 {"size", 0.05f * sz}, {"shrink", true}, {"glow", true}, {"grav", 0}});
    // a thread of smoke
    if (k == FIRE && !fp && R() < dt * 2.5f)   // (none in first person: it would be a cloud in your face)
      P.spawn("smoke_puff", tip + vec3(0, 0.08f, 0), 1, {{"vel", {R2() * 0.05f, 0.5f + R() * 0.2f, R2() * 0.05f}}, {"life", 0.9f + R() * 0.6f}, {"size", 0.05f * sz},
                                                         {"grow", 0.9}, {"grav", -0.02}, {"rise", true}});
    // moving: sparks strung out behind it
    const vec3 d = tip - was;
    const float L = glm::length(d);
    if (L > 0.04f && L < 4) {
      const int n = std::min(10, (int)(L / 0.06f));
      for (int i = 0; i < n; i++) {
        const vec3 at = was + d * ((i + R()) / std::max(1, n));
        P.spawn(fl, at, 1, {{"vel", {R2() * 0.2f, 0.15f + R() * 0.3f, R2() * 0.2f}}, {"life", 0.18f + R() * 0.22f},
                                                         {"size", 0.03f * sz}, {"shrink", true}, {"glow", true}, {"grav", 0.6}});
      }
    }
  }
};

void registerHeldLightFx() { registerRenderLayer([](GameSession&) { return std::make_shared<HeldLightFx>(); }); }

} // namespace dq
