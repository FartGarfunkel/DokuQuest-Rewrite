// client/client_fx.cpp — see client_fx.h.
#include "sim/creature.h"
#include "client/client_fx.h"
#include "client/session.h"
#include "client/app.h"
#include "audio/audio.h"
#include "gen/generator.h"
#include "core/registry.h"

namespace dq {

void ClientFx::sound(const std::string& name, vec3 pos, float range, float volume, float pitch) { if (s.app.audio) s.app.audio->playAt(name, pos, range, volume, pitch); }
void ClientFx::soundUi(u32 playerId, const std::string& name) {
  if (!s.app.audio || (playerId && s.player && playerId != s.player->id)) return;
  s.app.audio->play(name);
}
void ClientFx::material(const std::string& mat, const std::string& kind, vec3 pos) { if (s.app.audio) s.app.audio->materialAt(mat, kind, pos); }
void ClientFx::voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) { if (s.app.audio) s.app.audio->voice(creature, kind, pos, pitch); }

// The music of the place (music2.js biomeMood), read from the overworld generator's column under the player.
// TODO(gen): use the biome itself (and villages nearby) once the generators expose them.
static std::string placeMood(Level& lv, vec3 p) {
  if (!lv.gen) return "plains";
  DimGenerator* g = lv.gen->forDim(DIM_OVERWORLD);
  if (!g) return "plains";
  ColumnSample c; g->sample((int)std::floor(p.x), (int)std::floor(p.z), c);
  const std::string& top = reg().block(c.top).key, &leaves = reg().block(c.leaves).key;
  if (c.water >= 0 && c.water > c.height) return "ocean";
  if (top.find("snow") != std::string::npos || top.find("ice") != std::string::npos) return "frost";
  if (top == "sand" || top.find("sandstone") != std::string::npos) return "desert";
  if (top.find("red_sand") != std::string::npos || top.find("terracotta") != std::string::npos) return "savanna";
  if (top.find("mud") != std::string::npos || top.find("clay") != std::string::npos) return "swamp";
  if (c.height > 64 + 48) return "peaks";
  if (c.tree && leaves.find("jungle") != std::string::npos) return "jungle";
  if (c.tree) return "forest";
  return "plains";
}

class AudioLayer : public RenderLayer {
public:
  explicit AudioLayer(GameSession& s) : sess(s) {}
  ~AudioLayer() override { if (sess.app.audio) sess.app.audio->setScene(AudioScene()); }
  const char* name() const override { return "audio"; }
  void update(GameSession& s, double dt) override {
    AudioEngine* a = s.app.audio.get();
    if (!a || !s.player || dt <= 0) return;
    Player& p = *s.player;
    World& w = s.world();
    // footsteps (local.js): every 2.2 blocks walked on the ground, the block underfoot's own step
    vec3 mv = p.pos - lastPos; lastPos = p.pos;
    float hs = std::sqrt(mv.x * mv.x + mv.z * mv.z) / (float)dt;
    if (hs > 50) hs = 0;   // teleported
    if (p.onGround && !p.flying && hs > 1 && !s.paused()) {
      stepAcc += hs * (float)dt;
      if (stepAcc > 2.2f) {
        stepAcc = 0;
        BlockId id = w.getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y - 0.2f), (int)std::floor(p.pos.z));
        if (id != AIR) a->material(reg().block(id).sound, "step");
      }
    }
    // what the music and the ambience need
    AudioScene sc;
    sc.inWorld = true; sc.paused = s.paused(); sc.dead = p.hp <= 0; sc.creative = p.creative();
    sc.world = &w; sc.player = p.pos; sc.head = vec3(s.frame.cam.pos);
    sc.dim = dimOfY((int)std::floor(p.pos.y));
    ivec3 h((int)std::floor(sc.head.x), (int)std::floor(sc.head.y), (int)std::floor(sc.head.z));
    sc.eyeWater = reg().tLiquid[w.getBlock(h.x, h.y, h.z)] == 1;
    sc.skyAtHead = w.getLight(h.x, h.y + 0, h.z) >> 4;
    double t = s.level->dayFraction();                                    // game.js computeEnv: env.day
    float sh = (float)std::sin(t * 2 * 3.14159265358979), x = std::clamp((sh + 0.2f) / 0.45f, 0.f, 1.f);
    sc.day = 0.2f + 0.8f * x * x * (3 - 2 * x);
    sc.time = s.level->info.time;
    if ((placeT -= dt) <= 0) { placeT = 2; place = placeMood(*s.level, p.pos); }
    sc.place = place;
    // the nearest boss within 40 blocks (music2.js bossNear); (sleeping dragons and tamed ones don't count)
    float bd = 40;
    for (auto& e : s.level->entities()) {
      if (e->removed || e->realm != p.realm || e->isPlayer()) continue;
      auto it = reg().mobs.find(e->type);
      if (it == reg().mobs.end() || !it->value("boss", false)) continue;
      if (auto* c = dynamic_cast<Creature*>(e.get()); c && (c->state == "sleep" || !c->alive() || c->tamed)) continue;   // sleeping dragons don't count
      float d = glm::distance(e->pos, p.pos);
      if (d < bd) { bd = d; sc.boss = true; sc.bossId = e->id; sc.bossHp = e->maxHp > 0 ? e->hp / e->maxHp : 1;
        sc.bossDragon = e->type.find("dragon") != std::string::npos; sc.bossEerie = e->type == "herobrine"; }
    }
    a->setScene(sc);
  }
private:
  GameSession& sess;
  vec3 lastPos{0};
  float stepAcc = 0;
  double placeT = 0;
  std::string place = "plains";
};

void registerAudioLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession& s) { return std::make_shared<AudioLayer>(s); });
}

} // namespace dq
