// sim/seamless.cpp — the stacked worlds' edges (see seamless.h).
#include "sim/seamless.h"

namespace dq {

namespace {
const char* TOAST_FALL = "Down through the clouds, into the world below\xE2\x80\xA6";
const char* TOAST_CRUST = "You break through the crust and fall into the Ember Depths!";
const char* TOAST_RISE_WIND = "Up through the clouds \xE2\x80\x94 the winds carry you to the Skylands";
const char* TOAST_RISE = "Up through the clouds, into the Skylands";
const char* TOAST_VOID = "Through the void crust\xE2\x80\xA6 and out into the sky of The End. The void winds carry you down";
const char* TOAST_UNVOID = "Up out of the End, through the void crust, into the Ember Depths";
}

float SeamlessSystem::cloudAt(double y) {
  int d = dimOfY(clampv((int)std::floor(y), 0, WORLD_H - 1));
  float ly = (float)(y - dimBase(d)), k = 0;
  if (d == DIM_AETHER) k = smoothstepf(30, 0, ly);
  else if (d == DIM_OVERWORLD) k = smoothstepf(DIM_H - 22, DIM_H, ly);
  return k > 0 ? smoothstepf(0.15f, 0.85f, k) * 0.93f : 0.f;
}

// the nearest island surface to (x, z), worked out from the Skylands' own shape (worldupdate.js)
vec3 SeamlessSystem::skyLanding(double x, double z) const {
  DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_AETHER) : nullptr;
  for (int r = 0; g && r <= 160; r += 6) {
    int n = std::max(1, r / 3);
    for (int a = 0; a < n; a++) {
      float ang = (float)a / n * TAU;
      int wx = ifloor(x + std::cos(ang) * r), wz = ifloor(z + std::sin(ang) * r);
      ColumnSample s; g->sample(wx, wz, s);
      if (s.height < 0) continue;
      return vec3(wx + 0.5f, (float)(dimBase(DIM_AETHER) + s.height + 2), wz + 0.5f);
    }
  }
  return vec3((float)x, (float)(dimBase(DIM_AETHER) + 180), (float)z);
}

vec3 SeamlessSystem::endLanding(double x, double z) const {
  DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_END) : nullptr;
  for (int r = 0; g && r <= 200; r += 5) {
    int n = std::max(1, r / 3);
    for (int a = 0; a < n; a++) {
      float ang = (float)a / n * TAU;
      int wx = ifloor(x + std::cos(ang) * r), wz = ifloor(z + std::sin(ang) * r);
      ColumnSample s; g->sample(wx, wz, s);
      if (s.height >= 0 && (s.floatBottom < 0 || s.height - s.floatBottom >= 3)) return vec3(wx + 0.5f, (float)(dimBase(DIM_END) + s.height + 1), wz + 0.5f);
    }
  }
  ivec3 P = endPlatform();
  return vec3(P.x + 0.5f, P.y + 1.f, P.z + 0.5f);
}

void SeamlessSystem::firstVisit(Player& p, const char* flag, int xp) {
  json& f = lv.info.data["flags"];
  if (!f.is_object()) f = json::object();
  if (f.value(flag, false)) return;
  f[flag] = true;
  p.xp += xp;   // grantXP (levels are worked out by the gameplay system)
}

void SeamlessSystem::puff(State& s, int slot, float rate, double dt, const char* kind, vec3 pos, const json& extra) {
  s.acc[slot] += rate * (float)dt;
  if (s.acc[slot] < 1) return;
  int n = (int)s.acc[slot];
  s.acc[slot] -= n;
  lv.fx->particles(kind, pos, n, extra);
}

