// render/nether_fx_layer.cpp — the life of BTA 8.0's Nether lands (gen/nether_bta.h), on this device:
//  * the Volcanic Islands' volcanoes erupt now and then, each on its own clock taken from the world's time (so everyone
//    in a shared world sees the same eruption): a grumble a few seconds before, then the blast (heard far off, the
//    ground shakes near it), a fountain of lava, glowing bombs flung down the slopes and a column of smoke that
//    spreads under the roof; between eruptions a thin smoke curls up from each crater;
//  * embers rise off the lava in the Volcanic Islands, fumes off the acid in the Sulfur Pools, and red glints drift
//    among the Crystal Forest's spires.
// The lands' sounds are audio/ambience.cpp + audio/nether_sfx.cpp.
#include "client/session.h"
#include "client/app.h"
#include "audio/audio.h"
#include "sim/steam.h"
#include "render/particles.h"
#include "gen/nether_bta.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include <unordered_map>
#include <cstdlib>

namespace dq {

namespace {
inline float rnd() { static u32 s = 0x9e3779b9u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
inline float rs() { return rnd() * 2 - 1; }
constexpr float ERUPT = 11;   // seconds an eruption lasts
}

class NetherFxLayer : public RenderLayer {
public:
  const char* name() const override { return "nether_lands"; }
  void update(GameSession& s, double dtIn) override {
    if (!s.player || !s.level || s.paused() || dtIn <= 0) return;
    if (s.frame.dim != DIM_NETHER || s.player->realm != REALM_MAIN) { vols.clear(); scanT = 0; vents.clear(); return; }
    const float dt = (float)std::min(dtIn, 0.1);
    ParticleSystem& P = particles();
    if (P.level <= 0) return;
    const float amount = P.level == 1 ? 0.4f : 1.f;
    const vec3 p = s.player->pos;
    steamVents(s, P, p, amount, dt);   // (in every Nether: the old one has vents too)
    if (s.level->info.genVersion < 5) { vols.clear(); scanT = 0; return; }
    const i32 seed = s.level->info.seed;
    // The heightfield's volcano cones exist only on version 5. The cave Nether has no cones to erupt.
    if (s.level->info.genVersion < 6) {
      if ((scanT -= dt) <= 0 || glm::distance(vec2(p.x, p.z), vec2(scanAt.x, scanAt.z)) > 32) { scanT = 2; scanAt = p; netherVolcanoes(seed, (int)std::floor(p.x), (int)std::floor(p.z), 220, vols); }
      volcanoes(s, P, p, amount, dt);
    } else { vols.clear(); }
    lands(s, P, p, amount, dt);
  }

private:
  std::vector<NetherVolcano> vols;
  // the steam vents (sim/steam.h: each breathes on its own beat; sim/bta_more.cpp scalds and lifts): found every couple of
  // seconds round the player, a white plume shot up out of each while it breathes, a hiss as it starts, a wisp between
  std::vector<ivec3> vents; float ventScanT = 0; vec3 ventAt{0}; std::unordered_map<u64, float> ventPh;
  void steamVents(GameSession& s, ParticleSystem& P, vec3 p, float amount, float dt) {
    static const BlockId vent = reg().blockId("steam_vent");
    if (!vent) return;
    World& w = s.world();
    if ((ventScanT -= dt) <= 0 || glm::distance(p, ventAt) > 12) {
      ventScanT = 2; ventAt = p; vents.clear();
      const int px = (int)std::floor(p.x), py = (int)std::floor(p.y), pz = (int)std::floor(p.z);
      for (int z = pz - 24; z <= pz + 24; z++) for (int x = px - 24; x <= px + 24; x++) for (int y = py - 10; y <= py + 6; y++)
        if (w.getBlock(x, y, z) == vent && w.getBlock(x, y + 1, z) == AIR) vents.push_back(ivec3(x, y, z));
    }
    const double now = s.level->info.time;
    const u16 smoke = (u16)P.tile("smoke");
    AudioEngine* a = s.app.audio.get();
    for (const ivec3& v : vents) {
      const vec3 c(v.x + 0.5f, v.y + 1.02f, v.z + 0.5f);
      const float d = glm::distance(c, p);
      if (d > 40) continue;
      const float ph = steamPhase(now, v.x, v.y, v.z);
      const u64 key = ((u64)(u32)v.x << 40) ^ ((u64)(u32)v.y << 20) ^ (u64)(u32)v.z;
      float& last = ventPh[key];
      if (ph < 0.22f && (last >= 0.22f || last > ph) && a && d < 32) a->playAt("fizz", c, 32, 0.9f);   // the hiss as it starts
      last = ph;
      const bool gust = ph < 0.22f;
      const float rate = (gust ? 40.f : 1.5f) * amount;
      for (int n = (int)std::floor(rate * dt + rnd()); n > 0; n--) {
        Particle q; q.tile = smoke; q.rise = true; q.light = 0xF0 | 13; q.p = c + vec3(rs() * 0.2f, 0, rs() * 0.2f);
        const float up = gust ? 7.f + rnd() * 3.f : 0.6f + rnd() * 0.4f;
        q.v = vec3(rs() * (gust ? 0.5f : 0.15f), up, rs() * (gust ? 0.5f : 0.15f));
        q.life = q.max = gust ? 1.1f + rnd() * 0.6f : 2.5f; q.size = q.size0 = gust ? 0.25f + rnd() * 0.2f : 0.15f; q.grow = gust ? 3.f : 1.5f;
        q.grav = gust ? 6.f : -0.05f; q.wind = P.windDrift * 0.3f;   // (falls back from its burst height and spreads)
        q.tint[0] = 240; q.tint[1] = 240; q.tint[2] = 236;
        P.add(q);
      }
    }
  }
  std::unordered_map<u32, long long> erupted, grumbled;
  float scanT = 0, landT = 0; vec3 scanAt{0}; int land = -1;

