// sim/weather.cpp — port of game/weather.js + the weather half of game/skies.js + the Wind
// (game/vehicles.js, render/clouds.js drift). See weather.h.
#include "sim/weather.h"
#include "sim/player.h"
#include "core/init.h"
#include "gen/gendefs.h"
#include "gen/structures.h"
#include "world/shapes.h"
#include "world/carve_faces.h"

namespace dq {

namespace {
// The original world was 128 tall with the sea at 48; the Overworld band stretches the land above the
// sea (gen/gendefs.h vmap). Heights in the original's rules (snow above y 110, cold above 104...) are
// mapped with the generator's own curve.
inline int bandY(double jsY) { return (int)std::floor(vmap::owSurface(jsY)); }
inline double jsYOf(int bandLocalY) { return vmap::owSurfaceInv(bandLocalY); }
inline float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float smooth01(float v) { v = sat(v); return v * v * (3 - 2 * v); }
u32 g_rng = 0x9E3779B9u;
inline float rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (g_rng >> 8) * (1.0f / 16777216.0f); }

struct Ids {
  BlockId water, ice, snow_layer, puddle, fire, glass, sand, red_sand, desert_sand, charred_log, rod, snow_block;
  std::vector<u8> isLog, puddleGround, heat;   // heat: 0 cold, 1 melts by its own light, 2 hot even when dim (magma)
  Ids() {
    auto& r = reg();
    water = r.blockId("water"); ice = r.blockId("ice"); snow_layer = r.blockId("snow_layer"); puddle = r.blockId("puddle");
    fire = r.blockId("fire"); glass = r.blockId("glass"); sand = r.blockId("sand"); red_sand = r.blockId("red_sand");
    desert_sand = r.blockId("desert_sand"); charred_log = r.blockId("charred_log"); rod = r.blockId("lightning_rod"); snow_block = r.blockId("snow_block");
    isLog.assign(r.blocks.size(), 0); puddleGround.assign(r.blocks.size(), 0); heat.assign(r.blocks.size(), 0);
    for (auto& b : r.blocks) if (b.has("isLog")) isLog[b.id] = 1;
    for (const char* k : {"grass", "dirt", "coarse_dirt", "dirt_path", "podzol", "mud", "stone", "cobblestone", "gravel", "mossy_cobblestone", "stone_bricks",
                          "dirt_with_coniferous_litter", "dirt_with_rainforest_litter", "packed_mud", "farmland"}) { BlockId id = r.blockId(k); if (id) puddleGround[id] = 1; }
    // Flames, lava and a running furnace. Glowstone, lamps, end rods and soul fire are left out.
    for (auto& b : r.blocks) {
      if (b.has("hot")) { heat[b.id] = 2; continue; }
      bool by = r.tLiquid[b.id] == 2 || b.has("fire");
      if (!by && b.render == Render::Torch && !b.has("rs") && b.key != "old_torch") by = true;
      if (!by && (b.key == "jack_o_lantern" || b.key == "campfire" || b.key == "brazier")) by = true;
      if (!by && b.has("furnace") && b.key.rfind("lit_", 0) == 0) by = true;
      if (by) heat[b.id] = 1;
    }
  }
  // How many blocks away this cell still melts snow. A torch (14) reaches 2, lava (15) reaches 3, a lit furnace (13) reaches 1.
  int heatReach(BlockId id, u8 meta) const {
    if (id >= heat.size() || !heat[id]) return 0;
    const Registry& r = reg();
    int lit = r.tLMeta[id] ? r.metaLight(id, meta) : (int)r.tLight[id];
    if (heat[id] == 2) return std::max(1, lit > 11 ? lit - 12 : 0);
    if (lit <= 11) return 0;
    return lit - 12;
  }
  bool log(BlockId id) const { return id < isLog.size() && isLog[id]; }
  bool sandy(BlockId id) const { return id && (id == sand || id == red_sand || id == desert_sand); }
};
const Ids& ids() { static Ids i; return i; }

float zoneWet(const std::string& z) {
  if (z == "desert") return 0.15f; if (z == "savanna") return 0.35f; if (z == "jungle") return 1.2f; if (z == "swamp") return 1.3f;
  if (z == "cold") return 0.7f; if (z == "none") return 0; return 1;
}
float envDay(double time) {   // game.js computeEnv: e.day for the Overworld
  float ang = (float)(time / DAY_LEN * TAU), sh = std::sin(ang);
  return 0.2f + 0.8f * smoothstepf(-0.2f, 0.25f, sh);
}
float sunY(double time) {
  float ang = (float)(time / DAY_LEN * TAU), c = std::cos(ang), s = std::sin(ang);
  return s / std::sqrt(c * c + s * s + 0.0625f);
}
const int WDX[4] = {1, -1, 0, 0}, WDZ[4] = {0, 0, 1, -1};
json frontJson(const WeatherFront& f) { if (!f.on) return nullptr; return json{{"kind", f.kind}, {"ux", f.ux}, {"uz", f.uz}, {"edge", f.edge}}; }
WeatherFront frontFrom(const json& j) {
  WeatherFront f; if (!j.is_object()) return f;
  f.on = true; f.kind = j.value("kind", "rain"); f.ux = j.value("ux", 0.f); f.uz = j.value("uz", 1.f); f.edge = j.value("edge", 0.f); return f;
}
} // namespace

WeatherSystem* weatherOf(Level& lv) { return lv.system<WeatherSystem>(); }

void registerWeatherSystem() {
  registerLevelSystem([](Level& lv) { auto w = std::make_shared<WeatherSystem>(); w->load(lv); return w; });
}

