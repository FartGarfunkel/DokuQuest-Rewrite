// render/particles.cpp — see particles.h. Motion rules: game/entities.js Particles.update and the
// additions in game/particles2.js (fireflies blink, drips land and splash, splats dry up); the
// quads: render/renderer.js buildParticles.
#include "render/particles.h"
#include "world/world.h"

namespace dq {

namespace {
u32 g_seed = 0x2545F491u;
inline float rnd() { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return (g_seed >> 8) * (1.0f / 16777216.0f); }
inline float rs() { return rnd() - 0.5f; }
const std::vector<VertexAttrib> P_ATTRS = {
  {0, 3, GL_FLOAT, GL_FALSE, false, 0},
  {1, 1, GL_UNSIGNED_SHORT, GL_FALSE, true, 12},
  {2, 2, GL_UNSIGNED_BYTE, GL_FALSE, false, 14},
  {3, 1, GL_UNSIGNED_BYTE, GL_FALSE, true, 16},
  {4, 3, GL_UNSIGNED_BYTE, GL_TRUE, false, 17},
  {5, 1, GL_UNSIGNED_BYTE, GL_FALSE, true, 20},
};
bool solidAt(World* w, vec3 p) { return w && reg().tSolid[w->getBlock(ifloor(p.x), ifloor(p.y), ifloor(p.z))]; }
} // namespace

ParticleSystem& particles() { static ParticleSystem ps; return ps; }
void addWaterfallSplash(vec3 pos) { particles().addWaterfallSplash(pos); }

int ParticleSystem::tile(const std::string& name) const {
  int t = reg().tile(name);
  if (t < 0) t = reg().tile("white");
  return std::max(0, t);
}
int ParticleSystem::blockTile(const std::string& key, int face) const {
  auto it = reg().blockIds.find(key);
  if (it == reg().blockIds.end()) { auto a = reg().blockAliases.find(key); if (a == reg().blockAliases.end()) return -1; it = reg().blockIds.find(a->second); if (it == reg().blockIds.end()) return -1; }
  if (!it->second) return -1;
  return reg().block(it->second).faces[face];
}
u8 ParticleSystem::lightAt(vec3 p) const { return world ? world->getLight(ifloor(p.x), ifloor(p.y), ifloor(p.z)) : 0xF0; }

void ParticleSystem::add(const Particle& p) {
  size_t cap = level >= 2 ? 6000 : 2400;
  if (!level || list.size() >= cap) return;
  if (!p.drop && (int)list.size() - drops >= (level >= 2 ? 1800 : 700)) return;   // the original kept 900; the drops have their own share
  list.push_back(p);
  if (p.drop) drops++;
}

// ---- the effects helpers (game.js burst / flame, ambientfx splash, particles2 p2Splash, critStars...) ----
void ParticleSystem::burst(vec3 pos, const std::string& name, int n, float speed, float grav) {
  int t = blockTile(name, 0); if (t < 0) t = tile(name);
  u8 l = lightAt(pos);
  for (int i = 0; i < n; i++) {
    Particle q;
    q.tile = (u16)t;
    int su = (int)(rnd() * 12), sv = (int)(rnd() * 12);   // a quarter of the tile, in sixteenths
    q.uv[0] = (u8)(su * 8); q.uv[1] = (u8)(sv * 8); q.uv[2] = (u8)(su * 8 + 32); q.uv[3] = (u8)(sv * 8 + 32);
    q.life = q.max = 0.4f + rnd() * 0.5f;
    q.p = pos + vec3(rs(), rs(), rs()) * 0.4f;
    q.v = vec3(rs() * 4 * speed, rnd() * 4 * speed, rs() * 4 * speed);
    q.size = 0.06f + rnd() * 0.05f; q.light = l; q.grav = grav;
    add(q);
  }
}
void ParticleSystem::flame(vec3 pos) {
  Particle q; q.tile = (u16)tile("flame"); q.life = q.max = 0.35f + rnd() * 0.3f;
  q.p = pos; q.v = vec3(rs() * 0.4f, 1 + rnd(), rs() * 0.4f); q.size = q.size0 = 0.12f; q.shrink = true; q.light = 0xFF; q.grav = -1;
  add(q);
}
void ParticleSystem::splash(vec3 pos, bool big) {
  int n = (int)((big ? 22 : 10) * (level == 1 ? 0.5f : 1.f));
  u16 t = (u16)tile("splash");
  for (int i = 0; i < n; i++) {
    float a = rnd() * TAU, s = 0.8f + rnd() * (big ? 2.4f : 1.4f);
    Particle q; q.tile = t; q.p = pos + vec3(std::cos(a) * 0.3f, 0.05f, std::sin(a) * 0.3f);
    q.v = vec3(std::cos(a) * s, 2 + rnd() * (big ? 3.5f : 2.f), std::sin(a) * s); q.life = q.max = 0.8f; q.size = 0.05f; q.light = 0xF0; q.grav = 12;
    add(q);
  }
}
void ParticleSystem::rainSplash(vec3 pos, u8 light, int n) {
  u16 t = (u16)tile("rain_splash");
  for (int i = 0; i < n; i++) {
    Particle q; q.tile = t; q.p = pos + vec3(0, 0.03f, 0); q.v = vec3(rs() * 1.2f, 1 + rnd() * 0.8f, rs() * 1.2f);
    q.life = q.max = 0.3f; q.size = 0.025f; q.light = light; q.grav = 9;
    add(q);
  }
}
void ParticleSystem::addWaterfallSplash(vec3 pos) {
  if (!level) return;
  // spray off the foot of a falling stream: a few splash droplets and a puff of mist
  u8 l = lightAt(pos + vec3(0, 0.5f, 0));
  rainSplash(pos + vec3(rs() * 0.8f, 0, rs() * 0.8f), l, level >= 2 ? 3 : 1);
  if (rnd() < (level >= 2 ? 0.35f : 0.12f)) {
    Particle q; q.tile = (u16)tile("smoke_puff"); q.p = pos + vec3(rs() * 0.8f, 0.1f, rs() * 0.8f);
    q.v = vec3(rs() * 0.3f, 0.4f + rnd() * 0.3f, rs() * 0.3f); q.life = q.max = 1.0f + rnd() * 0.6f; q.size = q.size0 = 0.08f; q.grow = 1.4f;
    q.light = l; q.grav = 0; q.tint[0] = 225; q.tint[1] = 235; q.tint[2] = 245;
    add(q);
  }
}

void ParticleSystem::spawn(const std::string& kind, vec3 pos, int count, const json& extra) {
  if (!level) return;
  auto num = [&](const char* k, float d) { return extra.is_object() && extra.contains(k) && extra[k].is_number() ? extra[k].get<float>() : d; };
  auto str = [&](const char* k, const std::string& d) { return extra.is_object() && extra.contains(k) && extra[k].is_string() ? extra[k].get<std::string>() : d; };
  bool flag = extra.is_object() && extra.value("big", false);
  float few = level == 1 ? 0.5f : 1.f;
  if (kind == "block" || kind == "break" || kind == "crumbs") { burst(pos, str("block", str("tile", "stone")), count, num("speed", 1.2f), num("grav", 14)); return; }
  if (kind == "dust") { burst(pos, str("block", "dirt"), count, num("speed", 0.18f), num("grav", 9)); return; }
  if (kind == "burst") { burst(pos, str("tile", "white"), count, num("speed", 1), num("grav", 14)); return; }
  if (kind == "flame") { for (int i = 0; i < count; i++) flame(pos + vec3(rs(), rs(), rs()) * num("spread", 0.f)); return; }
  if (kind == "splash") { splash(pos, flag); return; }
  if (kind == "rain_splash") { rainSplash(pos, lightAt(pos), count); return; }
  vec2 wd = windDrift * num("wind", 1);
  if (kind == "smoke" || kind == "smoke_dark" || kind == "fire_smoke" || kind == "torch_smoke" || kind == "campfire_smoke") {
    bool dark = kind == "smoke_dark" || kind == "fire_smoke", wisp = kind == "torch_smoke", camp = kind == "campfire_smoke";
    u16 t = (u16)tile(dark ? "smoke_dark" : wisp ? "smoke_puff" : camp ? "campfire_smoke" : "smoke");
    for (int i = 0; i < count; i++) {
      Particle q; q.tile = t; q.light = 0xFF; q.rise = true; q.wind = wd * (dark ? 2.f : camp ? 2.f : 1.f);
      if (wisp) { q.p = pos + vec3(0, 0.04f, 0); q.v = vec3(rs() * 0.04f, 0.3f + rnd() * 0.15f, rs() * 0.04f); q.life = q.max = 1.3f + rnd() * 0.9f; q.size = q.size0 = 0.03f; q.grow = 1.6f; q.grav = -0.04f; }
      else if (dark) { q.p = pos + vec3(rs() * 0.6f, 0, rs() * 0.6f); q.v = vec3(0, 0.7f + rnd() * 0.4f, 0); q.life = q.max = 3; q.size = q.size0 = 0.12f; q.grow = 1.6f; q.grav = -0.08f; }
      else if (camp) { q.p = pos + vec3(rs() * 0.4f, 0, rs() * 0.4f); q.v = vec3(rs() * 0.12f, 1.25f + rnd() * 0.4f, rs() * 0.12f); q.life = q.max = 5 + rnd() * 3; q.size = q.size0 = 0.12f + rnd() * 0.06f; q.grow = 2.8f; q.grav = -0.03f; q.light = lightAt(pos); }
      else { q.p = pos + vec3(rs() * 0.5f, 0, rs() * 0.5f); q.v = vec3(0, 0.45f, 0); q.life = q.max = 2.2f; q.size = q.size0 = 0.06f; q.grow = 1.5f; q.grav = -0.08f; }
      add(q);
    }
    return;
  }
  if (kind == "explosion") {   // blockfx.js explode: a cloud of smoke
    float r = num("r", 3);
    u16 t = (u16)tile("smoke");
    for (int i = 0; i < count; i++) {
      Particle q; q.tile = t; q.p = pos + vec3(rs(), rs(), rs()) * r; q.v = vec3(rs() * 3, rnd() * 3, rs() * 3);
      q.life = q.max = 0.9f; q.size = 0.35f + rnd() * 0.3f; q.size0 = 0.5f; q.shrink = true; q.light = 0xFF; q.grav = -1;
      add(q);
    }
    return;
  }
  if (kind == "poof") {   // a creature goes up in a poof
    float w = num("w", 0.3f), h = num("h", 1.8f);
    u16 t = (u16)tile("poof"); u8 l = lightAt(pos + vec3(0, 0.5f, 0));
    for (int i = 0; i < count; i++) {
      Particle q; q.tile = t; q.life = q.max = 0.5f + rnd() * 0.4f;
      q.p = pos + vec3(rs() * w * 2, rnd() * h, rs() * w * 2); q.v = vec3(rs() * 0.6f, 0.3f + rnd() * 0.5f, rs() * 0.6f);
      q.size = q.size0 = 0.12f; q.grow = 0.8f; q.light = l; q.grav = 0;
      add(q);
    }
    return;
  }
  if (kind == "crit") {
    u16 t = (u16)tile("crit_star");
    for (int i = 0; i < count; i++) {
      float a = rnd() * TAU, s = 2 + rnd() * 2.5f;
      Particle q; q.tile = t; q.p = pos; q.v = vec3(std::cos(a) * s, 1 + rnd() * 2.5f, std::sin(a) * s); q.life = q.max = 0.5f;
      q.size = q.size0 = 0.06f; q.shrink = true; q.light = 0xFF; q.grav = 6;
      add(q);
    }
    return;
  }
  if (kind == "slime" || kind == "magma_slime") {   // particles2 slimeSplat
    bool magma = kind == "magma_slime"; float s = std::max(0.3f, num("size", 0.5f));
    u8 lt = magma ? 0xFF : lightAt(pos + vec3(0, 0.2f, 0));
    u16 t = (u16)tile(magma ? "magma_blob" : "slime_blob");
    int n = (int)std::round(6 + s * 8);
    for (int i = 0; i < n; i++) {
      float a = rnd() * TAU, sp = 1 + rnd() * 1.5f * s;
      Particle q; q.tile = t; q.p = pos + vec3(std::cos(a) * s * 0.6f, 0.15f, std::sin(a) * s * 0.6f);
      q.v = vec3(std::cos(a) * sp, 1.2f + rnd() * 1.8f, std::sin(a) * sp); q.life = q.max = 0.5f + rnd() * 0.4f; q.size = 0.04f + s * 0.04f; q.light = lt; q.grav = 10;
      add(q);
    }
    if (!solidAt(world, pos - vec3(0, 0.1f, 0))) return;
    Particle q; q.tile = (u16)tile(magma ? "magma_splat" : "slime_splat");
    q.p = vec3(pos.x + rs() * 0.2f, std::floor(pos.y + 0.01f) + 0.012f + rnd() * 0.004f, pos.z + rs() * 0.2f);
    q.life = q.max = 4 + rnd() * 3; q.size = q.size0 = s * 0.75f; q.dry = 1.5f; q.flat = true; q.light = lt;
    add(q);
    return;
  }
  if (kind == "drip_water" || kind == "drip_lava" || kind == "drip_honey" || kind == "drip_leaf" || kind == "drip_tear") {
    bool lava = kind == "drip_lava", honey = kind == "drip_honey", leaf = kind == "drip_leaf", tear = kind == "drip_tear";
    Particle q; q.tile = (u16)tile(lava ? "drip_lava" : honey ? "drip_honey" : tear ? "drip_tear" : "drip_water");
    q.p = pos; q.v = vec3(0, -0.05f, 0); q.life = q.max = 4; q.size = honey ? 0.045f : 0.04f;
    q.light = lava || tear ? 0xFF : lightAt(pos - vec3(0, 0.5f, 0)); q.grav = honey ? 6 : 9;
    q.hang = leaf ? 0.1f + rnd() * 0.3f : honey ? 1 + rnd() * 1.5f : tear ? 0.6f : 0.6f + rnd() * 1.2f;
    q.drip = lava ? 2 : honey ? 4 : tear ? 0 : 1;
    add(q);
    return;
  }
  if (kind == "leaf") {   // ambientfx: a leaf drifting down, carried by the wind
    std::string tl = str("tile", "leaf_green");
    float sp = num("speed", 0.6f), gust = num("gust", 0);
    Particle q; q.tile = (u16)tile(tl); q.p = pos; q.v = vec3(windDrift.x / 0.35f * sp, -0.5f - rnd() * 0.3f, windDrift.y / 0.35f * sp);
    q.life = 7 + rnd() * 4; q.max = 11; q.size = 0.075f + rnd() * 0.03f; q.light = lightAt(pos); q.grav = 0.02f; q.flutter = 1.1f + gust; q.ph = rnd() * 6;
    add(q);
    return;
  }
  if (kind == "torch_flame" || kind == "soul_flame" || kind == "held_flame") {
    Particle q; q.tile = (u16)tile(kind == "soul_flame" ? "soul_flame" : "torch_flame"); q.p = pos; q.v = vec3(0, 0.05f, 0);
    q.life = q.max = 0.35f; q.size = q.size0 = 0.04f; q.shrink = true; q.light = 0xFF;
    add(q);
    return;
  }
  if (kind == "ember" || kind == "spark") {
    u16 t = (u16)tile("ember_spark");
    for (int i = 0; i < count; i++) {
      Particle q; q.tile = t; float fl = 0.4f + rnd() * 0.4f; q.life = q.max = fl;
      q.p = pos + vec3(rs() * 0.04f, 0.04f, rs() * 0.04f); q.v = vec3(rs() * 0.25f, 0.5f + rnd() * 0.5f, rs() * 0.25f);
      q.size = q.size0 = 0.014f; q.shrink = true; q.light = 0xFF; q.grav = -0.2f;
      add(q);
    }
    return;
  }
  if (kind == "firefly") {
    Particle q; q.tile = (u16)tile("firefly"); q.p = pos; q.v = vec3(rs() * 0.3f, rs() * 0.12f, rs() * 0.3f);
    q.life = q.max = 4 + rnd() * 4; q.size = q.size0 = 0.03f; q.light = 0xFF; q.flutter = 0.35f; q.ph = rnd() * 6; q.blink = 3 + rnd() * 3;
    add(q);
    return;
  }
  (void)few;
  // anything else: a burst of that tile or block, like the original's game.burst(x, y, z, name, n, speed)
  burst(pos, str("tile", kind), count, num("speed", 0.6f), num("grav", 14));
}

// ---- the simulation ----------------------------------------------------------------------------------
void ParticleSystem::update(World* w, double dtd, double time) {
  world = w; now = time;
  float dt = (float)std::min(dtd, 0.1);
  auto& L = list;
  std::vector<Particle> born;   // splashes from landings, added after the loop
  for (size_t i = L.size(); i-- > 0;) {
    Particle& p = L[i];
    p.life -= dt;
    if (p.life <= 0) { if (p.drop) drops--; L[i] = L.back(); L.pop_back(); continue; }
    if (p.hang > 0) { p.hang -= dt; continue; }   // a drip gathering before it falls
    if (p.drop) {
      // rain and snow (game/weather.js updateDrops): fall, drift, and splash where they land
      p.p += p.v * dt;
      if (p.drop == 2) { p.p.x += std::sin((float)time * 1.3f + p.ph) * 0.4f * dt; p.p.z += std::cos((float)time * 1.1f + p.ph) * 0.4f * dt; }
      if (p.p.y <= p.floorY) {
        if (p.drop == 1 && rnd() < 0.25f && level > 0) {
          Particle s; s.tile = (u16)tile("rain_splash"); s.p = vec3(p.p.x, p.floorY + 0.05f, p.p.z); s.v = vec3(rs() * 0.8f, 1.2f, rs() * 0.8f);
          s.life = s.max = 0.25f; s.size = 0.03f; s.light = p.light; s.grav = 9; born.push_back(s);
        }
        drops--; L[i] = L.back(); L.pop_back();
      }
      continue;
    }
    if (p.flutter > 0) { float t = p.life * 2.4f + p.ph; p.p.x += std::sin(t) * p.flutter * dt; p.p.z += std::cos(t * 0.8f) * p.flutter * 0.7f * dt; }
    p.v.y -= p.grav * dt;
    // smoke: rises, and when it meets a ceiling it spreads out underneath and gathers there
    if (p.rise) {
      if (solidAt(w, vec3(p.p.x, p.p.y + p.size + p.v.y * dt + 0.04f, p.p.z))) {
        if (!p.pooled) { p.pooled = true; float a = rnd() * TAU, s = 0.15f + rnd() * 0.25f; p.v.x = std::cos(a) * s; p.v.z = std::sin(a) * s; p.life += 2.5f; p.max += 2.5f; }
        p.v.y = std::min(0.f, p.v.y); p.grav = 0;
        if (solidAt(w, vec3(p.p.x + p.v.x * dt * 4, p.p.y, p.p.z + p.v.z * dt * 4))) { p.v.x = -p.v.x; p.v.z = -p.v.z; }
      } else if (p.pooled && p.v.y <= 0) p.v.y = 0.05f;
      p.v.x *= 1 - dt * 0.3f; p.v.z *= 1 - dt * 0.3f;
      p.v.x += p.wind.x * dt; p.v.z += p.wind.y * dt;
    }
    vec3 n = p.p + p.v * dt;
    if (p.grav > 0 && solidAt(w, n)) { p.v.x *= 0.3f; p.v.z *= 0.3f; p.v.y = 0; }
    else p.p = n;
    if (p.grow > 0) { float k = p.life / p.max; p.size = p.size0 * (1 + (1 - k) * p.grow) * (k < 0.2f ? k * 5 : 1); }
    else if (p.shrink) p.size = p.size0 * std::max(0.1f, p.life / p.max);
    if (p.blink > 0) p.size = p.size0 * (std::sin(p.life * p.blink + p.ph) > -0.2f ? 1.f : 0.25f);   // fireflies wink on and off
    if (p.drip) {
      if (p.v.y < -1) p.fell = true;
      else if (p.fell && p.v.y == 0 && p.life > 0.01f) {
        // it landed: a splash of water, a glowing spot of lava, a blob of honey
        p.life = 0.001f;
        if (p.drip == 4) { Particle s; s.tile = (u16)tile("honey_spot"); s.p = vec3(p.p.x, std::floor(p.p.y) + 0.015f, p.p.z); s.life = s.max = 2 + rnd(); s.size = s.size0 = 0.06f; s.dry = 0.8f; s.flat = true; s.light = p.light; born.push_back(s); }
        else if (p.drip == 2) { Particle s; s.tile = (u16)tile("lava_spot"); s.p = vec3(p.p.x, std::floor(p.p.y) + 0.02f, p.p.z); s.life = s.max = 1 + rnd() * 0.8f; s.size = s.size0 = 0.07f; s.shrink = true; s.flat = true; s.light = 0xFF; born.push_back(s); }
        else for (int k = 0, m = 2 + (rnd() < 0.5f ? 1 : 0); k < m; k++) {
          Particle s; s.tile = (u16)tile("rain_splash"); s.p = vec3(p.p.x, std::floor(p.p.y) + 0.03f, p.p.z); s.v = vec3(rs() * 1.2f, 1 + rnd() * 0.8f, rs() * 1.2f);
          s.life = s.max = 0.3f; s.size = 0.025f; s.light = p.light; s.grav = 9; born.push_back(s);
        }
      }
    }
    if (p.dry > 0) p.size = p.size0 * std::min(1.f, p.life / p.dry);   // a splat drying up at the end
  }
  for (auto& b : born) add(b);
}

void ParticleSystem::ensureGl() {
  if (glReady) return;
  glReady = true;
  shader.load("particles.glsl", "particles.glsl");
  mesh.setup(sizeof(Vtx), P_ATTRS);
}

void ParticleSystem::draw(const FrameCtx& f, const TileTextures& tex) {
  if (list.empty()) return;
  ensureGl();
  if (!shader.ok()) return;
  const Camera& cam = f.cam;
  vec3 right = cam.right(), fwd = cam.forward(), up = glm::cross(right, fwd);
  verts.clear(); verts.reserve(list.size() * 4);
  glm::dvec3 o = cam.origin;
  for (const Particle& p : list) {
    vec3 c = vec3(glm::dvec3(p.p) - o);
    float s = p.size;
    if (s <= 0.0005f) continue;
    vec3 A = right, B = up;
    if (p.streak > 0) {
      // a long thin stroke along its direction, turned about that line to face you
      vec3 d = p.sd, nrm = glm::cross(d, fwd); float nl = glm::length(nrm);
      A = d * (p.streak / s); B = nl > 1e-5f ? nrm / nl : up;
    } else if (p.flat) { A = vec3(1, 0, 0); B = vec3(0, 0, 1); }
    else if (p.vert > 0) B = vec3(0, p.vert / s, 0);
    A *= s; B *= s;
    Vtx v{}; v.layer = p.tile; v.light = p.light; v.r = p.tint[0]; v.g = p.tint[1]; v.b = p.tint[2]; v.flags = p.light == 0xFF || p.glow ? 1 : 0;
    auto put = [&](vec3 q, u8 u, u8 vv) { v.x = q.x; v.y = q.y; v.z = q.z; v.u = u; v.v = vv; verts.push_back(v); };
    put(c - A - B, p.uv[0], p.uv[3]); put(c + A - B, p.uv[2], p.uv[3]); put(c + A + B, p.uv[2], p.uv[1]); put(c - A + B, p.uv[0], p.uv[1]);
  }
  if (verts.empty()) return;
  mesh.uploadQuads(verts.data(), verts.size() * sizeof(Vtx), verts.size() / 4, GL_STREAM_DRAW);
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", cam.viewProj);
  shader.set("uCamFrac", vec3(cam.pos - cam.origin));
  tex.bind(shader);
  glDisable(GL_CULL_FACE);
  mesh.draw();
  glEnable(GL_CULL_FACE);
}

} // namespace dq
