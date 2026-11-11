// render/weather_sim.cpp — see weather_sim.h
#include "render/weather_sim.h"
#include "core/registry.h"
#include "sim/weather.h"
#include "world/world.h"
#include "render/map_colors.h"
#include <algorithm>
#include <cmath>

namespace dq {

const WKindDef& kindDef(WKind k) {
  // gravity, wind, stormWind, size, rotSpeed, bounce, opacity, distScale, lifetime, rot, blended  (particles.json)
  static const WKindDef T[(int)WKind::Count] = {
    {0.9f, 0.4f, 0.8f, 0.75f, 0, 0, 1, 0, 0, WRot::Velocity, true},          // rain
    {1.0f, 0.4f, 0.8f, 0.75f, 0, 0, 1, 0, 0, WRot::Velocity, true},          // heavy rain
    {0.05f, 0.01f, 0.1f, 0.75f, 0.4f, 0, 1, 0, 0, WRot::Camera, true},       // snow
    {0.1f, 0.01f, 0.1f, 0.75f, 0.4f, 0, 1, 0, 0, WRot::Camera, true},        // heavy snow
    {0.1f, 0.7f, 1.0f, 0.25f, 0.1f, 0, 1, 0, 0, WRot::Camera, true},         // dust
    {0.5f, 0.8f, 1.0f, 0.2f, 1.0f, 0.15f, 1, 0, 60, WRot::Camera, false},    // skittering dust
    {0.5f, 0.4f, 0.5f, 1.0f, 1.0f, 0.15f, 1, 0, 200, WRot::Camera, false},   // shrub (10 s: a handful in view)
    {0.1f, 0.5f, 0.8f, 1.2f, 0.1f, 0.15f, 0.18f, 0.15f, 100, WRot::CameraCopy, true},   // dust haze (PR 0.4 / 0.4 / x1: a white wash in our light)
    {0, 0, 0, 2.0f, 0, 0, 1, 0.1f, 250, WRot::Flat, true},                   // fog lying flat
    {0, 0, 0, 2.0f, 0, 0, 1, 0.1f, 250, WRot::Upright, true},                // fog standing up
    {0, 0, 0, 0.25f, 0, 0, 1, 0, 7, WRot::Flat, true},                       // ripple
    {0.3f, 0.1f, 0.2f, 0.12f, 0, 0, 1, 0, 6, WRot::Camera, true},            // splash
    {0.05f, 0, 0, 0.5f, 0, 0, 0.9f, 0, 80, WRot::Wall, true},                // streak (down a wall)
    {-0.02f, 0.1f, 0.2f, 0.4f, 0.05f, 0, 0.5f, 0, 30, WRot::Camera, true},   // steam
  };
  return T[std::min((int)k, (int)WKind::Count - 1)];
}

vec3 skySpawnPoint(WRng& r, float radius, bool fast) {
  float h;
  if (!fast) {   // ParticleSpawner.tickSkyFX: 1 - |r1^2 - r2^2|, kept to the upper half
    float a = r.f(), b = r.f();
    h = 0.5f + 0.5f * (1 - std::abs(a * a - b * b));
  } else h = r.f();
  const float theta = 6.2831853f * r.f(), phi = std::acos(std::clamp(2 * h - 1, -1.f, 1.f));
  return vec3(radius * std::sin(phi) * std::cos(theta), radius * std::cos(phi), radius * std::sin(phi) * std::sin(theta));
}

static bool stops(BlockId id, bool& liquid) {
  const auto& R = reg();
  liquid = R.tLiquid[id] != 0;
  return id && (R.tSolid[id] || R.tLeaves[id] || liquid);
}

bool sweepHit(World& w, vec3 from, vec3 to, WeatherHit& out) {
  // Amanatides & Woo through the blocks the segment passes
  vec3 d = to - from;
  ivec3 c(ifloor(from.x), ifloor(from.y), ifloor(from.z));
  bool liquid = false;
  if (stops(w.getBlock(c.x, c.y, c.z), liquid)) { out.pos = from; out.face = 0; out.liquid = liquid; return true; }
  ivec3 st(d.x > 0 ? 1 : -1, d.y > 0 ? 1 : -1, d.z > 0 ? 1 : -1);
  vec3 tMax, tDelta;
  for (int a = 0; a < 3; a++) {
    if (std::abs(d[a]) < 1e-9f) { tMax[a] = 1e30f; tDelta[a] = 1e30f; continue; }
    float next = st[a] > 0 ? (float)(c[a] + 1) : (float)c[a];
    tMax[a] = (next - from[a]) / d[a]; tDelta[a] = std::abs(1.f / d[a]);
  }
  for (int i = 0; i < 64; i++) {
    int ax = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
    float t = tMax[ax];
    if (t > 1) return false;
    c[ax] += st[ax];
    tMax[ax] += tDelta[ax];
    if (stops(w.getBlock(c.x, c.y, c.z), liquid)) {
      out.pos = from + d * t;
      out.face = ax == 1 ? (st.y < 0 ? 0 : 5) : ax == 0 ? (st.x > 0 ? 1 : 2) : (st.z > 0 ? 3 : 4);
      out.liquid = liquid;
      return true;
    }
  }
  return false;
}

void WeatherSim::update(const WeatherInputs& in, double dt) {
  lastHits.clear();
  if (in.band != band) { parts.clear(); band = in.band; }
  const float ticks = (float)std::min(dt, 0.25) * 20.f;
  if (ticks <= 0) return;
  if (in.world && in.W) { spawnSky(in, ticks); spawnSurface(in, ticks); spawnDust(in, ticks); spawnFog(in, ticks); }
  for (size_t i = 0; i < parts.size();) {
    step(in, parts[i], ticks);
    WParticle& p = parts[i];
    const WKindDef& K = kindDef(p.kind);
    const float d = (float)glm::length(p.pos - glm::dvec3(in.cam));
    // sky things live within the weather distance; things spawned on the ground (shrubs, haze, fog) out to the surface range
    const bool sky = p.kind <= WKind::HeavySnow || p.kind == WKind::Dust || p.kind == WKind::SkitterDust;
    const bool distFade = sky || p.kind == WKind::Shrub || p.kind == WKind::Haze;   // FadeType.DISTANCE
    const float range = sky ? in.dist : p.kind == WKind::Haze ? in.dist * 2 : in.surface;
    const bool dead = p.impact <= 0 || d > range + 0.5f || (K.lifetime > 0 && p.age >= K.lifetime) || (K.lifetime == 0 && p.age > 3000);
    if (dead) { parts[i] = parts.back(); parts.pop_back(); continue; }
    if (!distFade) fade(p);
    else {   // Particle Rain's FadeType.DISTANCE: full over the nearer two thirds, fading out over the last third
      const float fall = range / 3;
      p.alpha = K.opacity * (1 - std::clamp((d + fall) / fall - 3, 0.f, 1.f));
    }
    i++;
  }
}

void WeatherSim::spawnSky(const WeatherInputs& in, float ticks) {
  WeatherSystem& W = *in.W;
  if (in.band != DIM_OVERWORLD || W.rain <= 0.001f) { skyAcc = 0; return; }
  const float speed = glm::length(in.camVel) / 20.f;   // blocks per tick, as Particle Rain measures it
  // getSkyParticleDensity: amount / 25 a tick, up to x1.5 in a thunderstorm, more while moving so it looks as thick
  const float perTick = budget / 25.f * (1 + 0.5f * W.thunderK) * W.rain * std::min(2.f, W.dens) * (1 + 2 * std::floor(speed));
  skyAcc += perTick * ticks;
  int n = (int)skyAcc; skyAcc -= n;
  World& w = *in.world;
  for (int i = 0; i < n && count() < budget; i++) {
    const vec3 o = skySpawnPoint(rng, in.dist, speed >= 0.8f);
    const glm::dvec3 at = glm::dvec3(in.cam) + glm::dvec3(o);
    const int x = ifloor((float)at.x), z = ifloor((float)at.z);
    if (!w.isLoaded(x, ifloor((float)at.y), z)) continue;
    const int top = W.topAt(w, x, z);
    if (top >= at.y) continue;                           // under a roof, a tree, the ground
    const int k = W.precipAt(w, (float)at.x, (float)top, (float)at.z);   // (the weather where it would land)
    const float chance = rng.f();
    if (k == 1) {
      spawnParticle(in, WKind::Rain, at);
      if (W.thunderK > 0.05f) spawnParticle(in, WKind::HeavyRain, at);
    } else if (k == 2) {
      if (0.3f > chance) spawnParticle(in, WKind::Snow, at);
      if (W.thunderK > 0.05f && 0.4f > chance) spawnParticle(in, WKind::HeavySnow, at);
    }
  }
}

void WeatherSim::spawnParticle(const WeatherInputs& in, WKind k, glm::dvec3 at) {
  if (count() >= budget) return;
  const WKindDef& K = kindDef(k);
  WParticle p;
  p.kind = k; p.pos = at; p.size = K.size;
  p.rollSpeed = K.rotSpeed * (rng.f() - 0.5f) * 2;
  const float mult = in.W && in.W->thunderK > 0.5f ? K.stormWind : K.wind;
  const vec3 wind = windAt(in.wind, at.x, at.y, at.z) * mult;
  p.vel = vec3(wind.x * 10, -K.gravity, wind.z * 10);   // (WindManager.applySpawnWind: x10 at the start)
  switch (k) {
    case WKind::Rain: p.sprite = (u8)((int)WSprite::Rain0 + rng.next() % 16); break;
    case WKind::HeavyRain: p.sprite = (u8)((int)WSprite::HeavyRain0 + rng.next() % 4); break;
    case WKind::Snow: p.sprite = (u8)((int)WSprite::Snow0 + rng.next() % 16); break;
    case WKind::HeavySnow: p.sprite = (u8)((int)WSprite::HeavySnow0 + rng.next() % 4); break;
    default: break;
  }
  if (k == WKind::Rain || k == WKind::HeavyRain) p.tint = glm::mix(in.waterCol, in.fogCol, 0.6f);   // TintType.WATER, tint mix 0.6
  if (in.world) p.light = in.world->getLight(ifloor((float)at.x), ifloor((float)at.y), ifloor((float)at.z));
  parts.push_back(p);
}

// ---- where the rain lands (ParticleSpawner.tickBlockFX: splashes, ripples, streaks, steam) ----
static bool streakStone(BlockId id) {   // impermeable / pickaxe-mineable sides: stone, bricks, glass, metal
  const auto& R = reg();
  return id && R.tSolid[id] && !R.tLeaves[id] && (R.tOpaque[id] || R.tTrans[id]);
}
void WeatherSim::onHit(const WeatherInputs& in, const WeatherHit& h) {
  if (!in.surfaceFx || (h.kind != WKind::Rain && h.kind != WKind::HeavyRain) || h.face != 0 || rng.f() > 0.25f) return;
  WParticle q; q.pos = glm::dvec3(h.pos) + glm::dvec3(0, 0.01, 0);
  q.kind = h.liquid ? WKind::Ripple : WKind::Splash;
  if (h.liquid) q.pos.y = std::floor(h.pos.y + 0.5) + 0.0;   // (on the water's surface: the top of its block)
  q.size = kindDef(q.kind).size; q.sprite = (u8)(h.liquid ? (int)WSprite::Ripple0 : (int)WSprite::Dust0);
  q.tint = glm::mix(in.waterCol, in.fogCol, 0.6f); q.roll = 1.5707963f * (rng.next() % 4);
  if (!h.liquid) q.vel = vec3((rng.f() - 0.5f) * 0.06f, 0.08f + rng.f() * 0.06f, (rng.f() - 0.5f) * 0.06f);
  if (in.world) q.light = in.world->getLight(ifloor(h.pos.x), ifloor(h.pos.y + 0.5f), ifloor(h.pos.z));
  if (count() < budget) parts.push_back(q);
}
void WeatherSim::spawnSurface(const WeatherInputs& in, float ticks) {
  WeatherSystem& W = *in.W;
  if (!in.surfaceFx || in.band != DIM_OVERWORLD || W.rain <= 0.05f) { surfAcc = 0; return; }
  surfAcc += budget / 100.f * W.rain * ticks;
  int n = (int)surfAcc; surfAcc -= n;
  World& w = *in.world;
  const auto& R = reg();
  for (int i = 0; i < n && count() < budget; i++) {
    const float a = rng.f() * 6.2831853f, r = std::sqrt(rng.f()) * in.dist;
    const int x = ifloor(in.cam.x + std::cos(a) * r), z = ifloor(in.cam.z + std::sin(a) * r);
    if (!w.isLoaded(x, ifloor(in.cam.y), z)) continue;
    const int top = W.topAt(w, x, z);
    if (W.precipAt(w, (float)x, (float)top, (float)z) != 1) continue;
    const BlockId below = w.getBlock(x, top - 1, z);
    WParticle q; q.roll = 1.5707963f * (rng.next() % 4); q.light = w.getLight(x, top, z);
    q.tint = glm::mix(in.waterCol, in.fogCol, 0.6f);
    if (rng.f() < 0.5f) {   // a streak down the side of a block beside this column, open to the sky
      const int f = 1 + (int)(rng.next() % 4);
      const int dx = f == 1 ? 1 : f == 2 ? -1 : 0, dz = f == 3 ? 1 : f == 4 ? -1 : 0;
      const int y = top + (int)(rng.next() % 6) - 1;
      if (y < top) continue;   // (it has to be out in the rain)
      if (w.getBlock(x, y, z) != 0 || !streakStone(w.getBlock(x + dx, y, z + dz))) continue;
      q.kind = WKind::Streak; q.sprite = (u8)WSprite::Streak; q.face = (u8)f; q.size = kindDef(q.kind).size;
      const double fx = dx > 0 ? x + 1 - 0.01 : dx < 0 ? x + 0.01 : x + 0.2 + rng.f() * 0.6;
      const double fz = dz > 0 ? z + 1 - 0.01 : dz < 0 ? z + 0.01 : z + 0.2 + rng.f() * 0.6;
      q.pos = glm::dvec3(dx ? (dx > 0 ? x + 1.0 : (double)x) : fx, y + 0.3 + rng.f() * 0.6, dz ? (dz > 0 ? z + 1.0 : (double)z) : fz);
      q.vel = vec3(0, -0.1f, 0);
      parts.push_back(q);
      continue;
    }
    q.pos = glm::dvec3(x + 0.1 + rng.f() * 0.8, top + 0.01, z + 0.1 + rng.f() * 0.8);
    if (R.tLiquid[below] == 1) { q.kind = WKind::Ripple; q.sprite = (u8)WSprite::Ripple0; q.pos.y = top; }
    else if (R.tLiquid[below] == 2 || R.blocks[below].has("fire")) { q.kind = WKind::Steam; q.sprite = (u8)((int)WSprite::Dust0 + 2); q.tint = vec3(0.85f); q.vel = vec3(0, 0.03f, 0); }
    else if (below && R.tSolid[below]) { q.kind = WKind::Splash; q.sprite = (u8)WSprite::Dust0; q.vel = vec3((rng.f() - 0.5f) * 0.06f, 0.08f + rng.f() * 0.06f, (rng.f() - 0.5f) * 0.06f); }
    else continue;
    q.size = kindDef(q.kind).size;
    parts.push_back(q);
  }
}
// ---- dust in dry places (particles.json dust, skittering_dust, shrub, dust_haze) ----
static bool dusty(BlockId id) {
  static std::vector<u8> t;
  if (t.empty()) {
    const auto& R = reg(); t.assign(R.blocks.size() + 1, 0);
    for (auto& b : R.blocks) {
      const std::string& k = b.key;
      if (k.find("soul") != std::string::npos || k.find("slab") != std::string::npos || k.find("stairs") != std::string::npos) continue;
      if (k == "sand" || k == "red_sand" || k.find("terracotta") != std::string::npos || k.find("sandstone") != std::string::npos || k == "coarse_dirt") t[b.id] = 1;
    }
  }
  return id < t.size() && t[id];
}
void WeatherSim::spawnDust(const WeatherInputs& in, float ticks) {
  WeatherSystem& W = *in.W;
  const float s = std::max(W.sand, W.windStrength > 0.5f && W.zone == "desert" ? 0.2f : 0.f);
  if (in.band != DIM_OVERWORLD || s <= 0.01f) { dustAcc = 0; return; }
  dustAcc += budget / 25.f * s * ticks;
  int n = (int)dustAcc; dustAcc -= n;
  World& w = *in.world;
  for (int i = 0; i < n && count() < budget; i++) {
    const float pick = rng.f();
    // what: mostly dust in the air; skittering dust on the ground; haze; now and then a tumbling shrub
    WKind k = pick < 0.6f ? WKind::Dust : pick < 0.72f ? WKind::SkitterDust : pick < 0.7205f ? WKind::Shrub : pick < 0.80f ? WKind::Haze : WKind::Count;
    if (k == WKind::Count) continue;
    glm::dvec3 at;
    if (k == WKind::Dust) at = glm::dvec3(in.cam) + glm::dvec3(skySpawnPoint(rng, in.dist, false));
    else {
      const float a = rng.f() * 6.2831853f, rr = k == WKind::Shrub ? 8 + rng.f() * in.dist * 2 : std::sqrt(rng.f()) * in.dist * (k == WKind::Haze ? 2 : 1);
      at = glm::dvec3(in.cam.x + std::cos(a) * rr, 0, in.cam.z + std::sin(a) * rr);
    }
    const int x = ifloor((float)at.x), z = ifloor((float)at.z);
    if (!w.isLoaded(x, ifloor(in.cam.y), z)) continue;
    const int top = W.topAt(w, x, z);
    if (k == WKind::Dust) { if (top >= at.y) continue; }
    else at.y = top + (k == WKind::Haze ? 0.3 + rng.f() * 1.2 : 0.06);
    const BlockId ground = w.getBlock(x, top - 1, z);
    if (!dusty(ground) || W.precipAt(w, (float)x, (float)top, (float)z) != 0) continue;   // dry sand, terracotta, sandstone
    const WKindDef& K = kindDef(k);
    WParticle q; q.kind = k; q.pos = at; q.size = K.size;
    // (haze: big faint soft blobs growing with distance, as Particle Rain's dust_haze)
    q.sprite = (u8)(k == WKind::Shrub ? (int)WSprite::Shrub : k == WKind::Haze ? (int)WSprite::FogFlat : (int)WSprite::Dust0 + (int)(rng.next() % 3));
    float col[3] = {0.76f, 0.62f, 0.42f};
    if (in.tiles && k != WKind::Shrub) { mapRGB(*in.tiles, ground, 0x88, col); for (float& c : col) c /= 255.f; }   // (mapRGB is 0..255: unscaled, every speck came out white)
    const float v = k == WKind::Shrub ? 1.f : 0.8f + rng.f() * 0.2f;   // TintType.MAP: a little lighter or darker each
    q.tint = k == WKind::Shrub ? vec3(1) : vec3(col[0], col[1], col[2]) * v * (k == WKind::Haze ? 0.85f : 1.f);
    q.rollSpeed = K.rotSpeed * (rng.f() - 0.5f) * 2;
    const float mult = W.sand > 0.3f || W.thunderK > 0.5f ? K.stormWind : K.wind;
    const vec3 wd = windAt(in.wind, at.x, at.y, at.z) * mult;
    q.vel = vec3(wd.x * 10, k == WKind::Dust ? -K.gravity : K.bounce, wd.z * 10);
    q.light = w.getLight(x, ifloor((float)at.y), z);
    parts.push_back(q);
  }
}

// ---- ground fog (particles.json fog / vertical_fog / *_after_rain) ----
float fogAlpha(const WParticle& p) {
  const WKindDef& K = kindDef(p.kind);
  const float L = (float)std::max(1, K.lifetime), half = L / 2;
  return std::clamp(p.age < half ? p.age / half : (L - p.age) / half, 0.f, 1.f) * K.opacity;
}
void WeatherSim::spawnFog(const WeatherInputs& in, float ticks) {
  WeatherSystem& W = *in.W;
  if (!in.groundFog || in.band != DIM_OVERWORLD) { fogAcc = 0; return; }
  const bool wetLand = W.zone == "swamp" || W.zone == "jungle";
  const bool after = W.afterRainT > 0 && W.zone != "desert" && W.zone != "savanna";
  const bool dawn = W.mistNow > 0.3f && W.sand < 0.1f && W.bliz < 0.1f;   // (a sandstorm's or blizzard's own haze is not a morning mist)
  if (!wetLand && !after && !dawn) { fogAcc = 0; return; }
  fogAcc += budget / 200.f * 0.5f * ticks;   // the surface particles' rate, fog's density 0.5
  int n = (int)fogAcc; fogAcc -= n;
  World& w = *in.world;
  const auto& R = reg();
  for (int i = 0; i < n && count() < budget; i++) {
    // RandomSource.triangle(cam, range): most of it near you, thinning out to the surface range
    const double x = in.cam.x + (rng.f() - rng.f()) * in.surface, z = in.cam.z + (rng.f() - rng.f()) * in.surface;
    const int ix = ifloor((float)x), iz = ifloor((float)z);
    if (!w.isLoaded(ix, ifloor(in.cam.y), iz)) continue;
    const int top = W.topAt(w, ix, iz);
    const BlockId ground = w.getBlock(ix, top - 1, iz);
    if (!ground || !R.tSolid[ground] || R.tLeaves[ground] || dusty(ground)) continue;   // on the land, not on sand or trees
    WParticle q;
    q.kind = rng.f() < 0.5f ? WKind::FogFlat : WKind::FogUpright;
    q.sprite = (u8)(q.kind == WKind::FogFlat ? WSprite::FogFlat : WSprite::FogUpright);
    q.pos = glm::dvec3(x, top + 0.1 + rng.f(), z);
    q.size = kindDef(q.kind).size;
    q.roll = q.kind == WKind::FogFlat ? 1.5707963f * (rng.next() % 4) : 0.f;
    q.tint = in.fogCol * (0.7f + 0.2f * (1 - W.rain));   // TintType.FOG: darker while it rains
    q.light = w.getLight(ix, top, iz);
    parts.push_back(q);
  }
}

void WeatherSim::fade(WParticle& p) {
  const WKindDef& K = kindDef(p.kind);
  const float L = (float)std::max(1, K.lifetime), half = L / 2;
  switch (p.kind) {
    case WKind::Ripple: case WKind::Splash:   // FadeType.OUT
      p.alpha = std::clamp((L - p.age) / half, 0.f, 1.f) * K.opacity;
      if (p.kind == WKind::Ripple) p.sprite = (u8)((int)WSprite::Ripple0 + std::min(7, (int)(p.age / L * 8)));
      break;
    case WKind::Streak: p.alpha = p.hit ? std::max(0.f, p.alpha - 0.1f) : K.opacity * std::min(1.f, (L - p.age) / 10.f); break;
    case WKind::FogFlat: case WKind::FogUpright: p.alpha = fogAlpha(p); break;
    default: p.alpha = (p.age < half ? p.age / half : (L - p.age) / half) * K.opacity; break;   // FadeType.IN_AND_OUT
  }
}

void WeatherSim::step(const WeatherInputs& in, WParticle& p, float ticks) {
  const WKindDef& K = kindDef(p.kind);
  p.age += ticks;
  if (p.hit) {   // CustomParticle.tickCollisionAnim: rain shortens into the ground, snow shrinks away
    p.impact -= (p.kind == WKind::Rain || p.kind == WKind::HeavyRain) ? 0.5f * ticks : 0.15f * ticks;
    if (p.kind != WKind::Rain && p.kind != WKind::HeavyRain) p.size = K.size * std::max(0.f, p.impact);
    return;
  }
  if (p.kind == WKind::Ripple) return;   // (rings stay where the drop fell)
  if (p.kind == WKind::Streak) {          // StreakParticle: down the wall, in little runs; it ends where the wall does
    if (std::fmod(p.age, 10.f) < ticks) p.vel.y = rng.f() < 0.5f ? -rng.f() * 0.1f : 0.f;
    p.pos.y += p.vel.y * ticks;
    if (in.world) {
      const int f = p.face, dx = f == 1 ? 1 : f == 2 ? -1 : 0, dz = f == 3 ? 1 : f == 4 ? -1 : 0;
      const int bxk = dx > 0 ? ifloor((float)p.pos.x) : dx < 0 ? ifloor((float)p.pos.x) - 1 : ifloor((float)p.pos.x);
      const int bzk = dz > 0 ? ifloor((float)p.pos.z) : dz < 0 ? ifloor((float)p.pos.z) - 1 : ifloor((float)p.pos.z);
      const int ax = dx ? bxk - dx : bxk, az = dz ? bzk - dz : bzk;   // the air cell in front of the wall
      const int y = ifloor((float)p.pos.y);
      bool liquid = false;
      if (!stops(in.world->getBlock(bxk, y, bzk), liquid) || liquid || stops(in.world->getBlock(ax, ifloor((float)p.pos.y - 0.05f), az), liquid)) p.hit = true;
    }
    return;
  }
  // Particle.tick: gravity, then friction 0.98 a tick; the wind adds to the sideways speed each tick (WindManager.applyWind)
  const float fr = std::pow(0.98f, ticks);
  p.vel.y = (p.vel.y - 0.04f * K.gravity * ticks) * fr;
  const float mult = in.W && in.W->thunderK > 0.5f ? K.stormWind : K.wind;
  if (mult != 0) { const vec3 wd = windAt(in.wind, p.pos.x, p.pos.y, p.pos.z) * mult; p.vel.x = p.vel.x * fr + wd.x * ticks; p.vel.z = p.vel.z * fr + wd.z * ticks; }
  else { p.vel.x *= fr; p.vel.z *= fr; }
  const glm::dvec3 next = p.pos + glm::dvec3(p.vel * ticks);
  WeatherHit h;
  if (in.world && K.bounce > 0 && sweepHit(*in.world, vec3(p.pos), vec3(next), h) && !h.liquid) {
    // CustomParticle.onCollision with bounciness: pushed off the face, up and over a side (wind rising over a hill)
    const float speed = glm::length(p.vel), b = K.bounce * speed * 6;
    const vec3 nrm = h.face == 0 ? vec3(0, 1, 0) : h.face == 5 ? vec3(0, -1, 0) : h.face == 1 ? vec3(-1, 0, 0) : h.face == 2 ? vec3(1, 0, 0) : h.face == 3 ? vec3(0, 0, -1) : vec3(0, 0, 1);
    if (nrm.y == 0) { if (p.bounces++ >= 3) p.hit = true; p.vel.y += 0.5f * b; }
    p.vel += nrm * b;
    if (nrm.y > 0 && p.vel.y < 0) p.vel.y = 0;
    p.pos = glm::dvec3(h.pos) + glm::dvec3(nrm * 0.02f);
  } else if (in.world && sweepHit(*in.world, vec3(p.pos), vec3(next), h)) {
    const vec3 back = glm::length(p.vel) > 1e-6f ? glm::normalize(p.vel) * 0.01f : vec3(0);
    p.pos = glm::dvec3(h.pos - back);
    p.hit = true; p.face = (u8)h.face;
    h.kind = p.kind;
    lastHits.push_back(h);
    onHit(in, h);
  } else p.pos = next;
  p.roll += p.rollSpeed * glm::length(p.vel) * ticks;
  if (in.world && std::fmod(p.age, 10.f) < ticks) p.light = in.world->getLight(ifloor((float)p.pos.x), ifloor((float)p.pos.y), ifloor((float)p.pos.z));
}

} // namespace dq
