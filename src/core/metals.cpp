// core/metals.cpp — see metals.h
#include "core/metals.h"
#include "common/fs.h"
#include <mutex>
#include <unordered_map>
#include <cstdio>

namespace dq {

namespace {
struct Tables {
  std::unordered_map<std::string, Metal> all;
  std::vector<const Metal*> base, alloys;
  std::unordered_map<std::string, std::pair<std::string, int>> ores;   // item -> metal, units
  std::unordered_map<std::string, std::string> ingots;                 // ingot item -> metal
  std::unordered_map<std::string, std::string> oreBlocks;              // ore block -> metal
};
const Tables& T() {
  static Tables t;
  static std::once_flag once;
  std::call_once(once, [] {
    auto j = readJson(dataDir() / "metals.json");
    if (!j || !j->is_object()) return;
    auto read = [&](const std::string& k, const json& m) {
      Metal x; x.key = k; x.name = m.value("name", k); x.ingot = m.value("ingot", ""); x.melt = m.value("melt", 1000.f);
      if (m["rgb"].is_array() && m["rgb"].size() >= 3) x.rgb = vec3(m["rgb"][0].get<float>(), m["rgb"][1].get<float>(), m["rgb"][2].get<float>()) / 255.f;
      x.tier = m.value("tier", 1); x.anvil = m.value("anvil", 0); x.speed = m.value("speed", 4.f); x.damage = m.value("damage", 3.f); x.durability = m.value("durability", 100.f);
      x.cast = m.value("cast", false);
      if (m.contains("ranges")) for (auto& [mk, r] : m["ranges"].items()) x.ranges[mk] = {r[0].get<float>(), r[1].get<float>()};
      return x;
    };
    for (auto& [k, m] : (*j)["metals"].items()) t.all[k] = read(k, m);
    for (auto& [k, m] : (*j)["alloys"].items()) t.all[k] = read(k, m);
    for (auto& [k, m] : t.all) { (m.ranges.empty() ? t.base : t.alloys).push_back(&m); if (!m.ingot.empty()) t.ingots[m.ingot] = k; }
    auto byKey = [](const Metal* a, const Metal* b) { return a->key < b->key; };
    std::sort(t.base.begin(), t.base.end(), byKey); std::sort(t.alloys.begin(), t.alloys.end(), byKey);
    for (auto& [k, o] : (*j)["ores"].items()) t.ores[k] = {o.value("metal", ""), o.value("u", 0)};
    if ((*j).contains("oreBlocks")) for (auto& [k, m] : (*j)["oreBlocks"].items()) t.oreBlocks[k] = m.get<std::string>();
  });
  return t;
}
std::string cap(std::string s) { if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]); return s; }
}

const Metal* metalDef(const std::string& k) { auto it = T().all.find(k); return it == T().all.end() ? nullptr : &it->second; }
const std::vector<const Metal*>& baseMetals() { return T().base; }
const std::vector<const Metal*>& namedAlloys() { return T().alloys; }

