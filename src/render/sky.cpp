// render/sky.cpp — see sky.h. Ports the computeEnv chain of the original (game.js base, then
// weather.js, skies.js, seamless.js, theend.js and riftarc.js) and the sky pass (renderer.js + gl.js skyFS).
#include "sim/endstory.h"
#include "render/sky.h"
#include "sim/level.h"
#include "sim/weather.h"
#include "core/registry.h"
#include "gen/gendefs.h"

namespace dq {

namespace {
float g_farRange = 0;
SkyEnv g_env;
inline float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float smooth01(float v) { v = sat(v); return v * v * (3 - 2 * v); }
vec3 rgb(u32 hex) { return vec3(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f); }
vec3 mixc(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
vec3 grey(vec3 c, float k) { float l = (c.x + c.y + c.z) / 3; return mixc(c, vec3(l), k); }

// game.js computeEnv (+ theend.js, riftarc.js): the land's own sky
SkyEnv baseEnv(Level& lv, int dim, int realm, const Settings* st) {
  SkyEnv e;
  float bright = st ? st->brightness : 0.35f;
  e.minLight = 0.025f + bright * 0.07f;
  WeatherSystem* W = weatherOf(lv);
  float ws = W ? W->windStrength : 0.5f;
  e.wind = !st || st->wavingPlants ? 0.45f + ws * 0.9f : 0.f;
  e.moonPhase = (int)floormod((int)(lv.info.day % 8), 8);
  if (realm == REALM_HIS) {   // riftarc.js: a bruised purple dark
    e.day = 0.3f; e.zenith = rgb(0x14041e); e.horizon = rgb(0x4a0e5a); e.fog = rgb(0x2c0838); e.fogStart = 0.35f; e.voidBelow = true;
    e.minLight = std::max(e.minLight, 0.06f); return e;
  }
  if (realm == REALM_POCKET) {
    e.day = 0.7f; e.zenith = rgb(0x0e0820); e.horizon = rgb(0x2a1a4a); e.fog = rgb(0x1b1132); e.stars = 1; e.voidBelow = true; return e;
  }
  switch (dim) {
    case DIM_OVERWORLD: {
      float t = (float)(lv.info.time / DAY_LEN), ang = t * TAU, sh = std::sin(ang);
      float dayF = smoothstepf(-0.2f, 0.25f, sh), sunset = clampv(1 - std::abs(sh) / 0.32f, 0.f, 1.f);
      e.day = 0.2f + 0.8f * dayF;
      e.zenith = mixc(rgb(0x03060f), rgb(0x4f8ee8), dayF);
      e.horizon = mixc(mixc(rgb(0x0b1222), rgb(0xb9d6f5), dayF), rgb(0xff8c4a), sunset * 0.65f);
      e.fog = mixc(mixc(rgb(0x0a101c), rgb(0xb3d0ee), dayF), rgb(0xe88a5a), sunset * 0.4f);
      e.stars = 1 - dayF; e.showSun = true;
      e.sunDir = glm::normalize(vec3(std::cos(ang), sh, 0.25f));
      e.sunGlow = vec3(sunset * 0.9f, sunset * 0.45f, sunset * 0.15f);
      if (realm == REALM_MAIN && riftStability(lv) < 15) {   // riftarc.js: the Overworld's sky going wrong near the end
        e.zenith = mixc(e.zenith, rgb(0x3a0a5a), 0.35f); e.horizon = mixc(e.horizon, rgb(0x8a2aa0), 0.35f); e.fog = mixc(e.fog, rgb(0x4a1a5a), 0.35f);
      }
      break;
    }
    case DIM_NETHER:
      e.day = 0.22f; e.zenith = rgb(0x1e0604); e.horizon = rgb(0x4a1408); e.fog = rgb(0x3a1006); e.fogStart = 0.25f; e.minLight = std::max(e.minLight, 0.07f);
      break;
    case DIM_AETHER:
      e.day = 1; e.zenith = rgb(0x7ab4f8); e.horizon = rgb(0xf2f8ff); e.fog = rgb(0xe2f0fc); e.showSun = true; e.voidBelow = st && !st->clouds;
      e.sunDir = glm::normalize(vec3(0.5f, 0.8f, 0.25f));
      break;
    default:   // theend.js
      e.day = 0.78f; e.zenith = rgb(0x07040e); e.horizon = rgb(0x241a34); e.fog = rgb(0x191224); e.fogStart = 0.45f; e.stars = 0.35f; e.voidBelow = true;
      e.minLight = std::max(e.minLight, 0.12f);
      break;
  }
  return e;
}

// weather.js + skies.js: the weather on the sky
void weatherEnv(Level& lv, int dim, int realm, SkyEnv& e) {
  WeatherSystem* W = weatherOf(lv);
  if (!W || realm != REALM_MAIN) return;
  if (dim == DIM_OVERWORLD && (W->rain > 0 || W->flashT > 0)) {   // weather.js: the sky greys and dims; lightning lights it up
    float r = W->rain, th = W->thunderK;
    auto gr = [&](vec3 c, float k) { float l = (c.x + c.y + c.z) / 3 * (1 - 0.35f * th); return vec3(lerpf(c.x, l, k), lerpf(c.y, l, k), lerpf(c.z, l * 1.05f, k)); };
    e.zenith = gr(e.zenith, 0.75f * r); e.horizon = gr(e.horizon, 0.75f * r); e.fog = gr(e.fog, 0.7f * r);
    e.day *= 1 - 0.28f * r - 0.25f * th;
    e.stars *= 1 - r; e.sunGlow *= 1 - r;
    e.fogStart = std::max(0.25f, e.fogStart - 0.2f * r);
    if (W->flashT > 0) {
      float k = std::min(1.f, W->flashT / 0.2f);
      e.day = std::max(e.day, 0.95f * k + e.day * (1 - k));
      e.zenith = mixc(e.zenith, vec3(0.9f), k * 0.6f); e.horizon = mixc(e.horizon, vec3(0.95f), k * 0.6f);
    }
    e.raining = r;
  }
  float mist = std::min(1.f, W->mistNow);
  if (W->leadDim != dim) mist = dim == DIM_NETHER ? 0.12f + std::max(0.f, W->ash - 0.2f) * 0.55f : 0.f;   // a sky for another land than the weather's
  if (dim == DIM_OVERWORLD) {
    float pre = std::max(0.f, W->over * 0.6f - W->rain);   // the storm's clouds overhead before its rain: the light goes grey
    if (pre > 0.01f) { e.zenith = grey(e.zenith, pre * 0.6f); e.horizon = grey(e.horizon, pre * 0.5f); e.fog = grey(e.fog, pre * 0.4f); e.day *= 1 - 0.18f * pre; e.sunGlow *= 1 - pre * 0.6f; }
    // a storm far off, or one moving away: its bank of cloud on the horizon
    vec4 bank(0); std::string bk; bool has = false;
    if (W->front.on && W->front.edge > 0) { bank = vec4(W->front.ux, W->front.uz, W->front.edge, (0.55f + 0.45f * sat(1 - W->front.edge / WeatherSystem::FRONT_START)) * smooth01((W->front.edge - 40) / 260)); bk = W->front.kind; has = true; }
    else if (W->trail.on && W->trail.edge < 0 && W->kind == "clear") { bank = vec4(-W->trail.ux, -W->trail.uz, -W->trail.edge, smooth01((-W->trail.edge - 40) / 260) * sat(1 - (-W->trail.edge - 1200) / 1000)); bk = W->lastStorm; has = true; }
    if (has && bank.w > 0.001f) {
      bool th = bk == "thunder"; float lum = 0.22f + 0.78f * sat((e.day - 0.2f) / 0.8f);
      float fl = W->bankFlash > 0 ? W->bankFlash / 0.25f : 0.f;
      e.storm = bank; e.stormCol = (th ? vec3(0.25f, 0.26f, 0.31f) : vec3(0.48f, 0.5f, 0.55f)) * lum + vec3(fl * 0.35f); e.hasStorm = true;
    }
    e.sea = sat(0.1f + W->windStrength * 0.3f + W->thunderK * 0.55f + W->rain * 0.2f + W->approach * 0.2f + (W->zone == "ocean" ? 0.15f : 0.f));
    // the northern lights: cold lands, clear nights (some nights more than others)
    WeatherColumn c = W->column(lv, ifloor(W->leadPos.x), ifloor(W->leadPos.z));
    float cold = sat((-c.temp - 0.12f) / 0.3f) + (W->leadPos.y - dimBase(DIM_OVERWORLD) > vmap::owSurface(100) ? 0.3f : 0.f);
    float night = sat((0.55f - e.day) / 0.3f), clearSky = 1 - std::min(1.f, W->rain * 2 + W->over);
    i32 nightNo = (i32)std::floor(lv.info.time / DAY_LEN + (double)lv.info.day);
    float strong = 0.45f + 0.55f * ((hash32(nightNo, 7, 3, lv.info.seed) & 255) / 255.f);
    e.aurora = sat(cold) * night * night * clearSky * (1 - mist) * strong;
    if (W->rainbowT > 0) e.rainbow = sat(W->rainbowT / 25) * sat((110 - W->rainbowT) / 10) * (1 - mist) * (1 - W->rain) * sat((e.day - 0.5f) / 0.3f);
  }
  if (mist > 0.01f && (dim == DIM_OVERWORLD || dim == DIM_NETHER)) {
    // mist (white by day, dark by night), a sandstorm's dust, the Depths' ash
    float dayK = sat((e.day - 0.2f) / 0.8f);
    vec3 col = dim == DIM_NETHER ? vec3(0.24f, 0.12f, 0.08f) : vec3(lerpf(0.08f, 0.76f, dayK), lerpf(0.09f, 0.79f, dayK), lerpf(0.11f, 0.82f, dayK));
    float sand = std::min(1.f, W->sand * 1.2f);
    if (dim == DIM_OVERWORLD && sand > 0.01f) col = mixc(col, vec3(0.74f, 0.58f, 0.38f) * (0.35f + 0.65f * dayK), sand);
    e.fog = mixc(e.fog, col, std::min(1.f, mist * 1.1f));
    e.horizon = mixc(e.horizon, e.fog, mist);
    e.zenith = mixc(e.zenith, e.fog, mist * 0.85f);
    e.sunGlow *= 1 - mist; e.stars *= 1 - mist;
    if (mist > 0.6f) e.showSun = false;
    e.mist = mist;
  }
}

SkyEnv landEnv(Level& lv, int dim, int realm, const Settings* st) {
  SkyEnv e = baseEnv(lv, dim, realm, st);
  weatherEnv(lv, dim, realm, e);
  return e;
}
const Settings* g_settings = nullptr;
} // namespace

void setSkyFarRange(float b) { g_farRange = b; }
float skyFarRange() { return g_farRange; }
const SkyEnv& currentSkyEnv() { return g_env; }

SkyEnv computeSkyEnv(Level& lv, int dim, int realm, glm::dvec3 camPos, bool blend) {
  SkyEnv e = landEnv(lv, dim, realm, g_settings);
  if (!blend || realm != REALM_MAIN) return e;
  // seamless.js: near the edge between two stacked lands their skies meet halfway, and between the
  // Overworld and the Skylands lies a bank of cloud
  float ly = (float)(camPos.y - dimBase(dim));
  float k = 0; int other = -1; bool cloudy = false;
  if (dim == DIM_AETHER) { k = smoothstepf(30, 0, ly); other = DIM_OVERWORLD; cloudy = true; }
  else if (dim == DIM_OVERWORLD && ly > DIM_H - 22) { k = smoothstepf(DIM_H - 22, DIM_H, ly); other = DIM_AETHER; cloudy = true; }
  else if (dim == DIM_OVERWORLD && ly < 16) { k = smoothstepf(16, 0, ly); other = DIM_NETHER; }
  else if (dim == DIM_NETHER && ly > DIM_H - 16) { k = smoothstepf(DIM_H - 16, DIM_H, ly); other = DIM_OVERWORLD; }
  else if (dim == DIM_NETHER && ly < 16) { k = smoothstepf(16, 0, ly); other = DIM_END; }
  else if (dim == DIM_END && ly > DIM_H - 16) { k = smoothstepf(DIM_H - 16, DIM_H, ly); other = DIM_NETHER; }
  if (!(k > 0) || other < 0) return e;
  SkyEnv o = landEnv(lv, other, realm, g_settings);
  float h = k * 0.5f;   // halfway through, the two worlds' skies and light are the same on either side
  e.zenith = mixc(e.zenith, o.zenith, h); e.horizon = mixc(e.horizon, o.horizon, h); e.fog = mixc(e.fog, o.fog, h); e.sunGlow = mixc(e.sunGlow, o.sunGlow, h);
  e.day = lerpf(e.day, o.day, h); e.stars = lerpf(e.stars, o.stars, h); e.minLight = lerpf(e.minLight, o.minLight, h); e.fogStart = lerpf(e.fogStart, o.fogStart, h);
  if (!cloudy) return e;
  // and in the middle of it all, cloud
  float c = smoothstepf(0.15f, 0.85f, k), lum = 0.3f + 0.7f * clampv((e.day - 0.2f) / 0.8f, 0.f, 1.f);
  vec3 cloud = mixc(e.fog, vec3(1), 0.6f) * lum;
  e.fog = mixc(e.fog, cloud, c); e.horizon = mixc(e.horizon, cloud, c); e.zenith = mixc(e.zenith, cloud, c * 0.9f);
  e.sunGlow *= 1 - c;
  if (c > 0.55f) e.showSun = false;
  if (c > 0.3f) e.voidBelow = false;
  e.cloud = c * 0.93f;
  return e;
}

bool SkyRenderer::init() {
  glGenVertexArrays(1, &vao);
  return shader.load("sky.glsl", "sky.glsl");
}

void SkyRenderer::computeFrame(const Level& clv, FrameCtx& f, int rd) {
  Level& lv = const_cast<Level&>(clv);
  g_settings = f.settings;
  int realm = f.cam.realm;
  int d = realm == REALM_MAIN ? dimOfY(clampv((int)std::floor(f.cam.pos.y), 0, WORLD_H - 1)) : DIM_OVERWORLD;
  SkyEnv e = computeSkyEnv(lv, d, realm, f.cam.pos, true);
  // weather.js: rain, mist and sandstorms are out under the sky. Below ground or indoors (no sky light at the
  // head) the weather's fog and grey give way to the clear land's sky, smoothly, so a cave isn't filled with a storm.
  if (realm == REALM_MAIN && (e.mist > 0.01f || e.raining > 0.01f)) {
    if (World* w = lv.worldIfOpen(realm)) {
      int sky = w->getLight(ifloor(f.cam.pos.x), ifloor(f.cam.pos.y), ifloor(f.cam.pos.z)) >> 4;
      static float open = 1; float want = std::clamp((sky - 3) / 9.f, 0.f, 1.f);
      open += (want - open) * 0.05f;
      if (open < 0.999f) {
        SkyEnv c = baseEnv(lv, d, realm, g_settings);
        auto mixv = [&](vec3 a, vec3 b) { return b + (a - b) * open; };
        e.fog = mixv(e.fog, c.fog); e.horizon = mixv(e.horizon, c.horizon); e.zenith = mixv(e.zenith, c.zenith);
        e.day = c.day + (e.day - c.day) * open; e.fogStart = c.fogStart + (e.fogStart - c.fogStart) * open;
        e.mist *= open; e.raining *= open;
      }
    }
  }
  // looking out from under water or lava (game.js viewEnv)
  if (World* w = lv.worldIfOpen(realm)) {
    BlockId eye = w->getBlock(ifloor(f.cam.pos.x), ifloor(f.cam.pos.y), ifloor(f.cam.pos.z));
    u8 liq = reg().tLiquid[eye];
    if (liq) { e.fog = liq == 1 ? vec3(0.09f, 0.23f, 0.48f) : vec3(1, 0.35f, 0.04f); e.fogStart = 0; e.underwater = true; }
  }
  // the fog: at the edge of the land you can see (renderer.js render)
  float far = std::max(48.f, rd * 16.f);
  float viewFar = g_farRange > 0 ? std::max(far, g_farRange) : far;
  vec2 fr = e.underwater ? vec2(0, 22) : vec2(viewFar * e.fogStart, viewFar - 6);
  // inside a bank of cloud the world closes in: white all round well before the edge between the lands,
  // so crossing it is a walk through cloud (only the camera's own land is drawn; see world_renderer.cpp)
  if (e.cloud > 0 && !e.underwater) { float s = smoothstepf(0.f, 0.75f, e.cloud); fr = vec2(fr.x * (1 - s), lerpf(fr.y, 14, s)); }
  if (e.mist > 0.01f && !e.underwater) { float end = 24 + (fr.y - 24) * std::pow(1 - std::min(1.f, e.mist), 2.f); fr = vec2(std::min(fr.x, end * 0.12f), std::min(fr.y, end)); }
  e.fogRange = fr;
  env = e; g_env = e;
  f.sky = std::make_shared<const SkyEnv>(e);
  WeatherSystem* W = weatherOf(lv);
  f.dim = d;
  f.skyColor = e.zenith; f.horizonColor = e.horizon; f.fogColor = e.fog;
  f.daylight = e.day; f.minLight = e.minLight;
  f.sunDir = e.showSun || d == DIM_OVERWORLD ? e.sunDir : vec3(0, 1, 0);
  f.fogStart = fr.x; f.fogEnd = fr.y;
  f.wind = e.wind;
  f.windDir = W ? vec2(-std::sin(W->windDir), -std::cos(W->windDir)) : vec2(1, 0);
  f.rain = e.raining; f.thunder = W && d == DIM_OVERWORLD ? W->thunderK : 0.f;
  f.flash = W && W->flashT > 0 ? std::min(1.f, W->flashT / 0.2f) : 0.f;
  f.mist = e.mist; f.sea = e.sea; f.cloudBank = e.cloud; f.underwater = e.underwater;
  if (W && f.settings) W->farRange = (float)std::max(f.settings->farTerrain, rd * 16);
  skyT = f.time;
}

void SkyRenderer::draw(const FrameCtx& f) {
  const SkyEnv& e = f.sky ? *f.sky : env;
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
  shader.use();
  f.apply(shader);
  shader.set("uInvViewProj", glm::inverse(f.cam.proj * mat4(mat3(f.cam.view))));
  shader.set("uZenith", e.zenith); shader.set("uHorizon", e.horizon);
  shader.set("uSunGlow", e.sunGlow); shader.set("uStars", e.stars); shader.set("uVoid", e.voidBelow ? 1.f : 0.f);
  shader.set("uStorm", e.hasStorm ? e.storm : vec4(0, 1, 1000, 0)); shader.set("uStormCol", e.stormCol);
  shader.set("uAurora", e.aurora); shader.set("uRainbow", e.rainbow); shader.set("uSkyT", (float)std::fmod(skyT, 10000.0));
  shader.set("uShowSun", e.showSun && !e.underwater ? 1.f : 0.f); shader.set("uSunFade", 1.f - e.cloud);
  shader.set("uMoonPhase", e.moonPhase);
  shader.set("uSunDir", e.sunDir);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
}

} // namespace dq
