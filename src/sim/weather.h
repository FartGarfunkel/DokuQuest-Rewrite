// sim/weather.h — rain, snow and thunderstorms (game/weather.js) with the sky update's weather
// (game/skies.js): storms arrive as fronts on the wind, local weather per land (showers, drizzle,
// sandstorms, blizzards, sea fog, ash in the Ember Depths), mist after rain, lightning that goes for
// high points (rods take it), puddles / snow layers / ice that come and go, and the wind
// (game/vehicles.js Wind + clouds.js drift). Runs as a LevelSystem, so the server runs it too;
// the state is saved in LevelInfo.data["weather"]. Visuals live in render/ (sky, clouds, weather_fx).
#pragma once
#include "sim/level.h"

namespace dq {

struct WeatherFront { bool on = false; std::string kind = "rain"; float ux = 0, uz = 1, edge = 0; };

// what the weather needs to know about a column (from the loaded chunk, else the generator)
struct WeatherColumn {
  float temp = 0, humid = 0;   // -1..1 like the original's climate
  int h = SEA_LEVEL;           // band-local ground height
  double jsH = 48;             // the original game's height of the column (its rules are written in it)
  int biome = -1;              // Biome (gen/gendefs.h), -1 unknown
  bool desert = false, ocean = false, beach = false, river = false;
};

class WeatherSystem : public LevelSystem {
public:
  static constexpr float FRONT_SPEED = 7, FRONT_START = 1650, BAND = 260, TRAIL = 80;
  const char* name() const override { return "weather"; }
  void tick(Level& lv, double dt) override;
  void save(Level& lv) override;
  void load(Level& lv);

  // ---- saved state (meta.weather) ----
  std::string kind = "clear";          // clear | rain | thunder
  double left = 900;                   // seconds until it changes
  WeatherFront front, trail;
  float mist = 0, rainbowT = 0, seaFog = 0;
  std::vector<ivec3> ice;              // ice a storm froze (thaws again)

  // ---- wind ----
  float windDir = 0, windStrength = 0.6f, gust = 0; double windT = 0;
  double cloudX = 0, cloudZ = 0;       // how far the wind has carried the clouds

  // ---- the weather where the lead player is (the client draws these) ----
  float rain = 0, thunderK = 0;        // smoothed 0..1
  float flashT = 0, bankFlash = 0, belowFlash = 0;
  float showerT = 0, showerK = 0, sand = 0, bliz = 0, ash = 0.2f, ashT = 0, mistNow = 0;
  float dens = 1, windDX = 1, windDZ = 0, windTilt = 0, snowDrift = 0, approach = 0, windUp = 0, over = 0;
  std::string zone = "temperate", showerZone, lastStorm = "rain";
  vec2 anchor{0};                      // the lead player's x,z (fronts are measured from here)
  vec3 leadPos{0};
  int leadDim = DIM_OVERWORLD, leadRealm = 0;
  float rainLevel = 0, rainOpen = 1;   // rain hiss loudness / how open the sky is (for the audio engine)
  float farRange = 512;                // distant terrain reach (set by the client: far lightning)
  bool hostRules = true;               // false on a multiplayer guest (state comes from the host)

  // lightning bolts in the sky (drawn by render/weather_fx)
  struct Bolt { vec3 pos; u32 seed; float t; };
  std::vector<Bolt> bolts;

  // ---- API (commands, other systems) ----
  void set(const std::string& kind, double secs = 0);
  void startFront(const std::string& kind);         // /weather front <rain|thunder>
  float stormAt(float x, float z) const;            // 0..1 how far into the storm a spot is
  std::string stormKind() const { return front.on ? front.kind : kind; }
  int precipAt(World& w, float x, float y, float z);   // 0 none, 1 rain, 2 snow (overworld only)
  int topAt(World& w, int x, int z);                   // global y above the highest rain-stopping block
  WeatherColumn column(Level& lv, int x, int z) const;
  std::string skyZone(Level& lv, float x, float z) const;
  void strike(Level& lv, int x, int y, int z, const ivec3* rod = nullptr);
  void noteIce(int x, int y, int z) { ice.push_back({x, y, z}); if (ice.size() > 1500) ice.erase(ice.begin(), ice.begin() + (ice.size() - 1500)); }
  // random block ticks on puddles and snow layers (call from the random-tick system)
  void blockTick(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta);
  json toJson() const;
  void fromJson(const json& j);

private:
  void updateWind(double dt);
  void localWeather(Level& lv, World& w, double dt);
  float mistTarget(Level& lv, World& w, const std::string& zone, const WeatherColumn* c);
  void groundTick(Level& lv, World& w);
  void thaw(Level& lv, World& w, double dt);
  void strikeNear(Level& lv, World& w);
  void farBolts(Level& lv, double dt);
  bool rodNear(World& w, int x, int y, int z, int R, ivec3& out);
  void soundLater(double delay, const std::string& name, vec3 pos, float range);
  Level* lvl = nullptr;
  bool loaded = false, cycling = false;
  std::string lastKind;
  float zoneT = 0, colT = 0, boltT = 10, groundT = 0, thawT = 0, farBoltT = 0, nearT = 0, wet = 1;
  bool haveCol = false; WeatherColumn colAt;
  struct Later { double t; std::string name; vec3 pos; float range; };
  std::vector<Later> later;
  double clock = 0;
  struct PCache { double t = -1; std::string stamp; std::unordered_map<i64, int> map; } pcache;
  std::unordered_map<i64, std::pair<int, double>> colTop;
};

WeatherSystem* weatherOf(Level& lv);
void registerWeatherSystem();   // called from initCore()

} // namespace dq
