// client/client_fx.cpp — see client_fx.h.
#include "sim/creature.h"
#include "gen/nether_bta.h"
#include "client/client_fx.h"
#include "client/session.h"
#include "client/app.h"
#include "audio/audio.h"
#include "gen/generator.h"
#include "core/registry.h"
#include "render/particles.h"
#include "ui/ui.h"
#include "sim/weather.h"

namespace dq {

void ClientFx::particles(const std::string& kind, vec3 pos, int count, const json& extra) {
  if (s.app.settings.particles <= 0) return;
  dq::particles().spawn(kind, pos, s.app.settings.particles == 1 ? std::max(1, count / 3) : count, extra);
}
void ClientFx::message(u32 playerId, const std::string& text, const std::string& kind) {
  if (playerId && (!s.player || playerId != s.player->id)) return;
  if (hudTakesMessages()) return;   // the HUD shows it (as a toast or a chat line); toasting here too doubled every one
  hudToast(text, kind);
}
class PlayerController; PlayerController* localController(GameSession& s);   // client/play.h
float& controllerShake(PlayerController& c);   // client/play.cpp
void ClientFx::shake(u32 playerId, float strength) {
  if (!s.player || (playerId && playerId != s.player->id)) return;
  if (PlayerController* c = localController(s)) { float& k = controllerShake(*c); k = std::max(k, strength); }
}
void ClientFx::sound(const std::string& name, vec3 pos, float range, float volume, float pitch) {
  if (s.app.audio) s.app.audio->playAt(name, pos, range, volume, pitch);
  if (s.app.settings.captions && s.player && glm::distance(s.player->pos, pos) < std::max(8.f, range)) hudCaption(captionForSound(name), pos, true);
}
void ClientFx::soundUi(u32 playerId, const std::string& name) {
  if (!s.app.audio || (playerId && s.player && playerId != s.player->id)) return;
  s.app.audio->play(name);
}
// The original plays your own steps, digging, placing and breaking flat and at full loudness (sfx.material);
// only sounds out in the world are placed (materialAt: half loudness, fading with distance, muffled by walls).
// Anything within arm's reach of the player is yours.
void ClientFx::material(const std::string& mat, const std::string& kind, vec3 pos) {
  if (!s.app.audio) return;
  if (s.player && glm::distance(s.player->pos + vec3(0, 0.9f, 0), pos) < 6.5f) { s.app.audio->material(mat, kind); return; }
  s.app.audio->materialAt(mat, kind, pos);
  if (s.app.settings.captions && s.player && glm::distance(s.player->pos, pos) < 20) hudCaption(captionForMaterial(mat, kind), pos, true);   // (not your own)
}
void ClientFx::voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) {
  if (s.app.audio) s.app.audio->voice(creature, kind, pos, pitch);
  if (s.app.settings.captions && s.player && glm::distance(s.player->pos, pos) < 24) hudCaption(captionForVoice(creature, kind), pos, true);
}

// The music of the place (music2.js biomeMood), read from the overworld generator's column under the player.
// TODO(gen): use the biome itself (and villages nearby) once the generators expose them.
static std::string placeMood(Level& lv, vec3 p) {
  if (!lv.gen) return "plains";
  DimGenerator* g = lv.gen->forDim(DIM_OVERWORLD);
  if (!g) return "plains";
  ColumnSample c; g->sample((int)std::floor(p.x), (int)std::floor(p.z), c);
  const std::string& top = reg().block(c.top).key, &leaves = reg().block(c.leaves).key;
  if (c.water >= 0 && c.water > c.height) return "ocean";
  // Better than Adventure's lands, each its own music (gen/overworld.h BtaVariant)
  static const char* BV[] = {"", "redlands", "glacier", "meadow", "mossmire", "thornscrub", "highlands", "tundra", "seasonal"};
  if (c.variant > 0 && c.variant < 9) return BV[c.variant];
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
    if ((placeT -= dt) <= 0) {
      placeT = 2; place = placeMood(*s.level, p.pos);
      land = -1;
      if (sc.dim == DIM_NETHER && p.realm == REALM_MAIN && s.level->info.genVersion >= 5) { u8 a, b; float m; netherBiomeMix(s.level->info.seed, (int)std::floor(p.pos.x), (int)std::floor(p.pos.z), a, b, m); land = a; }
    }
    sc.place = place;
    sc.netherLand = sc.dim == DIM_NETHER ? land : -1;
    sc.worldSeed = (u32)s.level->info.seed; sc.realm = p.realm;
    // a jukebox with a disc in it within 40 blocks: its music plays, louder the nearer (looked for once a second)
    if ((jukeT -= dt) <= 0) {
      jukeT = 1; disc.clear(); discD = 1e9f;
      const int pcx = (int)std::floor(p.pos.x) >> 4, pcz = (int)std::floor(p.pos.z) >> 4, pd = dimOfY((int)std::floor(p.pos.y));
      for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (const Chunk* c = w.chunk(pd, pcx + dx, pcz + dz)) for (auto& [i, e] : c->blockEntities) {
        if (!e.is_object() || e.value("type", "") != "jukebox" || !e.contains("disc") || !e["disc"].is_string()) continue;
        const int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
        const vec3 at((pcx + dx) * 16 + lx + 0.5f, dimBase(pd) + ly + 0.5f, (pcz + dz) * 16 + lz + 0.5f);
        const float d = glm::distance(at, p.pos);
        if (d > 40 || d >= discD) continue;
        const ItemDef* def = reg().item(e["disc"].get<std::string>());
        if (!def) continue;
        discD = d; disc = def->str("disc");
      }
    }
    if (!disc.empty()) { sc.jukebox = true; sc.jukeboxDisc = disc; sc.jukeboxGain = std::clamp(1.f - (discD - 6) / 34.f, 0.f, 1.f); }
    if (WeatherSystem* W = weatherOf(*s.level); W && p.realm == REALM_MAIN && sc.dim == DIM_OVERWORLD) sc.rain = W->rainLevel;
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
  vec3 lastPos{0}; int land = -1; float jukeT = 0, discD = 1e9f; std::string disc;
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
