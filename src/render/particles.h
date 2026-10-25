// render/particles.h — the particle engine (game/entities.js Particles + game/particles2.js +
// game/ambientfx.js + the effects helpers of game/game.js): block crumbs, smoke that gathers under
// ceilings, torch flames, falling and blowing leaves, drips that splash, rain and snow, splashes,
// fireflies, ash and embers, poofs, crits, explosion clouds... Simulated on the CPU, drawn as one
// batch of camera-facing quads (streaks, upright rain and flat splats too) from the tile arrays.
//
// Other areas call:  particles().spawn("smoke", pos, 6);  particles().spawn("block", pos, 14, {{"block","stone"}});
// The kinds are listed in docs/port/lod.md (they follow the original's tile names and helpers;
// an unknown kind is a burst of that tile or block, like the original's game.burst(x,y,z,name,n,speed)).
#pragma once
#include "render/frame.h"

namespace dq {

class World;

struct Particle {
  vec3 p{0}, v{0};
  float life = 1, max = 1, size = 0.06f, size0 = 0, grav = 0;
  float grow = 0, flutter = 0, ph = 0, blink = 0, hang = 0, dry = 0;
  float streak = 0, vert = 0;   // a long stroke along `sd` (wind, lightning) / upright length (rain)
  vec3 sd{0};
  vec2 wind{0};                 // smoke drift (blocks/s²)
  float floorY = -1e9f;         // rain and snow: where it lands
  u16 tile = 0;                 // tile layer
  u8 uv[4] = {0, 0, 128, 128};  // sub-rectangle of the tile, 1/128 units
  u8 light = 0xF0;              // sky << 4 | block (0xFF = full bright)
  u8 tint[3] = {255, 255, 255};
  u8 drip = 0;                  // 1 water, 2 lava, 3 rain off leaves, 4 honey
  u8 drop = 0;                  // weather: 1 rain, 2 snow
  bool shrink = false, rise = false, pooled = false, flat = false, fell = false, glow = false;
};

class ParticleSystem {
public:
  // ---- spawning ----
  void add(const Particle& p);
  void addForced(const Particle& p) { list.push_back(p); }   // not limited by the setting (lightning)
  void spawn(const std::string& kind, vec3 pos, int count = 8, const json& extra = nullptr);
  void burst(vec3 pos, const std::string& tileOrBlock, int n, float speed, float grav = 14);   // game.burst
  void flame(vec3 pos);                                                                         // game.flame
  void splash(vec3 pos, bool big);                                                              // ambientfx splash
  void rainSplash(vec3 pos, u8 light, int n = 3);                                               // p2Splash
  void addWaterfallSplash(vec3 pos);                                                            // the meshing engine's waterfall bases
  int tile(const std::string& name) const;      // tile layer by name (falls back to "white")
  int blockTile(const std::string& blockKey, int face = 0) const;   // -1 if not a block
  // ---- frame ----
  void update(World* w, double dt, double time);
  void draw(const FrameCtx& f, const TileTextures& tex);
  void clear() { list.clear(); drops = 0; }
  void removeDrops() { list.erase(std::remove_if(list.begin(), list.end(), [](const Particle& p) { return p.drop != 0; }), list.end()); drops = 0; }
  size_t count() const { return list.size(); }
  int dropCount() const { return drops; }
  int level = 2;            // settings.particles (0 off, 1 few, 2 all)
  World* world = nullptr;   // the world particles collide with (set by the session layer)
  vec2 windDrift{0};        // which way the breeze carries smoke (game.windDrift)
  std::vector<Particle> list;
private:
  u8 lightAt(vec3 p) const;
  void ensureGl();
  Shader shader; Mesh mesh; bool glReady = false;
  int drops = 0;
  double now = 0;
  struct Vtx { float x, y, z; u16 layer; u8 u, v; u8 light, r, g, b; u8 flags, pad[3]; };
  std::vector<Vtx> verts;
};

ParticleSystem& particles();          // the client's particle engine
void addWaterfallSplash(vec3 pos);    // = particles().addWaterfallSplash(pos)

} // namespace dq