Comp normComp(Comp c) {
  float sum = 0; for (auto& [k, v] : c) sum += std::max(0.f, v);
  Comp out;
  if (sum <= 0) return out;
  for (auto& [k, v] : c) if (v / sum >= 0.005f) out[k] = v / sum;
  float s2 = 0; for (auto& [k, v] : out) s2 += v;
  for (auto& [k, v] : out) v /= s2;
  return out;
}
std::string compKey(const Comp& c0) {
  const Comp c = normComp(c0);
  std::string k = "@";
  for (auto& [m, v] : c) { char b[64]; std::snprintf(b, sizeof b, "%s%s=%.2f", k.size() > 1 ? "," : "", m.c_str(), v); k += b; }
  return k;
}
Comp parseCompKey(const std::string& key) {
  Comp c;
  if (key.empty() || key[0] != '@') return c;
  size_t i = 1;
  while (i < key.size()) {
    const size_t e = key.find(',', i), eq = key.find('=', i);
    if (eq == std::string::npos) break;
    c[key.substr(i, eq - i)] = std::strtof(key.substr(eq + 1, (e == std::string::npos ? key.size() : e) - eq - 1).c_str(), nullptr);
    if (e == std::string::npos) break;
    i = e + 1;
  }
  return normComp(c);
}
std::string namedAlloy(const Comp& c0) {
  const Comp c = normComp(c0);
  for (auto& [k, v] : c) if (v >= 0.98f) return k;
  for (const Metal* a : namedAlloys()) {
    bool ok = c.size() == a->ranges.size();
    for (auto& [m, r] : a->ranges) { auto it = c.find(m); if (it == c.end() || it->second < r.first - 0.005f || it->second > r.second + 0.005f) { ok = false; break; } }
    if (ok) return a->key;
  }
  return "";
}
Metal blendMetal(const Comp& c0) {
  const Comp c = normComp(c0);
  if (const std::string n = namedAlloy(c); !n.empty()) if (const Metal* m = metalDef(n)) return *m;
  Metal x; x.key = compKey(c); x.name = compName(c); x.melt = 0; x.rgb = vec3(0); x.speed = x.damage = x.durability = 0; x.tier = 99; x.anvil = 0; x.cast = true;
  float big = 0; int many = 0;
  for (auto& [k, v] : c) {
    const Metal* m = metalDef(k);
    if (!m) continue;
    x.melt += m->melt * v; x.rgb += m->rgb * v; x.speed += m->speed * v; x.damage += m->damage * v; x.durability += m->durability * v;
    x.tier = std::min(x.tier, m->tier); x.anvil = std::max(x.anvil, m->anvil); x.cast = x.cast && m->cast;
    big = std::max(big, v); if (v >= 0.05f) many++;
  }
  if (x.tier == 99) x.tier = 1;
  x.durability *= (1 + 0.05f * std::max(0, many - 1)) * (big < 0.8f ? 0.85f : 1.f);
  return x;
}
std::string compName(const Comp& c0) {
  const Comp c = normComp(c0);
  if (const std::string n = namedAlloy(c); !n.empty()) if (const Metal* m = metalDef(n)) return m->name;
  std::vector<std::pair<float, std::string>> v; for (auto& [k, f] : c) v.push_back({f, k});
  std::sort(v.rbegin(), v.rend());
  std::string s;
  for (auto& [f, k] : v) { const Metal* m = metalDef(k); s += (s.empty() ? "" : "-") + (m ? m->name : cap(k)); }
  return s.empty() ? "Metal" : s + " Alloy";
}
std::string metalOfItem(const std::string& id) {
  if (auto it = T().ingots.find(id); it != T().ingots.end()) return it->second;
  if (auto it = T().ores.find(id); it != T().ores.end()) return it->second.first;
  return "";
}
Comp compOfStack(const ItemStack& s) {
  if (s.tag.is_object()) if (auto it = s.tag.find("comp"); it != s.tag.end() && it->is_object()) {
    Comp c; for (auto& [k, v] : it->items()) if (v.is_number()) c[k] = v.get<float>();
    return normComp(c);
  }
  const std::string m = metalOfItem(s.id);
  if (m.empty()) return {};
  const Metal* d = metalDef(m);
  if (d && !d->ranges.empty()) {   // a named alloy's ingot: the middle of its ranges
    Comp c; for (auto& [k, r] : d->ranges) c[k] = (r.first + r.second) / 2;
    return normComp(c);
  }
  return {{m, 1.f}};
}
std::string metalOfOreBlock(const std::string& k) { auto it = T().oreBlocks.find(k); return it == T().oreBlocks.end() ? "" : it->second; }
int unitsOf(const std::string& id) {
  if (auto it = T().ores.find(id); it != T().ores.end()) return it->second.second;
  if (T().ingots.count(id) || id == "alloy_ingot") return 100;
  auto ends = [&](const char* e) { const size_t n = std::strlen(e); return id.size() >= n && id.compare(id.size() - n, n, e) == 0; };
  if (ends("_double_sheet")) return 400;   // (before "_sheet", which it also ends with)
  if (ends("_double_ingot") || ends("_sheet")) return 200;
  return 0;
}
std::string ingotFor(const Comp& c) {
  const std::string n = namedAlloy(c);
  if (const Metal* m = n.empty() ? nullptr : metalDef(n); m && !m->ingot.empty()) return m->ingot;
  return "alloy_ingot";
}

} // namespace dq