// ---- state --------------------------------------------------------------------------------------
json WeatherSystem::toJson() const {
  json j{{"kind", kind}, {"left", left}, {"front", frontJson(front)}, {"trail", frontJson(trail)}, {"mist", mist}, {"rainbowT", rainbowT}, {"seaFog", seaFog},
         {"wind", {windDir, windStrength, windT, cloudX, cloudZ}}};
  json ic = json::array(); for (auto& p : ice) ic.push_back({p.x, p.y, p.z});
  j["ice"] = ic;
  return j;
}
void WeatherSystem::fromJson(const json& j) {
  if (!j.is_object()) return;
  kind = j.value("kind", "clear"); left = j.value("left", 900.0);
  front = frontFrom(j.value("front", json())); trail = frontFrom(j.value("trail", json()));
  mist = j.value("mist", 0.f); rainbowT = j.value("rainbowT", 0.f); seaFog = j.value("seaFog", 0.f);
  if (j.contains("wind") && j["wind"].is_array() && j["wind"].size() >= 5) {
    auto& w = j["wind"]; windDir = w[0]; windStrength = w[1]; windT = w[2]; cloudX = w[3]; cloudZ = w[4];
  }
  ice.clear();
  if (j.contains("ice") && j["ice"].is_array()) for (auto& p : j["ice"]) if (p.is_array() && p.size() == 3) ice.push_back({p[0].get<int>(), p[1].get<int>(), p[2].get<int>()});
}
void WeatherSystem::load(Level& lv) {
  lvl = &lv; loaded = true;
  g_rng ^= (u32)lv.info.seed * 2654435761u + (u32)unixMillis();
  if (!g_rng) g_rng = 1;
  windDir = (float)floormod(lv.info.seed, 628) / 100.f;
  const json& d = lv.info.data;
  if (d.contains("weather")) fromJson(d["weather"]);
  else left = 600 + rnd() * 1200;
  // a different world: start in its weather at once instead of fading over
  rain = kind != "clear" ? 1.f : 0.f; thunderK = kind == "thunder" ? 1.f : 0.f; lastKind = kind;
}
void WeatherSystem::save(Level& lv) { lv.info.data["weather"] = toJson(); }

void WeatherSystem::set(const std::string& k, double secs) {
  front.on = false;                       // /weather (or the front arriving) makes it so here and now
  if (!cycling) trail.on = false;         // (the storm ending by itself keeps its trailing edge going on past you)
  kind = k;
  left = secs > 0 ? secs : (k == "clear" ? 900 + rnd() * 1200 : 300 + rnd() * 360);
}
void WeatherSystem::startFront(const std::string& k) {
  front.on = true; front.kind = k; front.ux = std::sin(windDir); front.uz = std::cos(windDir); front.edge = FRONT_START;
  trail.on = false; left = 1e6;
}
float WeatherSystem::stormAt(float x, float z) const {
  float s;
  if (trail.on) { float d = (x - anchor.x) * trail.ux + (z - anchor.y) * trail.uz; s = sat((trail.edge - d) / BAND + 0.5f); }
  else s = kind != "clear" ? 1.f : 0.f;
  if (front.on) { float d = (x - anchor.x) * front.ux + (z - anchor.y) * front.uz; s = std::max(s, sat((d - front.edge) / BAND + 0.5f)); }
  return smooth01(s);
}

// ---- what the land is like ------------------------------------------------------------------------
WeatherColumn WeatherSystem::column(Level& lv, int x, int z) const {
  WeatherColumn c;
  DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_OVERWORLD) : nullptr;
  BlockId top = AIR; bool water = false; int ground = SEA_LEVEL;
  if (g) {
    // the land as generated: biome, climate, the original's height, rivers
    ColumnSample s; g->sample(x, z, s);
    c.biome = s.biome == 255 ? -1 : s.biome;
    if (s.temp || s.humid) { c.temp = (s.temp - 128) / 127.f; c.humid = (s.humid - 128) / 127.f; }
    c.h = s.height; ground = s.height; top = s.top;
    if (s.water >= 0 && s.water > s.height) { water = true; c.h = s.water; }
    c.jsH = jsYOf(s.height);
    if (const TerrainQuery* q = g->terrain()) { TerrainColumn tc = q->terrainColumn(x, z); c.river = tc.river; if (tc.jsY >= 0) c.jsH = tc.jsY; }
  }
  World* w = lv.worldIfOpen(REALM_MAIN);
  if (Chunk* ch = w ? w->chunk(DIM_OVERWORLD, x >> 4, z >> 4) : nullptr) {   // what is really there now (builds, digging)
    int lx = x & 15, lz = z & 15, ci = (lz * 16 + lx) * 2;
    if (!g && (ch->climate[ci] || ch->climate[ci + 1])) { c.temp = (ch->climate[ci] - 128) / 127.f; c.humid = (ch->climate[ci + 1] - 128) / 127.f; }
    c.h = ch->heightMap[lz * 16 + lx];
  }
  if (c.biome >= 0) {
    c.desert = c.biome == BIOME_DESERT; c.ocean = c.biome == BIOME_OCEAN; c.beach = c.biome == BIOME_BEACH;
  } else {   // no biome known (another world type): guess from the surface
    c.ocean = water && ground < SEA_LEVEL - 3;
    c.beach = ids().sandy(top) && std::abs(ground - SEA_LEVEL) <= 3;
    c.desert = ids().sandy(top) && ground > SEA_LEVEL + 2 && c.temp > 0.2f;
  }
  return c;
}
std::string WeatherSystem::skyZone(Level& lv, float x, float z) const {
  if (leadDim != DIM_OVERWORLD || leadRealm != REALM_MAIN) return "none";
  WeatherColumn c = column(lv, ifloor(x), ifloor(z));
  float t = c.temp, hm = c.humid;
  bool plains = c.biome == BIOME_PLAINS || c.biome < 0, forest = c.biome == BIOME_FOREST || c.biome < 0;
  if (c.desert) return "desert";
  if (c.ocean && c.jsH < vmap::JS_SEA - 3) return "ocean";
  if (plains && t > 0.28f && hm < 0.02f) return "savanna";
  if (t > 0.25f && hm > 0.3f) return "jungle";
  if ((plains || forest || c.beach) && c.jsH <= vmap::JS_SEA + 3 && hm > 0.38f && t > -0.1f) return "swamp";
  if (t < -0.3f || c.jsH > 104) return "cold";
  if (c.beach || c.ocean) return "coast";
  return "temperate";
}

float seasonPhase(const Level& lv) {
  const json& R = lv.info.rules;
  if (!R.is_object() || !R.contains("seasons") || R["seasons"] != true) return -1;   // off unless the world turns them on
  const double dayLen = 1200;   // (the original's day: 20 minutes)
  const double days = (double)lv.info.day + std::fmod(std::max(0.0, lv.info.time), dayLen) / dayLen;
  return (float)std::fmod(days / 7.0, 4.0);
}
int seasonOf(const Level& lv) { const float p = seasonPhase(lv); return p < 0 ? -1 : (int)p & 3; }
const char* seasonName(int s) { static const char* N[4] = {"Spring", "Summer", "Autumn", "Winter"}; return s >= 0 && s < 4 ? N[s] : ""; }

