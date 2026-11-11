// ui/item_tooltip_extra.cpp — the lines enchanting.js, brewing.js and skills.js add to itemInfoHTML:
// enchantments under the name (a book also says what each does), a potion's effects, and a tool / weapon /
// armour piece's level with its progress bar toward the next one ("\x02<fraction>" is the bar).
#include "sim/heat.h"
#include "ui/ui.h"
#include "sim/enchanting.h"
#include "sim/brewing.h"
#include "sim/skills.h"
#include "sim/magic.h"
#include "core/item_glint.h"
#include <cmath>
#include <cstdio>
#include "sim/quiver.h"
#include <map>

namespace dq {

namespace {
const u32 TS = 0x9fd0ff, TD = 0xb8a98f, TEN = 0xc9a6ff;   // styles.css .ts, .td, .ts.en
}

bool tooltipEnchantLines(const ItemStack& s0, std::vector<std::pair<std::string, u32>>& out) {
  ItemStack s = s0;
  json en = enchOf(s);
  bool any = false;
  if (en.is_object()) for (auto& [k, v] : en.items()) {
    const EnchDef* E = magicData().enchant(k);
    if (!E || !v.is_number()) continue;
    out.push_back({enchName(k, v.get<int>()) + (s.id == "enchanted_book" ? " \xC2\xB7 " + E->desc : std::string()), TEN});
    any = true;
  }
  return any || itemGlints(s0);
}

void tooltipEffectLines(const ItemStack& s, std::vector<std::pair<std::string, u32>>& out) {
  std::string pt = potionTooltip(s.id);
  size_t a = 0;
  while (!pt.empty() && a <= pt.size()) {
    size_t b = pt.find('\n', a); if (b == std::string::npos) b = pt.size();
    if (b > a) out.push_back({pt.substr(a, b - a), TS});
    a = b + 1;
  }
}

void tooltipGearLines(const ItemStack& s, std::vector<std::pair<std::string, u32>>& out) {
  const ItemDef* d = s.def();
  if (const float h = itemHeat(s); h > 25) {   // sim/heat.h: TFC's names for the heat
    const std::string n = heatName(h);
    out.push_back({(n.empty() ? std::string("Warm") : n) + " (" + std::to_string((int)h) + "\xC2\xB0" "C)", heatGlow(h) ? 0xFF000000u | ((heatGlow(h) & 0xFF) << 16) | (heatGlow(h) & 0xFF00) | (heatGlow(h) >> 16) : TS});
  }
  if (s.id == "quiver") {   // sim/quiver.cpp: what's in it, kind by kind
    std::map<std::string, int> kinds; int n = 0;
    for (auto& a : quiverLoad(s)) if (!a.empty()) { kinds[a.def() ? a.def()->name : a.id] += a.n; n += a.n; }
    if (!n) out.push_back({"Empty: wear it beside your backpack, or use it to gather your arrows", TS});
    for (auto& [k, c] : kinds) out.push_back({std::to_string(c) + " " + k + (c == 1 ? "" : "s"), TS});
    return;
  }
  if (!d || !(d->hasTool || d->has("armor") || d->has("bow"))) return;
  double gx = s.tag.is_object() ? s.tag.value("gx", 0.0) : 0.0;
  int L = gearLevelOf(gx);
  double a = gearXpFor(L), b = gearXpFor(L + 1);
  double pr = L >= GEAR_MAX ? 1.0 : std::max(0.0, std::min(1.0, (gx - a) / (b - a)));
  out.push_back({"Level " + std::to_string(L) + (L >= GEAR_MAX ? " (max)" : ""), TS});
  char buf[24]; std::snprintf(buf, sizeof buf, "\x02%.3f", pr);
  out.push_back({buf, TD});
}

} // namespace dq
