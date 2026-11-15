// gen/floating_land.cpp — see floating_land.h
#include "gen/floating_land.h"
#include "gen/density.h"
#include <unordered_set>
#include <vector>

namespace dq {

double FloatingLand::value(int lx, int j, int lz) const {
  const u64 k = key(lx, j, lz);
  Shard& s = shard(k);
  { std::lock_guard<std::mutex> lk(s.m); if (auto it = s.v.find(k); it != s.v.end()) return it->second; }
  const double v = g_.eval(field_, lx * 4, j * 8 - 1, lz * 4);
  std::lock_guard<std::mutex> lk(s.m);
  if (s.v.size() > (1u << 20)) { s.v.clear(); s.f.clear(); }   // (a long journey: start the memory again)
  s.v.emplace(k, v);
  return v;
}

bool FloatingLand::floating(int lx, int j, int lz) const {
  if (j < 0 || j >= NY || value(lx, j, lz) <= 0) return false;
  if (j * 8 <= GROUND_Y) return false;
  { const u64 k = key(lx, j, lz); Shard& s = shard(k); std::lock_guard<std::mutex> lk(s.m); if (auto it = s.f.find(k); it != s.f.end()) return it->second == 1; }
  // the piece this corner is in, walked through its positive corners, downward first: it is grounded as soon as it reaches
  // the ground or spreads wider than REACH; a scrap is walked whole and found wanting
  struct C { int x, j, z; };
  std::vector<C> stack{{lx, j, lz}};
  std::unordered_set<u64> seen{key(lx, j, lz)};
  std::vector<u64> order{key(lx, j, lz)};
  int x0 = lx, x1 = lx, z0 = lz, z1 = lz;
  bool grounded = false;
  while (!stack.empty() && !grounded) {
    const C c = stack.back(); stack.pop_back();
    x0 = std::min(x0, c.x); x1 = std::max(x1, c.x); z0 = std::min(z0, c.z); z1 = std::max(z1, c.z);
    if (c.j * 8 <= GROUND_Y || x1 - x0 > 2 * REACH || z1 - z0 > 2 * REACH || seen.size() > 30000) { grounded = true; break; }
    // (pushed so the one below is taken first: a mountain finds its ground at once)
    static const int N[6][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
    for (auto& n : N) {
      const C d{c.x + n[0], c.j + n[1], c.z + n[2]};
      if (d.j < 0 || d.j >= NY) continue;
      const u64 k = key(d.x, d.j, d.z);
      if (seen.count(k) || value(d.x, d.j, d.z) <= 0) continue;
      seen.insert(k); order.push_back(k); stack.push_back(d);
    }
  }
  const u8 r = grounded ? 2 : 1;
  for (u64 k : order) { Shard& s = shard(k); std::lock_guard<std::mutex> lk(s.m); s.f[k] = r; }
  return !grounded;
}

} // namespace dq