int WeatherSystem::precipAt(World& w, float x, float y, float z) {
  if (w.realm != REALM_MAIN || dimOfY(clampv(ifloor(y), 0, WORLD_H - 1)) != DIM_OVERWORLD || !lvl) return 0;
  bool shower = showerK > 0.01f && kind == "clear";
  std::string stamp = kind + (shower ? ":1" : ":0");
  if (pcache.stamp != stamp || clock - pcache.t > 4 || pcache.map.size() > 4000) { pcache.stamp = stamp; pcache.t = clock; pcache.map.clear(); }
  int ly = ifloor(y) - dimBase(DIM_OVERWORLD);
  bool high = ly > bandY(110);
  i64 key = (((i64)(ifloor(x) >> 3) * 65536 + (ifloor(z) >> 3)) * 2) + (high ? 1 : 0);
  auto it = pcache.map.find(key);
  if (it != pcache.map.end()) return it->second;
  WeatherColumn c = column(*lvl, ifloor(x), ifloor(z));
  int k = 1;
  if (c.desert || (c.temp > 0.28f && c.humid < 0.02f)) k = 0;
  else if (c.temp < -0.3f || high) k = 2;
  else if (seasonOf(*lvl) == 3 && c.temp < 0.25f) k = 2;   // winter: the temperate lands get snow too
  // rain only falls on the jungle or the swamp in one of their own showers
  if (k && shower) { std::string zn = skyZone(*lvl, x, z); if (zn != "jungle" && zn != "swamp") k = 0; }
  pcache.map[key] = k;
  return k;
}

int WeatherSystem::topAt(World& w, int x, int z) {
  i64 k = ((i64)x * 73856093) ^ ((i64)z * 19349663);
  auto it = colTop.find(k);
  if (it != colTop.end() && clock - it->second.second < 3) return it->second.first;
  Chunk* ch = w.chunk(DIM_OVERWORLD, x >> 4, z >> 4);
  int y = ch ? std::min(DIM_H - 1, ch->maxY + 1) : 0;
  if (ch) while (y > 0) { BlockId id = ch->get(x & 15, y, z & 15); if (id && (reg().tSolid[id] || reg().tLiquid[id] || reg().tLeaves[id])) break; y--; }
  int r = dimBase(DIM_OVERWORLD) + y + 1;
  if (colTop.size() > 4000) colTop.clear();
  colTop[k] = {r, clock};
  return r;
}

// ---- the wind --------------------------------------------------------------------------------------
void WeatherSystem::updateWind(double dt) {
  windT += dt;
  double t = windT;
  windDir += (float)((std::sin(t * 0.013) * 0.6 + std::sin(t * 0.0071 + 2) * 0.4) * dt * 0.02);
  gust += (float)(((std::sin(t * 0.37) * std::sin(t * 0.11 + 1) > 0.55 ? 0.35 : 0) - gust) * std::min(1.0, dt * 0.8));
  windStrength = clampv((float)(0.45 + std::sin(t * 0.021) * 0.2 + std::sin(t * 0.0057 + 3) * 0.15 + gust), 0.15f, 1.f);
  if (windUp > 0.01f) windStrength = std::min(1.f, windStrength + (1 - windStrength) * windUp * 0.75f);   // skies.js
  float sp = 1.2f + windStrength * 5.5f;                                                                 // clouds.js
  cloudX -= std::sin(windDir) * sp * dt; cloudZ -= std::cos(windDir) * sp * dt;
}

void WeatherSystem::soundLater(double delay, const std::string& name, vec3 pos, float range) { later.push_back({clock + delay, name, pos, range}); }

