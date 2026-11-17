// sim/lift.cpp — see lift.h.
#include "sim/lift.h"
#include "sim/moving.h"
#include "sim/glue.h"
#include "core/registry.h"
#include <algorithm>

namespace dq {

namespace {
bool isRail(const World& w, ivec3 p) { return reg().block(w.getBlock(p.x, p.y, p.z)).str("lift") == "lift_rail"; }
bool isSlider(const World& w, ivec3 p) { return reg().block(w.getBlock(p.x, p.y, p.z)).str("machine") == "slider"; }

void fill(const World& w, Lift& l) {
  l.len = 0; l.stops.clear();
  while (l.len < 256 && isRail(w, l.rail0 + l.dir * l.len)) l.len++;
  for (int k = 0; k < l.len; k++) {
    const ivec3 r = l.rail0 + l.dir * k;
    for (int f = 0; f < 6; f++) {
      const ivec3 d = faceDir(f);
      if (d == l.dir || d == -l.dir) continue;
      const ivec3 s = r + d;
      if (isLiftStop(w.getBlock(s.x, s.y, s.z)) && s != l.slider) l.stops.push_back({s, std::clamp(k - 1, 0, l.maxAt())});
    }
  }
  std::sort(l.stops.begin(), l.stops.end(), [](auto& a, auto& b) { return a.second < b.second; });
}
}

bool isLiftStop(BlockId id) { return reg().block(id).str("lift") == "lift_stop"; }

bool liftOfSlider(const World& w, ivec3 s, Lift& out) {
  if (!isSlider(w, s)) return false;
  const ivec3 dir = faceDir(machineFace(w, s));
  for (int f = 0; f < 6; f++) {
    const ivec3 d = faceDir(f);
    if (d == dir || d == -dir || !isRail(w, s + d)) continue;
    out = Lift(); out.slider = s; out.rail0 = s + d; out.dir = dir;
    fill(w, out);
    if (out.len >= 2) return true;
  }
  return false;
}

bool liftOfStop(const World& w, ivec3 stop, Lift& out, int* at) {
  for (int f = 0; f < 6; f++) {
    const ivec3 r = stop + faceDir(f);
    if (!isRail(w, r)) continue;
    // walk back along each line through this rail to its first block; a slider beside that, sliding this way, runs it
    for (int g = 0; g < 6; g++) {
      const ivec3 d = faceDir(g);
      if (d == faceDir(f) || d == -faceDir(f)) continue;
      ivec3 r0 = r; int k = 0;
      while (k < 256 && isRail(w, r0 - d)) { r0 -= d; k++; }
      for (int h = 0; h < 6; h++) {
        const ivec3 s = r0 + faceDir(h);
        if (!isSlider(w, s) || faceDir(machineFace(w, s)) != d || !liftOfSlider(w, s, out) || out.rail0 != r0) continue;
        if (at) *at = std::clamp(k - 1, 0, out.maxAt());
        return true;
      }
    }
  }
  return false;
}

int liftNext(const Lift& l, int at) {
  if (l.stops.empty()) return at < l.maxAt() ? l.maxAt() : 0;
  for (auto& [p, a] : l.stops) if (a > at) return a;
  return l.stops.front().second;
}

void liftMarkStops(World& w, const Lift& l, int at) {
  for (auto& [p, a] : l.stops) {
    const int m = w.getMeta(p.x, p.y, p.z), want = (m & ~1) | (a == at ? 1 : 0);
    if (m != want) w.setBlock(p.x, p.y, p.z, w.getBlock(p.x, p.y, p.z), want);
  }
}

} // namespace dq
