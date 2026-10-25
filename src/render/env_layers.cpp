// render/env_layers.cpp — see env_layers.h. The weather's visuals (game/weather.js updateDrops,
// game/skies.js extraParticles, the LightningBolt), the ambient effects (game/particles2.js
// particleWeather + torch flames + drips, game/ambientfx.js leaves, game/blockfx.js emitters) and the
// layers that draw distant terrain, clouds and particles.
#include "render/env_layers.h"
#include "render/distant.h"
#include "render/clouds.h"
#include "render/particles.h"
#include "render/sky.h"
#include "client/session.h"
#include "client/app.h"
#include "sim/weather.h"
#include <imgui.h>

namespace dq {

namespace {
u32 g_r = 0x1234567u;
inline float rnd() { g_r ^= g_r << 13; g_r ^= g_r >> 17; g_r ^= g_r << 5; return (g_r >> 8) * (1.0f / 16777216.0f); }
inline float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// ---- distant terrain --------------------------------------------------------------------------------
class DistantLayer : public RenderLayer {
public:
  DistantTerrain lod;
  const char* name() const override { return "distant"; }
  int order() const override { return -20; }
  ~DistantLayer() override {
    if (lod.built) DQ_LOG("distant terrain: %d tiles built, avg %.2f ms, max %.2f ms, %d held, %.1f MB GPU, %zu chunk summaries", lod.built, lod.buildMsTotal / lod.built, lod.buildMsMax, lod.tilesHeld, lod.gpuBytes / 1048576.0, lod.summaries);
  }
  void update(GameSession& s, double dt) override {
    // DQ_ENV_TEST="x y z yaw pitch [dayTime] [clear|rain|thunder|front]": hold the player there (testing the sky, weather and far land)
    if (const char* t = std::getenv("DQ_ENV_TEST"); t && s.player) {
      lod.adaptive = false;
      float x, y, z, yaw, pitch, tm = -1; char wk[16] = {0};
      int n = std::sscanf(t, "%f %f %f %f %f %f %15s", &x, &y, &z, &yaw, &pitch, &tm, wk);
      if (n >= 5) { s.player->pos = vec3(x, y, z); s.player->vel = vec3(0); s.player->yaw = yaw; s.player->pitch = pitch; s.player->flying = true; }
      if (n >= 6 && tm >= 0) s.level->info.time = tm;
      if (n >= 7 && !testWeather) { testWeather = true; if (WeatherSystem* W = weatherOf(*s.level)) { std::string k = wk; if (k == "front") W->startFront("thunder"); else { W->set(k); W->rain = k == "clear" ? 0.f : 1.f; W->thunderK = k == "thunder" ? 1.f : 0.f; } } }
    }
    lod.update(*s.level, s.world(), s.frame, *s.app.pool, dt, s.app.tiles);
  }
  bool testWeather = false;
  void drawOpaque(GameSession& s, const FrameCtx& f) override { lod.draw(f, skyOf(f)); }
  void drawHud(GameSession& s) override {
    if (!s.debugOverlay) return;
    WeatherSystem* W = weatherOf(*s.level);
    std::string t = strFormat("LOD %d drawn / %d tiles, %d building, %.1f MB GPU, %zu summaries, range %.0f, avg build %.1f ms\nparticles %zu (%d rain)  weather %s %.0fs  rain %.2f mist %.2f wind %.2f",
      lod.drawn, lod.tilesHeld, lod.building, lod.gpuBytes / 1048576.0, lod.summaries, lod.range, lod.built ? lod.buildMsTotal / lod.built : 0.0,
      particles().count(), particles().dropCount(), W ? W->kind.c_str() : "-", W ? W->left : 0.0, W ? W->rain : 0.f, W ? W->mistNow : 0.f, W ? W->windStrength : 0.f);
    ImGui::GetForegroundDrawList()->AddText(ImVec2(8, (float)s.app.height - 44), IM_COL32(255, 255, 255, 220), t.c_str());
  }
};

// ---- clouds -------------------------------------------------------------------------------------------
class CloudLayer : public RenderLayer {
public:
  CloudRenderer clouds;
  const char* name() const override { return "clouds"; }
  int order() const override { return -10; }
  void update(GameSession& s, double) override { clouds.update(*s.level, s.frame, skyOf(s.frame)); }
  void drawOpaque(GameSession&, const FrameCtx& f) override { clouds.drawOpaque(f, skyOf(f)); }
  void drawTranslucent(GameSession&, const FrameCtx& f) override { clouds.drawWisps(f, skyOf(f)); }
};

// ---- weather and ambient effects (spawn particles; the particle layer moves and draws them) ----------------
struct Emitter { i16 x, y, z; u8 kind, meta; float h; };   // x,z chunk-local, y global
enum EmKind : u8 { EM_TORCH = 1, EM_SOUL_TORCH, EM_FIRE, EM_LAVA, EM_FURNACE, EM_CANDLE, EM_END_ROD, EM_CRYING, EM_SPORE, EM_PORTAL, EM_RS_TORCH, EM_RS_WIRE, EM_RS_ORE, EM_CAMPFIRE,
                   EM_DRIP_WATER = 32, EM_DRIP_LAVA, EM_DRIP_LEAF, EM_DRIP_HONEY };

class WeatherFxLayer : public RenderLayer {
public:
  const char* name() const override { return "weather"; }
  int order() const override { return 10; }
  explicit WeatherFxLayer() { buildKinds(); }
  void update(GameSession& s, double dt) override;
private:
  std::vector<u8> kinds;   // per block id: what it gives off
  std::vector<u8> leafTile;
  BlockId grass = 0, mud = 0, moss = 0;
  int tileStreak = 0, tileSnow = 0;
  struct ChunkEm { const Chunk* ptr = nullptr; double at = -1; std::vector<Emitter> em; };
  std::unordered_map<ChunkKey, ChunkEm, ChunkKeyHash> emit;
  double clock = 0, ambAcc = 0;
  bool gusting = false;
  int dim = -1;
  void buildKinds();
  void scanChunk(const World& w, const Chunk& c, ChunkEm& out);
  void drops(GameSession& s, WeatherSystem& W, double dt);
  void extra(GameSession& s, WeatherSystem& W, double dt);
  void particleWeather(GameSession& s, WeatherSystem* W, double dt);
  void ambient(GameSession& s, WeatherSystem* W, double step);
  void emitters(GameSession& s, double dt, double step);
  void bolts(GameSession& s, WeatherSystem& W);
  std::unordered_set<u32> boltsSeen;
};

void WeatherFxLayer::buildKinds() {
  auto& R = reg();
  kinds.assign(R.blocks.size(), 0); leafTile.assign(R.blocks.size(), 0);
  for (auto& b : R.blocks) {
    u8 k = 0; std::string rs = b.str("rs");
    if (b.render == Render::Torch) k = rs == "torch" ? EM_RS_TORCH : rs.empty() ? (b.key == "soul_torch" ? EM_SOUL_TORCH : EM_TORCH) : 0;
    else if (b.has("fire")) k = EM_FIRE;
    else if (R.tLiquid[b.id] == 2) k = EM_LAVA;
    else if (b.has("furnace") && b.light) k = EM_FURNACE;
    else if (b.has("candle")) k = EM_CANDLE;
    else if (b.key == "end_rod") k = EM_END_ROD;
    else if (b.key == "crying_obsidian") k = EM_CRYING;
    else if (b.key == "spore_blossom") k = EM_SPORE;
    else if (R.tPortal[b.id]) k = EM_PORTAL;
    else if (rs == "wire") k = EM_RS_WIRE;
    else if (b.key == "redstone_ore_lit") k = EM_RS_ORE;
    else if (b.str("shape") == "campfire" || b.str("shape") == "soul_campfire") k = EM_CAMPFIRE;
    kinds[b.id] = k;
    // leaf colour for each kind of leaves (ambientfx LEAF_PARTICLE)
    if (R.tLeaves[b.id]) {
      const std::string& key = b.key; auto starts = [&](const char* p) { return key.rfind(p, 0) == 0; };
      std::string t = starts("cherry") || key.find("flowering") != std::string::npos ? "leaf_pink" : starts("birch") || starts("aspen") ? "leaf_light"
        : starts("spruce") || starts("pine") ? "leaf_dark" : starts("dark_oak") || starts("mangrove") || starts("jungle") ? "leaf_deep" : starts("cloud") ? "leaf_white" : "leaf_green";
      leafTile[b.id] = t == "leaf_pink" ? 1 : t == "leaf_light" ? 2 : t == "leaf_dark" ? 3 : t == "leaf_deep" ? 4 : t == "leaf_white" ? 5 : 0;
    }
  }
  grass = R.blockId("grass"); mud = R.blockId("mud"); moss = R.blockId("moss_block");
}

// where a chunk's torches, fires and drips are (worked out again every second or so)
void WeatherFxLayer::scanChunk(const World& w, const Chunk& c, ChunkEm& out) {
  out.em.clear(); out.ptr = &c; out.at = clock;
  auto& R = reg();
  int base = c.baseY(), leafDrips = 0;
  for (int sy = 0; sy < SECS; sy++) {
    const Section* sec = c.section(sy); if (!sec || !sec->solidCount) continue;
    for (int i = 0; i < 4096; i++) {
      BlockId id = sec->blocks[i]; if (!id) continue;
      int x = i & 15, z = (i >> 4) & 15, y = sy * 16 + (i >> 8);
      u8 k = id < kinds.size() ? kinds[id] : 0;
      if (k == EM_LAVA && c.get(x, y + 1, z) != AIR) k = 0;   // only the open surface pops
      if (k) { out.em.push_back({(i16)x, (i16)(base + y), (i16)z, k, sec->meta[i], (hash32(c.cx * 16 + x, base + y, c.cz * 16 + z, 77) & 1023) / 97.f}); }
      // drips form under any block with water or lava on top of it, and under leaves while it rains
      if (R.tLiquid[id] && y >= 2 && R.tSolid[c.get(x, y - 1, z)] && R.tOpaque[c.get(x, y - 1, z)] && c.get(x, y - 2, z) == AIR)
        out.em.push_back({(i16)x, (i16)(base + y - 1), (i16)z, (u8)(R.tLiquid[id] == 2 ? EM_DRIP_LAVA : EM_DRIP_WATER), 0, 0});
      else if (R.tLeaves[id] && y >= 1 && c.get(x, y - 1, z) == AIR && leafDrips < 48 && (hash32(x, y, z, 5) & 3) == 0) { leafDrips++; out.em.push_back({(i16)x, (i16)(base + y), (i16)z, EM_DRIP_LEAF, 0, 0}); }
    }
  }
  (void)w;
}

void WeatherFxLayer::update(GameSession& s, double dt) {
  if (!s.player) return;
  clock += dt;
  ParticleSystem& P = particles();
  WeatherSystem* W = weatherOf(*s.level);
  int d = s.frame.dim;
  if (d != dim) { dim = d; if (d != DIM_OVERWORLD) P.removeDrops(); }
  if (W) { bolts(s, *W); }
  if (!P.level) return;
  float fdt = (float)std::min(dt, 0.1);
  if (W) { drops(s, *W, fdt); extra(s, *W, fdt); }
  particleWeather(s, W, fdt);
  ambAcc += dt;
  if (ambAcc >= 0.05) { double step = ambAcc; ambAcc = 0; ambient(s, W, step); emitters(s, dt, step); }
  else emitters(s, dt, 0);
}

// rain and snow round each player (game/weather.js updateDrops): what falls is decided where each drop falls
void WeatherFxLayer::drops(GameSession& s, WeatherSystem& W, double dt) {
  ParticleSystem& P = particles();
  if (s.frame.dim != DIM_OVERWORLD || s.player->realm != REALM_MAIN || W.rain <= 0) return;
  World& w = s.world();
  int want = (int)std::floor((P.level == 1 ? 420 : 1100) * W.rain * (0.6f + 0.4f * W.thunderK) * W.dens);
  int n = std::min(want - P.dropCount(), 90), tries = n * 2;
  float tilt = W.thunderK * 2.2f + W.windTilt, wdx = W.windDX, wdz = W.windDZ, drift = W.snowDrift;
  vec3 p = s.player->pos, pv = s.player->vel;
  u16 tRain = (u16)P.tile("rain_streak"), tSnow = (u16)P.tile("snowflake");
  while (n > 0 && tries-- > 0) {
    float a = rnd() * TAU, r = std::pow(rnd(), 0.8f) * 30;
    float x = p.x + std::cos(a) * r + pv.x * 0.6f, z = p.z + std::sin(a) * r + pv.z * 0.6f;
    if (!w.chunk(DIM_OVERWORLD, ifloor(x) >> 4, ifloor(z) >> 4)) continue;
    int kind = W.precipAt(w, x, p.y, z);
    if (!kind) continue;
    n--;
    float floorY = (float)W.topAt(w, ifloor(x), ifloor(z));
    float y = std::max(floorY, p.y + 6 + rnd() * (8 + r * 0.4f));
    if (floorY > p.y + 14) continue;   // under a roof: it's raining on the roof, not here
    u8 lt = w.getLight(ifloor(x), std::min(WORLD_H - 1, (int)floorY + 1), ifloor(z));
    bool snow = kind == 2; float far = 1 + std::max(0.f, r - 9) / 6;   // far off they're drawn bigger, so a shower reads as a shower
    Particle q;
    float sx = snow ? drift * wdx : tilt * wdx, sz = snow ? drift * wdz : tilt * wdz;
    q.p = vec3(x - sx * 0.8f, y, z - sz * 0.8f);
    q.v = vec3(sx, snow ? -1.6f - rnd() * 0.6f - drift * 0.15f : -16 - rnd() * 4, sz);
    q.floorY = floorY; q.drop = snow ? 2 : 1; q.ph = rnd() * 6; q.life = q.max = 12;
    q.size = (snow ? 0.05f : 0.016f) * far; q.vert = snow ? 0 : 0.5f * std::min(2.f, far);
    q.tile = snow ? tSnow : tRain;
    q.light = (u8)((std::max(lt >> 4, 12) << 4) | (lt & 15));
    P.add(q);
  }
  (void)dt;
}

// sand, dust and ash (game/skies.js extraParticles)
void WeatherFxLayer::extra(GameSession& s, WeatherSystem& W, double dt) {
  ParticleSystem& P = particles();
  World& w = s.world(); vec3 p = s.player->pos;
  float wdx = W.windDX, wdz = W.windDZ, amount = P.level == 1 ? 0.4f : 1.f;
  u16 white = (u16)P.tile("white");
  auto put = [&](Particle& q) {
    u8 lt = w.getLight(ifloor(q.p.x), ifloor(q.p.y), ifloor(q.p.z));
    if ((lt >> 4) < 8 && s.frame.dim == DIM_OVERWORLD) return;
    q.tile = white; q.light = (u8)((std::max(lt >> 4, 10) << 4) | (lt & 15)); P.add(q);
  };
  if (s.frame.dim == DIM_OVERWORLD && W.sand > 0.05f) {
    int n = (int)std::ceil(W.sand * 160 * dt * amount);
    for (int k = 0; k < n; k++) {
      float a = rnd() * TAU, r = 2 + rnd() * 18, sp = 9 + rnd() * 7; bool big = rnd() < 0.25f;
      Particle q; q.p = vec3(p.x + std::cos(a) * r - wdx * 6, p.y + rnd() * 4 - 0.5f, p.z + std::sin(a) * r - wdz * 6);
      q.v = vec3(wdx * sp, (rnd() - 0.5f) * 0.6f, wdz * sp); q.life = q.max = 1.2f + rnd(); q.size = big ? 0.07f : 0.02f;
      if (!big) { q.streak = 0.35f; q.sd = vec3(wdx, 0, wdz); }
      u8 c[3] = {(u8)(big ? 150 : 178), (u8)(big ? 118 : 140), (u8)(big ? 78 : 92)}; std::copy(c, c + 3, q.tint);
      put(q);
    }
  }
  if (s.frame.dim == DIM_NETHER) {
    int n = (int)std::ceil(W.ash * 70 * dt * amount);
    for (int k = 0; k < n; k++) {
      float a = rnd() * TAU, r = std::pow(rnd(), 0.7f) * 22;
      Particle q; q.p = vec3(p.x + std::cos(a) * r, p.y + 2 + rnd() * 10, p.z + std::sin(a) * r);
      q.v = vec3((rnd() - 0.5f) * 0.5f + wdx * 0.4f, -0.4f - rnd() * 0.5f, (rnd() - 0.5f) * 0.5f + wdz * 0.4f); q.life = q.max = 6 + rnd() * 6; q.size = 0.03f + rnd() * 0.025f;
      bool hot = rnd() < 0.08f; q.tint[0] = hot ? 255 : 96; q.tint[1] = hot ? 120 : 90; q.tint[2] = hot ? 40 : 86;
      put(q);
    }
  }
}

// wind strokes, sand and snow lifted by gusts, fireflies, ash and embers, silt (game/particles2.js particleWeather)
void WeatherFxLayer::particleWeather(GameSession& s, WeatherSystem* W, double dt) {
  ParticleSystem& P = particles(); auto& R = reg();
  World& w = s.world(); Player& pl = *s.player; vec3 p = pl.pos;
  float gust = W ? clampv(W->gust / 0.35f, 0.f, 1.f) : 0.f, amount = P.level == 1 ? 0.45f : 1.f;
  bool gustSound = false;
  if (W) { if (gust > 0.6f && !gusting) { gusting = true; gustSound = true; } else if (gust < 0.3f) gusting = false; }
  int sky = w.getLight(ifloor(p.x), ifloor(p.y + 1.6f), ifloor(p.z)) >> 4;
  int d = s.frame.dim;
  bool open = (d == DIM_OVERWORLD || d == DIM_AETHER) && sky >= 13 && pl.realm == REALM_MAIN;
  if (gustSound && open) s.level->fx->soundUi(pl.id, "gust");
  if (W && open && gust > 0.05f) {
    float dx = -std::sin(W->windDir), dz = -std::cos(W->windDir), sp = 7 + gust * 6;
    float n = gust * 45 * amount * (float)dt;
    u16 t = (u16)P.tile("wind_streak");
    while (n > rnd()) {
      n -= 1;
      float x = p.x + (rnd() - 0.5f) * 24 - dx * 8, z = p.z + (rnd() - 0.5f) * 24 - dz * 8, y = p.y + rnd() * 6 - 1;
      u8 lt = w.getLight(ifloor(x), ifloor(y), ifloor(z));
      if ((lt >> 4) < 14 || R.tSolid[w.getBlock(ifloor(x), ifloor(y), ifloor(z))]) continue;
      Particle q; q.p = vec3(x, y, z); q.v = vec3(dx * sp, (rnd() - 0.5f) * 0.4f, dz * sp); q.life = q.max = 0.5f + rnd() * 0.6f; q.size = 0.05f;
      q.streak = 0.6f + rnd() * 0.8f; q.sd = vec3(dx, 0, dz); q.tile = t; q.light = lt;
      P.add(q);
    }
    if (gust > 0.5f) {   // sand and snow lifted off the ground in the strong ones
      float m = (gust - 0.5f) * 30 * amount * (float)dt;
      while (m > rnd()) {
        m -= 1;
        int x = ifloor(p.x + (rnd() - 0.5f) * 16), z = ifloor(p.z + (rnd() - 0.5f) * 16);
        if (!w.chunkAtBlock(x, ifloor(p.y), z)) continue;
        int y = std::min(WORLD_H - 1, ifloor(p.y) + 4);
        while (y > ifloor(p.y) - 6 && !w.getBlock(x, y, z)) y--;
        BlockId id = w.getBlock(x, y, z); const std::string& key = R.block(id).key;
        bool ok = key.size() >= 4 && (key.compare(key.size() - 4, 4, "sand") == 0 || key.rfind("snow", 0) == 0 || key == "powder_snow");
        if (!ok) continue;
        u8 lt = w.getLight(x, y + 1, z);
        int tl = R.block(id).faces[2];
        for (int i = 0; i < 3; i++) {
          Particle q; q.p = vec3(x + rnd(), y + 1.05f, z + rnd()); q.v = vec3(dx * (3 + rnd() * 3), 0.6f + rnd() * 0.8f, dz * (3 + rnd() * 3));
          q.life = q.max = 0.8f + rnd() * 0.8f; q.size = 0.03f + rnd() * 0.02f; q.tile = (u16)tl; q.light = lt; q.grav = 1.2f;
          int ou = (int)(rnd() * 96), ov = (int)(rnd() * 96); q.uv[0] = (u8)ou; q.uv[1] = (u8)ov; q.uv[2] = (u8)(ou + 32); q.uv[3] = (u8)(ov + 32);
          P.add(q);
        }
      }
    }
  }
  // fireflies over the grass on warm nights
  if (d == DIM_OVERWORLD && sky >= 12 && s.frame.daylight < 0.3f && !(W && W->rain > 0.3f)) {
    float n = 1.6f * amount * (float)dt;
    int flies = 0; for (auto& q : P.list) if (q.blink > 0) flies++;
    while (n > rnd() && flies < 26) {
      n -= 1;
      int x = ifloor(p.x + (rnd() - 0.5f) * 20), z = ifloor(p.z + (rnd() - 0.5f) * 20);
      int y = std::min(WORLD_H - 1, ifloor(p.y) + 5);
      while (y > ifloor(p.y) - 6 && !R.tSolid[w.getBlock(x, y, z)] && !R.tLiquid[w.getBlock(x, y, z)]) y--;
      BlockId id = w.getBlock(x, y, z);
      if (!id || (id != grass && id != mud && id != moss)) continue;
      if (W && W->column(*s.level, x, z).temp < -0.2f) continue;
      P.spawn("firefly", vec3(x + rnd(), y + 1.3f + rnd() * 1.5f, z + rnd()), 1);
      flies++;
    }
  }
  // the Ember Depths: ash drifting down, embers drifting up
  if (d == DIM_NETHER) {
    float n = 7 * amount * (float)dt;
    u16 tAsh = (u16)P.tile("ash_flake"), tEmb = (u16)P.tile("ember_spark");
    while (n > rnd()) {
      n -= 1;
      bool ember = rnd() < 0.25f; float x = p.x + (rnd() - 0.5f) * 18, z = p.z + (rnd() - 0.5f) * 18, y = p.y + (ember ? -3 : 1) + rnd() * 7;
      if (R.tSolid[w.getBlock(ifloor(x), ifloor(y), ifloor(z))]) continue;
      Particle q; q.p = vec3(x, y, z); q.life = q.max = ember ? 1.5f + rnd() * 1.5f : 4 + rnd() * 3;
      q.v = vec3((rnd() - 0.5f) * 0.3f, ember ? 0.5f + rnd() * 0.5f : -0.25f - rnd() * 0.2f, (rnd() - 0.5f) * 0.3f);
      q.size = q.size0 = ember ? 0.02f : 0.03f; q.shrink = ember; q.tile = ember ? tEmb : tAsh; q.light = ember ? 0xFF : w.getLight(ifloor(x), ifloor(y), ifloor(z));
      q.flutter = ember ? 0.2f : 0.5f; q.ph = rnd() * 6;
      P.add(q);
    }
  }
  // silt hanging in the water around you
  if (R.tLiquid[w.getBlock(ifloor(s.frame.cam.pos.x), ifloor(s.frame.cam.pos.y), ifloor(s.frame.cam.pos.z))] == 1) {
    float n = 6 * amount * (float)dt; u16 t = (u16)P.tile("water_speck"); vec3 e = vec3(s.frame.cam.pos);
    while (n > rnd()) {
      n -= 1;
      vec3 q0 = e + vec3((rnd() - 0.5f) * 7, (rnd() - 0.5f) * 4, (rnd() - 0.5f) * 7);
      if (!R.tLiquid[w.getBlock(ifloor(q0.x), ifloor(q0.y), ifloor(q0.z))]) continue;
      Particle q; q.p = q0; q.v = vec3((rnd() - 0.5f) * 0.08f, -0.03f, (rnd() - 0.5f) * 0.08f); q.life = q.max = 2 + rnd() * 2; q.size = 0.02f; q.tile = t; q.light = w.getLight(ifloor(q0.x), ifloor(q0.y), ifloor(q0.z));
      P.add(q);
    }
  }
}

// leaves drifting down from trees, and tumbling along the grass in a strong wind (game/ambientfx.js)
void WeatherFxLayer::ambient(GameSession& s, WeatherSystem* W, double step) {
  ParticleSystem& P = particles(); auto& R = reg();
  World& w = s.world(); vec3 p = s.player->pos;
  float dir = W ? W->windDir : 0, strength = W ? W->windStrength : 0.5f, gust = clampv((strength - 0.45f) * 2, 0.f, 1.f);
  int tries = (int)((P.level == 1 ? 30 : 70) * (1 + gust * 1.5f));
  static const char* LEAF[6] = {"leaf_green", "leaf_pink", "leaf_light", "leaf_dark", "leaf_deep", "leaf_white"};
  for (int i = 0; i < tries; i++) {
    int x = ifloor(p.x + (rnd() - 0.5f) * 34), y = ifloor(p.y + rnd() * 22 - 4), z = ifloor(p.z + (rnd() - 0.5f) * 34);
    BlockId id = w.getBlock(x, y, z);
    if (!R.tLeaves[id] || w.getBlock(x, y - 1, z) != AIR) continue;
    if (rnd() > step * (3 + gust * 6)) continue;
    float sp = strength * (0.9f + gust * 1.8f);
    Particle q; q.p = vec3(x + rnd(), y - 0.02f, z + rnd()); q.v = vec3(-std::sin(dir) * sp, -0.5f - rnd() * 0.3f, -std::cos(dir) * sp);
    q.life = 7 + rnd() * 4; q.max = 11; q.size = 0.075f + rnd() * 0.03f; q.tile = (u16)P.tile(LEAF[id < leafTile.size() ? leafTile[id] : 0]);
    q.light = w.getLight(x, y - 1, z); q.grav = 0.02f; q.flutter = 1.1f + gust; q.ph = rnd() * 6;
    P.add(q);
  }
  if (gust > 0.3f && rnd() < step * gust * 3) {
    int x = ifloor(p.x + (rnd() - 0.5f) * 20), z = ifloor(p.z + (rnd() - 0.5f) * 20), y = ifloor(p.y) + 1;
    if ((w.getBlock(x, y, z) == AIR && R.tSolid[w.getBlock(x, y - 1, z)]) || (w.getBlock(x, y - 1, z) == AIR && R.tSolid[w.getBlock(x, y - 2, z)])) {
      int yy = w.getBlock(x, y - 1, z) == AIR ? y - 1 : y;
      Particle q; q.p = vec3(x + rnd(), yy + 0.1f, z + rnd()); q.v = vec3(-std::sin(dir) * 2.5f, 0.6f, -std::cos(dir) * 2.5f); q.life = q.max = 4; q.size = 0.07f;
      q.tile = (u16)P.tile("leaf_green"); q.light = w.getLight(x, yy, z); q.grav = 2.5f; q.flutter = 1.5f; q.ph = rnd() * 6;
      P.add(q);
    }
  }
}

// what blocks give off: torch flames (each frame), sparks and smoke, fire, lava pops, furnaces, candles,
// end rods, portals, redstone, drips (game/particles2.js + game/blockfx.js)
void WeatherFxLayer::emitters(GameSession& s, double dt, double step) {
  ParticleSystem& P = particles();
  World& w = s.world(); vec3 pp = s.player->pos;
  WeatherSystem* W = weatherOf(*s.level);
  bool raining = W && W->rain > 0.3f;
  int pcx = ifloor(pp.x) >> 4, pcz = ifloor(pp.z) >> 4, pd = dimOfY(clampv(ifloor(pp.y), 0, WORLD_H - 1));
  float t = (float)clock;
  float k = P.level == 1 ? 0.5f : 1.f;
  u16 tFlA = (u16)P.tile("torch_flame"), tFlB = (u16)P.tile("torch_flame2"), tSoul = (u16)P.tile("soul_flame");
  int rescans = 2, leafBudget = 12;
  // forget chunks that are gone
  if ((int)(clock * 10) % 20 == 0) for (auto it = emit.begin(); it != emit.end();) { if (!w.chunk(it->first.dim, it->first.cx, it->first.cz)) it = emit.erase(it); else ++it; }
  for (int bd = std::max(0, pd - 1); bd <= std::min(DIM_COUNT - 1, pd + 1); bd++) {
    if (std::abs(pp.y - (dimBase(bd) + DIM_H / 2)) > DIM_H / 2 + 24) continue;
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      const Chunk* c = w.chunk(bd, pcx + dx, pcz + dz); if (!c) continue;
      ChunkEm& ce = emit[c->key()];
      if (ce.ptr != c || (clock - ce.at > 1.5 && rescans > 0)) { if (ce.ptr == c) rescans--; scanChunk(w, *c, ce); }
      for (const Emitter& e : ce.em) {
        float x = c->cx * 16.f + e.x, z = c->cz * 16.f + e.z, y = e.y;
        if (std::abs(x - pp.x) > 32 || std::abs(z - pp.z) > 32 || std::abs(y - pp.y) > 24) continue;
        u8 kind = e.kind, meta = e.meta;
        if (kind == EM_TORCH || kind == EM_SOUL_TORCH) {
          // the flame on its tip, burning all the time: it flickers between two shapes and breathes in size
          float tx = x + 0.5f, tz = z + 0.5f, ty = y + 10 / 16.f;
          if (meta >= 1 && meta <= 4) {
            float wx = meta == 1 ? 1.f : meta == 2 ? -1.f : 0.f, wz = meta == 3 ? 1.f : meta == 4 ? -1.f : 0.f, L = 10 / 16.f;
            tx = x + 0.5f - wx * (0.5f - 1.6f / 16) + wx * L * std::sin(0.4f); tz = z + 0.5f - wz * (0.5f - 1.6f / 16) + wz * L * std::sin(0.4f);
            ty = y + 3.5f / 16 + L * std::cos(0.4f);
          }
          float ph = t * 6 + e.h; int frame = (int)std::floor(ph + std::sin(ph * 1.9f) * 0.6f) & 1;
          Particle q; q.p = vec3(tx + std::sin(t * 11 + e.h) * 0.006f, ty, tz + std::cos(t * 9 + e.h) * 0.006f);
          q.size = 0.072f + std::sin(t * 23 + e.h * 3) * 0.007f + std::sin(t * 37 + e.h) * 0.005f; q.p.y += q.size * 0.3f;
          q.tile = kind == EM_SOUL_TORCH ? tSoul : frame ? tFlB : tFlA; q.light = 0xFF; q.life = q.max = (float)dt * 1.5f + 1e-3f;
          P.add(q);
          // now and then a spark flies up, and a wisp of smoke rises, grows and gathers under the ceiling
          if (step > 0 && rnd() <= step * 2.4f) {
            if (rnd() < 0.3f) P.spawn(kind == EM_SOUL_TORCH ? "soul_flame" : "ember", vec3(tx, ty, tz), 1);
            if (rnd() < 0.85f) P.spawn("torch_smoke", vec3(tx, ty, tz), 1);
          }
          continue;
        }
        if (step <= 0) continue;
        switch (kind) {
          case EM_FIRE: case EM_LAVA: {
            if (rnd() > step * (kind == EM_LAVA ? 0.4f : 2.f)) break;
            Particle q; q.p = vec3(x + rnd(), y + (kind == EM_LAVA ? 0.95f : 0.6f), z + rnd()); q.v = vec3((rnd() - 0.5f) * 1.5f, 2 + rnd() * 2, (rnd() - 0.5f) * 1.5f);
            q.life = q.max = 0.7f; q.size = 0.05f; q.tile = (u16)P.tile("flame"); q.light = 0xFF; q.grav = 6; P.add(q);
            if (kind == EM_FIRE && rnd() < 0.8f) P.spawn("fire_smoke", vec3(x + 0.5f, y + 0.8f, z + 0.5f), 1);
            if (kind == EM_FIRE && rnd() < 0.1f) s.level->fx->sound("fire_crackle", vec3(x + 0.5f, y + 0.5f, z + 0.5f), 14);
            if (kind == EM_LAVA && rnd() < 0.15f) s.level->fx->sound("lava_pop", vec3(x + 0.5f, y + 1, z + 0.5f), 16);
            break;
          }
          case EM_FURNACE:
            if (rnd() < step * 1.5f) P.spawn("smoke", vec3(x + 0.5f, y + 1.02f, z + 0.5f), 1);
            if (rnd() < step * 2) { int f = meta & 3; float ox = f == 1 ? 0.56f : f == 3 ? -0.56f : 0.f, oz = f == 0 ? -0.56f : f == 2 ? 0.56f : 0.f;
              P.spawn("torch_flame", vec3(x + 0.5f + ox + (rnd() - 0.5f) * 0.3f * (oz != 0), y + 0.2f + rnd() * 0.2f, z + 0.5f + oz + (rnd() - 0.5f) * 0.3f * (ox != 0)), 1); }
            break;
          case EM_CANDLE:
            if ((meta & 4) && rnd() < step * 2) { Particle q; q.p = vec3(x + 0.5f + (rnd() - 0.5f) * 0.3f, y + 0.55f, z + 0.5f + (rnd() - 0.5f) * 0.3f); q.v = vec3(0, 0.3f, 0); q.life = q.max = 1; q.size = 0.025f; q.tile = (u16)P.tile("smoke"); q.light = 0xFF; q.grav = -0.2f; P.add(q); }
            break;
          case EM_END_ROD:
            if (rnd() < step * 1.2f) { Particle q; q.p = vec3(x + 0.5f + (rnd() - 0.5f) * 0.3f, y + 0.5f + (rnd() - 0.5f) * 0.6f, z + 0.5f + (rnd() - 0.5f) * 0.3f); q.v = vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 0.3f; q.life = q.max = 1.6f; q.size = 0.04f; q.size0 = 0.05f; q.shrink = true; q.tile = (u16)P.tile("sparkle"); q.light = 0xFF; P.add(q); }
            break;
          case EM_CRYING:
            if (rnd() < step * 0.6f && !reg().tSolid[w.getBlock((int)x, (int)y - 1, (int)z)]) P.spawn("drip_tear", vec3(x + 0.2f + rnd() * 0.6f, y - 0.03f, z + 0.2f + rnd() * 0.6f), 1);
            break;
          case EM_SPORE:
            if (rnd() < step * 3) { Particle q; q.p = vec3(x + (rnd() - 0.5f) * 6, y - rnd() * 4, z + (rnd() - 0.5f) * 6); q.v = vec3((rnd() - 0.5f) * 0.15f, -0.08f, (rnd() - 0.5f) * 0.15f); q.life = q.max = 5; q.size = 0.025f; q.tile = (u16)P.tile("spore"); q.light = 0xFF; q.flutter = 0.2f; q.ph = rnd() * 6; P.add(q); }
            break;
          case EM_PORTAL:
            if (rnd() < step * 2) { Particle q; q.p = vec3(x + rnd(), y + rnd(), z + rnd()); q.v = vec3((rnd() - 0.5f) * 0.6f, (rnd() - 0.3f) * 0.6f, (rnd() - 0.5f) * 0.6f); q.life = q.max = 1.2f; q.size = 0.05f;
              q.tile = (u16)P.tile(reg().block(w.getBlock((int)x, (int)y, (int)z)).key == "ember_portal" ? "portal_mote_ember" : "portal_mote_sky"); q.light = 0xFF; P.add(q); }
            break;
          case EM_RS_TORCH: case EM_RS_WIRE: case EM_RS_ORE: {
            float pw = kind == EM_RS_WIRE ? (meta & 15) / 15.f : 1.f;
            if (!pw || rnd() > step * (kind == EM_RS_TORCH ? 4 : 1.2f) * pw) break;
            float tx = x + 0.5f, ty = y + 0.75f, tz = z + 0.5f;
            if (kind == EM_RS_TORCH && meta) { ty = y + 0.95f; tx += meta == 1 ? -0.17f : meta == 2 ? 0.17f : 0; tz += meta == 3 ? -0.17f : meta == 4 ? 0.17f : 0; }
            if (kind == EM_RS_WIRE) { tx = x + 0.2f + rnd() * 0.6f; tz = z + 0.2f + rnd() * 0.6f; ty = y + 0.08f; }
            if (kind == EM_RS_ORE) { int f = (int)(rnd() * 6); tx = x + (f == 0 ? 1.05f : f == 1 ? -0.05f : rnd()); ty = y + (f == 2 ? 1.05f : f == 3 ? -0.05f : rnd()); tz = z + (f == 4 ? 1.05f : f == 5 ? -0.05f : rnd()); }
            Particle q; q.p = vec3(tx, ty, tz); q.v = vec3(0, 0.15f, 0); q.life = q.max = 0.6f; q.size = 0.035f; q.tile = (u16)P.tile("redstone_spark"); q.light = 0xFF; P.add(q);
            break;
          }
          case EM_CAMPFIRE:
            if (rnd() < step * 3) P.spawn("campfire_smoke", vec3(x + 0.5f, y + 0.6f, z + 0.5f), 1);
            if (rnd() < step * 2) P.spawn("flame", vec3(x + 0.3f + rnd() * 0.4f, y + 0.3f, z + 0.3f + rnd() * 0.4f), 1);
            break;
          case EM_DRIP_WATER: case EM_DRIP_LAVA: case EM_DRIP_LEAF: {
            if (kind == EM_DRIP_LEAF && !raining) break;
            float rate = kind == EM_DRIP_LAVA ? 0.22f : kind == EM_DRIP_LEAF ? 0.18f : 0.35f;
            if (rnd() > rate * k * step) break;
            if (kind == EM_DRIP_LEAF) { if (leafBudget-- <= 0 || (w.getLight((int)x, (int)y - 1, (int)z) >> 4) < 13 || !W || W->precipAt(w, x, y, z) != 1) break; }
            else if (w.getBlock((int)x, (int)y - 1, (int)z) != AIR) break;
            P.spawn(kind == EM_DRIP_LAVA ? "drip_lava" : kind == EM_DRIP_LEAF ? "drip_leaf" : "drip_water", vec3(x + 0.15f + rnd() * 0.7f, y - 0.03f, z + 0.15f + rnd() * 0.7f), 1);
            break;
          }
        }
      }
    }
  }
}

// lightning: a jagged chain of glowing strokes from the clouds to the ground, gone in a blink (LightningBolt)
void WeatherFxLayer::bolts(GameSession& s, WeatherSystem& W) {
  ParticleSystem& P = particles();
  for (auto& b : W.bolts) {
    if (boltsSeen.count(b.seed)) continue;
    boltsSeen.insert(b.seed);
    Mulberry32 r(b.seed);
    float x = 0, z = 0, y = std::max(b.pos.y + 70, (float)CLOUD_BASE_Y + 10) - b.pos.y;
    u16 t = (u16)P.tile("white");
    auto seg = [&](vec3 a, vec3 c, float w) {
      Particle q; vec3 d = c - a; float len = glm::length(d); if (len < 1e-3f) return;
      q.p = b.pos + (a + c) * 0.5f; q.sd = d / len; q.streak = len * 0.5f; q.size = q.size0 = w; q.tile = t; q.light = 0xFF; q.glow = true;
      q.life = q.max = b.t; q.blink = 60; q.tint[0] = 222; q.tint[1] = 236; q.tint[2] = 255;
      P.addForced(q);
    };
    while (y > 0) {
      float ny = std::max(0.f, y - 2 - (float)r() * 4), nx = x + ((float)r() - 0.5f) * 2.4f, nz = z + ((float)r() - 0.5f) * 2.4f;
      seg(vec3(x, y, z), vec3(nx, ny, nz), 0.12f);
      if (r() < 0.12) { float bx = nx + ((float)r() - 0.5f) * 6, bz = nz + ((float)r() - 0.5f) * 6; seg(vec3(nx, ny, nz), vec3(bx, std::max(0.f, ny - 3), bz), 0.06f); }
      x = nx; z = nz; y = ny;
    }
  }
  if (boltsSeen.size() > 64) { std::unordered_set<u32> keep; for (auto& b : W.bolts) keep.insert(b.seed); boltsSeen.swap(keep); }
  (void)s;
}

// ---- the particle engine -------------------------------------------------------------------------------
class ParticleLayer : public RenderLayer {
public:
  explicit ParticleLayer() { particles().clear(); }
  ~ParticleLayer() override { particles().clear(); particles().world = nullptr; }
  const char* name() const override { return "particles"; }
  int order() const override { return 20; }
  void update(GameSession& s, double dt) override {
    ParticleSystem& P = particles();
    P.level = s.app.settings.particles;
    WeatherSystem* W = weatherOf(*s.level);
    float ws = W ? W->windStrength : 0.5f, wd = W ? W->windDir : 0;
    P.windDrift = vec2(-std::sin(wd) * ws * 0.35f, -std::cos(wd) * ws * 0.35f);
    P.update(&s.world(), s.paused() ? 0.0 : dt, s.app.time);
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override { particles().draw(f, s.app.tileTex); }
};
} // namespace

void registerEnvironmentLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<DistantLayer>(); });
  registerRenderLayer([](GameSession&) { return std::make_shared<CloudLayer>(); });
  registerRenderLayer([](GameSession&) { return std::make_shared<WeatherFxLayer>(); });
  registerRenderLayer([](GameSession&) { return std::make_shared<ParticleLayer>(); });
}

} // namespace dq