// Rising out of the Overworld on foot: the winds carry you to an island. They take you along under the
// islands to clear sky beside one, straight up past its top, then across onto it.
void SeamlessSystem::startUpdraft(Player& p, State& s) {
  vec3 L = skyLanding(p.pos.x, p.pos.z);
  DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_AETHER) : nullptr;
  auto open = [&](int cx, int cz) { if (!g) return true; ColumnSample c; g->sample(cx, cz, c); return c.height < 0; };
  bool found = false; vec3 col(0);
  if (std::hypot(L.x - p.pos.x, L.z - p.pos.z) < 40 && open(ifloor(p.pos.x), ifloor(p.pos.z))) { col = p.pos; found = true; }
  for (int r = 2; r <= 40 && !found; r += 2) {
    int n = std::max(8, (int)std::round(r * 1.6f));
    for (int a = 0; a < n; a++) {
      float ang = (float)a / n * TAU;
      int cx = ifloor(L.x + std::cos(ang) * r), cz = ifloor(L.z + std::sin(ang) * r);
      if (open(cx, cz)) { col = vec3(cx + 0.5f, 0, cz + 0.5f); found = true; break; }
    }
  }
  if (!found) col = vec3(L.x + 30, 0, L.z);
  s.updraft = 0; s.upCol = col; s.upTo = L;
}

void SeamlessSystem::crossed(Player& p, State& s, int from, int to) {
  u32 id = p.id;
  if (from == DIM_AETHER && to == DIM_OVERWORLD) {
    s.slowFallT = 40; s.cloudFall = true; p.vel.y = std::max(p.vel.y, -6.f);
    lv.fx->message(id, TOAST_FALL, "");
  } else if (from == DIM_OVERWORLD && to == DIM_AETHER) {
    p.vel.y = std::max(p.vel.y, 2.f);
    if (!p.flying && !p.riding) startUpdraft(p, s);
    firstVisit(p, "skyVisited", 40);
    lv.fx->message(id, s.updraft >= 0 ? TOAST_RISE_WIND : TOAST_RISE, "good");
  } else if (from == DIM_OVERWORLD && to == DIM_NETHER) {
    firstVisit(p, "depthsVisited", 40);
    lv.fx->message(id, TOAST_CRUST, "warn");
  } else if (from == DIM_NETHER && to == DIM_END) {
    s.slowFallT = 90; s.cloudFall = false;
    p.vel.x *= 0.3f; p.vel.z *= 0.3f; p.vel.y = std::min(p.vel.y, -2.f);
    s.drift = true; s.driftTo = endLanding(p.pos.x, p.pos.z);
    firstVisit(p, "endVisited", 120);
    lv.fx->message(id, TOAST_VOID, "epic");
  } else if (from == DIM_END && to == DIM_NETHER) {
    lv.fx->message(id, TOAST_UNVOID, "warn");
  }
}

void SeamlessSystem::arrived(Entity& e, PortalKind kind, int fromDim, int toDim) {
  State& s = st[e.id];
  s.init = true; s.lastPos = e.pos; s.lastRealm = e.realm; s.lastVy = e.vel.y;
  s.updraft = -1; s.drift = false;
  if (!e.isPlayer()) return;
  Player& p = static_cast<Player&>(e);
  if (toDim == DIM_AETHER && e.realm == REALM_MAIN) firstVisit(p, "skyVisited", 40);
  if (toDim == DIM_NETHER && e.realm == REALM_MAIN) firstVisit(p, "depthsVisited", 40);
  if (kind == PortalKind::End) {
    // (theend.js pvArrived) the winds of the portal set you down gently
    s.slowFallT = std::max(s.slowFallT, 40.f); s.cloudFall = false;
    if (toDim == DIM_END) {
      ivec3 P = endPlatform();
      s.drift = true; s.driftTo = vec3(P.x + 0.5f, P.y + 1.f, P.z + 0.5f);
      firstVisit(p, "endVisited", 120);
      lv.fx->message(p.id, "The End", "epic");
    } else if (toDim == DIM_OVERWORLD) lv.fx->message(p.id, "Back to the Overworld \xE2\x80\x94 the winds of the portal set you down gently", "good");
  }
  if (fromDim != toDim && lv.events) lv.events->emit("dimensionChange", json::array({p.id, p.name, fromDim, toDim, e.realm, e.realm}));
}

