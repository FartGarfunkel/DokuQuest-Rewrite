// render/weather_layer.cpp — the weather particles in the world (render/weather_sim + weather_draw): their inputs from
// the session each frame (camera, weather, wind, settings), drawn with the see-through things. Registered from
// registerEnvironmentLayers() (render/env_layers.cpp).
#include "render/weather_draw.h"
#include "client/session.h"
#include "client/app.h"
#include "sim/weather.h"
#include "sim/wind_field.h"
#include "render/particles.h"
#include <cstdlib>

namespace dq {
namespace { const WeatherSim* g_live = nullptr; }
const WeatherSim* weatherSimNow() { return g_live; }

class WeatherLayer : public RenderLayer {
public:
  const char* name() const override { return "weather-particles"; }
  int order() const override { return 15; }
  ~WeatherLayer() override { if (g_live == &sim) g_live = nullptr; }
  void update(GameSession& s, double dt) override {
    g_live = &sim;
    if (!s.player || !s.level) return;
    const Settings& st = s.app.settings;
    int budget = st.weatherParticles;
    if (st.particles <= 0) budget = 0;                         // Particles: Minimal (no particles at all)
    else if (st.particles == 1) budget = std::min(budget, 600); // Particles: Decreased forces Weather Particles: Low
    sim.setBudget(budget);
    WeatherInputs in;
    in.world = &s.world();
    in.W = s.player->realm == REALM_MAIN ? weatherOf(*s.level) : nullptr;
    in.wind = windParams(*s.level);
    in.cam = vec3(s.frame.cam.pos);
    in.camVel = s.player->vel;
    in.band = s.player->realm == REALM_MAIN ? s.frame.dim : -1 - s.player->realm;
    in.dist = (float)st.weatherDist;
    in.surfaceFx = st.weatherSurfaceFx;
    in.groundFog = st.groundFog;
    in.t = s.frame.time;
    in.fogCol = s.frame.fogColor;
    in.tiles = &s.app.tiles;
    if (budget == 0) { sim.clear(); return; }
    sim.update(in, dt);
    static const bool stats = std::getenv("DQ_FRAME_STATS") != nullptr;   // (a line every 5 s: what's falling)
    if (stats && (statT += dt) > 5) {
      statT = 0; int n[(int)WKind::Count] = {};
      for (const WParticle& p : sim.all()) n[(int)p.kind]++;
      DQ_LOG("weather fx: %d (rain %d, heavy %d, snow %d, heavy snow %d, dust %d, fog %d, ripples %d, streaks %d)", sim.count(), n[0], n[1], n[2], n[3], n[4] + n[5] + n[6] + n[7], n[8] + n[9], n[10], n[12]);
    }
  }
  double statT = 0;
  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    if (f.portalDepth > 0 || !s.player) return;   // (the weather is drawn once, round you)
    draw.draw(f, sim, s.player->vel);
  }
private:
  WeatherSim sim;
  WeatherDraw draw;
};

void registerWeatherLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<WeatherLayer>(); }); }

} // namespace dq