  void volcanoes(GameSession& s, ParticleSystem& P, vec3 p, float amount, float dt) {
    const double now = s.level->info.time;
    const int base = dimBase(DIM_NETHER);
    const u16 white = (u16)P.tile("white"), smoke = (u16)P.tile("smoke");
    const int lavaT = P.blockTile("lava", 0);
    AudioEngine* a = s.app.audio.get();
    for (const NetherVolcano& v : vols) {
      const vec3 c(v.x + 0.5f, (float)(base + v.lavaY) + 1.f, v.z + 0.5f);
      const float d = glm::distance(vec2(c.x, c.z), vec2(p.x, p.z));
      static const bool test = std::getenv("DQ_ERUPT") != nullptr;   // (testing) every volcano erupts every 20 s, starting 4 s in
      const double period = test ? 20 : 80 + (v.hash % 97), phase = test ? 12 : (double)((v.hash >> 9) % 997);
      const long long cycle = (long long)std::floor((now + phase) / period);
      const float t = (float)(now + phase - cycle * period);
      // the grumble before it goes
      if (t > period - 4 && grumbled[v.hash] != cycle) {
        grumbled[v.hash] = cycle;
        if (a && d < 150) a->playAt("nether_rumble", c, 150, 0.9f);
        if (d < 50) s.fx->shake(s.player->id, 0.15f);
      }
      if (t >= ERUPT) {   // quiet: a thin smoke from the crater
        if (d < 140 && rnd() < dt * 3 * amount) {
          Particle q; q.tile = smoke; q.rise = true; q.light = 0xF0 | 10; q.tint[0] = 120; q.tint[1] = 112; q.tint[2] = 108;
          q.p = c + vec3(rs() * v.crater * 0.5f, 0.5f, rs() * v.crater * 0.5f); q.v = vec3(rs() * 0.2f, 2.2f + rnd(), rs() * 0.2f);
          q.life = q.max = 6 + rnd() * 4; q.size = q.size0 = 1.0f + rnd() * 0.6f; q.grow = 2.2f; q.grav = -0.05f; q.wind = P.windDrift * 0.6f;
          P.add(q);
        }
        continue;
      }
      if (erupted[v.hash] != cycle) {   // the blast
        erupted[v.hash] = cycle;
        if (a && d < 240) a->playAt("eruption", c, 240, 1.f);
        if (d < 160) s.fx->shake(s.player->id, d < 40 ? 0.7f : d < 90 ? 0.35f : 0.14f);
      }
      if (d > 200) continue;
      const float k = t < 1.5f ? 1.f : 1 - (t - 1.5f) / (ERUPT - 1.5f);   // strongest at the start, dying away
      const float lod = d > 120 ? 0.35f : d > 60 ? 0.65f : 1.f;
      const float cr = std::max(1.5f, v.crater * 0.7f);
      // the fountain: blobs of lava thrown straight up out of the crater
      for (int n = (int)std::floor(90 * dt * k * amount * lod + rnd()); n > 0; n--) {
        Particle q; q.light = 0xFF; q.glow = true;
        if (lavaT >= 0) { q.tile = (u16)lavaT; const u8 o = (u8)(rnd() * 96); q.uv[0] = o; q.uv[1] = (u8)(rnd() * 96); q.uv[2] = (u8)(o + 32); q.uv[3] = (u8)(q.uv[1] + 32); }
        else { q.tile = white; q.tint[0] = 255; q.tint[1] = (u8)(120 + rnd() * 90); q.tint[2] = 30; }
        q.p = c + vec3(rs() * cr, 0.2f, rs() * cr); q.v = vec3(rs() * 3.5f, 16 + rnd() * 16 * (0.5f + 0.5f * k), rs() * 3.5f);
        q.life = q.max = 3.2f + rnd() * 1.4f; q.size = q.size0 = 0.22f + rnd() * 0.24f; q.grav = 13; q.shrink = rnd() < 0.5f;
        P.add(q);
      }
      // bombs: bigger glowing rocks flung out over the slopes, a trail of sparks behind
      for (int n = (int)std::floor(7 * dt * k * amount * lod + rnd() * 0.5f); n > 0; n--) {
        const float ang = rnd() * 6.2832f, sp = 5 + rnd() * 7;
        Particle q; q.tile = white; q.light = 0xFF; q.glow = true; q.tint[0] = 255; q.tint[1] = (u8)(90 + rnd() * 60); q.tint[2] = 20;
        q.p = c + vec3(rs() * cr * 0.5f, 0.4f, rs() * cr * 0.5f); q.v = vec3(std::cos(ang) * sp, 15 + rnd() * 8, std::sin(ang) * sp);
        q.life = q.max = 4.5f; q.size = q.size0 = 0.32f + rnd() * 0.2f; q.grav = 12; q.streak = 0.25f; q.sd = glm::normalize(q.v);
        P.add(q);
      }
      // the column of smoke and ash, rising to the roof and spreading under it
      for (int n = (int)std::floor(26 * dt * (0.35f + k) * amount * lod + rnd()); n > 0; n--) {
        Particle q; q.tile = smoke; q.rise = true; q.light = 0xF0 | 12;
        q.p = c + vec3(rs() * cr, 1 + rnd() * 3, rs() * cr); q.v = vec3(rs() * 0.8f, 6 + rnd() * 5, rs() * 0.8f);
        q.life = q.max = 9 + rnd() * 6; q.size = q.size0 = 1.8f + rnd() * 1.4f; q.grow = 2.6f; q.grav = -0.1f; q.wind = P.windDrift * 0.8f;
        const u8 g = (u8)(130 + rnd() * 70); q.tint[0] = g; q.tint[1] = (u8)(g * 0.92f); q.tint[2] = (u8)(g * 0.88f);
        P.add(q);
      }
      // sparks spraying off the fountain
      if (d < 100) for (int n = (int)std::floor(40 * dt * k * amount + rnd()); n > 0; n--) {
        Particle q; q.tile = (u16)P.tile("ember_spark"); q.light = 0xFF;
        q.p = c + vec3(rs() * cr, 2 + rnd() * 8 * k, rs() * cr); q.v = vec3(rs() * 5, 4 + rnd() * 9, rs() * 5);
        q.life = q.max = 1.2f + rnd(); q.size = q.size0 = 0.05f; q.shrink = true; q.grav = 6;
        P.add(q);
      }
    }
  }

