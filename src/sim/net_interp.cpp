// sim/net_interp.cpp — how a replica of another machine's entity moves on this screen (sim/entity.h): snapshot
// interpolation. Each place heard comes with the time it was true at its sender (on this computer's clock); the
// replica is drawn a little in the past, between two places it really was, so packets that come early, late, two at
// once or not at all don't turn into speeding up, stopping and lurching. How far in the past follows how late they
// have been coming (a calm link: ~70 ms; a jittery one: longer), changed slowly so the motion never jumps.
#include "sim/entity.h"
#include "sim/player.h"
#include "sim/attack_moves.h"
#include <algorithm>
#include <cmath>

namespace dq {

// With nothing heard, a replica goes on the way it was going: at full speed for NET_EXTRAPOLATE, then slowing (its speed
// halving every ~0.2 s) — a gap of 0.6 s on a lumpy link (a TCP relay) shows no stop, a real stop is hardly overshot.
static constexpr double NET_EXTRAPOLATE = 0.35, NET_COAST = 0.3, NET_COAST_MAX = 1.5;
static double coastTravel(double a) { return a <= NET_EXTRAPOLATE ? a : NET_EXTRAPOLATE + NET_COAST * (1 - std::exp(-(a - NET_EXTRAPOLATE) / NET_COAST)); }
static double coastSpeed(double a) { return a <= NET_EXTRAPOLATE ? 1 : std::exp(-(a - NET_EXTRAPOLATE) / NET_COAST); }
static float angLerp(float a, float b, float k) { return a + std::remainder(b - a, 2 * PI) * k; }

bool Entity::netEval(double rt, vec3& p, float& yaw, float& pitch, float& body, vec3* vel) const {
  const size_t n = netHist.size();
  if (n == 0) return false;
  if (vel) *vel = vec3(0);
  const NetSample& f = netHist.front();
  if (n == 1 || rt <= f.t) { p = f.p; yaw = f.yaw; pitch = f.pitch; body = f.body; return true; }
  const NetSample& b = netHist.back();
  if (rt >= b.t) {   // past the newest: on the way it was going for a moment (a late or lost packet), then wait there
    // the way it was going: over the last ~0.15 s heard (steadier than the last step alone)
    size_t ai = n - 2; while (ai > 0 && b.t - netHist[ai].t < 0.15) ai--;
    const NetSample& a = netHist[ai];
    p = b.p; yaw = b.yaw; pitch = b.pitch; body = b.body;
    const double span = b.t - a.t, ahead = std::min(rt - b.t, NET_COAST_MAX), go = coastTravel(ahead);
    const bool stopped = glm::distance(netHist[n - 2].p, b.p) < 1e-3f;   // (the sender says a stop twice: no coasting past it)
    if (!stopped && span > 1e-3 && span < 0.6) {
      const vec3 v = (b.p - a.p) / (float)span;
      const float k = (float)(go / span);
      p = b.p + v * (float)go;
      yaw = b.yaw + std::remainder(b.yaw - a.yaw, 2 * PI) * k; body = b.body + std::remainder(b.body - a.body, 2 * PI) * k;   // (turning on too)
      if (vel) *vel = v * (float)coastSpeed(ahead);
    }
    return true;
  }
  size_t i = 1;
  while (i < n && netHist[i].t <= rt) i++;
  const NetSample& a = netHist[i - 1]; const NetSample& c = netHist[i];
  const double span = std::max(1e-6, c.t - a.t);
  const float k = (float)((rt - a.t) / span);
  p = glm::mix(a.p, c.p, k);
  yaw = angLerp(a.yaw, c.yaw, k); pitch = angLerp(a.pitch, c.pitch, k); body = angLerp(a.body, c.body, k);
  if (vel) *vel = (c.p - a.p) / (float)span;
  return true;
}

void Entity::netSample(double t, double now, double off, vec3 p, float y, float pi, float b) {
  if (std::isfinite(off)) { netOffTarget = off; if (!netOffKnown || std::fabs(off - netOff) > 1.5) { netOff = off; netOffKnown = true; } }
  if (!std::isfinite(t) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
  if (!netHist.empty()) {
    if (t < netHist.back().t - 5 || t > netHist.back().t + 30) netReset();   // the sender's clock started over (a restart)
    else {
      // a teleport (a portal, a respawn, /tp): there at once, not slid across the map
      const NetSample& near = t >= netHist.back().t ? netHist.back() : netHist.front();
      if (glm::distance(near.p, p) > 8.f + 12.f * (float)std::fabs(t - near.t)) netReset();
    }
  }
  if (!netHist.empty()) {
    if (netHist.size() >= 2 && t < netHist.front().t) return;   // older than all we keep: no use now
    for (auto& s : netHist) if (std::fabs(s.t - t) < 1e-4) return;   // (heard twice)
  }
  // what is drawn now, before this place is known (a correction it makes is eased out, not jumped)
  vec3 before{0}; float yawBefore = 0, a0, b0; bool haveBefore = false;
  if (hasView && netLastRt >= 0) haveBefore = netEval(netLastRt, before, yawBefore, a0, b0);
  const bool newest = netHist.empty() || t > netHist.back().t;
  if (newest && !netHist.empty()) {
    const double gap = t - netHist.back().t;
    if (gap < 0.5) netGap += (std::clamp(gap, 0.02, 0.5) - netGap) * 0.1;
    // nothing heard for a while after it had stopped (the sender says a stop twice, then nothing while it stays put): it
    // set off only now, not slowly all that time. (Moving when last heard: packets were lost, and the way it was going
    // is the best guess for the gap.)
    const size_t n = netHist.size();
    const bool stood = n >= 2 && glm::distance(netHist[n - 1].p, netHist[n - 2].p) < 1e-3f;
    if (stood && gap > std::max(0.35, 3 * netGap)) { NetSample h = netHist.back(); h.t = t - netGap; netHist.push_back(h); }
  }
  NetSample s{t, p, y, pi, b};
  netHist.insert(std::upper_bound(netHist.begin(), netHist.end(), t, [](double v, const NetSample& q) { return v < q.t; }), s);
  // how late it came, beyond the quickest any has (the clock is set by the quickest): the delay needs to cover this
  const double age = std::max(0.0, now + netOffTarget - t);   // (on the sender's clock: how long ago it was true)
  {   // (a smoothed mean and spread: one hitch, a frame of ours that took long, moves it only a little)
    const double a = std::min(age, 0.6);
    if (netAgeMean < 0) netAgeMean = a;
    const double err = a - netAgeMean;
    netAgeMean += err * 0.05; netAgeDev += (std::fabs(err) - netAgeDev) * 0.05;
  }
  // forget what's long drawn (keeping two before the time drawn)
  if (netLastRt >= 0) {
    size_t drop = 0;
    while (drop + 2 < netHist.size() && netHist[drop + 1].t < netLastRt - 0.5) drop++;
    if (drop) netHist.erase(netHist.begin(), netHist.begin() + drop);
  }
  if (netHist.size() > 40) netHist.erase(netHist.begin(), netHist.begin() + (netHist.size() - 40));
  if (haveBefore) {
    vec3 after; float yawAfter, c, d;
    if (netEval(netLastRt, after, yawAfter, c, d)) {
      netCorr += before - after;
      netCorrYaw = std::remainder(netCorrYaw + std::remainder(yawBefore - yawAfter, 2 * PI), 2 * PI);
      if (glm::length(netCorr) > 3.f) netCorr = vec3(0);   // (too far to ease: there at once)
    }
  }
}

void Entity::netUpdateView(double now) {
  if (netHist.empty()) { hasView = false; return; }
  const bool first = netViewAt < 0;
  const double dt = first ? 0.0 : std::clamp(now - netViewAt, 0.0, 0.25);
  netViewAt = now;
  const double want = std::clamp(netGap + std::max(0.0, netAgeMean) + 2.5 * netAgeDev + 0.01, 0.05, 0.75);   // (late by up to about the mean and 2.5 spreads: ~1 in 50 comes too late and is bridged)
  if (first) netDelay = want;
  else if (want > netDelay) netDelay = std::min(want, netDelay + dt * 0.3);   // (played at 70% speed meanwhile: no jump)
  else netDelay = std::max(want, netDelay - dt * 0.04);
  // the sender's clock as drawn by: eased toward the newest estimate (at most 0.1 s a second: never a jump)
  if (first || std::fabs(netOffTarget - netOff) > 1.5) netOff = netOffTarget;
  else netOff += std::clamp(netOffTarget - netOff, -0.1 * dt, 0.1 * dt);
  double rt = now + netOff - netDelay;
  if (netLastRt >= 0 && rt < netLastRt) rt = netLastRt;   // never backwards
  vec3 p; float y, pi, b;
  netEval(rt, p, y, pi, b, &viewVel);
  const float fade = (float)std::exp(-dt / 0.2);   // (a correction blended in over ~0.2 s)
  netCorr *= fade; netCorrYaw *= fade;
  if (glm::dot(netCorr, netCorr) < 1e-8f) netCorr = vec3(0);
  viewPos = p + netCorr; viewYaw = std::remainder(y + netCorrYaw, 2 * PI); viewPitch = pi; viewBody = std::remainder(b + netCorrYaw, 2 * PI);
  hasView = true;
  netLastRt = rt;
}

void remoteSwingStep(Player& p, float dt, float rate) {
  if (!p.swingKnown) { p.swingKnown = true; p.swingSeen = p.swingN; }
  u8 fresh = (u8)(p.swingN - p.swingSeen);
  if (fresh >= 128) fresh = 0;       // (an older count: news overtaken)
  else { p.swingSeen = p.swingN; if (fresh > 4) fresh = 1; }   // (a long gap: not a backlog of swings to play)
  p.swingQueue = (u8)std::min(2, p.swingQueue + fresh);
  const bool digging = p.mineP > 0 && !p.dead;
  if (p.swingT > 0) {
    p.swingT += dt * rate;
    if (p.swingT >= 1) {
      if (digging) { p.swingT = std::max(0.0001f, p.swingT - std::floor(p.swingT)); p.swingQueue = 0; }   // (digging: the cycle goes on, its phase kept)
      else if (p.swingQueue) { p.swingT = std::max(0.0001f, p.swingT - std::floor(p.swingT)); p.swingQueue--; }   // (the next one, straight after)
      else p.swingT = 0;
    }
  } else if (p.swingQueue || digging) { p.swingT = 0.0001f; if (p.swingQueue) p.swingQueue--; }
}

void Entity::glide(double dt) {
  if (!netHist.empty()) {
    netUpdateView(nowSeconds());
    pos = viewPos; yaw = viewYaw; pitch = viewPitch; bodyYaw = viewBody;
    return;
  }
  // no timed samples (an older sender): eased toward the newest place
  const float k = std::min(1.f, (float)dt * 14.f);
  if (glm::distance(netPos, pos) > 8) pos = netPos; else pos += (netPos - pos) * k;
  yaw += std::remainder(netYaw - yaw, 2 * PI) * k;
}


void advanceReplicaView(const Player& body, Player& v, float dt) {
  v.id = body.id; v.type = body.type; v.realm = body.realm; v.ghost = true; v.riding = body.riding; v.removed = body.removed;
  if (v.atkMove) { v.atkT += dt; if (v.atkMove >= MV_COUNT || v.atkT >= attackMove(v.atkMove).len) { v.atkMove = MV_NONE; v.atkT = 0; } }
  auto down = [dt](float& t) { if (t > 0) t = std::max(0.f, t - dt); };
  down(v.dodgeT); down(v.parryFlash); down(v.guardFlash); down(v.hurtTime);
  if (!v.data.is_object()) v.data = json::object();
  if (body.data.is_object())
    for (const char* k : {"hideGear", "customCape", "sleeping", "rodOut", "invisible", "fx", "bedHead", "bedFrom", "bedMoved", "sleepT", "cape", "hook"}) {
      auto it = body.data.find(k);
      if (it != body.data.end()) v.data[k] = *it; else v.data.erase(k);
    }
  v.skin = body.skin; v.name = body.name;
}
} // namespace dq