// ---- the tick ---------------------------------------------------------------------------------------
void WeatherSystem::tick(Level& lv, double dt) {
  if (!loaded) load(lv);
  lvl = &lv;
  clock += dt;
  {   // a new season is announced
    const int se = seasonOf(lv);
    if (se != lastSeason) { if (lastSeason >= -1 && se >= 0 && !lv.isRemote()) lv.fx->message(0, std::string(seasonName(se)) + " has come", "good"); lastSeason = se; }
  }
  // the lead player: fronts are measured from them, and the local weather is theirs
  Player* lead = nullptr;
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer()) { lead = static_cast<Player*>(e.get()); break; }
  if (lead) { anchor = vec2(lead->pos.x, lead->pos.z); leadPos = lead->pos; leadRealm = lead->realm; leadDim = dimOfY(clampv(ifloor(lead->pos.y), 0, WORLD_H - 1)); }
  World& w = lv.world(REALM_MAIN);
  bool host = hostRules;
  bool cycle = host && lv.info.rules.value("weatherCycle", true);
  updateWind(dt);
  // the cycle: a clear spell ending sends a front in, instead of the rain simply starting
  if (cycle && kind == "clear" && !front.on && left - dt <= 0) startFront(rnd() < 0.3f ? "thunder" : "rain");
  if (front.on) {
    left = std::max(left, 1e5);
    front.edge -= FRONT_SPEED * (float)dt;
    if (front.edge <= 0 && host) set(front.kind);
  }
  // the end of a storm: the clear sky comes in from upwind over its last minute or so
  if (cycle && kind != "clear" && !trail.on && left < TRAIL) { trail.on = true; trail.ux = std::sin(windDir); trail.uz = std::cos(windDir); trail.edge = (float)left * FRONT_SPEED; }
  if (trail.on) { trail.edge -= FRONT_SPEED * (float)dt; if (trail.edge < -2400) trail.on = false; }
  std::string was = lastKind;
  // ---- weather.js update ----
  cycling = true;
  if (cycle) { left -= dt; if (left <= 0) { if (kind == "clear") set(rnd() < 0.3f ? "thunder" : "rain"); else set("clear"); } }
  cycling = false;
  bool overworld = leadDim == DIM_OVERWORLD && leadRealm == REALM_MAIN;
  float wantRain = overworld && kind != "clear" ? 1.f : 0.f, wantThunder = kind == "thunder" && wantRain > 0 ? 1.f : 0.f;
  rain += (wantRain - rain) * std::min(1.f, (float)dt * 0.08f);
  thunderK += (wantThunder - thunderK) * std::min(1.f, (float)dt * 0.08f);
  if (rain < 0.002f) rain = 0;
  if (flashT > 0) flashT -= (float)dt;
  if ((groundT -= (float)dt) <= 0) { groundT = 0.5f; groundTick(lv, w); }
  // the rain's hiss (the audio engine reads rainLevel / rainOpen) and the rumble of far thunder
  if (lead && overworld) {
    float sky = (w.getLight(ifloor(leadPos.x), ifloor(leadPos.y + 1.5f), ifloor(leadPos.z)) >> 4) / 15.f;
    if ((nearT -= (float)dt) <= 0) {
      nearT = 1; float wetN = 0;
      for (int k = 0; k < 9; k++) { float a = k * TAU / 8, r = k == 8 ? 0.f : 16.f; if (precipAt(w, leadPos.x + std::cos(a) * r, leadPos.y, leadPos.z + std::sin(a) * r) == 1) wetN += k == 8 ? 3 : 1; }
      wet = std::min(1.f, wetN / 6);
    }
    rainOpen = sky * (0.12f + 0.88f * wet);
  } else rainOpen = 0;
  rainLevel = rain * (0.08f + 0.22f * rainOpen);
  if (lead && thunderK > 0.5f && rnd() < dt / 25) lv.fx->soundUi(lead->id, "thunder_far");
  if (thunderK > 0.6f && host && lead) {
    boltT -= (float)dt;
    if (boltT <= 0) { boltT = 6 + rnd() * 22; strikeNear(lv, w); }
  }
  // ---- skies.js ----
  if (!was.empty() && was != "clear" && kind == "clear") {
    mist = std::max(mist, was == "thunder" ? 0.85f : 0.62f);
    float sy = sunY(lv.info.time);
    if (sy > 0.05f && sy < 0.72f) rainbowT = 110;
  }
  lastKind = kind; if (kind != "clear") lastStorm = kind;
  if (bankFlash > 0) bankFlash -= (float)dt;
  if (front.on && front.kind == "thunder" && rnd() < dt / 5) bankFlash = 0.25f;
  float day = envDay(lv.info.time);
  if (mist > 0) { float sunUp = sat((day - 0.3f) / 0.6f); mist = std::max(0.f, mist - (float)dt * (0.5f + sunUp * 1.6f + windStrength) / 420); }
  if (rainbowT > 0) rainbowT -= (float)dt;
  if (host && kind == "clear" && !(seaFog > 0) && rnd() < dt / 2400) seaFog = 180 + rnd() * 240;
  if (seaFog > 0) seaFog -= (float)dt;
  localWeather(lv, w, dt);
  thaw(lv, w, dt);
  farBolts(lv, dt);
  // bolts fade, delayed thunder arrives
  for (auto& b : bolts) b.t -= (float)dt;
  bolts.erase(std::remove_if(bolts.begin(), bolts.end(), [](const Bolt& b) { return b.t <= 0; }), bolts.end());
  for (size_t i = 0; i < later.size();) {
    if (later[i].t <= clock) {
      if (lead) lv.fx->soundUi(lead->id, later[i].name);
      later[i] = later.back(); later.pop_back();
    } else i++;
  }
}

void WeatherSystem::localWeather(Level& lv, World& w, double dt) {
  float fdt = (float)dt;
  bool ow = leadDim == DIM_OVERWORLD && leadRealm == REALM_MAIN;
  if ((zoneT -= fdt) <= 0) { zoneT = 0.5f; zone = skyZone(lv, leadPos.x, leadPos.z); }
  std::string zn = ow ? zone : "none";
  float here = ow ? stormAt(leadPos.x, leadPos.z) : 0.f;
  std::string sk = stormKind();
  float storm = kind != "clear" ? here : 0.f;
  // showers in the jungle and drizzle in the swamp, between the storms
  if (kind == "clear" && !front.on && (zn == "jungle" || zn == "swamp") && !(showerT > 0) && rnd() < dt / (zn == "jungle" ? 260 : 340)) {
    showerT = zn == "jungle" ? 40 + rnd() * 60 : 60 + rnd() * 100; showerZone = zn;
  }
  if (showerT > 0) { showerT -= fdt; if (kind != "clear") showerT = 0; }
  float shower = showerT > 0 ? (showerZone == "jungle" ? 0.9f : 0.42f) : 0.f;
  showerK += (shower - showerK) * std::min(1.f, fdt * 0.25f);
  if (showerK > 0.01f && kind == "clear" && ow) rain = std::max(rain, showerK);
  bool jungle = zn == "jungle" || (showerZone == "jungle" && showerK > 0.05f);
  bliz += ((zn == "cold" && sk == "thunder" ? storm : 0.f) - bliz) * std::min(1.f, fdt * 0.15f);
  sand += ((zn == "desert" && kind != "clear" ? storm : 0.f) - sand) * std::min(1.f, fdt * 0.12f);
  dens = kind == "clear" && showerK > 0.01f ? (showerZone == "jungle" ? 1.3f : 0.5f) : jungle ? 1.5f : zn == "ocean" ? 1.3f : 1 + bliz * 0.7f;
  windDX = -std::sin(windDir); windDZ = -std::cos(windDir);
  windTilt = windStrength * 1.2f + (zn == "ocean" ? 1.5f * storm : 0.f);
  snowDrift = bliz * 9 + windStrength * 0.6f;
  // the wind gets up as a front comes in, and in a storm
  approach = front.on ? sat(1 - front.edge / FRONT_START) : 0.f;
  windUp = std::max({approach * approach, storm * (sk == "thunder" ? 1.f : 0.6f), sand, bliz});
  if (front.on && front.kind == "thunder" && approach > 0.35f && rnd() < dt / 18) soundLater(0, "thunder_far", leadPos, 1e4f);
  // the storm's clouds reach over you before its rain does
  over = ow ? std::max(here, front.on ? stormAt(leadPos.x + front.ux * 260, leadPos.z + front.uz * 260) * 0.7f : 0.f) : 0.f;
  WeatherColumn* col = nullptr;
  if (ow) {
    if (!haveCol || (colT -= fdt) <= 0) { colT = 0.25f; colAt = column(lv, ifloor(leadPos.x), ifloor(leadPos.z)); haveCol = true; }
    col = &colAt;
  } else if (leadDim == DIM_NETHER) {
    // ash: always a little, now and then a storm of it
    if (!(ashT > 0) && rnd() < dt / 1500) ashT = 90 + rnd() * 120;
    if (ashT > 0) ashT -= fdt;
    ash += ((ashT > 0 ? 1.f : 0.2f) - ash) * std::min(1.f, fdt * 0.08f);
  }
  float m = mistTarget(lv, w, zn, col);
  mistNow += (m - mistNow) * std::min(1.f, fdt * 0.35f);
  // the Skylands: the storms are below you, and flash
  if (belowFlash > 0) belowFlash -= fdt;
  if (leadDim == DIM_AETHER && kind == "thunder" && rnd() < dt / 7) { belowFlash = 0.3f; if (rnd() < 0.5f) soundLater(0.6 + rnd() * 1.8, "thunder_far", leadPos, 1e4f); }
}

