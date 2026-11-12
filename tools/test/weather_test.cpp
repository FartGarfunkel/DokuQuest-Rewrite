// tools/test/weather_test.cpp — headless checks for the weather particles (render/weather_sim, weather_sprites):
// the sprite atlas, sky spawning, collision with blocks, the budget, surface effects, dust and ground fog.
// No GL context: only the pure parts are called. Run: ./build/test_weather [--userdir DIR] (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "assets/tileset.h"
#include "render/map_colors.h"
#include "render/weather_sprites.h"
#include "render/weather_sim.h"
#include "render/height_fog.h"
#include "sim/weather.h"
#include "sim/wind_field.h"
#include "world/world.h"
#include <thread>
#include <array>
#include <cstdio>

using namespace dq;
static int fails = 0, passes = 0;
#define CHECK(c, ...) do { if (c) { printf("  ok   " __VA_ARGS__); passes++; } else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static int opaque(const WeatherSprites& S, int sprite) {
  int n = 0, c = S.cell, cx = sprite % S.cols, cy = sprite / S.cols, W = S.cols * c;
  for (int y = 0; y < c; y++) for (int x = 0; x < c; x++) if (S.rgba[((size_t)(cy * c + y) * W + cx * c + x) * 4 + 3] > 16) n++;
  return n;
}