void SeamlessSystem::tick(Level& l, double dt) {
  const Registry& R = reg();
  PortalSystem* ps = portalSystem(l);
  for (size_t i = 0; i < l.entities().size(); i++) {
    Entity* e = l.entities()[i].get();
    if (e->removed || !e->isPlayer()) continue;
    Player& p = static_cast<Player&>(*e);
    State& s = st[p.id];
    if (!s.init || s.lastRealm != p.realm) {
      if (s.init && l.events) l.events->emit("dimensionChange", json::array({p.id, p.name, dimOfY(ifloor(s.lastPos.y)), dimOfY(ifloor(p.pos.y)), s.lastRealm, p.realm}));
      s = State{}; s.init = true; s.lastPos = p.pos; s.lastRealm = p.realm; s.lastVy = p.vel.y;
      continue;
    }
    if (p.dead) { s.lastPos = p.pos; continue; }
    bool main = p.realm == REALM_MAIN;
    World& W = l.world(p.realm);
    int d0 = dimOfY(ifloor(s.lastPos.y)), d1 = dimOfY(ifloor(p.pos.y));
    bool jumped = glm::distance(s.lastPos, p.pos) > 6;   // a portal or a teleport, not a crossing
    if (main && d0 != d1) {
      if (!jumped) crossed(p, s, d0, d1);
      if (l.events) l.events->emit("dimensionChange", json::array({p.id, p.name, d0, d1, p.realm, p.realm}));
    }
    int ix = ifloor(p.pos.x), iz = ifloor(p.pos.z);
    if (main) {
      // down through the crust: standing on the Depths' roof where the Overworld's floor was broken,
      // a shaft opens through the roof to the first open cave below
      int owBase = dimBase(DIM_OVERWORLD);
      if (d1 == DIM_OVERWORLD && p.pos.y < owBase + 0.5f && p.onGround && R.tSolid[W.getBlock(ix, owBase - 1, iz)]) {
        int top = owBase - 1;
        for (int y = top; y > dimBase(DIM_NETHER) + 40; y--) {
          if (y < top - 2 && !R.tSolid[W.getBlock(ix, y, iz)] && !R.tSolid[W.getBlock(ix, y - 1, iz)] && !R.tLiquid[W.getBlock(ix, y, iz)]) break;
          if (W.getBlock(ix, y, iz) != AIR) W.setBlock(ix, y, iz, AIR, 0, SetFlags::Urgent);
        }
        p.pos.x = ix + 0.5f; p.pos.z = iz + 0.5f; p.vel.x = p.vel.z = 0; p.vel.y = std::min(p.vel.y, -2.f); p.onGround = false;
      }
      // up out of the End: a pocket opens in the Depths' floor
      int nb = dimBase(DIM_NETHER);
      if (d1 == DIM_END && p.pos.y + p.h > nb - 0.3f && (p.vel.y > 0 || s.lastVy > 0 || p.flying)) {
        BlockId crust = R.blockId("void_crust");
        for (int y = 1; y <= 3; y++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
          BlockId id = W.getBlock(ix + dx, nb + y, iz + dz);
          if (id && !R.tLiquid[id]) W.setBlock(ix + dx, nb + y, iz + dz, AIR, 0, SetFlags::Urgent);
        }
        if (crust && !R.tSolid[W.getBlock(ix, nb, iz)]) W.setBlock(ix, nb, iz, crust, 0, SetFlags::Urgent);
        p.pos = vec3(ix + 0.5f, nb + 1.05f, iz + 0.5f); p.vel = vec3(0); p.fallDist = 0;
        crossed(p, s, DIM_END, DIM_NETHER);
        if (l.events) l.events->emit("dimensionChange", json::array({p.id, p.name, DIM_END, DIM_NETHER, p.realm, p.realm}));
        d1 = DIM_NETHER;
      }
      // the cloud bank under the Skylands slows a fall, so you come out the other side gently
      if (d1 == DIM_AETHER && p.pos.y - dimBase(DIM_AETHER) < 30 && p.vel.y < 0 && !p.flying && !p.riding && cloudAt(p.pos.y) > 0.05f) {
        s.slowFallT = std::max(s.slowFallT, 2.f); s.cloudFall = true;
      }
    }
    // the winds that carry you down from the sky (worldupdate.js)
    if (s.slowFallT > 0) {
      s.slowFallT -= (float)dt; p.fallDist = 0;
      if (s.cloudFall && !(cloudAt(p.pos.y) > 0.05f)) { s.slowFallT = 0; s.cloudFall = false; }
      if (p.vel.y < -5) { p.vel.y += (-5 - p.vel.y) * std::min(1.f, (float)dt * 5); if (p.vel.y > -7) p.vel.y = -5; }
      puff(s, 0, 12, dt, d1 == DIM_NETHER ? "flame" : "white", p.pos + vec3(0, 0.5f, 0));
      if (p.onGround) s.slowFallT = 0;
    }
    // the updraft: along under the islands, straight up, then across onto one
    if (s.updraft >= 0) {
      if (!main || d1 != DIM_AETHER || p.riding || p.flying) s.updraft = -1;
      else {
        p.fallDist = 0;
        float ab = (float)dimBase(DIM_AETHER);
        if (s.updraft == 0) {
          float dx = s.upCol.x - p.pos.x, dz = s.upCol.z - p.pos.z, d = std::hypot(dx, dz);
          if (d < 0.6f) s.updraft = 1;
          else { float sp = std::min(14.f, d * 2); p.vel.x = dx / d * sp; p.vel.z = dz / d * sp; p.vel.y = clampv((ab + 6 - p.pos.y) * 2, -2.f, 4.f); }
        } else if (s.updraft == 1) {
          p.vel.x = (s.upCol.x - p.pos.x) * 3; p.vel.z = (s.upCol.z - p.pos.z) * 3; p.vel.y = 14;
          if (p.pos.y > s.upTo.y + 2) s.updraft = 2;
        } else {
          float dx = s.upTo.x - p.pos.x, dz = s.upTo.z - p.pos.z, d = std::hypot(dx, dz);
          p.vel.y = clampv((s.upTo.y + 2.5f - p.pos.y) * 2, -3.f, 3.f);
          if (d < 0.8f || p.onGround) { s.updraft = -1; s.slowFallT = 8; }
          else { float sp = std::min(9.f, d * 2); p.vel.x = dx / d * sp; p.vel.z = dz / d * sp; }
        }
        puff(s, 1, 30, dt, "cloud", p.pos + vec3(0, 1, 0), json{{"spread", 0.8}, {"vel", {-p.vel.x * 0.2f, -p.vel.y * 0.25f, -p.vel.z * 0.2f}}, {"life", 0.8}, {"size", 0.35}, {"grow", 1.5}});
      }
    }
    // the void winds: down gently and across onto the nearest island (theend.js)
    if (s.drift) {
      if (!main || d1 != DIM_END || p.flying || p.riding) s.drift = false;
      else {
        float dx = s.driftTo.x - p.pos.x, dz = s.driftTo.z - p.pos.z, d = std::hypot(dx, dz);
        p.fallDist = 0; s.slowFallT = std::max(s.slowFallT, 1.f);
        if (d > 0.8f) { float sp = std::min(10.f, d * 1.5f); p.vel.x = dx / d * sp; p.vel.z = dz / d * sp; }
        if (d > 4 && p.pos.y < s.driftTo.y + 3) p.vel.y = std::max(p.vel.y, 0.f);
        if (p.onGround) { s.drift = false; s.slowFallT = 0; }
        puff(s, 1, 20, dt, "end_mote", p.pos + vec3(0, 0.6f, 0));
      }
    }
    // wisps of cloud streaming past in the bank between the worlds
    if (main) {
      float k = cloudAt(p.pos.y);
      if (k > 0.05f) puff(s, 2, 60 * k, dt, "cloud", p.pos + vec3(0, 1, 0), json{{"ring", {2, 12}}, {"spreadY", 8}, {"life", 1.6}, {"size", 1.5}, {"grow", 0.6}});
    }
    // the void below everything
    float voidY = main ? -32.f : (float)dimBase(DIM_OVERWORLD) - 32.f;
    if (p.pos.y < voidY && ps && ps->hurt) ps->hurt(p, 4, "void");
    s.lastPos = p.pos; s.lastVy = p.vel.y;
  }
  if (st.size() > l.entities().size() + 32)
    for (auto it = st.begin(); it != st.end();) it = l.entity(it->first) ? std::next(it) : st.erase(it);
}

void registerSeamless() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return std::make_shared<SeamlessSystem>(lv); });
}

} // namespace dq