float WeatherSystem::mistTarget(Level& lv, World& w, const std::string& zn, const WeatherColumn* c) {
  if (leadRealm != REALM_MAIN) return 0;
  if (leadDim == DIM_NETHER) return 0.12f + std::max(0.f, ash - 0.2f) * 0.55f;
  if (leadDim != DIM_OVERWORLD) return 0;
  float m = mist * zoneWet(zn);
  float ang = (float)(lv.info.time / DAY_LEN * TAU), sh = std::sin(ang);
  float morning = std::cos(ang) > -0.2f ? smooth01((sh + 0.15f) / 0.15f) * (1 - smooth01((sh - 0.05f) / 0.3f)) : 0.f;
  float low = 0;
  if (c) low = (c->river ? 0.8f : 0.f) + (c->jsH < vmap::JS_SEA + 10 && c->humid > 0.05f ? 0.5f : 0.f) + (zn == "swamp" ? 0.8f : 0.f);
  if (leadPos.y - dimBase(DIM_OVERWORLD) > bandY(vmap::JS_SEA + 26)) low *= 0.3f;
  m = std::max(m, morning * std::min(1.f, low) * 0.62f);
  if (zn == "swamp") m = std::max(m, 0.28f + 0.2f * (1 - envDay(lv.info.time)));
  if (seaFog > 0 && (zn == "ocean" || zn == "coast" || zn == "swamp")) m = std::max(m, 0.62f * sat(seaFog / 30) * sat((420 - seaFog) / 30 + 1));
  m = std::max({m, rain * 0.12f, sand * 0.82f, bliz * 0.72f});
  if ((w.getLight(ifloor(leadPos.x), ifloor(leadPos.y + 1.5f), ifloor(leadPos.z)) >> 4) < 6) m *= 0.25f;   // indoors and down a mine
  return m;
}

// ---- the ground in the weather ------------------------------------------------------------------------
struct Warmth {
  int sky = 0, blk = 0;
  bool snowing = false, sun = false, rain = false;
};
static Warmth warmthAt(WeatherSystem& ws, Level& lv, World& w, int x, int y, int z) {
  Warmth e;
  u8 l = w.getLight(x, y, z);
  u8 a = inWorldY(y + 1) ? w.getLight(x, y + 1, z) : 0;
  e.sky = std::max(l >> 4, a >> 4);
  e.blk = std::max(l & 15, a & 15);
  int pk = ws.precipAt(w, (float)x, (float)y, (float)z);
  e.snowing = ws.rain > 0.3f && pk == 2;                 // falling snow, not merely a cold biome
  float day = e.sky * envDay(lv.info.time);
  e.sun = !e.snowing && day > 11.f;                      // open sun on a clear day, cold lands included
  e.rain = !e.snowing && pk == 1 && ws.rain > 0.5f && e.sky >= 15;
  return e;
}
static bool heatNear(World& w, int x, int y, int z, int blk) {
  const Ids& I = ids();
  const Registry& r = reg();
  int R = blk > 11 ? 3 : 1;   // a dim cell can only feel magma and the other low flames next door
  for (int dy = -R; dy <= R; dy++) {
    int yy = y + dy;
    if (!inWorldY(yy)) continue;
    int spanY = R - std::abs(dy);
    for (int dz = -spanY; dz <= spanY; dz++) {
      int spanZ = spanY - std::abs(dz);
      for (int dx = -spanZ; dx <= spanZ; dx++) {
        if (!dx && !dy && !dz) continue;
        BlockId id = w.getBlock(x + dx, yy, z + dz);
        if (!id || id >= I.heat.size() || !I.heat[id]) continue;
        u8 meta = r.tLMeta[id] ? w.getMeta(x + dx, yy, z + dz) : 0;
        if (I.heatReach(id, meta) >= std::abs(dx) + std::abs(dy) + std::abs(dz)) return true;
      }
    }
  }
  return false;
}
// A ground probe is rare, so sun, rain or a flame is enough. A random tick visits every snow layer
// once a second, so each cause waits: a clear day about a minute, rain a few seconds, a torch sooner than that.
static bool meltsNow(WeatherSystem& ws, Level& lv, World& w, int x, int y, int z, bool sample, bool stormSnow) {
  Warmth e = warmthAt(ws, lv, w, x, y, z);
  if (sample) {
    if (e.sun || e.rain) return true;
    return heatNear(w, x, y, z, e.blk);
  }
  if (e.snowing) {
    if (rnd() > (e.blk > 11 ? 1.f / 8.f : 1.f / 20.f)) return false;
    return heatNear(w, x, y, z, e.blk);
  }
  if ((e.sun && rnd() < 1.f / 60.f) || (e.rain && rnd() < 1.f / 6.f)) return true;
  if (stormSnow && !e.sun && rnd() < 1.f / 80.f) return true;   // snowfall left in the shade thins out after the storm
  if (e.sun || e.rain) return false;
  if (rnd() > (e.blk > 11 ? 1.f / 8.f : 1.f / 20.f)) return false;
  return heatNear(w, x, y, z, e.blk);
}
static void meltCoatAt(WeatherSystem& ws, Level& lv, World& w, int x, int y, int z) {
  if (!inWorldY(y) || !w.chunk(dimOfY(y), x >> 4, z >> 4)) return;
  u8 coat = w.getCoat(x, y, z);
  if (!(coat & 0x0F) || (coat & 0x80)) return;   // (bit 7: a rug, world/rug.h)
  if (!meltsNow(ws, lv, w, x, y, z, true, false) || rnd() >= 0.4f) return;
  w.setCoat(x, y, z, (coat & 0x10) ? (u8)(coat & ~0x10) : 0);
}
static bool partialCarvedTop(const World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (!fullSolid(id)) return false;
  u8 cv = w.getCarve(x, y, z);
  return cv && !carveFaceFull(cv, 0, 1, 0);
}

