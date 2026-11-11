// render/sky.cpp — see sky.h. Ports the computeEnv chain of the original (game.js base, then
// weather.js, skies.js, seamless.js, theend.js and riftarc.js) and the sky pass (renderer.js + gl.js skyFS).
#include "common/holiday.h"
#include "sim/endstory.h"
#include "render/sky.h"
#include "render/height_fog.h"
#include "sim/level.h"
#include "sim/weather.h"
#include "core/registry.h"
#include "gen/gendefs.h"
#include "gen/dimgens.h"

namespace dq {
double netherExactTemperature(i32 seed, int x, int z);   // gen/nether_exact.cpp

namespace {
float g_farRange = 0;
SkyEnv g_env;
inline float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float smooth01(float v) { v = sat(v); return v * v * (3 - 2 * v); }
vec3 rgb(u32 hex) { return vec3(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f); }
vec3 mixc(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
vec3 grey(vec3 c, float k) { float l = (c.x + c.y + c.z) / 3; return mixc(c, vec3(l), k); }

glm::dvec3 g_cam(0);   // where the sky is seen from (computeSkyEnv / computeFrame set it)
// BTA 8.0's Nether (gen/nether_bta.h): each land its own haze, and light enough to find your way across it
// BTA 8.0's fog ramp (textures/colormap/fog/nether.png) by temperature, and each land's mean temperature
vec3 btaFogRamp(float t) {
  static const float T[10] = {0.0f, 0.1f, 0.25f, 0.35f, 0.5f, 0.6f, 0.72f, 0.8f, 0.9f, 1.0f};
  static const u32 C[10] = {0x10100F, 0x160E0D, 0x210606, 0x2C0002, 0x450500, 0x5F1A00, 0x863900, 0xA34F00, 0xC16700, 0xD67900};
  t = std::clamp(t, 0.f, 1.f);
  for (int i = 1; i < 10; i++) if (t <= T[i]) return mixc(rgb(C[i - 1]), rgb(C[i]), (t - T[i - 1]) / (T[i] - T[i - 1]));
  return rgb(C[9]);
}
float btaLandTemp(u8 land) {
  switch (land) {
    case NB_VOLCANIC: return 0.99f; case NB_CRAG: case NB_SULFUR: return 0.88f; case NB_SHELF: return 0.73f;
    case NB_CRYSTAL: case NB_CRYSTAL_PLAINS: return 0.59f; default: return 0.26f;
  }
}
void btaNetherEnv(Level& lv, SkyEnv& e, const Settings* st) {
  if (lv.info.genVersion >= 6) {
    // BTA 8.0: no sky. The fog and the background are one flat colour from the fog ramp at the temperature where you
    // stand, darkened for the render distance (1 - rds) and a little for the light (fogBrightness)
    u8 a, b; float m;
    netherLandAt(lv.info.seed, lv.info.genVersion, (int)std::floor(g_cam.x), (int)std::floor(g_cam.y), (int)std::floor(g_cam.z), a, b, m);
    // terrain 7: BTA's own temperature at the camera (BiomeProviderNether, exact and smooth); older: the land's mean
    const float t = lv.info.genVersion >= 7 ? (float)netherExactTemperature(lv.info.seed, (int)std::floor(g_cam.x), (int)std::floor(g_cam.z))
                                            : btaLandTemp(a) * m + btaLandTemp(b) * (1 - m);
    const float rd = st ? (float)std::clamp(st->renderDist, 2, 32) : 8.f;
    const float k = std::pow(1.f / (40.f - rd), 0.25f) * 0.86f;
    static vec3 eased(-1);   // BTA eases its fog; a land's edge isn't a jump
    vec3 want = btaFogRamp(t) * k;
    if (eased.x < 0) eased = want; else eased += (want - eased) * 0.04f;
    e.fog = e.zenith = e.horizon = eased;
    e.stars = 0; e.voidBelow = false; e.showSun = false;
    e.btaFog = a == NB_OLDWORLD || a == NB_OLD_DESERT ? 2 : 1;
    e.minLight = std::max(e.minLight, 0.15f); e.day = std::max(e.day, 0.30f);
    return;
  }
  // Crag, volcanic, sulphur, crystal, old world, shelf, crystal plains, old desert.
  // Each haze is its own colour so a flight from one land into the next is obvious.
  static const u32 FOG[NB_COUNT] = {0x6a1a12, 0x2a0a06, 0x8a8a28, 0x6a2868, 0x5a3a22, 0x3a4a58, 0x7a5888, 0x6a5030};
  static const u32 ZEN[NB_COUNT] = {0x2a0806, 0x140402, 0x3a3a10, 0x2a1028, 0x24160e, 0x141c28, 0x301828, 0x2a1c10};
  u8 a, b; float m;
  netherLandAt(lv.info.seed, lv.info.genVersion, (int)std::floor(g_cam.x), (int)std::floor(g_cam.y), (int)std::floor(g_cam.z), a, b, m);
  if (a >= NB_COUNT) a = NB_CRAG;
  if (b >= NB_COUNT) b = a;
  e.fog = mixc(rgb(FOG[b]), rgb(FOG[a]), m); e.zenith = mixc(rgb(ZEN[b]), rgb(ZEN[a]), m); e.horizon = mixc(e.fog, e.zenith, 0.35f);
  if (lv.info.genVersion >= 6) { e.fogStart = 0.14f; e.minLight = std::max(e.minLight, 0.18f); e.day = std::max(e.day, 0.30f); }
  else { e.fogStart = std::max(e.fogStart, 0.38f); e.minLight = std::max(e.minLight, 0.27f); e.day = std::max(e.day, 0.3f); }
}
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
  if (realm == REALM_DRIFT) {   // BTA's Drift: a warm golden sky, the nights only dim
    float t = (float)(lv.info.time / DAY_LEN), sh = std::sin(t * TAU);
    float dayF = smoothstepf(-0.2f, 0.25f, sh), sunset = clampv(1 - std::abs(sh) / 0.32f, 0.f, 1.f);
    e.day = 0.47f + 0.53f * dayF;
    e.zenith = mixc(rgb(0x3a2a52), rgb(0xffc687), dayF);
    e.horizon = mixc(mixc(rgb(0x5a3a5a), rgb(0xffe6b8), dayF), rgb(0xff9a5a), sunset * 0.5f);
    e.fog = mixc(rgb(0x4a3654), rgb(0xffd8a0), dayF);
    e.stars = (1 - dayF) * 0.7f; e.showSun = true; e.voidBelow = true; e.fogStart = 0.5f;
    e.minLight = std::max(e.minLight, 0.3f);
    return e;
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
      if (realm == REALM_MAIN && lv.info.genVersion >= 5) btaNetherEnv(lv, e, st);
      break;
    case DIM_AETHER:
      // A bright high sky, a warm sun, and a pale cloud haze. The islands stay in clear air.
      e.day = 1; e.zenith = rgb(0x6eb6ff); e.horizon = rgb(0xfff6e4); e.fog = rgb(0xe7f4ff); e.fogStart = 0.55f;
      e.showSun = true; e.voidBelow = st && !st->clouds;
      e.sunDir = glm::normalize(vec3(0.45f, 0.82f, 0.22f));
      e.sunGlow = vec3(0.45f, 0.32f, 0.12f);
      e.minLight = std::max(e.minLight, 0.22f);
      if (lv.info.genVersion >= 7) {   // The Aether: a periwinkle sky (#C0C0FF), lavender fog (#9393BC) from half the view out
        e.zenith = rgb(0xC0C0FF); e.horizon = rgb(0xB0B0E8); e.fog = rgb(0xA4A4D2); e.fogStart = 0.5f;
        e.voidBelow = false;   // the Aether draws no dark disc below: the sky stays sky-coloured all round
        e.sunGlow = vec3(0.30f, 0.18f, 0.22f);   // its sunsets are pink
      }
      break;
    default: {
      // Better End (render/be_sky.cpp): the End's sky is its fog colour all round, under turning nebulae and stars,
      // and each land's fog has its own colour and density. The dragon's island is vanilla End (#A080A0, density 1).
      static const u32 FOG[19] = {
        0x571A57, 0xF192E5, 0x297AAD, 0x63E4F7, 0xA080A0, 0xCFC23E, 0x57DFDD, 0x000000,
        0xE0F5FE, 0xFFB847, 0xB2D1F8, 0xBD5246, 0xE2EFA8, 0xE2EFA8, 0xF42E4F, 0x84230D,
        0xE2EFA8, 0x646464, 0xB2D1F8
      };
      static const float DEN[19] = {1.5f, 1.7f, 3.0f, 1.3f, 1.0f, 1.5f, 2.3f, 2.5f, 2.2f, 2.0f, 1.75f, 1.1f, 2.0f, 2.0f, 1.3f, 1.2f, 2.0f, 1.0f, 2.0f};
      e.day = 0.78f; e.stars = 0; e.voidBelow = false;
      e.minLight = std::max(e.minLight, 0.12f);
      vec3 fogc = rgb(0xA080A0); float dens = 1;
      if (realm == REALM_MAIN) {
        // BCLib's fog density: interpolated between the corners of the 32-block cell round the eye (the colour too)
        const double gx = g_cam.x / 32.0, gz = g_cam.z / 32.0;
        const int x0 = (int)std::floor(gx), z0 = (int)std::floor(gz);
        const float fx = (float)(gx - x0), fz = (float)(gz - z0);
        vec3 col(0); float den = 0, dayK = 0;
        for (int k = 0; k < 4; k++) {
          int cx = (x0 + (k & 1)) * 32, cz = (z0 + (k >> 1)) * 32;
          float w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fz : 1 - fz);
          vec3 c = rgb(0xA080A0); float d = 1, dd = 0.78f;
          if (std::hypot((double)cx, (double)cz) > 300) {
            int land = endLandAt(lv.info.seed, lv.info.genVersion, cx, cz);
            if (land < 0 || land > 18) land = 0;
            c = rgb(FOG[land]); d = DEN[land]; dd = land == 7 ? 0.46f : land == 8 ? 0.78f : 0.66f;
          }
          col += c * w; den += d * w; dayK += dd * w;
        }
        fogc = col; dens = den; e.day = dayK;
        if (dayK < 0.5f) e.minLight = std::max(e.minLight, 0.10f); else e.minLight = std::max(e.minLight, 0.16f);
      }
      // vanilla's End fog (x0.15), BCLib's x4, dimmed toward the far render distances (spec §1.7)
      const float rdc = st ? (float)st->renderDist : 8.f;
      const float hk = 1 - std::pow(0.25f + 0.75f * std::min(rdc, 32.f) / 32.f, 0.25f);
      vec3 fc = fogc * 0.6f * (1 - hk);
      e.fog = fc; e.zenith = fc; e.horizon = fc;
      e.beDensity = std::max(0.5f, dens);
      e.beTicks = ((double)lv.info.day * DAY_LEN + lv.info.time) * 20.0;
      e.fogStart = 0.25f / e.beDensity;
      break;
    }
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
  g_cam = camPos;
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

void SkyRenderer::computeFrame(const Level& clv, FrameCtx& f, int rd, bool primary) {
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
      static float openMain = 1; float want = std::clamp((sky - 3) / 9.f, 0.f, 1.f);
      float viewOpen = want;   // a portal view takes its far side as it is; the eased value is the player's own
      float& open = primary ? openMain : viewOpen;
      if (primary) open += (want - open) * 0.05f;
      if (open < 0.999f) {
        SkyEnv c = baseEnv(lv, d, realm, g_settings);
        auto mixv = [&](vec3 a, vec3 b) { return b + (a - b) * open; };
        e.fog = mixv(e.fog, c.fog); e.horizon = mixv(e.horizon, c.horizon); e.zenith = mixv(e.zenith, c.zenith);
        e.day = c.day + (e.day - c.day) * open; e.fogStart = c.fogStart + (e.fogStart - c.fogStart) * open;
        e.mist *= open; e.raining *= open;
      }
    }
  }
  // Halloween: the Overworld's nights are foggier, with a cold blue-grey mist. Caves and rooms (no sky light at the head) keep
  // the plain sky; the eased openness follows you in and out.
  float hwFog = 0;
  if (halloween() && realm == REALM_MAIN && d == DIM_OVERWORLD) {
    float night = smoothstepf(0.55f, 0.25f, e.day);
    if (night > 0.01f)
      if (World* w = lv.worldIfOpen(realm)) {
        int sky = w->getLight(ifloor(f.cam.pos.x), ifloor(f.cam.pos.y), ifloor(f.cam.pos.z)) >> 4;
        static float openHw = 1; float want = std::clamp((sky - 3) / 9.f, 0.f, 1.f), viewOpen = want;
        float& open = primary ? openHw : viewOpen;
        if (primary) open += (want - open) * 0.05f;
        hwFog = night * open;
        e.fog = mixc(e.fog, rgb(0x3a4a60), 0.5f * hwFog); e.horizon = mixc(e.horizon, e.fog, 0.35f * hwFog);
      }
  }
  // looking out from under water or lava (game.js viewEnv)
  u8 eyeLiq = 0;
  if (World* w = lv.worldIfOpen(realm)) {
    BlockId eye = w->getBlock(ifloor(f.cam.pos.x), ifloor(f.cam.pos.y), ifloor(f.cam.pos.z));
    u8 liq = reg().tLiquid[eye];
    if (liq) { e.fog = liq == 1 ? vec3(0.09f, 0.23f, 0.48f) : vec3(1, 0.35f, 0.04f); e.fogStart = 0; e.underwater = true; eyeLiq = liq; }
    // BTA's Nether (FogManager): its water a murky red, its lava all but opaque
    if (liq && e.btaFog) e.fog = liq == 1 ? vec3(0.5f, 0.098f, 0.063f) * 0.8f : vec3(0.6f, 0.1f, 0.f);
  }
  // the fog: at the edge of the land you can see (renderer.js render)
  float far = std::max(48.f, rd * 16.f);
  float viewFar = g_farRange > 0 ? std::max(far, g_farRange) : far;
  vec2 fr = e.underwater ? (e.btaFog ? (eyeLiq == 1 ? vec2(0, 30) : vec2(0, 2.5f)) : vec2(0, 22)) : vec2(viewFar * e.fogStart, viewFar - 6);
  const float dtSky = primary && skyT > 0 ? (float)std::clamp(f.time - skyT, 0.0, 0.25) : 0.f;
  if (e.btaFog && !e.underwater) {   // BTA: linear from the camera to the render distance (512 at most); the Old World closer
    // BTA eases the Old World's fog in and out (oldWorldFogTransition) rather than snapping at the land's edge
    static float ow = -1; const float tgt = e.btaFog == 2 ? 1.f : 0.f;
    if (ow < 0 || !primary) ow = ow < 0 ? tgt : ow; else ow += (tgt - ow) * std::min(1.f, dtSky * 0.6f);
    fr = glm::mix(vec2(0.f, std::min(far, 512.f)), vec2(far * 0.15f, far * 0.45f), ow);
  }
  if (e.beDensity > 0 && !e.underwater) {   // Better End: fog from a quarter of the view to all of it, over the density
    float vd = std::max(96.f, far);
    fr = vec2(vd * 0.25f / e.beDensity, vd / e.beDensity);
  }
  // inside a bank of cloud the world closes in: white all round well before the edge between the lands,
  // so crossing it is a walk through cloud (only the camera's own land is drawn; see world_renderer.cpp)
  if (e.cloud > 0 && !e.underwater) { float s = smoothstepf(0.f, 0.75f, e.cloud); fr = vec2(fr.x * (1 - s), lerpf(fr.y, 14, s)); }
  if (e.mist > 0.01f && !e.underwater) { float end = 24 + (fr.y - 24) * std::pow(1 - std::min(1.f, e.mist), 2.f); fr = vec2(std::min(fr.x, end * 0.12f), std::min(fr.y, end)); }
  if (hwFog > 0.01f && !e.underwater) fr = vec2(fr.x * (1 - 0.4f * hwFog), std::max(fr.x * (1 - 0.4f * hwFog) + 24, fr.y * (1 - 0.3f * hwFog)));
  // the old fog: in dread, and on nights once the boundary has thinned, the land closes in the way it did in the earliest
  // days, a thick wall a few chunks out, with the far lands only faint beyond it (layered: chunk shaders, uNearFog)
  float oldFog = 0;
  {
    const Entity* me = nullptr; float bd = 8;
    for (auto& u : lv.entities()) if (u && u->isPlayer() && u->realm == realm) { float dd = glm::distance(vec3(f.cam.pos), u->pos); if (dd < bd) { bd = dd; me = u.get(); } }
    auto jf = [](const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<float>() : 0.f; };
    if (me) oldFog = std::min(1.f, std::max(jf(me->data, "dreadT"), jf(me->data, "dreadAll")) / 0.6f);
    if (realm == REALM_MAIN && d == DIM_OVERWORLD) {
      const int st = riftStability(lv);
      if (st < 75) oldFog = std::max(oldFog, smoothstepf(0.55f, 0.25f, e.day) * (75 - st) / 67.f * 0.85f);
    }
    static float eased = 0;
    if (primary) { eased += (oldFog - eased) * 0.02f; oldFog = eased; }
  }
  // seasons (sim/weather.h): spring fresh, summer as it is, autumn gold and rust, winter dull; eased across a day
  if (realm == REALM_MAIN && d == DIM_OVERWORLD) {
    const float ph = seasonPhase(lv);
    if (ph >= 0) {
      static const vec4 S[4] = {vec4(0.62f, 1.0f, 0.42f, 0.18f), vec4(1, 1, 1, 0), vec4(1.0f, 0.56f, 0.18f, 0.6f), vec4(0.72f, 0.68f, 0.56f, 0.5f)};
      const int a = (int)ph & 3, b = (a + 1) & 3;
      const float k = smoothstepf(6.f / 7.f, 1.f, ph - std::floor(ph));   // the last day of a season turns into the next
      vec4 A = S[a], B = S[b];
      if (A.a == 0) A = vec4(vec3(B), 0); if (B.a == 0) B = vec4(vec3(A), 0);
      f.season = glm::mix(A, B, k);
    } else f.season = vec4(0);
  } else f.season = vec4(0);
  f.nearFog = oldFog > 0.01f && !e.underwater ? vec3(lerpf(fr.y, 8, oldFog), lerpf(fr.y, 46, oldFog), 0.92f * oldFog) : vec3(0);
  // a grown Wither Storm darkens the sky round it to a bruised purple (Cracker's Wither Storm Mod: sky 20,0,19, fog
  // 133,69,62; full within 200 blocks, gone by 400; most of the way once it has grown past its first great form)
  {
    float k = 0;
    for (auto& up : lv.entities()) {
      const Entity& q = *up;
      if (q.removed || q.type != "wither_storm" || q.realm != realm) continue;
      const int ph = q.data.value("phase", 0);
      if (ph < 4 || q.data.value("st", 0) == 4) continue;
      const float dd = (float)glm::distance(glm::dvec3(q.pos), f.cam.pos);
      k = std::max(k, (ph >= 5 ? 0.85f : 0.6f) * clampv((400.f - dd) / 200.f, 0.f, 1.f));
    }
    static float sk = 0; sk += (k - sk) * std::min(1.f, dtSky * 0.5f);
    if (sk > 0.005f && !e.underwater) {
      e.zenith = glm::mix(e.zenith, vec3(20, 0, 19) / 255.f, sk); e.horizon = glm::mix(e.horizon, vec3(58, 14, 56) / 255.f, sk);
      e.fog = glm::mix(e.fog, vec3(133, 69, 62) / 255.f * 0.7f, sk * 0.8f);
    }
  }
  // through a portal into another land: its air fades in over 0.7 s from the one you left instead of in one frame
  if (primary && !e.underwater) {
    static int pd = -99, pr = -99; static float fade = 1; static vec3 aFog, aZen, aHor, lFog, lZen, lHor; static vec2 aFr, lFr;
    if (pd != -99 && (pd != d || pr != realm)) { aFog = lFog; aZen = lZen; aHor = lHor; aFr = lFr; fade = 0; }
    pd = d; pr = realm;
    if (fade < 1) {
      fade = std::min(1.f, fade + dtSky / 0.7f);
      const float k = fade * fade * (3 - 2 * fade);
      e.fog = glm::mix(aFog, e.fog, k); e.zenith = glm::mix(aZen, e.zenith, k); e.horizon = glm::mix(aHor, e.horizon, k);
      fr = glm::mix(aFr, fr, k);
    }
    lFog = e.fog; lZen = e.zenith; lHor = e.horizon; lFr = fr;
  }
  e.fogRange = fr;
  if (primary) { env = e; g_env = e; }
  f.sky = std::make_shared<const SkyEnv>(e);
  WeatherSystem* W = weatherOf(lv);
  f.dim = d;
  if (d == DIM_END) f.blockLightColor = vec3(0.90f, 0.84f, 1.0f);
  else if (d == DIM_NETHER) f.blockLightColor = vec3(1.0f, 0.62f, 0.38f);
  else if (d == DIM_AETHER) f.blockLightColor = vec3(1.0f, 0.97f, 0.88f);
  else f.blockLightColor = vec3(1.0f, 0.85f, 0.62f);
  f.skyColor = e.zenith; f.horizonColor = e.horizon; f.fogColor = e.fog;
  f.daylight = e.day; f.minLight = e.minLight;
  f.sunDir = e.showSun || d == DIM_OVERWORLD ? e.sunDir : vec3(0, 1, 0);
  f.fogStart = fr.x; f.fogEnd = fr.y;
  // A shader pack draws its own air: light shafts, bloom fog, the tonemap. The game's fog would
  // flatten the far land to one colour before that, which is not how the pack looks in Minecraft.
  // Water, lava, a cloud bank, mist and the dread wall stay, because the pack does not replace those.
  if (f.settings && !f.settings->shaderPack.empty() && !e.underwater && e.cloud <= 0.f && e.mist <= 0.01f && oldFog <= 0.01f) {
    f.fogStart = 1.0e6f;
    f.fogEnd = 2.0e6f;
    f.nearFog = vec3(0);
  }
  f.wind = e.wind;
  f.windDir = W ? vec2(-std::sin(W->windDir), -std::cos(W->windDir)) : vec2(1, 0);
  f.rain = e.raining; f.thunder = W && d == DIM_OVERWORLD ? W->thunderK : 0.f;
  f.flash = W && W->flashT > 0 ? std::min(1.f, W->flashT / 0.2f) : 0.f;
  f.mist = e.mist; f.sea = e.sea; f.cloudBank = e.cloud; f.underwater = e.underwater;
  {   // fog lying low in the valleys (not under water, not with a shader pack: it draws its own air)
    const bool hf = f.settings && f.settings->heightFog && f.settings->shaderPack.empty() && !e.underwater;
    // only under the open sky: in a cave or a closed room it fades away (eased, like the weather's fog above)
    float hfOpen = 1;
    if (realm == REALM_MAIN) if (World* w = lv.worldIfOpen(realm)) {
      const int sky = w->getLight(ifloor(f.cam.pos.x), ifloor(f.cam.pos.y), ifloor(f.cam.pos.z)) >> 4;
      static float eased = 1; const float want = std::clamp((sky - 3) / 9.f, 0.f, 1.f);
      if (primary) { eased += (want - eased) * 0.05f; hfOpen = eased; } else hfOpen = want;
    }
    f.heightFog = heightFogParams((float)f.cam.pos.y, d, e.mist, f.rain, hf, hfOpen);
  }
  if (primary && W && f.settings) W->farRange = (float)std::max(f.settings->farTerrain, rd * 16);
  if (primary) skyT = f.time;
}

void SkyRenderer::draw(const FrameCtx& f) {
  const SkyEnv& e = f.sky ? *f.sky : env;
  // Full-screen triangle. It has no back face: leave culling off for this draw.
  // Vulkan's framebuffer winding does not match this triangle's OpenGL winding,
  // so leaving GL_CULL_FACE on drops the whole sky and the clear colour shows through.
  GLboolean cull = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  shader.use();
  f.apply(shader);
  shader.set("uInvViewProj", glm::inverse(f.cam.proj * mat4(mat3(f.cam.view))));
  shader.set("uZenith", e.zenith); shader.set("uHorizon", e.horizon);
  shader.set("uSunGlow", e.sunGlow); shader.set("uStars", e.stars); shader.set("uVoid", e.voidBelow ? 1.f : 0.f);
  shader.set("uStorm", e.hasStorm ? e.storm : vec4(0, 1, 1000, 0)); shader.set("uStormCol", e.stormCol);
  shader.set("uAurora", e.aurora); shader.set("uRainbow", e.rainbow); shader.set("uSkyT", (float)std::fmod(skyT, 10000.0));
  shader.set("uShowSun", e.showSun && !e.underwater ? 1.f : 0.f); shader.set("uSunFade", 1.f - e.cloud);
  { const int dl = g_settings ? g_settings->extra.value("dreamLight", 0) : 0; shader.set("uDream", dl <= 0 ? 0.f : dl >= 2 ? 1.6f : 1.f); }   // dreamlight.glsl's sky
  shader.set("uMoonPhase", e.moonPhase);
  shader.set("uHarvest", halloween() && f.dim == DIM_OVERWORLD && e.moonPhase == 0 ? 1.f : 0.f);
  shader.set("uSunDir", e.sunDir);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  if (f.dim == DIM_END && e.beDensity > 0 && !e.underwater && beSky.init()) {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    beSky.draw(f.cam.proj * mat4(mat3(f.cam.view)), e.beTicks, e.fog, e.beDensity, 1.f);
    glDisable(GL_BLEND);
  }
  glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
  if (cull) glEnable(GL_CULL_FACE);
}

} // namespace dq