int main(int argc, char** argv) {
  CoreOptions co; co.server = true; co.userDirOverride = "/tmp/dq-weather-test";
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) co.userDirOverride = argv[++i];
  fs::remove_all(co.userDirOverride);
  if (!initCore(co)) { printf("initCore failed\n"); return 2; }

  printf("[sprites]\n");
  {
    TileSet ts; loadBuiltinTiles(ts);
    buildWeatherSprites(ts, false);
    const WeatherSprites& S = weatherSprites();
    CHECK(S.rgba.size() == (size_t)S.cols * S.cell * S.rows * S.cell * 4 && S.cols * S.rows >= (int)WSprite::Count, "atlas holds all %d sprites", (int)WSprite::Count);
    bool all = true; for (int i = 0; i < (int)WSprite::Count; i++) if (opaque(S, i) == 0) { all = false; printf("    sprite %d is empty\n", i); }
    CHECK(all, "every sprite has something in it");
    bool grows = true; for (int i = 1; i < 8; i++) if (opaque(S, (int)WSprite::Ripple0 + i) <= opaque(S, (int)WSprite::Ripple0 + i - 1)) grows = false;
    CHECK(grows, "ripple rings grow frame by frame");
    CHECK(opaque(S, (int)WSprite::HeavyRain0) > opaque(S, (int)WSprite::Rain0), "heavy rain is denser than one rain slice");
    CHECK(opaque(S, (int)WSprite::HeavySnow0) > opaque(S, (int)WSprite::Snow0), "heavy snow is denser than one snow slice");
    vec4 u = S.uv(0); CHECK(u.x > 0 && u.y > 0 && u.z < 1.f / S.cols && u.w < 1.f / S.rows, "uv stays inside its cell");
  }

  printf("[height fog]\n");
  {
    const float sea = (float)dimBase(DIM_OVERWORLD) + 64;
    const vec4 lo = heightFogParams(sea + 2, DIM_OVERWORLD, 0.f, 0.f, true), hi = heightFogParams(sea + 80, DIM_OVERWORLD, 0.f, 0.f, true);
    CHECK(lo.z > 0 && lo.x > hi.x, "fog thins as you climb (%.5f low, %.5f high)", lo.x, hi.x);
    const vec4 misty = heightFogParams(sea + 2, DIM_OVERWORLD, 0.8f, 0.f, true);
    CHECK(misty.x > lo.x, "mist thickens it (%.5f against %.5f)", misty.x, lo.x);
    CHECK(heightFogParams(sea + 2, DIM_OVERWORLD, 0.f, 0.f, false).z == 0, "Height Fog off: none");
    CHECK(heightFogParams((float)dimBase(DIM_NETHER) + 40, DIM_NETHER, 0.5f, 0.f, true).z == 0 && heightFogParams(40, DIM_END, 0.5f, 0.f, true).z == 0, "not in the Nether or the End");
    const vec4 deep = heightFogParams(sea - 60, DIM_OVERWORLD, 1.f, 1.f, true);
    CHECK(std::isfinite(deep.x) && deep.x < 0.2f, "deep in a valley it stays finite and bounded (%.4f)", deep.x);
    CHECK(heightFogParams(sea - 30, DIM_OVERWORLD, 0.f, 0.f, true, 0.f).x == 0, "none in a cave or a closed room (no open sky)");
    const vec4 half = heightFogParams(sea + 2, DIM_OVERWORLD, 0.f, 0.f, true, 0.5f), full = heightFogParams(sea + 2, DIM_OVERWORLD, 0.f, 0.f, true, 1.f);
    CHECK(std::abs(half.x - full.x * 0.5f) < 1e-6f, "half open, half the fog");
    // looking down a pit: along the ray the density may rise to the floor's cap (d0 e^1.5), no further
    const vec4 top = heightFogParams(sea + 40, DIM_OVERWORLD, 0.f, 0.f, true), floorCap = heightFogParams(sea - 200, DIM_OVERWORLD, 0.f, 0.f, true);
    CHECK(top.w > 0 && std::abs(top.x * std::exp(top.w) - floorCap.x) < floorCap.x * 1e-3f, "a ray down a pit thickens only to the floor's cap (w %.2f)", top.w);
    CHECK(floorCap.w == 0, "already at the cap: no more thickening (w %.2f)", floorCap.w);
  }

  printf("[spawning]\n");
  {
    WRng r(7); bool upper = true, onSphere = true; int below = 0;
    for (int i = 0; i < 10000; i++) {
      vec3 p = skySpawnPoint(r, 16, false);
      if (p.y < -1e-3f) upper = false;
      if (std::abs(glm::length(p) - 16) > 1e-3f) onSphere = false;
      if (skySpawnPoint(r, 16, true).y < 0) below++;
    }
    CHECK(upper && onSphere, "slow: points on the upper half of the sphere");
    CHECK(below >= 3000, "fast: the whole sphere (%d of 10000 below)", below);
  }

  // a test world: a cleared stone floor at band-local y 100 where it rains
  ThreadPool pool(1);
  EventBus events;
  LevelInfo info; info.name = "WeatherTest"; info.seedText = "weather"; info.time = 200;
  fs::create_directories(userDir() / "saves");
  auto lv = Level::create(userDir() / "saves", info, &pool, &events);
  World& w = lv->world();
  WeatherSystem* W = weatherOf(*lv);
  W->load(*lv);
  const int FY = dimBase(DIM_OVERWORLD) + 100;   // the floor
  int bx = 0;
  for (int t = 0; t < 40; t++) { if (W->precipAt(w, bx + 8.f, FY + 2.f, 8.f) == 1) break; bx += 256; }
  std::vector<StreamAnchor> anchors = {{vec3(bx + 8, FY, 8), 3, false}};
  for (int i = 0; i < 600 && !(w.isLoaded(bx - 24, FY, -24) && w.isLoaded(bx + 40, FY, 40) && w.pendingGeneration() == 0); i++) { w.stream(anchors, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  const BlockId stone = reg().blockId("stone"), water = reg().blockId("water");
  for (int x = bx - 24; x <= bx + 40; x++) for (int z = -24; z <= 40; z++) {
    for (int y = FY + 1; y < FY + 60; y++) w.setBlock(x, y, z, 0, 0);
    w.setBlock(x, FY, z, stone, 0);
  }
  // a closed hut (5x5 inside, 4 high) round x bx+6..bx+10, z 6..10; a wall at x bx+20; a pool at bx..bx+3, z 20..23
  for (int x = bx + 5; x <= bx + 11; x++) for (int z = 5; z <= 11; z++) for (int y = FY + 1; y <= FY + 5; y++) {
    bool shell = x == bx + 5 || x == bx + 11 || z == 5 || z == 11 || y == FY + 5;
    w.setBlock(x, y, z, shell ? stone : 0, 0);
  }
  for (int z = -10; z <= 30; z++) for (int y = FY + 1; y <= FY + 8; y++) w.setBlock(bx + 20, y, z, stone, 0);
  for (int x = bx; x <= bx + 3; x++) for (int z = 20; z <= 23; z++) w.setBlock(x, FY, z, water, 0);
  printf("test site at x %d (rain there: %d)\n", bx, W->precipAt(w, bx + 8.f, FY + 2.f, 8.f));

  auto storm = [&](float windStrength) {
    W->kind = "thunder"; W->rain = 1; W->thunderK = 1; W->dens = 1; W->windStrength = windStrength; W->windDir = 1.2f; W->gust = 0.3f;
  };
  auto inputs = [&](vec3 cam, double t) {
    WeatherInputs in; in.world = &w; in.W = W; in.cam = cam; in.camVel = vec3(0); in.band = DIM_OVERWORLD; in.dist = 16; in.surface = 100;
    in.surfaceFx = true; in.groundFog = true; in.t = t; in.wind = windParams(*lv); in.wind.strength = W->windStrength; in.wind.gust = W->gust; return in;
  };

  printf("[collision]\n");
  {
    WeatherHit h;
    CHECK(sweepHit(w, vec3(bx + 19.5f, FY + 2.5f, 0.5f), vec3(bx + 20.5f, FY + 2.5f, 0.5f), h) && h.face != 0 && h.pos.x <= bx + 20.001f, "a sweep stops at a wall's face (x %.3f)", h.pos.x);
    CHECK(sweepHit(w, vec3(bx + 1.5f, FY + 1.5f, 21.5f), vec3(bx + 1.5f, FY - 0.5f, 21.5f), h) && h.liquid && h.face == 0, "water is hit as a liquid, from the top");
    CHECK(!sweepHit(w, vec3(bx + 2.5f, FY + 9.5f, 2.5f), vec3(bx + 2.5f, FY + 3.5f, 2.5f), h), "open air is no hit");
  }

  printf("[rain]\n");
  {
    storm(1.0f);
    WeatherSim sim; sim.setBudget(1500);
    const vec3 cam(bx + 8.5f, FY + 2.6f, 8.5f);   // inside the hut
    int inside = 0, most = 0;
    for (int i = 0; i < 400; i++) {
      sim.update(inputs(cam, 1000 + i * 0.05), 0.05);
      most = std::max(most, sim.count());
      for (const WParticle& p : sim.all()) if (p.pos.x > bx + 5 && p.pos.x < bx + 11 && p.pos.z > 5 && p.pos.z < 11 && p.pos.y > FY && p.pos.y < FY + 5) inside++;
    }
    CHECK(most > 50, "a storm makes particles round the hut (%d at most)", most);
    CHECK(inside == 0, "nothing falls inside a closed hut in a gale (%d seen inside)", inside);
  }
  {
    storm(0.6f);
    WeatherSim sim; sim.setBudget(600);
    const vec3 cam(bx + 2.5f, FY + 1.6f, 2.5f);
    bool capped = true; int most = 0;
    for (int i = 0; i < 200; i++) { sim.update(inputs(cam, 2000 + i * 0.05), 0.05); most = std::max(most, sim.count()); if (sim.count() > 600) capped = false; }
    CHECK(capped && most > 300, "the budget is a hard cap (most %d of 600)", most);
    bool rainy = false, heavy = false; for (const WParticle& p : sim.all()) { if (p.kind == WKind::Rain) rainy = true; if (p.kind == WKind::HeavyRain) heavy = true; }
    CHECK(rainy && heavy, "a thunderstorm has rain and heavy rain");
    WeatherInputs in = inputs(cam, 3000); in.band = DIM_NETHER;
    sim.update(in, 0.05);
    CHECK(sim.count() == 0, "changing band clears the weather (%d left)", sim.count());
  }

  printf("[surface]\n");
  {
    storm(0.3f);
    auto run = [&](vec3 cam, bool fx, int steps) {
      WeatherSim sim; sim.setBudget(1500);
      int ripples = 0, rippleOff = 0, streaks = 0, streakOff = 0, surface = 0;
      for (int i = 0; i < steps; i++) {
        WeatherInputs in = inputs(cam, 4000 + i * 0.05); in.surfaceFx = fx;
        sim.update(in, 0.05);
        for (const WParticle& p : sim.all()) {
          if (p.kind == WKind::Ripple) { ripples++; if (std::abs(p.pos.y - (FY + 1)) > 0.02) rippleOff++; }
          if (p.kind == WKind::Streak) { streaks++; const double fx0 = std::min(std::abs(p.pos.x - (bx + 20)), std::abs(p.pos.x - (bx + 21))); if (fx0 > 0.02) streakOff++; }
          if (p.kind >= WKind::Ripple) surface++;
        }
      }
      return std::array<int, 5>{ripples, rippleOff, streaks, streakOff, surface};
    };
    auto r = run(vec3(bx + 2.5f, FY + 2.6f, 21.5f), true, 200);
    CHECK(r[0] > 0 && kindDef(WKind::Ripple).rot == WRot::Flat, "rain on a pool makes flat ripples (%d seen)", r[0]);
    CHECK(r[1] == 0, "ripples lie on the water's surface (%d off it)", r[1]);
    auto w2 = run(vec3(bx + 18.5f, FY + 2.6f, 0.5f), true, 200);
    CHECK(w2[2] > 0, "rain beside a stone wall makes streaks down it (%d seen)", w2[2]);
    CHECK(w2[3] == 0, "streaks hug the wall's face (%d off it)", w2[3]);
    auto off = run(vec3(bx + 2.5f, FY + 2.6f, 21.5f), false, 200);
    CHECK(off[4] == 0, "Splashes & Ripples off: no surface effects (%d)", off[4]);
  }

  // (fog before dust: the dust test streams in a far-off desert, and the chunks here go)
  printf("[fog]\n");
  {
    auto runFog = [&](const char* zone, const char* kind, float afterRain, bool on, int steps, int& flat, int& upright, int& badAlpha) {
      W->kind = kind; W->rain = std::string(kind) == "clear" ? 0.f : 1.f; W->thunderK = 0; W->sand = 0; W->windStrength = 0.3f; W->zone = zone; W->afterRainT = afterRain; W->mistNow = 0;
      WeatherSim sim; sim.setBudget(1500); flat = upright = badAlpha = 0;
      for (int i = 0; i < steps; i++) {
        WeatherInputs in = inputs(vec3(bx + 8.5f, FY + 1.6f, 20.5f), 6000 + i * 0.05); in.groundFog = on; in.surfaceFx = false;
        sim.update(in, 0.05);
        for (const WParticle& p : sim.all()) {
          if (p.kind == WKind::FogFlat) flat++;
          if (p.kind == WKind::FogUpright) upright++;
          if ((p.kind == WKind::FogFlat || p.kind == WKind::FogUpright) && (p.alpha < 0 || p.alpha > 1.0001f || glm::length(vec2(p.pos.x - (bx + 8.5), p.pos.z - 20.5)) > 100.5)) badAlpha++;
        }
      }
    };
    int f, u, bad;
    runFog("swamp", "clear", 0, true, 300, f, u, bad);
    CHECK(f > 0 && u > 0, "a swamp has fog lying flat and standing up (%d, %d)", f, u);
    CHECK(bad == 0, "fog fades in and out within 0..1 and stays within 100 blocks (%d bad)", bad);
    runFog("swamp", "clear", 0, false, 300, f, u, bad);
    CHECK(f == 0 && u == 0, "Ground Fog off: none (%d)", f + u);
    runFog("temperate", "clear", 0, true, 300, f, u, bad);
    CHECK(f == 0 && u == 0, "a clear temperate day has no ground fog (%d)", f + u);
    runFog("temperate", "clear", 120, true, 300, f, u, bad);
    CHECK(f > 0, "after the rain, fog lies on the land (%d)", f);
    WParticle p; p.kind = WKind::FogFlat; p.age = 0;
    CHECK(fogAlpha(p) == 0, "fog starts unseen");
    p.age = 250; CHECK(fogAlpha(p) <= 0.0001f, "and is gone at the end of its life");
    p.age = 125; CHECK(fogAlpha(p) > 0.99f, "and is fullest halfway (%.2f)", fogAlpha(p));
  }

  printf("[dust]\n");
  {
    // a desert site (no rain or snow there) with a red sand floor
    int dxs = bx + 512; for (int t = 0; t < 60; t++) { if (W->precipAt(w, dxs + 8.f, FY + 2.f, 8.f) == 0) break; dxs += 256; }
    std::vector<StreamAnchor> da = {{vec3(dxs + 8, FY, 8), 3, false}};
    for (int i = 0; i < 600 && !(w.isLoaded(dxs - 24, FY, -24) && w.isLoaded(dxs + 40, FY, 40) && w.pendingGeneration() == 0); i++) { w.stream(da, 50); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    const BlockId red = reg().blockId("red_sand");
    for (int x = dxs - 24; x <= dxs + 40; x++) for (int z = -24; z <= 40; z++) { for (int y = FY + 1; y < FY + 40; y++) w.setBlock(x, y, z, 0, 0); w.setBlock(x, FY, z, red, 0); }
    printf("desert site at x %d (precipitation there: %d)\n", dxs, W->precipAt(w, dxs + 8.f, FY + 2.f, 8.f));
    TileSet ts; loadBuiltinTiles(ts);
    float want[3]; mapRGB(ts, red, 0x88, want);
    for (float& c : want) c /= 255.f;   // (mapRGB is 0..255; a particle's tint is 0..1)
    const vec3 cam(dxs + 8.5f, FY + 1.6f, 8.5f);
    auto runDust = [&](float sand, float wind, int steps, int& dust, int& shrubs, int& offColour) {
      W->kind = "thunder"; W->rain = 1; W->thunderK = 1; W->sand = sand; W->windStrength = wind; W->zone = "desert";
      WeatherSim sim; sim.setBudget(1500); dust = shrubs = offColour = 0;
      for (int i = 0; i < steps; i++) {
        WeatherInputs in = inputs(cam, 5000 + i * 0.05); in.tiles = &ts;
        sim.update(in, 0.05);
        for (const WParticle& p : sim.all()) {
          if (p.kind == WKind::Dust || p.kind == WKind::SkitterDust) {
            dust++;
            for (int c = 0; c < 3; c++) if (std::abs(p.tint[c] - want[c]) > 0.25f * std::max(want[c], 0.1f)) { offColour++; break; }
          }
          if (p.kind == WKind::Shrub) shrubs++;
        }
      }
    };
    int dust, shrubs, off;
    runDust(1, 1, 300, dust, shrubs, off);
    CHECK(dust > 0, "a sandstorm raises dust (%d seen)", dust);
    CHECK(off == 0, "the dust is the colour of the ground under it (%d off colour)", off);
    runDust(1, 1, 2000, dust, shrubs, off);
    CHECK(shrubs <= dust / 50, "tumbling shrubs are rare (%d against %d dust)", shrubs, dust);
    runDust(0, 0.2f, 300, dust, shrubs, off);
    CHECK(dust == 0 && shrubs == 0, "no dust when it's calm (%d)", dust);
  }

  printf("%d passed, %d failed\n", passes, fails);
  return fails;
}
