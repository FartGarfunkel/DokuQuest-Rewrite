// sim/prospect.cpp — see prospect.h
#include "sim/prospect.h"
#include "core/metals.h"
#include "core/registry.h"
#include <map>

namespace dq {

std::string prospect(World& w, int x, int y, int z) {
  std::map<std::string, int> n;
  const Registry& R = reg();
  const int r = PROSPECT_RANGE;
  for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
    const BlockId b = w.getBlock(x + dx, y + dy, z + dz);
    if (!b) continue;
    const std::string& k = R.block(b).key;
    if (k.size() < 4 || k.compare(k.size() - 4, 4, "_ore") != 0) continue;
    const std::string m = metalOfOreBlock(k);
    const Metal* md = m.empty() ? nullptr : metalDef(m);
    std::string name = md ? md->name : R.block(b).name;
    if (!md && name.size() > 4 && name.compare(name.size() - 4, 4, " Ore") == 0) name = name.substr(0, name.size() - 4);
    n[name]++;
  }
  if (n.empty()) return "Nothing of note nearby";
  auto best = std::max_element(n.begin(), n.end(), [](auto& a, auto& b) { return a.second < b.second; });
  const int c = best->second;
  const char* how = c < 10 ? "Traces of " : c < 40 ? "A small amount of " : c < 120 ? "A large amount of " : "A very large amount of ";
  return std::string(how) + best->first + " nearby";
}

} // namespace dq