void WeatherSystem::settleCarvedCover(World& w, int x, int y, int z) {
  const Ids& I = ids();
  BlockId id = w.getBlock(x, y, z);
  u8 coat = w.getCoat(x, y, z);
  if (coat & 0x80) return;   // a rug on the floor (world/rug.h), not snow
  if (!fullSolid(id)) {
    if (coat) w.setCoat(x, y, z, 0);
    return;
  }
  u8 cv = w.getCarve(x, y, z);
  bool fullTop = carveFaceFull(cv, 0, 1, 0);
  u8 cols = exposedCols(cv);
  if (inWorldY(y + 1)) {
    BlockId up = w.getBlock(x, y + 1, z);
    if (!fullTop && cv && (up == I.snow_layer || (I.puddle && up == I.puddle))) {
      u8 grown = (up == I.snow_layer && (w.getMeta(x, y + 1, z) & 7) >= 1) ? 0x10 : 0;
      w.setCoat(x, y, z, (u8)(cols | ((coat | grown) & 0x10)));
      w.setBlock(x, y + 1, z, AIR);
      return;
    }
  }
  if (fullTop || !cv) {
    if (coat & 0x0F) {
      w.setCoat(x, y, z, 0);
      if (I.snow_layer && inWorldY(y + 1) && w.getBlock(x, y + 1, z) == AIR)
        w.setBlock(x, y + 1, z, I.snow_layer, (u8)((coat & 0x10) ? 9 : 8));
    } else if (coat) w.setCoat(x, y, z, 0);
    return;
  }
  u8 keep = (u8)((coat & cols) | (coat & 0x10));
  if (!(keep & 0x0F)) keep = 0;
  if (keep != coat) w.setCoat(x, y, z, keep);
}

void WeatherSystem::coverMelt(Level& lv, World& w) {
  if (leadDim != DIM_OVERWORLD || leadRealm != REALM_MAIN || !hostRules) return;
  for (int k = 0; k < 6; k++) {
    int x = ifloor(leadPos.x + (rnd() - 0.5f) * 56), z = ifloor(leadPos.z + (rnd() - 0.5f) * 56);
    if (!w.chunk(DIM_OVERWORLD, x >> 4, z >> 4)) continue;
    int y = topAt(w, x, z) - 1;
    if (ids().snow_layer && w.getBlock(x, y, z) == ids().snow_layer) y--;
    meltCoatAt(*this, lv, w, x, y, z);
  }
  // a torch in a dug-out room is not on the sky column
  for (int k = 0; k < 4; k++) {
    int x = ifloor(leadPos.x + (rnd() - 0.5f) * 24);
    int y = ifloor(leadPos.y + (rnd() - 0.5f) * 12);
    int z = ifloor(leadPos.z + (rnd() - 0.5f) * 24);
    meltCoatAt(*this, lv, w, x, y, z);
  }
}

void WeatherSystem::groundTick(Level& lv, World& w) {
  if (leadDim != DIM_OVERWORLD || leadRealm != REALM_MAIN || !hostRules) return;
  coverMelt(lv, w);
  if (rain < 0.5f) return;
  const Ids& I = ids(); auto& r = reg();
  for (int k = 0; k < 8; k++) {
    int x = ifloor(leadPos.x + (rnd() - 0.5f) * 56), z = ifloor(leadPos.z + (rnd() - 0.5f) * 56);
    if (!w.chunk(DIM_OVERWORLD, x >> 4, z >> 4)) continue;
    int top = topAt(w, x, z), y = top - 1;
    BlockId id = w.getBlock(x, y, z), above = w.getBlock(x, top, z);
    int pk = precipAt(w, (float)x, (float)y, (float)z);
    if (!pk || (w.getLight(x, top, z) >> 4) < 15) continue;
    if (pk == 2) {
      if (id == I.water && I.water && w.getMeta(x, y, z) == 0 && I.ice && rnd() < 0.25f) { w.setBlock(x, y, z, I.ice, 0); noteIce(x, y, z); continue; }
      if (!I.snow_layer) continue;
      if (id == I.snow_layer) {
        if (inWorldY(y - 1) && partialCarvedTop(w, x, y - 1, z)) { settleCarvedCover(w, x, y - 1, z); continue; }
        u8 mm = w.getMeta(x, y, z), m = mm & 7; if (m < 1 && (mm & 8) && rnd() < 0.15f) w.setBlock(x, y, z, I.snow_layer, (u8)((m + 1) | 8));
        continue;
      }
      if (partialCarvedTop(w, x, y, z)) {
        if (above == I.snow_layer || (I.puddle && above == I.puddle)) settleCarvedCover(w, x, y, z);
        else if (above == AIR && rnd() < 0.35f) {
          u8 cv = w.getCarve(x, y, z), coat = w.getCoat(x, y, z), cols = exposedCols(cv);
          if (!(coat & 0x0F)) w.setCoat(x, y, z, cols);
          else if (!(coat & 0x10) && rnd() < 0.15f) w.setCoat(x, y, z, (u8)((coat & 0x0F) | 0x10));
        }
        continue;
      }
      if (above == AIR && r.tSolid[id] && r.tOpaque[id] && !r.tShape[id] && !r.tLeaves[id] && rnd() < 0.35f) w.setBlock(x, top, z, I.snow_layer, 8);
      if (above && above == I.puddle) w.setBlock(x, top, z, I.snow_layer, 8);
      continue;
    }
    // rain: snow and plain ice melt, a puddle forms in a dip. Packed ice stays.
    if (I.ice && id == I.ice) {
      if (rnd() < 0.5f) w.setBlock(x, y, z, I.water, 0);
      continue;
    }
    if (I.snow_layer && (id == I.snow_layer || above == I.snow_layer)) {
      int sy = id == I.snow_layer ? y : top; u8 m = w.getMeta(x, sy, z) & 7;
      if (rnd() < 0.5f) { if (m > 0) w.setBlock(x, sy, z, I.snow_layer, (u8)(m - 1)); else w.setBlock(x, sy, z, AIR, 0); }
      continue;
    }
    if ((w.getCoat(x, y, z) & 0x0F)) w.setCoat(x, y, z, 0);
    if (id == I.snow_layer && inWorldY(y - 1) && (w.getCoat(x, y - 1, z) & 0x0F)) w.setCoat(x, y - 1, z, 0);
    if (partialCarvedTop(w, x, y, z)) continue;   // rain does not leave a puddle floating over a low step
    if (!I.puddle || above != AIR || id >= I.puddleGround.size() || !I.puddleGround[id] || rnd() > 0.3f) continue;
    if ((hash32(x, 7, z, lv.info.seed) & 7) > 2) continue;   // puddles gather in patches, not everywhere
    int low = 0;
    for (int d = 0; d < 4; d++) {
      int nx = x + WDX[d], nz = z + WDZ[d];
      if (r.tSolid[w.getBlock(nx, top, nz)] || topAt(w, nx, nz) > top) low++; else if (topAt(w, nx, nz) < top) low -= 2;
    }
    if (low >= 0) w.setBlock(x, top, z, I.puddle, 0);
  }
}

