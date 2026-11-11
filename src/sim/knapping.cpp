// sim/knapping.cpp — see knapping.h
#include "sim/knapping.h"
#include "common/fs.h"
#include <unordered_map>

namespace dq {

const std::vector<KnapPattern>& knapPatterns(const std::string& material) {
  static const std::unordered_map<std::string, std::vector<KnapPattern>> all = [] {
    std::unordered_map<std::string, std::vector<KnapPattern>> out;
    auto j = readJson(dataDir() / "knapping.json");
    if (!j || !j->is_object()) return out;
    for (auto& [m, list] : j->items()) {
      if (!list.is_array()) continue;
      for (auto& p : list) {
        KnapPattern k; k.out = p.value("out", ""); k.n = p.value("n", 1); k.defOn = p.value("defOn", false);
        if (p.contains("rows")) for (auto& r : p["rows"]) k.rows.push_back(r.get<std::string>());
        size_t wmax = 0; for (auto& r : k.rows) wmax = std::max(wmax, r.size());
        for (auto& r : k.rows) r.resize(wmax, ' ');   // ragged rows: short ones are chipped at the end
        if (!k.out.empty() && !k.rows.empty()) out[m].push_back(k);
      }
    }
    return out;
  }();
  static const std::vector<KnapPattern> none;
  auto it = all.find(material);
  return it == all.end() ? none : it->second;
}

std::string knapMaterialOf(const std::string& id) {
  if (id == "pebble") return "stone";
  if (id == "flint") return "flint";
  if (id == "clay_ball") return "clay";
  if (id == "fire_clay") return "fireclay";
  return "";
}
int knapCost(const std::string& m) { return m == "clay" || m == "fireclay" ? 5 : 1; }
int knapNeeds(const std::string& m) { return m == "clay" || m == "fireclay" ? 5 : 2; }

const KnapPattern* knapMatch(const std::string& material, const KnapGrid& g) {
  for (const KnapPattern& p : knapPatterns(material)) {
    const int h = (int)p.rows.size(), w = (int)p.rows[0].size();
    if (h > 5 || w > 5) continue;
    for (int mir = 0; mir < 2; mir++) for (int oy = 0; oy + h <= 5; oy++) for (int ox = 0; ox + w <= 5; ox++) {
      bool ok = true;
      for (int y = 0; y < 5 && ok; y++) for (int x = 0; x < 5 && ok; x++) {
        const int px = x - ox, py = y - oy;
        const bool inBox = px >= 0 && px < w && py >= 0 && py < h;
        const bool want = inBox ? p.rows[py][mir ? w - 1 - px : px] == '#' : p.defOn;
        if (g.kept[y * 5 + x] != want) ok = false;
      }
      if (ok) return &p;
    }
  }
  return nullptr;
}

} // namespace dq
