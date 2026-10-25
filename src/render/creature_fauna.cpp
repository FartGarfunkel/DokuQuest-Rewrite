// render/creature_fauna.cpp — game/fauna.js: ambient wildlife. Butterflies by day, fireflies by night,
// songbirds that perch in trees and scatter when you come close, and fish in lakes and rivers. Purely
// atmospheric: each player's game spawns its own; never saved, never sent over the network.
#include "render/creature_draw.h"
#include "render/creature_models.h"
#include "assets/tileset.h"
#include "assets/voxel_paint.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/creature.h"
#include "core/registry.h"

namespace dq {

struct Fauna::Critter {
  int kind = 0;   // 0 butterfly 1 firefly 2 bird 3 fish
  int tex = 0;
  vec3 p{0}, v{0}, home{0}, goal{0};
  bool hasGoal = false, perched = true;
  float yaw = 0, age = 0, life = 60, tt = 0, flap = 0, ph = 0, peck = 0, launched = 0;
};
struct FPart { VoxelMeshGL gl; mat4 local{1}; };
struct Fauna::Meshes {
  FPart birdBody[3], birdHead[3], birdBeak, birdTail[3], wingL[3], wingR[3];
  FPart flyL[4], flyR[4];
  FPart fishBody[2], fishTail[2];
  FPart firefly;
};

static const char* BIRDS[3] = {"bird_blue", "bird_red", "bird_brown"};
static const char* FLIES[4] = {"butterfly_orange", "butterfly_blue", "butterfly_yellow", "butterfly_white"};
static const char* FISH[2] = {"fish_koi", "fish_silver"};

static void boxPart(FPart& out, const char* tile, vec3 lo, vec3 hi, bool glow = false) {
  vec3 size = hi - lo;
  ivec3 n = glm::max(ivec3(glm::round(size * 64.f)), ivec3(1));
  VoxelGrid g; buildTileBox(g, n, tile, glow);
  VoxelMeshData md; buildVoxelMesh(g, md); out.gl.upload(md);
  out.local = glm::scale(glm::translate(mat4(1), lo), size / vec3(n));
}
// a butterfly wing: one half of the painted pair (seen from above), one voxel thin
static void wingPart(FPart& out, const char* tile, bool left) {
  const TileSet& ts = app().tiles;
  auto it = ts.index.find(tile);
  int t = it == ts.index.end() ? 0 : it->second, S = ts.size, H = S / 2;
  VoxelGrid g(ivec3(H, 1, S));
  for (int z = 0; z < S; z++) for (int x = 0; x < H; x++) {
    int sx = left ? x : H + x;
    u32 c = tileVoxel(ts, t, sx, z);
    if (c) g.set(x, 0, z, c | 0xFF000000u);
  }
  VoxelMeshData md; buildVoxelMesh(g, md); out.gl.upload(md);
  // fauna.js wing quads: x -0.5..0 (left) or 0..0.5, z -0.5..0.5, then scaled 0.22
  out.local = glm::scale(glm::translate(mat4(1), vec3(left ? -0.5f : 0.f, -0.01f, -0.5f)), vec3(0.5f / H, 0.02f, 1.f / S));
}

Fauna::Fauna() : list(new std::vector<Critter>()) {}
Fauna::~Fauna() { delete list; }
int Fauna::count() const { return (int)list->size(); }

static const std::string& keyOf(BlockId id) { return reg().block(id).key; }
// the highest solid, liquid or leafy block under fromY (plants and air skipped)
static bool surfaceAt(World& w, int x, int z, int fromY, int dim, int& y, BlockId& id) {
  const Registry& R = reg();
  for (int yy = std::min(dimBase(dim) + DIM_H - 2, fromY); yy > dimBase(dim) + 1; yy--) {
    BlockId b = w.getBlock(x, yy, z);
    if (!b || R.tRender[b] == (u8)Render::Cross || (!R.tSolid[b] && !R.tLiquid[b] && !R.tLeaves[b])) continue;
    y = yy + 1; id = b; return true;
  }
  return false;
}

void Fauna::spawn(GameSession& s, vec3 p) {
  World& w = s.world();
  int dim = dimOfY(clampv(ifloor(p.y), 0, WORLD_H - 1));
  if (dim != DIM_OVERWORLD && dim != DIM_AETHER) return;
  float day = envDay(*s.level, dim);
  auto ring = [&](float a, float b) { float ang = frand() * TAU, d = a + frand() * (b - a); return ivec2(ifloor(p.x + std::cos(ang) * d), ifloor(p.z + std::sin(ang) * d)); };
  auto count = [&](int k) { int n = 0; for (auto& c : *list) if (c.kind == k) n++; return n; };
  const Registry& R = reg();
  auto grassy = [&](BlockId id) { const std::string& k = keyOf(id); return k == "grass" || k == "sky_grass"; };
  if (day > 0.6f && count(0) < 10 && frand() < 0.5f) {   // butterflies over flowers and grass on bright days
    ivec2 xz = ring(8, 26); int y; BlockId id;
    if (w.isLoaded(xz.x, ifloor(p.y), xz.y) && surfaceAt(w, xz.x, xz.y, ifloor(p.y) + 20, dim, y, id) && grassy(id) && std::fabs(y - p.y) < 20) {
      int n = 1 + (int)(frand() * 3), tex = (int)(frand() * 4) & 3;
      for (int i = 0; i < n; i++) { Critter c; c.kind = 0; c.tex = tex; c.p = vec3(xz.x + frand(), y + 0.8f + frand() * 1.2f, xz.y + frand()); c.home = vec3(xz.x, y + 1.2f, xz.y); c.yaw = frand() * TAU; c.life = 70 + frand() * 80; c.flap = frand() * 10; list->push_back(c); }
    }
  }
  if (dim == DIM_OVERWORLD && day < 0.45f && count(1) < 36 && frand() < 0.6f) {   // fireflies in grassy places at night
    ivec2 xz = ring(6, 22); int y; BlockId id;
    float temp = 0; if (Chunk* ch = w.chunk(DIM_OVERWORLD, xz.x >> 4, xz.y >> 4)) temp = (ch->climate[((xz.y & 15) * 16 + (xz.x & 15)) * 2] - 128) / 127.f;
    if (w.isLoaded(xz.x, ifloor(p.y), xz.y) && surfaceAt(w, xz.x, xz.y, ifloor(p.y) + 16, dim, y, id) && keyOf(id) == "grass" && temp > -0.3f && std::fabs(y - p.y) < 16) {
      int n = 4 + (int)(frand() * 7);
      for (int i = 0; i < n; i++) { Critter c; c.kind = 1; c.p = vec3(xz.x + frand() * 4 - 2, y + 0.4f + frand() * 2.2f, xz.y + frand() * 4 - 2); c.life = 50 + frand() * 60; c.ph = frand() * TAU; c.home = vec3(0, y + 1.2f, 0); list->push_back(c); }
    }
  }
  if (day > 0.35f && count(2) < 14 && frand() < 0.3f) {   // songbirds perched in tree tops and meadows
    ivec2 xz = ring(18, 40); int y; BlockId id;
    if (w.isLoaded(xz.x, ifloor(p.y), xz.y) && surfaceAt(w, xz.x, xz.y, ifloor(p.y) + 30, dim, y, id) && (R.tLeaves[id] || grassy(id)) && std::fabs(y - p.y) < 30) {
      int tex = (int)(frand() * 3) % 3, n = 2 + (int)(frand() * 4);
      for (int i = 0; i < n; i++) {
        int bx = xz.x + (int)(frand() * 3) - 1, bz = xz.y + (int)(frand() * 3) - 1, yy = y; BlockId b2;
        if (!surfaceAt(w, bx, bz, y + 4, dim, yy, b2)) yy = y;
        Critter c; c.kind = 2; c.tex = tex; c.p = vec3(bx + 0.5f, yy, bz + 0.5f); c.yaw = frand() * TAU; c.life = 300; c.tt = frand() * 2; list->push_back(c);
      }
    }
  }
  if (dim == DIM_OVERWORLD && count(3) < 10 && frand() < 0.4f) {   // fish in open water
    ivec2 xz = ring(8, 28);
    if (w.isLoaded(xz.x, ifloor(p.y), xz.y)) for (int y = std::min(dimBase(dim) + SEA_LEVEL + 2, ifloor(p.y) + 10); y > dimBase(dim) + 4; y--) {
      if (R.tLiquid[w.getBlock(xz.x, y, xz.y)] == 1 && R.tLiquid[w.getBlock(xz.x, y - 1, xz.y)] == 1) {
        int tex = frand() < 0.4f ? 0 : 1;
        for (int i = 0, n = 1 + (int)(frand() * 3); i < n; i++) { Critter c; c.kind = 3; c.tex = tex; c.p = vec3(xz.x + 0.5f, y - 0.3f, xz.y + 0.5f); c.yaw = frand() * TAU; c.life = 200; c.ph = frand() * 10; list->push_back(c); }
        break;
      }
    }
  }
}

static void steerTo(vec3& p, vec3& v, float& yaw, vec3 t, float speed, float dt, float k) {
  vec3 d = t - p; float l = glm::length(d); if (l < 1e-4f) l = 1;
  v += (d / l * speed - v) * std::min(1.f, dt * k);
  p += v * dt;
  if (std::hypot(v.x, v.z) > 0.05f) yaw = std::atan2(-v.x, -v.z);
}

void Fauna::update(GameSession& s, float dt) {
  if (!s.player || s.player->dead) return;
  World& w = s.world();
  const Registry& R = reg();
  Player& pl = *s.player;
  t += dt;
  if (t > 1) { t = 0; spawn(s, pl.pos); }
  int dim = dimOfY(clampv(ifloor(pl.pos.y), 0, WORLD_H - 1));
  float day = envDay(*s.level, dim);
  auto& L = *list;
  for (int i = (int)L.size() - 1; i >= 0; i--) {
    Critter& c = L[i];
    c.age += dt;
    float near = glm::distance(pl.pos, c.p);
    if (near > 64 || c.age > c.life) { L[i] = L.back(); L.pop_back(); continue; }
    if (c.kind == 0) {
      c.tt -= dt; c.flap += dt * 18;
      if (c.tt <= 0) { c.tt = 1 + frand() * 2.5f; c.goal = c.home + vec3(frand() * 6 - 3, frand() * 1.6f - 0.3f, frand() * 6 - 3); c.hasGoal = true; }
      steerTo(c.p, c.v, c.yaw, c.goal + vec3(0, std::sin(c.age * 4) * 0.3f, 0), 1.4f, dt, 1.5f);
      if (day < 0.45f) c.life = std::min(c.life, c.age + 5);
    } else if (c.kind == 1) {
      c.ph += dt;
      c.v.x += (frand() - 0.5f) * dt * 1.2f; c.v.z += (frand() - 0.5f) * dt * 1.2f; c.v.y += ((c.home.y - c.p.y) * 0.3f + (frand() - 0.5f)) * dt;
      c.v.x *= 0.98f; c.v.y *= 0.97f; c.v.z *= 0.98f;
      c.p += c.v * dt;
      if (day > 0.55f) c.life = std::min(c.life, c.age + 3);
    } else if (c.kind == 2) {
      bool scare = near < (pl.sneaking ? 3 : 7);
      if (c.perched) {
        c.v = vec3(0);
        c.tt -= dt; c.peck = std::max(0.f, c.peck - dt);
        if (c.tt <= 0) { c.tt = 0.8f + frand() * 2.5f; if (frand() < 0.5f) c.peck = 0.3f; else c.yaw += (frand() - 0.5f) * 2; if (frand() < 0.12f) s.level->fx->sound("chirp", c.p, 30); }
        BlockId under = w.getBlock(ifloor(c.p.x), ifloor(c.p.y - 0.1f), ifloor(c.p.z));
        if (!R.tSolid[under] && !R.tLeaves[under]) c.p.y -= dt * 2;
        if (scare) {   // the whole flock takes off together
          float away = std::atan2(c.p.x - pl.pos.x, c.p.z - pl.pos.z);
          for (auto& o : L) if (o.kind == 2 && o.perched && std::hypot(o.p.x - c.p.x, o.p.z - c.p.z) < 6) {
            o.perched = false; o.launched = o.age;
            float ang = away + (frand() - 0.5f) * 1.2f, d = 18 + frand() * 22;
            int gx = ifloor(o.p.x + std::sin(ang) * d), gz = ifloor(o.p.z + std::cos(ang) * d), gy; BlockId gid;
            bool ok = w.isLoaded(gx, ifloor(o.p.y), gz) && surfaceAt(w, gx, gz, ifloor(o.p.y) + 25, dim, gy, gid);
            o.goal = vec3(gx + 0.5f, ok ? gy : o.p.y + 6, gz + 0.5f);
            if (!ok) o.life = std::min(o.life, o.age + 6);
          }
          s.level->fx->sound("flap", c.p, 24);
        }
      } else {
        c.flap += dt * 22;
        float dist = std::hypot(c.goal.x - c.p.x, c.goal.z - c.p.z), cruise = c.goal.y + std::min(8.f, dist * 0.4f);
        steerTo(c.p, c.v, c.yaw, vec3(c.goal.x, dist > 2 ? cruise : c.goal.y, c.goal.z), dist > 2 ? 6 : 2.5f, dt, 1.6f);
        if (dist < 0.6f && std::fabs(c.p.y - c.goal.y) < 0.6f) { c.perched = true; c.p.y = c.goal.y; c.tt = 1; }
        if (c.age - c.launched > 20) c.life = 0;
      }
    } else {
      auto wet = [&](vec3 q) { return R.tLiquid[w.getBlock(ifloor(q.x), ifloor(q.y), ifloor(q.z))] == 1; };
      c.ph += dt * (near < 4 ? 30 : 10);
      c.tt -= dt;
      if (c.tt <= 0 || near < 4) {
        c.tt = 1.5f + frand() * 3;
        for (int k = 0; k < 6; k++) {
          float ang = frand() * TAU;
          if (near < 4) ang = std::atan2(c.p.x - pl.pos.x, c.p.z - pl.pos.z) + (frand() - 0.5f);
          vec3 g(c.p.x + std::sin(ang) * 3, c.p.y + (frand() - 0.5f), c.p.z + std::cos(ang) * 3);
          if (wet(g) && wet(g + vec3(0, 0.3f, 0))) { c.goal = g; c.hasGoal = true; break; }
        }
      }
      if (!c.hasGoal) { c.goal = c.p; c.hasGoal = true; }
      steerTo(c.p, c.v, c.yaw, c.goal, near < 4 ? 3 : 1, dt, 3);
      if (!wet(c.p)) { c.p.y -= dt; if (!wet(c.p)) c.life = 0; }
    }
  }
}

void Fauna::draw(GameSession& s, const FrameCtx& f) {
  if (list->empty()) return;
  if (!meshes) {
    meshes = std::make_unique<Meshes>();
    Meshes& M = *meshes;
    for (int i = 0; i < 3; i++) {
      boxPart(M.birdBody[i], BIRDS[i], {-0.07f, -0.06f, -0.12f}, {0.07f, 0.07f, 0.1f});
      boxPart(M.birdHead[i], BIRDS[i], {-0.055f, 0.03f, -0.21f}, {0.055f, 0.13f, -0.1f});
      boxPart(M.birdTail[i], BIRDS[i], {-0.05f, 0.0f, 0.1f}, {0.05f, 0.02f, 0.22f});
      boxPart(M.wingL[i], BIRDS[i], {-0.22f, 0, -0.08f}, {0, 0.015f, 0.08f});
      boxPart(M.wingR[i], BIRDS[i], {0, 0, -0.08f}, {0.22f, 0.015f, 0.08f});
    }
    boxPart(M.birdBeak, "beak", {-0.02f, 0.06f, -0.27f}, {0.02f, 0.09f, -0.21f});
    for (int i = 0; i < 4; i++) { wingPart(M.flyL[i], FLIES[i], true); wingPart(M.flyR[i], FLIES[i], false); }
    for (int i = 0; i < 2; i++) { boxPart(M.fishBody[i], FISH[i], {-0.05f, -0.06f, -0.16f}, {0.05f, 0.06f, 0.12f}); boxPart(M.fishTail[i], FISH[i], {-0.012f, -0.08f, 0}, {0.012f, 0.08f, 0.13f}); }
    boxPart(M.firefly, "firefly", {-0.03f, -0.03f, -0.03f}, {0.03f, 0.03f, 0.03f}, true);
  }
  Meshes& M = *meshes;
  CreatureDraw& D = creatureDraw();
  World& w = s.world();
  for (auto& c : *list) {
    vec3 rel = f.cam.rel(glm::dvec3(c.p));
    if (glm::dot(rel, rel) > 64 * 64 || !f.cam.frustum.sphere(rel, 0.5f)) continue;
    VoxelLight L = lightAt(w, c.p);
    mat4 base = glm::rotate(glm::translate(mat4(1), rel), c.yaw, vec3(0, 1, 0));
    if (c.kind == 1) {
      float b = 0.5f + 0.5f * std::sin(c.ph * 3.1f + std::sin(c.ph * 1.3f) * 2);
      if (b > 0.25f) D.draw(M.firefly.gl, glm::scale(glm::translate(mat4(1), rel), vec3(0.6f + b)) * M.firefly.local, {1, 1});
    } else if (c.kind == 0) {
      float fl = std::sin(c.flap) * 1.1f;
      mat4 m = glm::scale(base, vec3(0.22f));
      D.draw(M.flyL[c.tex].gl, glm::rotate(m, -fl, vec3(0, 0, 1)) * M.flyL[c.tex].local, L);
      D.draw(M.flyR[c.tex].gl, glm::rotate(m, fl, vec3(0, 0, 1)) * M.flyR[c.tex].local, L);
    } else if (c.kind == 2) {
      mat4 m = glm::translate(base, vec3(0, 0.06f, 0));
      mat4 bm = c.peck > 0 ? glm::rotate(m, -0.5f, vec3(1, 0, 0)) : m;
      D.draw(M.birdBody[c.tex].gl, bm * M.birdBody[c.tex].local, L);
      D.draw(M.birdHead[c.tex].gl, bm * M.birdHead[c.tex].local, L);
      D.draw(M.birdTail[c.tex].gl, bm * M.birdTail[c.tex].local, L);
      D.draw(M.birdBeak.gl, bm * M.birdBeak.local, L);
      float fl = !c.perched ? std::sin(c.flap) * 1.0f : 0.1f;
      D.draw(M.wingL[c.tex].gl, glm::rotate(glm::translate(m, vec3(-0.06f, 0.04f, 0)), fl, vec3(0, 0, 1)) * M.wingL[c.tex].local, L);
      D.draw(M.wingR[c.tex].gl, glm::rotate(glm::translate(m, vec3(0.06f, 0.04f, 0)), -fl, vec3(0, 0, 1)) * M.wingR[c.tex].local, L);
    } else {
      D.draw(M.fishBody[c.tex].gl, base * M.fishBody[c.tex].local, L);
      D.draw(M.fishTail[c.tex].gl, glm::rotate(glm::translate(base, vec3(0, 0, 0.12f)), std::sin(c.ph) * 0.5f, vec3(0, 1, 0)) * M.fishTail[c.tex].local, L);
    }
  }
}

} // namespace dq