void WeatherSystem::blockTick(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta) {
  const Ids& I = ids();
  if (id == I.puddle) {
    int pk = precipAt(w, (float)x, (float)y, (float)z);
    u8 l = w.getLight(x, y, z);
    float sun = (l >> 4) * envDay(lv.info.time);
    if (rain > 0.3f && pk == 1) return;
    if (rnd() < (sun > 12 ? 1.f / 25 : 1.f / 90)) w.setBlock(x, y, z, AIR, 0);
    return;
  }
  if (id != I.snow_layer && id != I.ice) return;
  if (!meltsNow(*this, lv, w, x, y, z, false, id == I.snow_layer && (meta & 8))) return;
  if (id == I.ice) { w.setBlock(x, y, z, I.water, 0); return; }
  u8 m = meta & 7;
  if (m > 0) w.setBlock(x, y, z, I.snow_layer, (u8)((m - 1) | (meta & 8)));
  else w.setBlock(x, y, z, AIR, 0);
}

void WeatherSystem::thaw(Level& lv, World& w, double dt) {
  if (ice.empty() || !hostRules || leadDim != DIM_OVERWORLD || (thawT -= (float)dt) > 0) return;
  thawT = 1;
  const Ids& I = ids();
  for (int i = (int)ice.size() - 1; i >= 0; i--) {
    ivec3 p = ice[i];
    if (!w.isLoaded(p.x, p.y, p.z)) continue;
    if (w.getBlock(p.x, p.y, p.z) != I.ice) { ice.erase(ice.begin() + i); continue; }
    if (!meltsNow(*this, lv, w, p.x, p.y, p.z, false, false)) continue;
    w.setBlock(p.x, p.y, p.z, I.water, 0);
    ice.erase(ice.begin() + i);
  }
}

// ---- lightning ----------------------------------------------------------------------------------------
bool WeatherSystem::rodNear(World& w, int x, int y, int z, int R, ivec3& out) {
  BlockId rodId = ids().rod; if (!rodId) return false;
  bool found = false;
  // a rod has to stand in the open to be reached, so only the tops of the columns are looked at
  for (int cz = (z - R) >> 4; cz <= (z + R) >> 4; cz++) for (int cx = (x - R) >> 4; cx <= (x + R) >> 4; cx++) {
    Chunk* ch = w.chunk(DIM_OVERWORLD, cx, cz); if (!ch) continue;
    for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
      int ly = ch->heightMap[lz * 16 + lx];
      if (ch->get(lx, ly, lz) != rodId) continue;
      int rx = cx * 16 + lx, rz = cz * 16 + lz, ry = dimBase(DIM_OVERWORLD) + ly;
      float d = std::hypot((float)(rx - x), (float)(rz - z));
      if (d > R) continue;
      if (!found || ry > out.y || (ry == out.y && d < std::hypot((float)(out.x - x), (float)(out.z - z)))) { out = {rx, ry, rz}; found = true; }
    }
  }
  return found;
}

void WeatherSystem::strikeNear(Level& lv, World& w) {
  // it goes for the high places: of a few spots in the storm near someone, the tallest
  std::vector<Player*> ps;
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && e->realm == REALM_MAIN && dimOfY(clampv(ifloor(e->pos.y), 0, WORLD_H - 1)) == DIM_OVERWORLD) ps.push_back(static_cast<Player*>(e.get()));
  if (ps.empty()) return;
  Player* p = ps[std::min((size_t)(rnd() * ps.size()), ps.size() - 1)];
  bool have = false; ivec3 best{0}; float bestScore = 0;
  for (int i = 0; i < 7; i++) {
    int x, z;
    if (i == 0 && rnd() < 0.2f && p->pos.y - dimBase(DIM_OVERWORLD) > bandY(vmap::JS_SEA + 30) && (w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.6f), ifloor(p->pos.z)) >> 4) == 15) {
      x = ifloor(p->pos.x + (rnd() - 0.5f) * 6); z = ifloor(p->pos.z + (rnd() - 0.5f) * 6);   // out on a high top
    } else { float a = rnd() * TAU, d = 12 + rnd() * 60; x = ifloor(p->pos.x + std::cos(a) * d); z = ifloor(p->pos.z + std::sin(a) * d); }
    if (!w.chunk(DIM_OVERWORLD, x >> 4, z >> 4) || stormAt((float)x, (float)z) < 0.6f) continue;
    if (!precipAt(w, (float)x, (float)dimBase(DIM_OVERWORLD) + SEA_LEVEL + 16, (float)z) && skyZone(lv, (float)x, (float)z) != "savanna") continue;   // the savanna's storms are dry, and strike all the same
    int y = topAt(w, x, z); float score = y + rnd() * 6;
    if (!have || score > bestScore) { best = {x, y, z}; bestScore = score; have = true; }
  }
  if (!have) return;
  ivec3 rod;
  if (rodNear(w, best.x, best.y, best.z, 40, rod)) { ivec3 at = rod + ivec3(0, 1, 0); strike(lv, at.x, at.y, at.z, &rod); }
  else strike(lv, best.x, best.y, best.z);
}