  // the small life of each land round the player
  void lands(GameSession& s, ParticleSystem& P, vec3 p, float amount, float dt) {
    if ((landT -= dt) <= 0) {
      landT = 1; u8 a, b; float m;
      netherLandAt(s.level->info.seed, s.level->info.genVersion, (int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z), a, b, m);
      land = a;
    }
    World& w = s.world();
    const Registry& R = reg();
    static const BlockId acid = R.blockId("acid"), lava = R.blockId("lava");
    // a liquid's surface near the player, by a few random looks down
    auto surface = [&](BlockId want, vec3& at) {
      const int x = (int)std::floor(p.x + rs() * 14), z = (int)std::floor(p.z + rs() * 14), y0 = (int)std::floor(p.y) + 4;
      for (int y = y0; y > y0 - 12; y--) {
        const BlockId id = w.getBlock(x, y, z);
        if (id == AIR) continue;
        if (id == want && w.getBlock(x, y + 1, z) == AIR) { at = vec3(x + rnd(), y + 0.95f, z + rnd()); return true; }
        return false;
      }
      return false;
    };
    vec3 at;
    if (land == NB_VOLCANIC && lava) {   // embers lifting off the lava
      for (int n = (int)std::floor(10 * dt * amount + rnd()); n > 0; n--) if (surface(lava, at)) {
        Particle q; q.tile = (u16)P.tile("ember_spark"); q.light = 0xFF; q.p = at; q.v = vec3(rs() * 0.3f, 1.2f + rnd() * 1.4f, rs() * 0.3f);
        q.life = q.max = 2 + rnd() * 2; q.size = q.size0 = 0.035f; q.shrink = true; q.grav = -0.15f; q.flutter = 0.3f; q.ph = rnd() * 6;
        P.add(q);
      }
    } else if (land == NB_SULFUR && acid) {   // yellow fumes off the acid
      for (int n = (int)std::floor(8 * dt * amount + rnd()); n > 0; n--) if (surface(acid, at)) {
        Particle q; q.tile = (u16)P.tile("smoke"); q.rise = true; q.light = 0xF0 | 11; q.p = at; q.v = vec3(rs() * 0.15f, 0.5f + rnd() * 0.4f, rs() * 0.15f);
        q.life = q.max = 3 + rnd() * 2; q.size = q.size0 = 0.2f + rnd() * 0.15f; q.grow = 2; q.grav = -0.05f; q.wind = P.windDrift * 0.5f;
        q.tint[0] = 214; q.tint[1] = 222; q.tint[2] = 90;
        P.add(q);
      }
    } else if (land == NB_CRYSTAL || land == NB_CRYSTAL_PLAINS) {   // red glints drifting among the spires
      const float k = land == NB_CRYSTAL_PLAINS ? 0.45f : 1.f;
      for (int n = (int)std::floor(6 * dt * amount * k + rnd()); n > 0; n--) {
        const vec3 q0 = p + vec3(rs() * 12, rs() * 5 + 1.5f, rs() * 12);
        if (w.getBlock((int)std::floor(q0.x), (int)std::floor(q0.y), (int)std::floor(q0.z)) != AIR) continue;
        Particle q; q.tile = (u16)P.tile("white"); q.light = 0xFF; q.glow = true; q.p = q0; q.v = vec3(rs() * 0.15f, 0.08f + rnd() * 0.1f, rs() * 0.15f);
        q.life = q.max = 3 + rnd() * 3; q.size = q.size0 = 0.03f; q.flutter = 0.25f; q.ph = rnd() * 6; q.blink = 4 + rnd() * 3;
        q.tint[0] = 255; q.tint[1] = (u8)(70 + rnd() * 70); q.tint[2] = (u8)(80 + rnd() * 50);
        P.add(q);
      }
    } else if (land == NB_OLDWORLD || land == NB_OLD_DESERT) {   // ash motes on the wind
      for (int n = (int)std::floor(8 * dt * amount + rnd()); n > 0; n--) {
        const vec3 q0 = p + vec3(rs() * 14, rs() * 3 + 0.5f, rs() * 14);
        if (w.getBlock((int)std::floor(q0.x), (int)std::floor(q0.y), (int)std::floor(q0.z)) != AIR) continue;
        Particle q; q.tile = (u16)P.tile("smoke"); q.rise = true; q.light = 0xF0 | 8; q.p = q0;
        q.v = vec3(rs() * 0.4f, 0.15f + rnd() * 0.25f, rs() * 0.4f);
        q.life = q.max = 3 + rnd() * 3; q.size = q.size0 = 0.12f + rnd() * 0.1f; q.grow = 1.4f; q.grav = -0.02f; q.wind = P.windDrift;
        const u8 g = (u8)(140 + rnd() * 50); q.tint[0] = g; q.tint[1] = (u8)(g * 0.9f); q.tint[2] = (u8)(g * 0.8f);
        P.add(q);
      }
    } else if (land == NB_SHELF) {   // a little dust, the same dry rock as the Crag
      for (int n = (int)std::floor(3 * dt * amount + rnd()); n > 0; n--) {
        const vec3 q0 = p + vec3(rs() * 10, rs() * 2, rs() * 10);
        if (w.getBlock((int)std::floor(q0.x), (int)std::floor(q0.y), (int)std::floor(q0.z)) != AIR) continue;
        Particle q; q.tile = (u16)P.tile("smoke"); q.light = 0xF0 | 6; q.p = q0; q.v = vec3(rs() * 0.1f, -0.2f, rs() * 0.1f);
        q.life = q.max = 1.5f + rnd(); q.size = q.size0 = 0.08f; q.grav = 0.4f;
        q.tint[0] = 120; q.tint[1] = 90; q.tint[2] = 70;
        P.add(q);
      }
    }
  }
};

void registerNetherFxLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<NetherFxLayer>(); }); }

} // namespace dq