void WeatherSystem::strike(Level& lv, int x, int y, int z, const ivec3* rod) {
  World& w = lv.world(REALM_MAIN);
  flashT = 0.35f;
  bolts.push_back({vec3(x + 0.5f, (float)y, z + 0.5f), (u32)(rnd() * 1e6f), 0.45f});
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != REALM_MAIN) continue;
    float d = std::hypot(e->pos.x - x - 0.5f, e->pos.z - z - 0.5f);
    if (e->isPlayer()) {
      soundLater(std::min(3.0, d * 0.012), d < 40 ? "thunder" : "thunder_far", e->pos, 1e4f);
      if (d < 28) lv.fx->shake(e->id, 0.9f * (1 - d / 28));   // close by: you feel it
      Player* p = static_cast<Player*>(e.get());
      bool safe = p->mode == "creative" || p->mode == "spectator" || p->dead;
      if (!safe && d < 2.5f * (rod ? 0.5f : 1.f) && std::abs(e->pos.y - y) < 4) { p->hp -= 5; p->hurtTime = 0.5f; p->data["burn"] = 4; }
    } else if (e->type != "item" && d < 3 * (rod ? 1.2f : 1.f) && std::abs(e->pos.y - y) < 4) {
      e->hp -= 5; e->hurtTime = 0.5f; e->data["burn"] = std::max(e->data.value("burn", 0.f), 4.f);
    }
  }
  if (lvl && lvl->events) lvl->events->emit("lightning", json::array({x, y, z, rod != nullptr}));
  if (rod) {   // the rod takes it: nothing burns, and it gives a pulse of redstone (the redstone system listens for "lightningRod")
    if (lvl && lvl->events) lvl->events->emit("lightningRod", json::array({rod->x, rod->y, rod->z}));
    lv.fx->particles("white", vec3(rod->x + 0.5f, rod->y + 1, rod->z + 0.5f), 10, json{{"speed", 1.4}});
    return;
  }
  if (!hostRules) return;
  const Ids& I = ids(); auto& r = reg();
  bool fire = lv.info.rules.value("fireSpread", true) && I.fire;
  std::string zn = skyZone(lv, (float)x, (float)z);
  BlockId below = w.getBlock(x, y - 1, z);
  // sand fuses into glass where it struck
  if (I.glass && (below == I.sand || below == I.red_sand) && below) {
    for (int k = 1; k <= 3; k++) { BlockId id = w.getBlock(x, y - k, z); if (id != I.sand && id != I.red_sand) break; if (k == 1 || rnd() < 0.6f) w.setBlock(x, y - k, z, I.glass, 0); }
    return;
  }
  // a tree: down through its leaves to the trunk, which chars; the crown catches
  int ty = y - 1; while (ty > y - 7 && r.tLeaves[w.getBlock(x, ty, z)]) ty--;
  if (I.log(w.getBlock(x, ty, z)) && I.charred_log) {
    for (int k = 0, yy = ty; k < 9 && I.log(w.getBlock(x, yy, z)); k++, yy--) w.setBlock(x, yy, z, I.charred_log, w.getMeta(x, yy, z));
    if (fire) for (int k = 0; k < 5; k++) {
      int fx = x + (int)(rnd() * 5) - 2, fz = z + (int)(rnd() * 5) - 2, fy = ty + (int)(rnd() * 4);
      if (w.getBlock(fx, fy, fz) == AIR && r.tLeaves[w.getBlock(fx, fy - 1, fz)]) w.setBlock(fx, fy, fz, I.fire, 0);
    }
    return;
  }
  // the ground: a fire, likelier on the dry grass of the savanna, where it spreads about
  if (fire && w.getBlock(x, y, z) == AIR && r.tSolid[below] && rnd() < (zn == "savanna" ? 0.95f : 0.6f)) {
    w.setBlock(x, y, z, I.fire, 0);
    if (zn == "savanna") for (int k = 0; k < 4; k++) {
      int fx = x + (int)(rnd() * 7) - 3, fz = z + (int)(rnd() * 7) - 3, fy = topAt(w, fx, fz);
      if (std::abs(fy - y) < 3 && w.getBlock(fx, fy, fz) == AIR && r.tSolid[w.getBlock(fx, fy - 1, fz)]) w.setBlock(fx, fy, fz, I.fire, 0);
    }
  }
}

// lightning out over the distant land in a thunderstorm (game/distant.js farBolts)
void WeatherSystem::farBolts(Level& lv, double dt) {
  bool th = kind == "thunder" || (front.on && front.kind == "thunder" && approach > 0.3f);
  if (!th || leadDim != DIM_OVERWORLD || leadRealm != REALM_MAIN || (farBoltT -= (float)dt) > 0) return;
  farBoltT = 3 + rnd() * 7;
  DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_OVERWORLD) : nullptr;
  if (!g) return;
  float R = 128, range = std::max(farRange, R + 256) * 0.9f;
  for (int i = 0; i < 8; i++) {
    float a = rnd() * TAU, d = R + 40 + rnd() * (range - R - 40), x = leadPos.x + std::cos(a) * d, z = leadPos.z + std::sin(a) * d;
    if (stormAt(x, z) < 0.65f) continue;
    ColumnSample s; g->sample(ifloor(x), ifloor(z), s);
    float y = dimBase(DIM_OVERWORLD) + std::max(s.height, s.water) + 1 - 0.6f;
    bolts.push_back({vec3(x, y, z), (u32)(rnd() * 1e6f), 0.45f});
    flashT = std::max(flashT, d < 400 ? 0.2f : 0.1f);
    soundLater(std::min(4.0, d * 0.005), "thunder_far", leadPos, 1e4f);
    return;
  }
}

} // namespace dq
